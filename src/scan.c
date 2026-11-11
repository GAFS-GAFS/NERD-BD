#include "materialized.h"

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

int materialized_scan(const Table *input, Table *output, QueryMetrics *metrics)
{
    clock_t start = clock();
    clock_t end;
    size_t i;
    if (input == NULL || output == NULL || input == output) return 0;
    table_init_with_schema(output, input->schema);
    if (input->size != 0 && !table_reserve_rows(output, input->size)) return 0;
    for (i = 0; i < input->size; ++i) {
        if (!table_append_values(output, table_row_const(input, i))) {
            table_free(output);
            return 0;
        }
    }
    end = clock();
    set_metrics(metrics, input->size, output->size, table_memory_bytes(output), start, end);
    return 1;
}
