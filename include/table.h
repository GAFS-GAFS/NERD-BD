#ifndef MINIQUERY_TABLE_H
#define MINIQUERY_TABLE_H

#include <stddef.h>

#define DEPARTMENT_MAX_LEN 64
#define COLUMN_NAME_MAX_LEN 32
#define TABLE_MAX_COLUMNS 16

typedef enum {
    DATA_TYPE_INT,
    DATA_TYPE_DOUBLE,
    DATA_TYPE_STRING
} DataType;

typedef struct {
    char name[COLUMN_NAME_MAX_LEN];
    DataType type;
} Column;

typedef struct {
    Column columns[TABLE_MAX_COLUMNS];
    size_t column_count;
} Schema;

typedef struct {
    int id;
    char department[DEPARTMENT_MAX_LEN];
    double salary;
    int age;
} EmployeeRow;

typedef struct {
    Schema schema;
    EmployeeRow *rows;
    size_t size;
    size_t capacity;
} Table;

void schema_init(Schema *schema);
int schema_add_column(Schema *schema, const char *name, DataType type);
Schema employee_schema(void);
void table_init(Table *table);
void table_init_with_schema(Table *table, Schema schema);
void table_free(Table *table);
int table_append(Table *table, EmployeeRow row);
int table_load_csv(Table *table, const char *path);

#endif
