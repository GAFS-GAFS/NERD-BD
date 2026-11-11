#include "materialized.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define HASH_CHAIN_END SIZE_MAX

/* One BUILD row in the hash table; entries of a bucket are chained by index. */
typedef struct {
    size_t row_index;
    size_t next;
} HashEntry;

typedef struct {
    size_t *heads;
    HashEntry *entries;
    size_t bucket_count;
    size_t entry_count;
} HashTable;

static double elapsed_ms(clock_t start, clock_t end)
{
    return ((double)(end - start) * 1000.0) / (double)CLOCKS_PER_SEC;
}

static int join_schema(const Table *left, const char *left_alias, const Table *right,
                       const char *right_alias, Schema *schema)
{
    size_t i;
    char name[COLUMN_NAME_MAX_LEN];
    schema_init(schema);
    if (left->schema.column_count + right->schema.column_count > TABLE_MAX_COLUMNS) return 0;
    for (i = 0; i < left->schema.column_count; ++i) {
        if (snprintf(name, sizeof(name), "%s.%s", left_alias, left->schema.columns[i].name) >=
                (int)sizeof(name) ||
            !schema_add_column(schema, name, left->schema.columns[i].type)) return 0;
    }
    for (i = 0; i < right->schema.column_count; ++i) {
        if (snprintf(name, sizeof(name), "%s.%s", right_alias, right->schema.columns[i].name) >=
                (int)sizeof(name) ||
            !schema_add_column(schema, name, right->schema.columns[i].type)) return 0;
    }
    return 1;
}

/* Output layout is always left columns followed by right columns. */
static int append_joined_row(Table *output, const Table *left, const Value *left_row,
                             const Table *right, const Value *right_row)
{
    Value values[TABLE_MAX_COLUMNS];
    size_t left_width = left->schema.column_count;
    memcpy(values, left_row, left_width * sizeof(*values));
    memcpy(values + left_width, right_row, right->schema.column_count * sizeof(*values));
    return table_append_values(output, values);
}

static int validate_join(const Table *left, const char *left_key, const char *left_alias,
                         const Table *right, const char *right_key, const char *right_alias,
                         const Table *output, size_t *left_index, size_t *right_index,
                         Schema *schema)
{
    return left != NULL && right != NULL && output != NULL && output != left &&
           output != right && left_alias != NULL && right_alias != NULL &&
           schema_find_column(&left->schema, left_key, left_index) &&
           schema_find_column(&right->schema, right_key, right_index) &&
           join_schema(left, left_alias, right, right_alias, schema);
}

void join_metrics_print(FILE *stream, const char *name, const JoinMetrics *metrics)
{
    if (stream == NULL || name == NULL || metrics == NULL) return;
    fprintf(stream, "[%s]\n", name);
    fprintf(stream, "build_rows=%zu\nprobe_rows=%zu\n", metrics->build_rows, metrics->probe_rows);
    fprintf(stream, "hash_table_entries=%zu\nhash_table_bytes=%zu\n",
            metrics->hash_table_entries, metrics->hash_table_bytes);
    fprintf(stream, "build_time_ms=%.3f\nprobe_time_ms=%.3f\ntotal_time_ms=%.3f\n",
            metrics->build_time_ms, metrics->probe_time_ms, metrics->total_time_ms);
    fprintf(stream, "rows_out=%zu\nmemory_bytes=%zu\nkey_comparisons=%zu\n",
            metrics->rows_out, metrics->memory_bytes, metrics->key_comparisons);
}

/*
 * Nested Loop Join: the left input is the outer relation and the right input is
 * the inner relation. Both are already materialized; the inner one is rescanned
 * once per outer row. There is no BUILD phase, so build_time_ms is zero.
 */
int materialized_nested_loop_join(const Table *left, const char *left_key,
                                  const char *left_alias, const Table *right,
                                  const char *right_key, const char *right_alias,
                                  Table *output, JoinMetrics *metrics)
{
    clock_t start = clock();
    clock_t end;
    Schema schema;
    size_t left_index, right_index, i, j;
    size_t comparisons = 0;
    if (!validate_join(left, left_key, left_alias, right, right_key, right_alias, output,
                       &left_index, &right_index, &schema)) return 0;
    table_init_with_schema(output, schema);
    for (i = 0; i < left->size; ++i) {
        const Value *outer = table_row_const(left, i);
        for (j = 0; j < right->size; ++j) {
            const Value *inner = table_row_const(right, j);
            if (value_equals(&outer[left_index], &inner[right_index]) &&
                !append_joined_row(output, left, outer, right, inner)) {
                table_free(output);
                return 0;
            }
        }
        comparisons += right->size;
    }
    end = clock();
    if (metrics != NULL) {
        *metrics = (JoinMetrics){0};
        metrics->build_rows = right->size;
        metrics->probe_rows = left->size;
        metrics->rows_out = output->size;
        metrics->output_bytes = table_memory_bytes(output);
        metrics->memory_bytes = metrics->output_bytes;
        metrics->key_comparisons = comparisons;
        metrics->build_is_left = 0;
        metrics->probe_time_ms = elapsed_ms(start, end);
        metrics->total_time_ms = metrics->probe_time_ms;
    }
    return 1;
}

static void hash_table_free(HashTable *table)
{
    free(table->heads);
    free(table->entries);
    table->heads = NULL;
    table->entries = NULL;
    table->bucket_count = 0;
    table->entry_count = 0;
}

static size_t hash_table_bytes(const HashTable *table)
{
    return table->bucket_count * sizeof(*table->heads) +
           table->entry_count * sizeof(*table->entries);
}

/* BUILD: one bucket array plus one entry per BUILD row, both allocated up front. */
static int hash_build(HashTable *table, const Table *build, size_t key)
{
    size_t i;
    table->heads = NULL;
    table->entries = NULL;
    table->bucket_count = 0;
    table->entry_count = 0;
    if (build->size == 0) return 1;
    if (build->size > (SIZE_MAX - 1) / 2) return 0;
    table->bucket_count = build->size * 2 + 1;
    table->heads = malloc(table->bucket_count * sizeof(*table->heads));
    table->entries = malloc(build->size * sizeof(*table->entries));
    if (table->heads == NULL || table->entries == NULL) {
        hash_table_free(table);
        return 0;
    }
    for (i = 0; i < table->bucket_count; ++i) table->heads[i] = HASH_CHAIN_END;
    for (i = 0; i < build->size; ++i) {
        size_t bucket = value_hash(table_cell(build, i, key)) % table->bucket_count;
        table->entries[i].row_index = i;
        table->entries[i].next = table->heads[bucket];
        table->heads[bucket] = i;
    }
    table->entry_count = build->size;
    return 1;
}

/*
 * Hash Join: the smaller input (left on ties) is BUILD and the other is PROBE.
 * BUILD must finish before PROBE starts; the hash table is the intermediate
 * structure kept between both phases and is released before returning.
 */
int materialized_hash_join(const Table *left, const char *left_key, const char *left_alias,
                           const Table *right, const char *right_key, const char *right_alias,
                           Table *output, JoinMetrics *metrics)
{
    const Table *build;
    const Table *probe;
    size_t build_key, probe_key, left_index, right_index, i;
    size_t comparisons = 0;
    size_t hash_bytes;
    int build_is_left;
    HashTable hash;
    clock_t total_start = clock();
    clock_t build_start;
    clock_t build_end;
    clock_t probe_start;
    clock_t end;
    Schema schema;

    if (!validate_join(left, left_key, left_alias, right, right_key, right_alias, output,
                       &left_index, &right_index, &schema)) return 0;
    build_is_left = left->size <= right->size;
    build = build_is_left ? left : right;
    probe = build_is_left ? right : left;
    build_key = build_is_left ? left_index : right_index;
    probe_key = build_is_left ? right_index : left_index;
    table_init_with_schema(output, schema);

    build_start = clock();
    if (!hash_build(&hash, build, build_key)) return 0;
    build_end = clock();
    hash_bytes = hash_table_bytes(&hash);

    probe_start = clock();
    for (i = 0; i < probe->size && hash.bucket_count != 0; ++i) {
        const Value *probe_row = table_row_const(probe, i);
        const Value *probe_value = &probe_row[probe_key];
        size_t entry = hash.heads[value_hash(probe_value) % hash.bucket_count];
        while (entry != HASH_CHAIN_END) {
            const Value *build_row = table_row_const(build, hash.entries[entry].row_index);
            ++comparisons;
            if (value_equals(&build_row[build_key], probe_value)) {
                const Value *left_row = build_is_left ? build_row : probe_row;
                const Value *right_row = build_is_left ? probe_row : build_row;
                if (!append_joined_row(output, left, left_row, right, right_row)) {
                    hash_table_free(&hash);
                    table_free(output);
                    return 0;
                }
            }
            entry = hash.entries[entry].next;
        }
    }
    end = clock();
    if (metrics != NULL) {
        *metrics = (JoinMetrics){0};
        metrics->build_rows = build->size;
        metrics->probe_rows = probe->size;
        metrics->rows_out = output->size;
        metrics->hash_table_entries = hash.entry_count;
        metrics->hash_table_bytes = hash_bytes;
        metrics->hash_buckets = hash.bucket_count;
        metrics->output_bytes = table_memory_bytes(output);
        metrics->memory_bytes = hash_bytes + metrics->output_bytes;
        metrics->key_comparisons = comparisons;
        metrics->build_is_left = build_is_left;
        metrics->build_time_ms = elapsed_ms(build_start, build_end);
        metrics->probe_time_ms = elapsed_ms(probe_start, end);
        metrics->total_time_ms = elapsed_ms(total_start, end);
    }
    hash_table_free(&hash);
    return 1;
}
