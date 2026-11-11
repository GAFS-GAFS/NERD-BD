#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "materialized.h"
#include "table.h"

#define EMPLOYEE_CSV "data/funcionarios_etapa3.csv"
#define DEPARTMENT_CSV "data/departamentos.csv"
#define RESULT_PRINT_LIMIT 10

static void print_result(const char *name, const Table *result)
{
    size_t i;
    printf("%s\n", name);
    for (i = 0; i < result->size && i < RESULT_PRINT_LIMIT; ++i) {
        printf("  %s: %.2f\n", table_cell(result, i, 0)->as.string_value,
               table_cell(result, i, 1)->as.double_value);
    }
    if (result->size > RESULT_PRINT_LIMIT) {
        printf("  ... (%zu grupos no total)\n", result->size);
    }
}

static int same_group_results(const Table *left, const Table *right)
{
    size_t i, j;
    if (left->size != right->size) return 0;
    for (i = 0; i < left->size; ++i) {
        for (j = 0; j < right->size; ++j) {
            if (strcmp(table_cell(left, i, 0)->as.string_value,
                       table_cell(right, j, 0)->as.string_value) == 0 &&
                table_cell(left, i, 1)->as.double_value ==
                    table_cell(right, j, 1)->as.double_value) break;
        }
        if (j == right->size) return 0;
    }
    return 1;
}

/*
 * SELECT d.name, AVG(f.salary) FROM funcionarios f JOIN departamentos d
 * ON f.department_id = d.id WHERE f.salary > 5000 GROUP BY d.name;
 *
 * SCAN f -> FILTER -> JOIN <- SCAN d -> PROJECT -> GROUP BY/AGGREGATE
 *
 * Usage: miniquery [funcionarios.csv departamentos.csv [salary_threshold]]
 */
int main(int argc, char **argv)
{
    const char *employee_csv = argc >= 3 ? argv[1] : EMPLOYEE_CSV;
    const char *department_csv = argc >= 3 ? argv[2] : DEPARTMENT_CSV;
    double threshold = argc >= 4 ? strtod(argv[3], NULL) : 5000.0;
    const char *projection[] = { "d.name", "f.salary" };
    const char *group_columns[] = { "d.name" };
    Table employees, departments, scanned_employees, scanned_departments, filtered;
    Table nested_joined, hash_joined, nested_projected, hash_projected, nested_result, hash_result;
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
    table_init(&nested_projected);
    table_init(&hash_projected);
    table_init(&nested_result);
    table_init(&hash_result);
    if (argc == 2 || argc > 4) {
        fprintf(stderr, "usage: %s [funcionarios.csv departamentos.csv [salary_threshold]]\n", argv[0]);
        return 2;
    }
    if (!table_load_csv(&employees, employee_csv) || !table_load_csv(&departments, department_csv)) {
        fprintf(stderr, "error: could not load stage 3 datasets\n");
        goto cleanup_sources;
    }
    if (!materialized_scan(&employees, &scanned_employees, &metrics) ||
        !materialized_scan(&departments, &scanned_departments, &metrics) ||
        !materialized_filter(&scanned_employees, "salary", COMPARE_GREATER, value_double(threshold),
                             &filtered, &metrics)) goto cleanup_scans;
    if (!materialized_nested_loop_join(&filtered, "department_id", "f", &scanned_departments,
                                       "id", "d", &nested_joined, &nested_metrics) ||
        !materialized_hash_join(&filtered, "department_id", "f", &scanned_departments,
                                "id", "d", &hash_joined, &hash_metrics)) goto cleanup_results;
    if (!materialized_project(&nested_joined, projection, 2, &nested_projected, &metrics) ||
        !materialized_project(&hash_joined, projection, 2, &hash_projected, &metrics) ||
        !materialized_group_by(&nested_projected, group_columns, 1, AGG_AVG, "f.salary",
                               "avg_salary", &nested_result, &metrics) ||
        !materialized_group_by(&hash_projected, group_columns, 1, AGG_AVG, "f.salary",
                               "avg_salary", &hash_result, &metrics)) goto cleanup_results;
    printf("SCAN f: %zu linhas | SCAN d: %zu linhas | FILTER salary > %.2f: %zu linhas\n\n",
           scanned_employees.size, scanned_departments.size, threshold, filtered.size);
    join_metrics_print(stdout, "NESTED_LOOP_JOIN", &nested_metrics);
    putchar('\n');
    join_metrics_print(stdout, "HASH_JOIN", &hash_metrics);
    putchar('\n');
    if (!same_group_results(&nested_result, &hash_result)) {
        fprintf(stderr, "error: JOIN strategies produced different groups\n");
        goto cleanup_results;
    }
    print_result("Query result (equivalent strategies):", &nested_result);
    status = 0;

cleanup_results:
    table_free(&hash_result);
    table_free(&nested_result);
    table_free(&hash_projected);
    table_free(&nested_projected);
    table_free(&hash_joined);
    table_free(&nested_joined);
    table_free(&filtered);
cleanup_scans:
    table_free(&scanned_departments);
    table_free(&scanned_employees);
cleanup_sources:
    table_free(&departments);
    table_free(&employees);
    return status;
}
