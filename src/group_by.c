#include "materialized.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define GROUP_CHAIN_END SIZE_MAX
#define GROUP_INITIAL_BUCKETS 64

typedef struct {
    size_t count;
    double sum;
    double min;
    double max;
} GroupState;

/*
 * Hash index over the groups already materialized in the output table.
 * heads[bucket] is the first group of the bucket and next[group] chains the
 * remaining ones. Groups keep their first-appearance order in the output.
 */
typedef struct {
    size_t *heads;
    size_t *next;
    GroupState *states;
    size_t bucket_count;
    size_t capacity;
} GroupIndex;

static double elapsed_ms(clock_t start, clock_t end)
{
    return ((double)(end - start) * 1000.0) / (double)CLOCKS_PER_SEC;
}

static unsigned long group_hash(const Value *values, const size_t *indexes, size_t count)
{
    unsigned long hash = 1469598103UL;
    size_t i;
    for (i = 0; i < count; ++i) hash = hash * 31UL + value_hash(&values[indexes == NULL ? i : indexes[i]]);
    return hash;
}

static int group_matches(const Value *group, const Value *input, const size_t *indexes,
                         size_t count)
{
    size_t i;
    for (i = 0; i < count; ++i) {
        if (!value_equals(&group[i], &input[indexes[i]])) return 0;
    }
    return 1;
}

static void group_index_free(GroupIndex *index)
{
    free(index->heads);
    free(index->next);
    free(index->states);
    index->heads = NULL;
    index->next = NULL;
    index->states = NULL;
    index->bucket_count = 0;
    index->capacity = 0;
}

static int group_index_rehash(GroupIndex *index, const Table *groups, size_t key_count,
                              size_t bucket_count)
{
    size_t *heads = malloc(bucket_count * sizeof(*heads));
    size_t i;
    if (heads == NULL) return 0;
    for (i = 0; i < bucket_count; ++i) heads[i] = GROUP_CHAIN_END;
    for (i = 0; i < groups->size; ++i) {
        size_t bucket = group_hash(table_row_const(groups, i), NULL, key_count) % bucket_count;
        index->next[i] = heads[bucket];
        heads[bucket] = i;
    }
    free(index->heads);
    index->heads = heads;
    index->bucket_count = bucket_count;
    return 1;
}

/* Makes room for one more group: grows states/next and keeps load factor <= 1. */
static int group_index_reserve(GroupIndex *index, const Table *groups, size_t key_count)
{
    if (groups->size == index->capacity) {
        size_t capacity = index->capacity == 0 ? GROUP_INITIAL_BUCKETS : index->capacity * 2;
        size_t *next;
        GroupState *states;
        if (capacity < index->capacity) return 0;
        next = realloc(index->next, capacity * sizeof(*next));
        if (next == NULL) return 0;
        index->next = next;
        states = realloc(index->states, capacity * sizeof(*states));
        if (states == NULL) return 0;
        index->states = states;
        index->capacity = capacity;
    }
    if (groups->size + 1 > index->bucket_count) {
        size_t bucket_count = index->bucket_count == 0 ? GROUP_INITIAL_BUCKETS : index->bucket_count * 2;
        if (!group_index_rehash(index, groups, key_count, bucket_count)) return 0;
    }
    return 1;
}

static size_t group_index_bytes(const GroupIndex *index)
{
    return index->bucket_count * sizeof(*index->heads) +
           index->capacity * (sizeof(*index->next) + sizeof(*index->states));
}

int materialized_group_by(const Table *input, const char *const *group_columns,
                          size_t group_column_count, AggregateFunction function,
                          const char *aggregate_column, const char *result_name,
                          Table *output, QueryMetrics *metrics)
{
    clock_t start = clock();
    clock_t end;
    size_t indexes[TABLE_MAX_COLUMNS];
    size_t aggregate_index = 0;
    size_t index_bytes;
    Schema schema;
    GroupIndex index = { NULL, NULL, NULL, 0, 0 };
    size_t i, j;

    if (input == NULL || output == NULL || input == output || group_columns == NULL ||
        group_column_count == 0 || group_column_count >= TABLE_MAX_COLUMNS ||
        result_name == NULL || result_name[0] == '\0' || function < AGG_COUNT ||
        function > AGG_AVG) return 0;
    if (function != AGG_COUNT &&
        (!schema_find_column(&input->schema, aggregate_column, &aggregate_index) ||
         (input->schema.columns[aggregate_index].type != DATA_TYPE_INT &&
          input->schema.columns[aggregate_index].type != DATA_TYPE_DOUBLE))) return 0;
    schema_init(&schema);
    for (j = 0; j < group_column_count; ++j) {
        if (!schema_find_column(&input->schema, group_columns[j], &indexes[j]) ||
            !schema_add_column(&schema, input->schema.columns[indexes[j]].name,
                               input->schema.columns[indexes[j]].type)) return 0;
    }
    if (!schema_add_column(&schema, result_name, DATA_TYPE_DOUBLE)) return 0;
    table_init_with_schema(output, schema);
    for (i = 0; i < input->size; ++i) {
        const Value *row_values = table_row_const(input, i);
        double value = function == AGG_COUNT ? 0.0 : value_as_double(&row_values[aggregate_index]);
        size_t group_index = GROUP_CHAIN_END;
        if (index.bucket_count != 0) {
            group_index = index.heads[group_hash(row_values, indexes, group_column_count) %
                                      index.bucket_count];
            while (group_index != GROUP_CHAIN_END &&
                   !group_matches(table_row_const(output, group_index), row_values, indexes,
                                  group_column_count)) {
                group_index = index.next[group_index];
            }
        }
        if (group_index == GROUP_CHAIN_END) {
            Row row;
            size_t bucket;
            if (!group_index_reserve(&index, output, group_column_count)) {
                group_index_free(&index);
                table_free(output);
                return 0;
            }
            row_init(&row, &schema);
            for (j = 0; j < group_column_count; ++j) row.values[j] = row_values[indexes[j]];
            row.values[group_column_count] = value_double(0.0);
            group_index = output->size;
            if (!table_append(output, &row)) {
                group_index_free(&index);
                table_free(output);
                return 0;
            }
            bucket = group_hash(row_values, indexes, group_column_count) % index.bucket_count;
            index.next[group_index] = index.heads[bucket];
            index.heads[bucket] = group_index;
            index.states[group_index] = (GroupState){ .count = 0, .sum = 0.0, .min = value, .max = value };
        }
        index.states[group_index].count++;
        if (function != AGG_COUNT) {
            index.states[group_index].sum += value;
            if (value < index.states[group_index].min) index.states[group_index].min = value;
            if (value > index.states[group_index].max) index.states[group_index].max = value;
        }
    }
    for (i = 0; i < output->size; ++i) {
        double result = (double)index.states[i].count;
        if (function == AGG_SUM) result = index.states[i].sum;
        else if (function == AGG_MIN) result = index.states[i].min;
        else if (function == AGG_MAX) result = index.states[i].max;
        else if (function == AGG_AVG) result = index.states[i].sum / (double)index.states[i].count;
        table_row_mut(output, i)[group_column_count] = value_double(result);
    }
    index_bytes = group_index_bytes(&index);
    group_index_free(&index);
    end = clock();
    if (metrics != NULL) {
        metrics->rows_in = input->size;
        metrics->rows_out = output->size;
        metrics->memory_bytes = table_memory_bytes(output) + index_bytes;
        metrics->time_ms = elapsed_ms(start, end);
    }
    return 1;
}
