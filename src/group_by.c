#include "materialized.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct {
    size_t count;
    double sum;
    double min;
    double max;
} GroupState;

static double elapsed_ms(clock_t start, clock_t end)
{
    return ((double)(end - start) * 1000.0) / (double)CLOCKS_PER_SEC;
}

static int same_value(const Value *left, const Value *right)
{
    if (left->type == DATA_TYPE_STRING && right->type == DATA_TYPE_STRING) {
        return strcmp(left->as.string_value, right->as.string_value) == 0;
    }
    return value_is_numeric(left) && value_is_numeric(right) &&
           value_as_double(left) == value_as_double(right);
}

static int group_matches(const Row *group, const Row *input, const size_t *indexes,
                         size_t count)
{
    size_t i;
    for (i = 0; i < count; ++i) {
        if (!same_value(&group->values[i], &input->values[indexes[i]])) return 0;
    }
    return 1;
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
    Schema schema;
    GroupState *states = NULL;
    size_t state_capacity = 0;
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
        size_t group_index;
        double value = function == AGG_COUNT ? 0.0 : value_as_double(&input->rows[i].values[aggregate_index]);
        for (group_index = 0; group_index < output->size; ++group_index) {
            if (group_matches(&output->rows[group_index], &input->rows[i], indexes, group_column_count)) break;
        }
        if (group_index == output->size) {
            Row row;
            GroupState *new_states;
            if (output->size == state_capacity) {
                size_t new_capacity = state_capacity == 0 ? 8 : state_capacity * 2;
                new_states = realloc(states, new_capacity * sizeof(*states));
                if (new_states == NULL) {
                    free(states);
                    table_free(output);
                    return 0;
                }
                states = new_states;
                state_capacity = new_capacity;
            }
            row_init(&row, &schema);
            for (j = 0; j < group_column_count; ++j) row.values[j] = input->rows[i].values[indexes[j]];
            if (!table_append(output, &row)) {
                free(states);
                table_free(output);
                return 0;
            }
            states[group_index] = (GroupState){ .count = 0, .sum = 0.0, .min = value, .max = value };
        }
        states[group_index].count++;
        if (function != AGG_COUNT) {
            states[group_index].sum += value;
            if (value < states[group_index].min) states[group_index].min = value;
            if (value > states[group_index].max) states[group_index].max = value;
        }
    }
    for (i = 0; i < output->size; ++i) {
        double result = (double)states[i].count;
        if (function == AGG_SUM) result = states[i].sum;
        else if (function == AGG_MIN) result = states[i].min;
        else if (function == AGG_MAX) result = states[i].max;
        else if (function == AGG_AVG) result = states[i].sum / (double)states[i].count;
        output->rows[i].values[group_column_count] = value_double(result);
    }
    free(states);
    end = clock();
    if (metrics != NULL) {
        metrics->rows_in = input->size;
        metrics->rows_out = output->size;
        metrics->memory_bytes = table_memory_bytes(output);
        metrics->time_ms = elapsed_ms(start, end);
    }
    return 1;
}
