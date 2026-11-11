#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "materialized.h"
#include "table.h"

#define TEST_CSV_PATH "data/funcionarios.csv"
#define STAGE3_EMPLOYEE_CSV "data/funcionarios_etapa3.csv"
#define STAGE3_DEPARTMENT_CSV "data/departamentos.csv"

static Row make_employee(int id, const char *department, double salary, int age)
{
    Schema schema = employee_schema();
    Row row;
    row_init(&row, &schema);
    assert(row_set_int(&row, &schema, 0, id));
    assert(row_set_string(&row, &schema, 1, department));
    assert(row_set_double(&row, &schema, 2, salary));
    assert(row_set_int(&row, &schema, 3, age));
    return row;
}

static void append_employee(Table *table, int id, const char *department, double salary, int age)
{
    Row row = make_employee(id, department, salary, age);
    assert(table_append(table, &row));
}

static void test_schema_and_csv_load(void)
{
    Table table;
    Table missing;
    table_init(&table);
    assert(table_load_csv(&table, TEST_CSV_PATH));
    assert(table.size == 6);
    assert(table.schema.column_count == 4);
    assert(strcmp(table.schema.columns[0].name, "id") == 0);
    assert(table.schema.columns[2].type == DATA_TYPE_DOUBLE);
    assert(table_row_mut(&table, 0)[0].as.int_value == 1);
    assert(fabs(table_row_mut(&table, 1)[2].as.double_value - 7200.0) < 0.001);
    table_free(&table);
    table_init(&missing);
    assert(!table_load_csv(&missing, "data/arquivo-inexistente.csv"));
    assert(missing.size == 0);
    table_free(&missing);
}

static void test_scan_is_independent_materialization(void)
{
    Table input, scanned;
    QueryMetrics metrics;
    table_init(&input);
    append_employee(&input, 1, "engineering", 4000.0, 29);
    assert(materialized_scan(&input, &scanned, &metrics));
    table_row_mut(&input, 0)[2].as.double_value = 999.0;
    assert(scanned.size == 1);
    assert(fabs(table_row_mut(&scanned, 0)[2].as.double_value - 4000.0) < 0.001);
    assert(metrics.rows_in == 1 && metrics.rows_out == 1 && metrics.memory_bytes > 0);
    table_free(&scanned);
    table_free(&input);
}

static void test_filter_limits_and_errors(void)
{
    Table input, output;
    QueryMetrics metrics;
    table_init(&input);
    append_employee(&input, 1, "a", 5000.0, 20);
    append_employee(&input, 2, "b", 5000.01, 21);
    assert(materialized_filter_salary_gt(&input, 5000.0, &output, &metrics));
    assert(output.size == 1 && table_row_mut(&output, 0)[0].as.int_value == 2);
    assert(metrics.rows_in == 2 && metrics.rows_out == 1);
    table_free(&output);
    assert(!materialized_filter(&input, "missing", COMPARE_EQUAL, value_int(1), &output, NULL));
    assert(!materialized_filter(&input, "salary", COMPARE_EQUAL, value_int(1), &output, NULL));
    table_free(&input);
}

static void test_project(void)
{
    const char *columns[] = { "salary", "id" };
    const char *missing[] = { "missing" };
    const size_t indexes[] = { 0, 2 };
    const size_t invalid_indexes[] = { 4 };
    Table input, projected, empty, projected_empty;
    QueryMetrics metrics;
    table_init(&input);
    append_employee(&input, 7, "ops", 9000.0, 38);
    assert(materialized_project(&input, columns, 2, &projected, &metrics));
    assert(projected.schema.column_count == 2 && projected.size == 1);
    assert(strcmp(projected.schema.columns[0].name, "salary") == 0);
    assert(projected.schema.columns[0].type == DATA_TYPE_DOUBLE);
    assert(fabs(table_row_mut(&projected, 0)[0].as.double_value - 9000.0) < 0.001);
    assert(table_row_mut(&projected, 0)[1].as.int_value == 7);
    table_free(&projected);
    assert(materialized_project_indices(&input, indexes, 2, &projected, NULL));
    assert(strcmp(projected.schema.columns[0].name, "id") == 0);
    assert(fabs(table_row_mut(&projected, 0)[1].as.double_value - 9000.0) < 0.001);
    table_free(&projected);
    assert(!materialized_project_indices(&input, invalid_indexes, 1, &projected_empty, NULL));
    assert(!materialized_project(&input, missing, 1, &projected_empty, NULL));
    assert(!materialized_project(&input, columns, 0, &projected_empty, NULL));
    table_init(&empty);
    assert(materialized_project(&empty, columns, 2, &projected_empty, &metrics));
    assert(projected_empty.size == 0 && projected_empty.schema.column_count == 2);
    table_free(&projected_empty);
    table_free(&empty);
    table_free(&input);
}

static void expect_aggregate(const Table *table, AggregateFunction function, double expected,
                             int has_value)
{
    AggregateResult result;
    QueryMetrics metrics;
    assert(materialized_aggregate(table, function, function == AGG_COUNT ? NULL : "salary",
                                  &result, &metrics));
    assert(result.count == table->size && result.has_value == has_value);
    if (has_value) assert(fabs(result.value - expected) < 0.001);
    assert(metrics.rows_in == table->size && metrics.rows_out == 1);
}

static void test_all_aggregates(void)
{
    Table table, empty;
    table_init(&table);
    append_employee(&table, 1, "a", 7000.0, 20);
    append_employee(&table, 2, "b", 9000.0, 30);
    expect_aggregate(&table, AGG_COUNT, 2.0, 1);
    expect_aggregate(&table, AGG_SUM, 16000.0, 1);
    expect_aggregate(&table, AGG_MIN, 7000.0, 1);
    expect_aggregate(&table, AGG_MAX, 9000.0, 1);
    expect_aggregate(&table, AGG_AVG, 8000.0, 1);
    table_init(&empty);
    expect_aggregate(&empty, AGG_COUNT, 0.0, 1);
    expect_aggregate(&empty, AGG_SUM, 0.0, 1);
    expect_aggregate(&empty, AGG_MIN, 0.0, 0);
    expect_aggregate(&empty, AGG_MAX, 0.0, 0);
    expect_aggregate(&empty, AGG_AVG, 0.0, 0);
    assert(!materialized_aggregate(&table, AGG_AVG, "department", &(AggregateResult){0}, NULL));
    table_free(&empty);
    table_free(&table);
}

static void test_complete_pipeline(void)
{
    const char *columns[] = { "id", "salary" };
    Table source, scanned, filtered, projected;
    AggregateResult result;
    table_init(&source);
    assert(table_load_csv(&source, TEST_CSV_PATH));
    assert(materialized_scan(&source, &scanned, NULL));
    assert(materialized_filter_salary_gt(&scanned, 5000.0, &filtered, NULL));
    assert(materialized_project(&filtered, columns, 2, &projected, NULL));
    assert(materialized_aggregate(&projected, AGG_AVG, "salary", &result, NULL));
    assert(projected.size == 3 && result.count == 3);
    assert(fabs(result.value - (22800.0 / 3.0)) < 0.001);
    table_free(&projected);
    table_free(&filtered);
    table_free(&scanned);
    table_free(&source);
}

static int same_value(const Value *left, const Value *right)
{
    if (left->type != right->type) return 0;
    if (left->type == DATA_TYPE_INT) return left->as.int_value == right->as.int_value;
    if (left->type == DATA_TYPE_DOUBLE) return fabs(left->as.double_value - right->as.double_value) < 0.001;
    return strcmp(left->as.string_value, right->as.string_value) == 0;
}

static int same_row(const Value *left, const Value *right, size_t column_count)
{
    size_t i;
    for (i = 0; i < column_count; ++i) {
        if (!same_value(&left[i], &right[i])) return 0;
    }
    return 1;
}

static void assert_same_multiset(const Table *left, const Table *right)
{
    unsigned char *matched;
    size_t i, j;
    assert(left->schema.column_count == right->schema.column_count);
    assert(left->size == right->size);
    matched = calloc(right->size == 0 ? 1 : right->size, sizeof(*matched));
    assert(matched != NULL);
    for (i = 0; i < left->size; ++i) {
        for (j = 0; j < right->size; ++j) {
            if (!matched[j] && same_row(table_row_const(left, i), table_row_const(right, j), left->schema.column_count)) {
                matched[j] = 1;
                break;
            }
        }
        assert(j != right->size);
    }
    free(matched);
}

static void load_stage3(Table *employees, Table *departments)
{
    table_init_with_schema(employees, employee_stage3_schema());
    table_init_with_schema(departments, department_schema());
    assert(table_load_csv(employees, STAGE3_EMPLOYEE_CSV));
    assert(table_load_csv(departments, STAGE3_DEPARTMENT_CSV));
}

static void test_join_algorithms_and_edge_cases(void)
{
    Table employees, departments, nested, hashed, empty, no_matches, small, small_hash;
    JoinMetrics nested_metrics, hash_metrics;
    load_stage3(&employees, &departments);
    assert(materialized_nested_loop_join(&employees, "department_id", "f", &departments, "id", "d",
                                         &nested, &nested_metrics));
    assert(materialized_hash_join(&employees, "department_id", "f", &departments, "id", "d",
                                  &hashed, &hash_metrics));
    assert(nested.size == 9 && hashed.size == 9);
    assert(strcmp(nested.schema.columns[0].name, "f.id") == 0);
    assert(strcmp(nested.schema.columns[4].name, "d.id") == 0);
    assert(nested_metrics.rows_out == 9 && hash_metrics.rows_out == 9);
    assert(hash_metrics.build_rows == 5 && hash_metrics.probe_rows == 8);
    assert(hash_metrics.hash_table_entries == 5 && hash_metrics.hash_table_bytes > 0);
    assert_same_multiset(&nested, &hashed);
    table_init_with_schema(&small, employee_stage3_schema());
    assert(table_append_values(&small, table_row_const(&employees, 0)));
    assert(materialized_hash_join(&small, "department_id", "f", &departments, "id", "d",
                                  &small_hash, &hash_metrics));
    assert(small_hash.size == 1 && hash_metrics.build_rows == 1 && hash_metrics.probe_rows == 5);
    table_free(&small_hash);
    table_free(&small);
    table_init_with_schema(&empty, employee_stage3_schema());
    assert(materialized_hash_join(&empty, "department_id", "f", &departments, "id", "d",
                                  &no_matches, &hash_metrics));
    assert(no_matches.size == 0 && hash_metrics.hash_table_entries == 0);
    table_free(&no_matches);
    table_free(&empty);
    table_free(&hashed);
    table_free(&nested);
    table_free(&departments);
    table_free(&employees);
}

static void test_group_by(void)
{
    const char *integer_group[] = { "department_id" };
    const char *text_group[] = { "d.name" };
    Table employees, departments, joined, grouped, grouped_text;
    QueryMetrics metrics;
    load_stage3(&employees, &departments);
    assert(materialized_group_by(&employees, integer_group, 1, AGG_SUM, "salary", "total_salary",
                                 &grouped, &metrics));
    assert(grouped.size == 5 && grouped.schema.columns[1].type == DATA_TYPE_DOUBLE);
    assert(table_row_mut(&grouped, 0)[0].as.int_value == 10);
    assert(fabs(table_row_mut(&grouped, 0)[1].as.double_value - 16000.0) < 0.001);
    table_free(&grouped);
    assert(materialized_group_by(&employees, integer_group, 1, AGG_COUNT, NULL, "employee_count",
                                 &grouped, NULL));
    assert(fabs(table_row_mut(&grouped, 0)[1].as.double_value - 3.0) < 0.001);
    table_free(&grouped);
    assert(materialized_group_by(&employees, integer_group, 1, AGG_MIN, "salary", "min_salary",
                                 &grouped, NULL));
    assert(fabs(table_row_mut(&grouped, 0)[1].as.double_value - 4500.0) < 0.001);
    table_free(&grouped);
    assert(materialized_group_by(&employees, integer_group, 1, AGG_MAX, "salary", "max_salary",
                                 &grouped, NULL));
    assert(fabs(table_row_mut(&grouped, 0)[1].as.double_value - 6500.0) < 0.001);
    table_free(&grouped);
    assert(materialized_group_by(&employees, integer_group, 1, AGG_AVG, "salary", "avg_salary",
                                 &grouped, NULL));
    assert(fabs(table_row_mut(&grouped, 0)[1].as.double_value - (16000.0 / 3.0)) < 0.001);
    table_free(&grouped);
    assert(materialized_nested_loop_join(&employees, "department_id", "f", &departments, "id", "d",
                                         &joined, NULL));
    assert(materialized_group_by(&joined, text_group, 1, AGG_AVG, "f.salary", "avg_salary",
                                 &grouped_text, &metrics));
    assert(grouped_text.size == 5 && metrics.rows_out == 5);
    assert(!materialized_group_by(&employees, integer_group, 0, AGG_COUNT, NULL, "count", &grouped, NULL));
    assert(!materialized_group_by(&employees, integer_group, 1, AGG_AVG, "age_missing", "avg", &grouped, NULL));
    table_free(&grouped_text);
    table_free(&joined);
    table_free(&departments);
    table_free(&employees);
}

static void test_complete_stage3_query(void)
{
    const char *groups[] = { "d.name" };
    const char *projection[] = { "d.name", "avg_salary" };
    Table employees, departments, employee_scan, department_scan, filtered;
    Table nested, hashed, nested_groups, hash_groups, nested_result, hash_result;
    load_stage3(&employees, &departments);
    assert(materialized_scan(&employees, &employee_scan, NULL));
    assert(materialized_scan(&departments, &department_scan, NULL));
    assert(materialized_filter(&employee_scan, "salary", COMPARE_GREATER, value_double(5000.0),
                               &filtered, NULL));
    assert(materialized_nested_loop_join(&filtered, "department_id", "f", &department_scan, "id", "d",
                                         &nested, NULL));
    assert(materialized_hash_join(&filtered, "department_id", "f", &department_scan, "id", "d",
                                  &hashed, NULL));
    assert(materialized_group_by(&nested, groups, 1, AGG_AVG, "f.salary", "avg_salary",
                                 &nested_groups, NULL));
    assert(materialized_group_by(&hashed, groups, 1, AGG_AVG, "f.salary", "avg_salary",
                                 &hash_groups, NULL));
    assert(materialized_project(&nested_groups, projection, 2, &nested_result, NULL));
    assert(materialized_project(&hash_groups, projection, 2, &hash_result, NULL));
    assert(nested_result.size == 4);
    assert_same_multiset(&nested_result, &hash_result);
    table_free(&hash_result);
    table_free(&nested_result);
    table_free(&hash_groups);
    table_free(&nested_groups);
    table_free(&hashed);
    table_free(&nested);
    table_free(&filtered);
    table_free(&department_scan);
    table_free(&employee_scan);
    table_free(&departments);
    table_free(&employees);
}

static Row make_stage3_employee(int id, int department_id, double salary, int age)
{
    Schema schema = employee_stage3_schema();
    Row row;
    row_init(&row, &schema);
    assert(row_set_int(&row, &schema, 0, id));
    assert(row_set_int(&row, &schema, 1, department_id));
    assert(row_set_double(&row, &schema, 2, salary));
    assert(row_set_int(&row, &schema, 3, age));
    return row;
}

static Row make_department(int id, const char *name, const char *region)
{
    Schema schema = department_schema();
    Row row;
    row_init(&row, &schema);
    assert(row_set_int(&row, &schema, 0, id));
    assert(row_set_string(&row, &schema, 1, name));
    assert(row_set_string(&row, &schema, 2, region));
    return row;
}

static void test_compact_storage(void)
{
    const char *long_csv = "test_long_string.csv";
    Table employees, scanned, rejected;
    Row copy;
    FILE *file;
    QueryMetrics metrics;
    table_init_with_schema(&employees, employee_stage3_schema());
    assert(table_load_csv(&employees, STAGE3_EMPLOYEE_CSV));
    assert(table_row_bytes(&employees) == 4 * sizeof(Value));
    assert(employees.capacity == employees.size);
    assert(materialized_scan(&employees, &scanned, &metrics));
    assert(scanned.capacity == scanned.size);
    assert(metrics.memory_bytes == scanned.size * 4 * sizeof(Value));
    assert(table_get_row(&scanned, 1, &copy));
    assert(copy.values[1].as.int_value == 20);
    assert(!table_get_row(&scanned, scanned.size, &copy));
    table_free(&scanned);
    table_free(&employees);
    file = fopen(long_csv, "w");
    assert(file != NULL);
    fputs("id,name,region\n1,this-department-name-is-too-long,sul\n", file);
    fclose(file);
    table_init_with_schema(&rejected, department_schema());
    assert(!table_load_csv(&rejected, long_csv));
    assert(rejected.size == 0 && rejected.values == NULL);
    table_free(&rejected);
    remove(long_csv);
}

static void test_join_metrics_and_build_side(void)
{
    Table employees, departments, nested, hashed, one_department, swapped;
    JoinMetrics nested_metrics, hash_metrics;
    load_stage3(&employees, &departments);
    assert(materialized_nested_loop_join(&employees, "department_id", "f", &departments, "id", "d",
                                         &nested, &nested_metrics));
    assert(nested_metrics.build_rows == departments.size);
    assert(nested_metrics.probe_rows == employees.size);
    assert(nested_metrics.key_comparisons == employees.size * departments.size);
    assert(nested_metrics.build_time_ms == 0.0 && nested_metrics.hash_table_entries == 0);
    assert(materialized_hash_join(&employees, "department_id", "f", &departments, "id", "d",
                                  &hashed, &hash_metrics));
    assert(hash_metrics.build_is_left == 0);
    assert(hash_metrics.hash_buckets == 2 * departments.size + 1);
    assert(hash_metrics.hash_table_bytes ==
           hash_metrics.hash_buckets * sizeof(size_t) + departments.size * 2 * sizeof(size_t));
    assert(hash_metrics.key_comparisons >= hash_metrics.rows_out);
    assert(hash_metrics.key_comparisons < nested_metrics.key_comparisons);
    assert(hash_metrics.memory_bytes == hash_metrics.hash_table_bytes + hash_metrics.output_bytes);
    assert(hash_metrics.total_time_ms + 1e-9 >= hash_metrics.probe_time_ms);
    table_free(&hashed);
    table_init_with_schema(&one_department, department_schema());
    {
        Row row = make_department(10, "engineering", "sul");
        assert(table_append(&one_department, &row));
    }
    assert(materialized_hash_join(&one_department, "id", "d", &employees, "department_id", "f",
                                  &swapped, &hash_metrics));
    assert(hash_metrics.build_is_left == 1 && hash_metrics.build_rows == 1);
    assert(swapped.size == 3);
    assert(strcmp(swapped.schema.columns[0].name, "d.id") == 0);
    assert(strcmp(table_cell(&swapped, 0, 1)->as.string_value, "engineering") == 0);
    table_free(&swapped);
    table_free(&one_department);
    table_free(&nested);
    table_free(&departments);
    table_free(&employees);
}

static void test_join_randomized_equivalence(void)
{
    Table employees, departments, nested, hashed;
    JoinMetrics nested_metrics, hash_metrics;
    unsigned long seed = 12345UL;
    int i;
    table_init_with_schema(&employees, employee_stage3_schema());
    table_init_with_schema(&departments, department_schema());
    for (i = 0; i < 300; ++i) {
        char name[VALUE_STRING_MAX_LEN];
        Row row;
        (void)snprintf(name, sizeof(name), "dep_%d", i % 250);
        row = make_department(i % 250, name, "r");
        assert(table_append(&departments, &row));
    }
    for (i = 0; i < 2000; ++i) {
        Row row;
        seed = seed * 1103515245UL + 12345UL;
        row = make_stage3_employee(i, (int)((seed >> 8) % 320UL), 1000.0 + (double)i, 30);
        assert(table_append(&employees, &row));
    }
    assert(materialized_nested_loop_join(&employees, "department_id", "f", &departments, "id", "d",
                                         &nested, &nested_metrics));
    assert(materialized_hash_join(&employees, "department_id", "f", &departments, "id", "d",
                                  &hashed, &hash_metrics));
    assert(nested.size > 0 && nested.size == hashed.size);
    assert(nested_metrics.rows_out == hash_metrics.rows_out);
    assert_same_multiset(&nested, &hashed);
    table_free(&hashed);
    table_free(&nested);
    table_free(&departments);
    table_free(&employees);
}

static void test_group_by_many_groups(void)
{
    const char *group_columns[] = { "department_id" };
    Table employees, grouped;
    QueryMetrics metrics;
    int i;
    table_init_with_schema(&employees, employee_stage3_schema());
    for (i = 0; i < 20000; ++i) {
        Row row = make_stage3_employee(i, 4999 - (i % 5000), (double)(i % 5000) + (double)(i / 5000), 30);
        assert(table_append(&employees, &row));
    }
    assert(materialized_group_by(&employees, group_columns, 1, AGG_AVG, "salary", "avg_salary",
                                 &grouped, &metrics));
    assert(grouped.size == 5000 && metrics.rows_out == 5000 && metrics.rows_in == 20000);
    for (i = 0; i < 5000; ++i) {
        /* first-appearance order; each group has salaries g+0, g+1, g+2, g+3 */
        assert(table_cell(&grouped, (size_t)i, 0)->as.int_value == 4999 - i);
        assert(fabs(table_cell(&grouped, (size_t)i, 1)->as.double_value - ((double)i + 1.5)) < 1e-9);
    }
    table_free(&grouped);
    table_free(&employees);
}

static void test_reference_query_pdf_order(void)
{
    const char *projection[] = { "d.name", "f.salary" };
    const char *groups[] = { "d.name" };
    const char *expected_names[] = { "finance", "operations", "engineering", "finance-remote" };
    const double expected_avg[] = { 7600.0, 9100.0, 6500.0, 7600.0 };
    Table employees, departments, filtered, joined, projected, grouped;
    size_t i, j;
    load_stage3(&employees, &departments);
    assert(materialized_filter(&employees, "salary", COMPARE_GREATER, value_double(5000.0),
                               &filtered, NULL));
    assert(materialized_hash_join(&filtered, "department_id", "f", &departments, "id", "d",
                                  &joined, NULL));
    assert(materialized_project(&joined, projection, 2, &projected, NULL));
    assert(projected.schema.column_count == 2);
    assert(materialized_group_by(&projected, groups, 1, AGG_AVG, "f.salary", "avg_salary",
                                 &grouped, NULL));
    assert(grouped.size == 4);
    for (i = 0; i < 4; ++i) {
        for (j = 0; j < grouped.size; ++j) {
            if (strcmp(table_cell(&grouped, j, 0)->as.string_value, expected_names[i]) == 0) break;
        }
        assert(j < grouped.size);
        assert(fabs(table_cell(&grouped, j, 1)->as.double_value - expected_avg[i]) < 0.001);
    }
    table_free(&grouped);
    table_free(&projected);
    table_free(&joined);
    table_free(&filtered);
    table_free(&departments);
    table_free(&employees);
}

int main(void)
{
    test_schema_and_csv_load();
    test_scan_is_independent_materialization();
    test_filter_limits_and_errors();
    test_project();
    test_all_aggregates();
    test_complete_pipeline();
    test_join_algorithms_and_edge_cases();
    test_group_by();
    test_complete_stage3_query();
    test_compact_storage();
    test_join_metrics_and_build_side();
    test_join_randomized_equivalence();
    test_group_by_many_groups();
    test_reference_query_pdf_order();
    puts("all materialized tests passed");
    return 0;
}
