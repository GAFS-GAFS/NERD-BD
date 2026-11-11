/*
 * Stage 3 benchmark for the materialized model (Group A).
 *
 * Query (same for every experiment, only the salary threshold changes):
 *   SELECT d.name, AVG(f.salary)
 *   FROM funcionarios f JOIN departamentos d ON f.department_id = d.id
 *   WHERE f.salary > <threshold>
 *   GROUP BY d.name;
 *
 * Plan: SCAN f -> FILTER -> JOIN <- SCAN d -> PROJECT (d.name, f.salary) -> GROUP BY/AVG
 *
 * The CSV files are loaded once (base tables, outside the measured query).
 * Each repetition runs the whole plan again with fresh allocations. Every
 * intermediate table is released as soon as its consumer finishes, so the
 * tracked peak is the largest set of intermediates alive at the same time.
 *
 * Usage:
 *   miniquery_bench <experiment> <label> <funcionarios.csv> <departamentos.csv>
 *                   <nested_loop|hash> <salary_threshold> <repeats> <warmup> [header]
 * One CSV line is written to stdout per measured repetition.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "materialized.h"
#include "table.h"

typedef struct {
    QueryMetrics scan_f, scan_d, filter, project, group_by;
    JoinMetrics join;
    double wall_ms;
    double avg_checksum;
    size_t peak_live_bytes;
} RunResult;

static double wall_now_ms(void)
{
    struct timespec ts;
    if (timespec_get(&ts, TIME_UTC) != TIME_UTC) return 0.0;
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

/* Linux only: peak resident set size of the process (kB), -1 when unknown. */
static long peak_rss_kb(void)
{
    char line[256];
    long value = -1;
    FILE *file = fopen("/proc/self/status", "r");
    if (file == NULL) return -1;
    while (fgets(line, sizeof(line), file) != NULL) {
        if (strncmp(line, "VmHWM:", 6) == 0) {
            value = strtol(line + 6, NULL, 10);
            break;
        }
    }
    fclose(file);
    return value;
}

static void track(size_t *peak, size_t live)
{
    if (live > *peak) *peak = live;
}

static int run_query(const Table *employees, const Table *departments, int use_hash,
                     double threshold, RunResult *result)
{
    const char *projection[] = { "d.name", "f.salary" };
    const char *groups[] = { "d.name" };
    Table scan_f, scan_d, filtered, joined, projected, grouped;
    size_t live, i;
    double start;
    int ok = 0;

    memset(result, 0, sizeof(*result));
    table_init(&scan_f);
    table_init(&scan_d);
    table_init(&filtered);
    table_init(&joined);
    table_init(&projected);
    table_init(&grouped);
    start = wall_now_ms();
    if (!materialized_scan(employees, &scan_f, &result->scan_f)) goto done;
    live = result->scan_f.memory_bytes;
    track(&result->peak_live_bytes, live);
    if (!materialized_scan(departments, &scan_d, &result->scan_d)) goto done;
    live += result->scan_d.memory_bytes;
    track(&result->peak_live_bytes, live);
    if (!materialized_filter(&scan_f, "salary", COMPARE_GREATER, value_double(threshold),
                             &filtered, &result->filter)) goto done;
    live += result->filter.memory_bytes;
    track(&result->peak_live_bytes, live);
    table_free(&scan_f);
    live -= result->scan_f.memory_bytes;
    if (use_hash) {
        if (!materialized_hash_join(&filtered, "department_id", "f", &scan_d, "id", "d", &joined,
                                    &result->join)) goto done;
    } else if (!materialized_nested_loop_join(&filtered, "department_id", "f", &scan_d, "id", "d",
                                              &joined, &result->join)) {
        goto done;
    }
    track(&result->peak_live_bytes, live + result->join.memory_bytes);
    live += result->join.output_bytes;
    table_free(&filtered);
    table_free(&scan_d);
    live -= result->filter.memory_bytes + result->scan_d.memory_bytes;
    if (!materialized_project(&joined, projection, 2, &projected, &result->project)) goto done;
    live += result->project.memory_bytes;
    track(&result->peak_live_bytes, live);
    table_free(&joined);
    live -= result->join.output_bytes;
    if (!materialized_group_by(&projected, groups, 1, AGG_AVG, "f.salary", "avg_salary", &grouped,
                               &result->group_by)) goto done;
    track(&result->peak_live_bytes, live + result->group_by.memory_bytes);
    table_free(&projected);
    result->wall_ms = wall_now_ms() - start;
    for (i = 0; i < grouped.size; ++i) result->avg_checksum += table_cell(&grouped, i, 1)->as.double_value;
    ok = 1;
done:
    table_free(&grouped);
    table_free(&projected);
    table_free(&joined);
    table_free(&filtered);
    table_free(&scan_d);
    table_free(&scan_f);
    return ok;
}

static void print_header(void)
{
    puts("experiment,label,model,algorithm,run,employees,departments,threshold,"
         "filtered_rows,selectivity,build_rows,probe_rows,rows_out,groups,"
         "scan_f_ms,scan_d_ms,filter_ms,build_time_ms,probe_time_ms,join_total_ms,"
         "project_ms,group_by_ms,operators_ms,wall_ms,key_comparisons,hash_table_entries,"
         "hash_table_bytes,join_memory_bytes,base_bytes,scan_bytes,filter_bytes,"
         "join_output_bytes,project_bytes,group_by_bytes,intermediate_rows,"
         "intermediate_bytes,peak_live_bytes,peak_rss_kb,avg_checksum");
}

int main(int argc, char **argv)
{
    Table employees, departments;
    double threshold;
    int repeats, warmup, use_hash, run;
    size_t base_bytes;
    double load_start;

    if (argc != 9 && argc != 10) {
        fprintf(stderr, "usage: %s <experiment> <label> <funcionarios.csv> <departamentos.csv> "
                        "<nested_loop|hash> <salary_threshold> <repeats> <warmup> [header]\n", argv[0]);
        return 2;
    }
    if (strcmp(argv[5], "hash") == 0) use_hash = 1;
    else if (strcmp(argv[5], "nested_loop") == 0) use_hash = 0;
    else return 2;
    threshold = strtod(argv[6], NULL);
    repeats = atoi(argv[7]);
    warmup = atoi(argv[8]);
    if (repeats <= 0 || warmup < 0) return 2;
    if (argc == 10 && strcmp(argv[9], "header") == 0) print_header();

    table_init_with_schema(&employees, employee_stage3_schema());
    table_init_with_schema(&departments, department_schema());
    load_start = wall_now_ms();
    if (!table_load_csv(&employees, argv[3]) || !table_load_csv(&departments, argv[4])) {
        fprintf(stderr, "error: could not load %s / %s\n", argv[3], argv[4]);
        table_free(&departments);
        table_free(&employees);
        return 1;
    }
    fprintf(stderr, "loaded %zu funcionarios and %zu departamentos in %.1f ms\n",
            employees.size, departments.size, wall_now_ms() - load_start);
    base_bytes = table_memory_bytes(&employees) + table_memory_bytes(&departments);

    for (run = -warmup; run < repeats; ++run) {
        RunResult r;
        double operators_ms;
        size_t intermediate_rows, intermediate_bytes;
        if (!run_query(&employees, &departments, use_hash, threshold, &r)) {
            fprintf(stderr, "error: query failed (out of memory?)\n");
            table_free(&departments);
            table_free(&employees);
            return 1;
        }
        if (run < 0) continue;
        operators_ms = r.scan_f.time_ms + r.scan_d.time_ms + r.filter.time_ms +
                       r.join.total_time_ms + r.project.time_ms + r.group_by.time_ms;
        intermediate_rows = r.scan_f.rows_out + r.scan_d.rows_out + r.filter.rows_out +
                            r.join.rows_out + r.project.rows_out + r.group_by.rows_out;
        intermediate_bytes = r.scan_f.memory_bytes + r.scan_d.memory_bytes + r.filter.memory_bytes +
                             r.join.memory_bytes + r.project.memory_bytes + r.group_by.memory_bytes;
        printf("%s,%s,materialized,%s,%d,%zu,%zu,%.2f,%zu,%.6f,%zu,%zu,%zu,%zu,"
               "%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%zu,%zu,%zu,%zu,%zu,%zu,%zu,"
               "%zu,%zu,%zu,%zu,%zu,%zu,%ld,%.4f\n",
               argv[1], argv[2], use_hash ? "hash" : "nested_loop", run + 1, employees.size,
               departments.size, threshold, r.filter.rows_out,
               employees.size == 0 ? 0.0 : (double)r.filter.rows_out / (double)employees.size,
               r.join.build_rows, r.join.probe_rows, r.join.rows_out, r.group_by.rows_out,
               r.scan_f.time_ms, r.scan_d.time_ms, r.filter.time_ms, r.join.build_time_ms,
               r.join.probe_time_ms, r.join.total_time_ms, r.project.time_ms, r.group_by.time_ms,
               operators_ms, r.wall_ms, r.join.key_comparisons, r.join.hash_table_entries,
               r.join.hash_table_bytes, r.join.memory_bytes, base_bytes,
               r.scan_f.memory_bytes + r.scan_d.memory_bytes, r.filter.memory_bytes,
               r.join.output_bytes, r.project.memory_bytes, r.group_by.memory_bytes,
               intermediate_rows, intermediate_bytes, r.peak_live_bytes, peak_rss_kb(),
               r.avg_checksum);
        fflush(stdout);
    }
    table_free(&departments);
    table_free(&employees);
    return 0;
}
