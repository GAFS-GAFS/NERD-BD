#include "materialized.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct HashNode {
    size_t row_index;
    struct HashNode *next;
} HashNode;

static double elapsed_ms(clock_t start, clock_t end)
{
    return ((double)(end - start) * 1000.0) / (double)CLOCKS_PER_SEC;
}

static int values_equal(const Value *left, const Value *right)
{
    if (left->type == DATA_TYPE_STRING && right->type == DATA_TYPE_STRING) {
        return strcmp(left->as.string_value, right->as.string_value) == 0;
    }
    return value_is_numeric(left) && value_is_numeric(right) &&
           value_as_double(left) == value_as_double(right);
}

static unsigned long hash_value(const Value *value)
{
    const unsigned char *text;
    unsigned long hash = 5381UL;
    if (value_is_numeric(value)) {
        union { double number; unsigned char bytes[sizeof(double)]; } bits;
        size_t i;
        bits.number = value_as_double(value);
        for (i = 0; i < sizeof(bits.bytes); ++i) hash = ((hash << 5) + hash) ^ bits.bytes[i];
        return hash;
    }
    text = (const unsigned char *)value->as.string_value;
    while (*text != '\0') hash = ((hash << 5) + hash) ^ *text++;
    return hash;
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

static int append_joined_row(Table *output, const Table *left, const Row *left_row,
                             const Table *right, const Row *right_row)
{
    Row row;
    size_t i;
    row_init(&row, &output->schema);
    for (i = 0; i < left->schema.column_count; ++i) row.values[i] = left_row->values[i];
    for (i = 0; i < right->schema.column_count; ++i) {
        row.values[left->schema.column_count + i] = right_row->values[i];
    }
    return table_append(output, &row);
}

static void free_buckets(HashNode **buckets, size_t bucket_count)
{
    size_t i;
    for (i = 0; i < bucket_count; ++i) {
        HashNode *node = buckets[i];
        while (node != NULL) {
            HashNode *next = node->next;
            free(node);
            node = next;
        }
    }
    free(buckets);
}

int materialized_nested_loop_join(const Table *left, const char *left_key,
                                  const char *left_alias, const Table *right,
                                  const char *right_key, const char *right_alias,
                                  Table *output, JoinMetrics *metrics)
{
    clock_t start = clock();
    clock_t end;
    Schema schema;
    size_t left_index, right_index, i, j;
    if (left == NULL || right == NULL || output == NULL || output == left || output == right ||
        left_alias == NULL || right_alias == NULL ||
        !schema_find_column(&left->schema, left_key, &left_index) ||
        !schema_find_column(&right->schema, right_key, &right_index) ||
        !join_schema(left, left_alias, right, right_alias, &schema)) return 0;
    table_init_with_schema(output, schema);
    for (i = 0; i < left->size; ++i) {
        for (j = 0; j < right->size; ++j) {
            if (values_equal(&left->rows[i].values[left_index], &right->rows[j].values[right_index]) &&
                !append_joined_row(output, left, &left->rows[i], right, &right->rows[j])) {
                table_free(output);
                return 0;
            }
        }
    }
    end = clock();
    if (metrics != NULL) {
        *metrics = (JoinMetrics){0};
        metrics->build_rows = left->size;
        metrics->probe_rows = right->size;
        metrics->rows_out = output->size;
        metrics->memory_bytes = table_memory_bytes(output);
        metrics->probe_time_ms = elapsed_ms(start, end);
        metrics->total_time_ms = metrics->probe_time_ms;
    }
    return 1;
}

int materialized_hash_join(const Table *left, const char *left_key, const char *left_alias,
                           const Table *right, const char *right_key, const char *right_alias,
                           Table *output, JoinMetrics *metrics)
{
    const Table *build;
    const Table *probe;
    size_t build_key, probe_key, left_index, right_index;
    int build_is_left;
    size_t bucket_count, i;
    size_t hash_bytes = 0;
    HashNode **buckets = NULL;
    clock_t total_start = clock();
    clock_t build_start;
    clock_t build_end;
    clock_t probe_start;
    clock_t end;
    Schema schema;

    if (left == NULL || right == NULL || output == NULL || output == left || output == right ||
        left_alias == NULL || right_alias == NULL ||
        !schema_find_column(&left->schema, left_key, &left_index) ||
        !schema_find_column(&right->schema, right_key, &right_index) ||
        !join_schema(left, left_alias, right, right_alias, &schema)) return 0;
    build_is_left = left->size <= right->size;
    build = build_is_left ? left : right;
    probe = build_is_left ? right : left;
    build_key = build_is_left ? left_index : right_index;
    probe_key = build_is_left ? right_index : left_index;
    bucket_count = build->size == 0 ? 0 : build->size * 2 + 1;
    if (build->size != 0 && bucket_count <= build->size) return 0;
    if (bucket_count != 0) {
        buckets = calloc(bucket_count, sizeof(*buckets));
        if (buckets == NULL) return 0;
        hash_bytes = bucket_count * sizeof(*buckets);
    }
    table_init_with_schema(output, schema);
    build_start = clock();
    for (i = 0; i < build->size; ++i) {
        size_t bucket = hash_value(&build->rows[i].values[build_key]) % bucket_count;
        HashNode *node = malloc(sizeof(*node));
        if (node == NULL) {
            free_buckets(buckets, bucket_count);
            table_free(output);
            return 0;
        }
        node->row_index = i;
        node->next = buckets[bucket];
        buckets[bucket] = node;
        hash_bytes += sizeof(*node);
    }
    build_end = clock();
    probe_start = clock();
    for (i = 0; i < probe->size; ++i) {
        size_t bucket;
        HashNode *node;
        if (bucket_count == 0) break;
        bucket = hash_value(&probe->rows[i].values[probe_key]) % bucket_count;
        for (node = buckets[bucket]; node != NULL; node = node->next) {
            const Row *left_row = build_is_left ? &build->rows[node->row_index] : &probe->rows[i];
            const Row *right_row = build_is_left ? &probe->rows[i] : &build->rows[node->row_index];
            const Value *build_value = &build->rows[node->row_index].values[build_key];
            if (values_equal(build_value, &probe->rows[i].values[probe_key]) &&
                !append_joined_row(output, left, left_row, right, right_row)) {
                free_buckets(buckets, bucket_count);
                table_free(output);
                return 0;
            }
        }
    }
    end = clock();
    if (metrics != NULL) {
        *metrics = (JoinMetrics){0};
        metrics->build_rows = build->size;
        metrics->probe_rows = probe->size;
        metrics->rows_out = output->size;
        metrics->hash_table_entries = build->size;
        metrics->hash_table_bytes = hash_bytes;
        metrics->memory_bytes = hash_bytes + table_memory_bytes(output);
        metrics->build_time_ms = elapsed_ms(build_start, build_end);
        metrics->probe_time_ms = elapsed_ms(probe_start, end);
        metrics->total_time_ms = elapsed_ms(total_start, end);
    }
    free_buckets(buckets, bucket_count);
    return 1;
}
