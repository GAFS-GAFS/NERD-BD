#ifndef MINIQUERY_MATERIALIZED_H
#define MINIQUERY_MATERIALIZED_H

#include <stddef.h>

#include "table.h"

typedef struct {
    size_t rows_in;
    size_t rows_out;
    double time_ms;
} QueryMetrics;

typedef struct {
    size_t count;
    double avg_salary;
} AggregateResult;

const Table *scan_table(const Table *table);
int materialized_filter_salary_gt(
    const Table *input,
    double min_salary,
    Table *output,
    QueryMetrics *metrics
);
AggregateResult aggregate_count_avg_salary(const Table *table);

#endif
