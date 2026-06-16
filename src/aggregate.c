#include "materialized.h"

AggregateResult aggregate_count_avg_salary(const Table *table)
{
    AggregateResult result;
    double total_salary = 0.0;
    size_t index;

    result.count = table->size;
    result.avg_salary = 0.0;

    if (table->size == 0) {
        return result;
    }

    /* Aggregation runs after the intermediate table is fully materialized. */
    for (index = 0; index < table->size; index++) {
        total_salary += table->rows[index].salary;
    }

    result.avg_salary = total_salary / (double)table->size;
    return result;
}
