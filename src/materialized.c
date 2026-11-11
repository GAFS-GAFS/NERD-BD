#include "materialized.h"

#include <string.h>
#include <time.h>

static double elapsed_ms(clock_t start, clock_t end)
{
    return ((double)(end - start) * 1000.0) / (double)CLOCKS_PER_SEC;
}

static void set_metrics(QueryMetrics *metrics, size_t in, size_t out, size_t bytes,
                        clock_t start, clock_t end)
{
    if (metrics != NULL) {
        metrics->rows_in = in;
        metrics->rows_out = out;
        metrics->memory_bytes = bytes;
        metrics->time_ms = elapsed_ms(start, end);
    }
}

static int compare_values(const Value *left, Comparison comparison, const Value *right,
                          int *matches)
{
    int result;
    if (left->type == DATA_TYPE_STRING && right->type == DATA_TYPE_STRING) {
        result = strcmp(left->as.string_value, right->as.string_value);
    } else if (value_is_numeric(left) && value_is_numeric(right)) {
        double lhs = value_as_double(left);
        double rhs = value_as_double(right);
        result = (lhs > rhs) - (lhs < rhs);
    } else {
        return 0;
    }
    switch (comparison) {
    case COMPARE_EQUAL: *matches = result == 0; break;
    case COMPARE_NOT_EQUAL: *matches = result != 0; break;
    case COMPARE_LESS: *matches = result < 0; break;
    case COMPARE_LESS_EQUAL: *matches = result <= 0; break;
    case COMPARE_GREATER: *matches = result > 0; break;
    case COMPARE_GREATER_EQUAL: *matches = result >= 0; break;
    default: return 0;
    }
    return 1;
}

int materialized_filter(const Table *input, const char *column_name, Comparison comparison,
                        Value value, Table *output, QueryMetrics *metrics)
{
    clock_t start = clock();
    clock_t end;
    size_t column_index;
    size_t i;
    if (input == NULL || output == NULL || input == output ||
        !schema_find_column(&input->schema, column_name, &column_index) ||
        input->schema.columns[column_index].type != value.type) return 0;
    table_init_with_schema(output, input->schema);
    for (i = 0; i < input->size; ++i) {
        int matches;
        if (!compare_values(table_cell(input, i, column_index), comparison, &value, &matches)) {
            table_free(output);
            return 0;
        }
        if (matches && !table_append_values(output, table_row_const(input, i))) {
            table_free(output);
            return 0;
        }
    }
    end = clock();
    set_metrics(metrics, input->size, output->size, table_memory_bytes(output), start, end);
    return 1;
}

int materialized_filter_salary_gt(const Table *input, double min_salary, Table *output,
                                  QueryMetrics *metrics)
{
    return materialized_filter(input, "salary", COMPARE_GREATER, value_double(min_salary),
                               output, metrics);
}

int materialized_project(const Table *input, const char *const *column_names,
                         size_t column_count, Table *output, QueryMetrics *metrics)
{
    clock_t start = clock();
    clock_t end;
    Schema schema;
    size_t indexes[TABLE_MAX_COLUMNS];
    size_t i;
    size_t j;
    if (input == NULL || output == NULL || input == output || column_names == NULL ||
        column_count == 0 || column_count > TABLE_MAX_COLUMNS) return 0;
    schema_init(&schema);
    for (j = 0; j < column_count; ++j) {
        if (!schema_find_column(&input->schema, column_names[j], &indexes[j]) ||
            !schema_add_column(&schema, input->schema.columns[indexes[j]].name,
                               input->schema.columns[indexes[j]].type)) return 0;
    }
    table_init_with_schema(output, schema);
    if (input->size != 0 && !table_reserve_rows(output, input->size)) return 0;
    for (i = 0; i < input->size; ++i) {
        Row row;
        row_init(&row, &schema);
        for (j = 0; j < column_count; ++j) row.values[j] = *table_cell(input, i, indexes[j]);
        if (!table_append(output, &row)) {
            table_free(output);
            return 0;
        }
    }
    end = clock();
    set_metrics(metrics, input->size, output->size, table_memory_bytes(output), start, end);
    return 1;
}

int materialized_project_indices(const Table *input, const size_t *column_indexes,
                                 size_t column_count, Table *output,
                                 QueryMetrics *metrics)
{
    clock_t start = clock();
    clock_t end;
    Schema schema;
    size_t i;
    size_t j;
    if (input == NULL || output == NULL || input == output || column_indexes == NULL ||
        column_count == 0 || column_count > TABLE_MAX_COLUMNS) return 0;
    schema_init(&schema);
    for (j = 0; j < column_count; ++j) {
        if (column_indexes[j] >= input->schema.column_count ||
            !schema_add_column(&schema, input->schema.columns[column_indexes[j]].name,
                               input->schema.columns[column_indexes[j]].type)) return 0;
    }
    table_init_with_schema(output, schema);
    if (input->size != 0 && !table_reserve_rows(output, input->size)) return 0;
    for (i = 0; i < input->size; ++i) {
        Row row;
        row_init(&row, &schema);
        for (j = 0; j < column_count; ++j) {
            row.values[j] = *table_cell(input, i, column_indexes[j]);
        }
        if (!table_append(output, &row)) {
            table_free(output);
            return 0;
        }
    }
    end = clock();
    set_metrics(metrics, input->size, output->size, table_memory_bytes(output), start, end);
    return 1;
}
