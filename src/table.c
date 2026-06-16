#include "table.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INITIAL_CAPACITY 1024
#define CSV_LINE_MAX_LEN 256

static int table_reserve(Table *table, size_t new_capacity)
{
    EmployeeRow *new_rows = realloc(table->rows, new_capacity * sizeof(*new_rows));
    if (new_rows == NULL) {
        return 0;
    }

    table->rows = new_rows;
    table->capacity = new_capacity;
    return 1;
}

static int table_grow(Table *table)
{
    /* Grow exponentially to keep appends amortized O(1). */
    size_t new_capacity = table->capacity == 0
        ? INITIAL_CAPACITY
        : table->capacity * 2;

    return table_reserve(table, new_capacity);
}

static void trim_newline(char *text)
{
    text[strcspn(text, "\r\n")] = '\0';
}

static int parse_employee_row(char *line, EmployeeRow *row)
{
    /* The project expects a fixed CSV format: id,department,salary,age. */
    char *token = strtok(line, ",");
    if (token == NULL) {
        return 0;
    }
    row->id = atoi(token);

    token = strtok(NULL, ",");
    if (token == NULL) {
        return 0;
    }
    snprintf(row->department, sizeof(row->department), "%s", token);

    token = strtok(NULL, ",");
    if (token == NULL) {
        return 0;
    }
    row->salary = atof(token);

    token = strtok(NULL, ",");
    if (token == NULL) {
        return 0;
    }
    row->age = atoi(token);

    return 1;
}

void schema_init(Schema *schema)
{
    schema->column_count = 0;
}

int schema_add_column(Schema *schema, const char *name, DataType type)
{
    Column *column;

    if (schema->column_count == TABLE_MAX_COLUMNS) {
        return 0;
    }

    column = &schema->columns[schema->column_count];
    snprintf(column->name, sizeof(column->name), "%s", name);
    column->type = type;
    schema->column_count++;

    return 1;
}

Schema employee_schema(void)
{
    Schema schema;

    schema_init(&schema);
    (void)schema_add_column(&schema, "id", DATA_TYPE_INT);
    (void)schema_add_column(&schema, "department", DATA_TYPE_STRING);
    (void)schema_add_column(&schema, "salary", DATA_TYPE_DOUBLE);
    (void)schema_add_column(&schema, "age", DATA_TYPE_INT);

    return schema;
}

void table_init(Table *table)
{
    table_init_with_schema(table, employee_schema());
}

void table_init_with_schema(Table *table, Schema schema)
{
    table->schema = schema;
    table->rows = NULL;
    table->size = 0;
    table->capacity = 0;
}

void table_free(Table *table)
{
    free(table->rows);
    table_init(table);
}

int table_append(Table *table, EmployeeRow row)
{
    if (table->size == table->capacity && !table_grow(table)) {
        return 0;
    }

    table->rows[table->size] = row;
    table->size++;
    return 1;
}

int table_load_csv(Table *table, const char *path)
{
    FILE *file = fopen(path, "r");
    char line[CSV_LINE_MAX_LEN];

    if (file == NULL) {
        return 0;
    }

    /* Skip the header row. */
    if (fgets(line, sizeof(line), file) == NULL) {
        fclose(file);
        return 1;
    }

    while (fgets(line, sizeof(line), file) != NULL) {
        EmployeeRow row;
        trim_newline(line);

        if (line[0] == '\0') {
            continue;
        }

        if (!parse_employee_row(line, &row) || !table_append(table, row)) {
            fclose(file);
            return 0;
        }
    }

    fclose(file);
    return 1;
}
