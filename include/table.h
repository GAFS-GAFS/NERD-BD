#ifndef MINIQUERY_TABLE_H
#define MINIQUERY_TABLE_H

#include <stddef.h>

#define COLUMN_NAME_MAX_LEN 32
#define TABLE_MAX_COLUMNS 16
#define VALUE_STRING_MAX_LEN 128

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

/* Values own their string bytes; no row points into a CSV input buffer. */
typedef struct {
    DataType type;
    union {
        int int_value;
        double double_value;
        char string_value[VALUE_STRING_MAX_LEN];
    } as;
} Value;

typedef struct {
    Value values[TABLE_MAX_COLUMNS];
} Row;

typedef struct {
    Schema schema;
    Row *rows;
    size_t size;
    size_t capacity;
} Table;

void schema_init(Schema *schema);
int schema_add_column(Schema *schema, const char *name, DataType type);
int schema_find_column(const Schema *schema, const char *name, size_t *index);
Schema employee_schema(void);
Schema employee_stage3_schema(void);
Schema department_schema(void);

Value value_int(int value);
Value value_double(double value);
Value value_string(const char *value);
int value_is_numeric(const Value *value);
double value_as_double(const Value *value);

void row_init(Row *row, const Schema *schema);
int row_set_value(Row *row, const Schema *schema, size_t column_index, Value value);
int row_set_int(Row *row, const Schema *schema, size_t column_index, int value);
int row_set_double(Row *row, const Schema *schema, size_t column_index, double value);
int row_set_string(Row *row, const Schema *schema, size_t column_index, const char *value);

void table_init(Table *table);
void table_init_with_schema(Table *table, Schema schema);
void table_free(Table *table);
int table_append(Table *table, const Row *row);
int table_load_csv(Table *table, const char *path);
size_t table_memory_bytes(const Table *table);

#endif
