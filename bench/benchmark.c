#include <stdio.h>

#include "materialized.h"
#include "table.h"

int main(int argc, char **argv)
{
    Table employees, departments, nested, hashed;
    JoinMetrics nested_metrics, hash_metrics;
    if (argc != 3) return 2;
    table_init_with_schema(&employees, employee_stage3_schema());
    table_init_with_schema(&departments, department_schema());
    table_init(&nested);
    table_init(&hashed);
    if (!table_load_csv(&employees, argv[1]) || !table_load_csv(&departments, argv[2]) ||
        !materialized_nested_loop_join(&employees, "department_id", "f", &departments, "id", "d",
                                       &nested, &nested_metrics) ||
        !materialized_hash_join(&employees, "department_id", "f", &departments, "id", "d",
                                &hashed, &hash_metrics)) {
        table_free(&hashed);
        table_free(&nested);
        table_free(&departments);
        table_free(&employees);
        return 1;
    }
    puts("algorithm,employees,departments,rows_out,total_time_ms,memory_bytes");
    printf("nested_loop,%zu,%zu,%zu,%.6f,%zu\n", employees.size, departments.size,
           nested_metrics.rows_out, nested_metrics.total_time_ms, nested_metrics.memory_bytes);
    printf("hash_join,%zu,%zu,%zu,%.6f,%zu\n", employees.size, departments.size,
           hash_metrics.rows_out, hash_metrics.total_time_ms, hash_metrics.memory_bytes);
    table_free(&hashed);
    table_free(&nested);
    table_free(&departments);
    table_free(&employees);
    return 0;
}
