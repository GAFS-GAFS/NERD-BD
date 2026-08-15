#include <stdio.h>
#include <time.h>
#include "materialized.h"
#include "table.h"
#include "pipeline.h"

#define CSV_PATH "data/funcionarios.csv"
#define SALARY_FILTER 5000.0

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <model>\n", argv[0]);
        return 1;
    }


    Table employees;
    Table filtered;
    QueryMetrics metrics;
    AggregateResult aggregate;
    const Table *scanned;

    table_init(&employees);

    if (!table_load_csv(&employees, CSV_PATH)) {
        fprintf(stderr, "error: could not load %s\n", CSV_PATH);
        table_free(&employees);
        return 1;
    }

    if (argc >= 3 && strcmp(argv[1], "--modelo") == 0 && strcmp(argv[2], "pipeline") == 0) {
        execute_pipeline_query(&employees); // Passa a tabela correta
        table_free(&employees);
        return 0; // Encerra o programa aqui, ignorando o resto do código do Grupo A
    }

    scanned = scan_table(&employees);

    /* Fixed query for Group A: materialize WHERE salary > 5000. */
    if (!materialized_filter_salary_gt(scanned, SALARY_FILTER, &filtered, &metrics)) {
        fprintf(stderr, "error: could not materialize query result\n");
        table_free(&employees);
        return 1;
    }

    aggregate = aggregate_count_avg_salary(&filtered);

    printf("rows_in=%zu\n", metrics.rows_in);
    printf("rows_out=%zu\n", aggregate.count);
    printf("avg_salary=%.2f\n", aggregate.avg_salary);
    printf("time_ms=%.3f\n", metrics.time_ms);

    table_free(&filtered);
    table_free(&employees);
    return 0;
}
