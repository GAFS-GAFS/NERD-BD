#include <stdio.h>
#include <string.h>

#include "materialized.h"
#include "table.h"

#define EMPLOYEE_CSV "data/funcionarios_etapa3.csv"
#define DEPARTMENT_CSV "data/departamentos.csv"

static void print_join_metrics(const char *name, const JoinMetrics *metrics)
{
    printf("%s: build_rows=%zu probe_rows=%zu rows_out=%zu memory_bytes=%zu total_ms=%.3f\n",
           name, metrics->build_rows, metrics->probe_rows, metrics->rows_out,
           metrics->memory_bytes, metrics->total_time_ms);
}

static void print_result(const char *name, const Table *result)
{
    size_t i;
    printf("%s\n", name);
    for (i = 0; i < result->size; ++i) {
        printf("  %s: %.2f\n", result->rows[i].values[0].as.string_value,
               result->rows[i].values[1].as.double_value);
    }
}

static int same_group_results(const Table *left, const Table *right)
{
    size_t i, j;
    if (left->size != right->size) return 0;
    for (i = 0; i < left->size; ++i) {
        for (j = 0; j < right->size; ++j) {
            if (strcmp(left->rows[i].values[0].as.string_value,
                       right->rows[j].values[0].as.string_value) == 0 &&
                left->rows[i].values[1].as.double_value == right->rows[j].values[1].as.double_value) break;
        }
        if (j == right->size) return 0;
    }
    return 1;
}

int main(void)
{
    const char *group_columns[] = { "d.name" };
    const char *final_columns[] = { "d.name", "avg_salary" };
    Table employees, departments, scanned_employees, scanned_departments, filtered;
    Table nested_joined, hash_joined, nested_grouped, hash_grouped, nested_result, hash_result;
    QueryMetrics metrics;
    JoinMetrics nested_metrics, hash_metrics;
    int status = 1;

    table_init_with_schema(&employees, employee_stage3_schema());
    table_init_with_schema(&departments, department_schema());
    table_init(&scanned_employees);
    table_init(&scanned_departments);
    table_init(&filtered);
    table_init(&nested_joined);
    table_init(&hash_joined);
    table_init(&nested_grouped);
    table_init(&hash_grouped);
    table_init(&nested_result);
    table_init(&hash_result);
    if (!table_load_csv(&employees, EMPLOYEE_CSV) || !table_load_csv(&departments, DEPARTMENT_CSV)) {
        fprintf(stderr, "error: could not load stage 3 datasets\n");
        goto cleanup_sources;
    }
    if (!materialized_scan(&employees, &scanned_employees, &metrics) ||
        !materialized_scan(&departments, &scanned_departments, &metrics) ||
        !materialized_filter(&scanned_employees, "salary", COMPARE_GREATER, value_double(5000.0),
                             &filtered, &metrics)) goto cleanup_scans;
    if (!materialized_nested_loop_join(&filtered, "department_id", "f", &scanned_departments,
                                       "id", "d", &nested_joined, &nested_metrics) ||
        !materialized_hash_join(&filtered, "department_id", "f", &scanned_departments,
                                "id", "d", &hash_joined, &hash_metrics)) goto cleanup_filtered;
    if (!materialized_group_by(&nested_joined, group_columns, 1, AGG_AVG, "f.salary", "avg_salary",
                               &nested_grouped, &metrics) ||
        !materialized_group_by(&hash_joined, group_columns, 1, AGG_AVG, "f.salary", "avg_salary",
                               &hash_grouped, &metrics) ||
        !materialized_project(&nested_grouped, final_columns, 2, &nested_result, &metrics) ||
        !materialized_project(&hash_grouped, final_columns, 2, &hash_result, &metrics)) goto cleanup_joins;
    print_join_metrics("NESTED_LOOP_JOIN", &nested_metrics);
    print_join_metrics("HASH_JOIN", &hash_metrics);
    if (!same_group_results(&nested_result, &hash_result)) {
        fprintf(stderr, "error: JOIN strategies produced different groups\n");
        goto cleanup_results;
    }
    print_result("Query result (equivalent strategies):", &nested_result);
    status = 0;

cleanup_results:
    table_free(&hash_result);
    table_free(&nested_result);
    table_free(&hash_grouped);
    table_free(&nested_grouped);
cleanup_joins:
    table_free(&hash_joined);
    table_free(&nested_joined);
cleanup_filtered:
    table_free(&filtered);
cleanup_scans:
    table_free(&scanned_departments);
    table_free(&scanned_employees);
cleanup_sources:
    table_free(&departments);
    table_free(&employees);
    return status;
}
