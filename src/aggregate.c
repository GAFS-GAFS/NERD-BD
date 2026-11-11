#include "materialized.h"

#include <time.h>

static double elapsed_ms(clock_t start, clock_t end)
{
    return ((double)(end - start) * 1000.0) / (double)CLOCKS_PER_SEC;
}

int materialized_aggregate(const Table *input, AggregateFunction function,
                           const char *column_name, AggregateResult *result,
                           QueryMetrics *metrics)
{
    clock_t start = clock();
    clock_t end;
    size_t column_index = 0;
    size_t i;
    double sum = 0.0;

    if (input == NULL || result == NULL || function < AGG_COUNT || function > AGG_AVG) return 0;
    if (function != AGG_COUNT &&
        (!schema_find_column(&input->schema, column_name, &column_index) ||
         (input->schema.columns[column_index].type != DATA_TYPE_INT &&
          input->schema.columns[column_index].type != DATA_TYPE_DOUBLE))) return 0;

    result->function = function;
    result->count = input->size;
    result->value = 0.0;
    result->has_value = function == AGG_COUNT || function == AGG_SUM;
    if (function == AGG_COUNT) {
        result->value = (double)input->size;
    } else {
        for (i = 0; i < input->size; ++i) {
            double value = value_as_double(table_cell(input, i, column_index));
            sum += value;
            if (i == 0 || (function == AGG_MIN && value < result->value) ||
                (function == AGG_MAX && value > result->value)) result->value = value;
        }
        if (function == AGG_SUM) result->value = sum;
        if (function == AGG_AVG && input->size != 0) {
            result->value = sum / (double)input->size;
            result->has_value = 1;
        }
        if ((function == AGG_MIN || function == AGG_MAX) && input->size != 0) {
            result->has_value = 1;
        }
    }
    end = clock();
    if (metrics != NULL) {
        metrics->rows_in = input->size;
        metrics->rows_out = 1;
        metrics->memory_bytes = 0;
        metrics->time_ms = elapsed_ms(start, end);
    }
    return 1;
}
