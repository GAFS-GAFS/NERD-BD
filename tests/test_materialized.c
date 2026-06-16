#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "materialized.h"
#include "table.h"

#define TEST_CSV_PATH "data/funcionarios.csv"

static EmployeeRow make_row(int id, const char *department, double salary, int age)
{
    EmployeeRow row;

    row.id = id;
    snprintf(row.department, sizeof(row.department), "%s", department);
    row.salary = salary;
    row.age = age;

    return row;
}

static void test_schema_creation(void)
{
    Schema schema = employee_schema();

    assert(schema.column_count == 4);
    assert(schema.columns[0].type == DATA_TYPE_INT);
    assert(schema.columns[1].type == DATA_TYPE_STRING);
    assert(schema.columns[2].type == DATA_TYPE_DOUBLE);
    assert(schema.columns[3].type == DATA_TYPE_INT);
    assert(strcmp(schema.columns[0].name, "id") == 0);
    assert(strcmp(schema.columns[1].name, "department") == 0);
    assert(strcmp(schema.columns[2].name, "salary") == 0);
    assert(strcmp(schema.columns[3].name, "age") == 0);
}

static void test_csv_load(void)
{
    Table table;

    table_init(&table);

    assert(table_load_csv(&table, TEST_CSV_PATH));
    assert(table.size == 6);
    assert(table.schema.column_count == 4);
    assert(table.rows[0].id == 1);
    assert(fabs(table.rows[1].salary - 7200.0) < 0.001);

    table_free(&table);
}

static void test_materialized_filter(void)
{
    Table input;
    Table output;
    QueryMetrics metrics;
    const Table *scanned;

    table_init(&input);

    assert(table_append(&input, make_row(1, "engineering", 4000.0, 29)));
    assert(table_append(&input, make_row(2, "finance", 7000.0, 41)));
    assert(table_append(&input, make_row(3, "sales", 5000.0, 33)));
    assert(table_append(&input, make_row(4, "ops", 9000.0, 38)));

    scanned = scan_table(&input);
    assert(scanned == &input);
    assert(materialized_filter_salary_gt(scanned, 5000.0, &output, &metrics));

    assert(metrics.rows_in == 4);
    assert(metrics.rows_out == 2);
    assert(output.size == 2);
    assert(output.schema.column_count == input.schema.column_count);

    table_free(&output);
    table_free(&input);
}

static void test_aggregate_count_avg_salary(void)
{
    Table table;
    AggregateResult result;

    table_init(&table);

    assert(table_append(&table, make_row(1, "finance", 7000.0, 41)));
    assert(table_append(&table, make_row(2, "ops", 9000.0, 38)));

    result = aggregate_count_avg_salary(&table);

    assert(result.count == 2);
    assert(fabs(result.avg_salary - 8000.0) < 0.001);

    table_free(&table);
}

int main(void)
{
    test_schema_creation();
    test_csv_load();
    test_materialized_filter();
    test_aggregate_count_avg_salary();
    return 0;
}
