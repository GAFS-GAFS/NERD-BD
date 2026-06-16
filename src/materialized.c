#include "materialized.h"

#include <time.h>

static double elapsed_ms(clock_t start, clock_t end)
{
    return ((double)(end - start) * 1000.0) / (double)CLOCKS_PER_SEC;
}

int materialized_filter_salary_gt(
    const Table *input,
    double min_salary,
    Table *output,
    QueryMetrics *metrics
)
{
    clock_t start = clock();
    clock_t end;
    size_t index;

    table_init_with_schema(output, input->schema);

    /* Full scan: each approved row is copied into a new materialized table. */
    for (index = 0; index < input->size; index++) {
        EmployeeRow row = input->rows[index];

        if (row.salary > min_salary && !table_append(output, row)) {
            table_free(output);
            return 0;
        }
    }

    end = clock();

    metrics->rows_in = input->size;
    metrics->rows_out = output->size;
    metrics->time_ms = elapsed_ms(start, end);

    return 1;
}
