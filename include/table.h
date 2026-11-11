#ifndef MINIQUERY_TABLE_H
#define MINIQUERY_TABLE_H

#include <stddef.h>

#define COLUMN_NAME_MAX_LEN 32
#define TABLE_MAX_COLUMNS 16
#define VALUE_STRING_MAX_LEN 24

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

/*
 * Staging tuple used to build one row before it is appended. A Table does not
 * store Row structs: it stores only schema.column_count values per row.
 */
typedef struct {
    Value values[TABLE_MAX_COLUMNS];
} Row;

/*
 * Compact row-major storage: row i occupies
 * values[i * schema.column_count .. i * schema.column_count + column_count - 1].
 * The table owns the values buffer and releases it in table_free.
 */
typedef struct {
    Schema schema;
    Value *values;
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
int value_equals(const Value *left, const Value *right);
unsigned long value_hash(const Value *value);

void row_init(Row *row, const Schema *schema);
int row_set_value(Row *row, const Schema *schema, size_t column_index, Value value);
int row_set_int(Row *row, const Schema *schema, size_t column_index, int value);
int row_set_double(Row *row, const Schema *schema, size_t column_index, double value);
int row_set_string(Row *row, const Schema *schema, size_t column_index, const char *value);

void table_init(Table *table);
void table_init_with_schema(Table *table, Schema schema);
void table_free(Table *table);
int table_reserve_rows(Table *table, size_t row_capacity);
int table_shrink_to_fit(Table *table);
int table_append(Table *table, const Row *row);
int table_append_values(Table *table, const Value *values);
int table_get_row(const Table *table, size_t row_index, Row *row);
int table_load_csv(Table *table, const char *path);
size_t table_row_bytes(const Table *table);
size_t table_memory_bytes(const Table *table);

/* Pointers returned below stay valid until the next growth or table_free. */
static inline const Value *table_row_const(const Table *table, size_t row_index)
{
    return table->values + row_index * table->schema.column_count;
}

static inline Value *table_row_mut(Table *table, size_t row_index)
{
    return table->values + row_index * table->schema.column_count;
}

static inline const Value *table_cell(const Table *table, size_t row_index, size_t column_index)
{
    return table->values + row_index * table->schema.column_count + column_index;
}

#endif
