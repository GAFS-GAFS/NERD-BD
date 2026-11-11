#include "table.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#define INITIAL_CAPACITY 16
#define CSV_LINE_MAX_LEN 512

static int table_reserve(Table *table, size_t capacity)
{
    Value *values;
    size_t width = table->schema.column_count;

    if (width == 0 || capacity == 0) return 0;
    if (capacity > (size_t)-1 / width / sizeof(*values)) return 0;
    values = realloc(table->values, capacity * width * sizeof(*values));
    if (values == NULL) return 0;
    table->values = values;
    table->capacity = capacity;
    return 1;
}

static int table_grow(Table *table)
{
    size_t capacity = table->capacity == 0 ? INITIAL_CAPACITY : table->capacity * 2;
    if (capacity < table->capacity) return 0;
    return table_reserve(table, capacity);
}

void schema_init(Schema *schema)
{
    schema->column_count = 0;
}

int schema_add_column(Schema *schema, const char *name, DataType type)
{
    Column *column;
    if (schema == NULL || name == NULL || name[0] == '\0' ||
        schema->column_count >= TABLE_MAX_COLUMNS) return 0;
    column = &schema->columns[schema->column_count++];
    (void)snprintf(column->name, sizeof(column->name), "%s", name);
    column->type = type;
    return 1;
}

int schema_find_column(const Schema *schema, const char *name, size_t *index)
{
    size_t i;
    if (schema == NULL || name == NULL) return 0;
    for (i = 0; i < schema->column_count; ++i) {
        if (strcmp(schema->columns[i].name, name) == 0) {
            if (index != NULL) *index = i;
            return 1;
        }
    }
    return 0;
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

Schema employee_stage3_schema(void)
{
    Schema schema;
    schema_init(&schema);
    (void)schema_add_column(&schema, "id", DATA_TYPE_INT);
    (void)schema_add_column(&schema, "department_id", DATA_TYPE_INT);
    (void)schema_add_column(&schema, "salary", DATA_TYPE_DOUBLE);
    (void)schema_add_column(&schema, "age", DATA_TYPE_INT);
    return schema;
}

Schema department_schema(void)
{
    Schema schema;
    schema_init(&schema);
    (void)schema_add_column(&schema, "id", DATA_TYPE_INT);
    (void)schema_add_column(&schema, "name", DATA_TYPE_STRING);
    (void)schema_add_column(&schema, "region", DATA_TYPE_STRING);
    return schema;
}

Value value_int(int value)
{
    Value result = { .type = DATA_TYPE_INT, .as.int_value = value };
    return result;
}

Value value_double(double value)
{
    Value result = { .type = DATA_TYPE_DOUBLE, .as.double_value = value };
    return result;
}

Value value_string(const char *value)
{
    Value result = { .type = DATA_TYPE_STRING, .as.string_value = "" };
    if (value != NULL) {
        (void)snprintf(result.as.string_value, sizeof(result.as.string_value), "%s", value);
    }
    return result;
}

int value_is_numeric(const Value *value)
{
    return value != NULL && (value->type == DATA_TYPE_INT || value->type == DATA_TYPE_DOUBLE);
}

double value_as_double(const Value *value)
{
    return value->type == DATA_TYPE_INT ? (double)value->as.int_value : value->as.double_value;
}

int value_equals(const Value *left, const Value *right)
{
    if (left->type == DATA_TYPE_STRING && right->type == DATA_TYPE_STRING) {
        return strcmp(left->as.string_value, right->as.string_value) == 0;
    }
    return value_is_numeric(left) && value_is_numeric(right) &&
           value_as_double(left) == value_as_double(right);
}

/* Final avalanche step so that close keys (1, 2, 3...) spread over the buckets. */
static unsigned long hash_mix(unsigned long hash)
{
    hash ^= hash >> 16;
    hash *= 0x45d9f3bUL;
    hash ^= hash >> 16;
    return hash;
}

/* djb2 over the double bytes (numbers) or the characters (strings), then mixed. */
unsigned long value_hash(const Value *value)
{
    const unsigned char *text;
    unsigned long hash = 5381UL;
    if (value_is_numeric(value)) {
        union { double number; unsigned char bytes[sizeof(double)]; } bits;
        size_t i;
        bits.number = value_as_double(value);
        if (bits.number == 0.0) bits.number = 0.0; /* -0.0 and 0.0 compare equal */
        for (i = 0; i < sizeof(bits.bytes); ++i) hash = ((hash << 5) + hash) ^ bits.bytes[i];
        return hash_mix(hash);
    }
    text = (const unsigned char *)value->as.string_value;
    while (*text != '\0') hash = ((hash << 5) + hash) ^ *text++;
    return hash_mix(hash);
}

void row_init(Row *row, const Schema *schema)
{
    size_t i;
    memset(row, 0, sizeof(*row));
    for (i = 0; schema != NULL && i < schema->column_count; ++i) {
        row->values[i].type = schema->columns[i].type;
    }
}

int row_set_value(Row *row, const Schema *schema, size_t column_index, Value value)
{
    if (row == NULL || schema == NULL || column_index >= schema->column_count ||
        schema->columns[column_index].type != value.type) return 0;
    row->values[column_index] = value;
    return 1;
}

int row_set_int(Row *row, const Schema *schema, size_t column_index, int value)
{
    return row_set_value(row, schema, column_index, value_int(value));
}

int row_set_double(Row *row, const Schema *schema, size_t column_index, double value)
{
    return row_set_value(row, schema, column_index, value_double(value));
}

int row_set_string(Row *row, const Schema *schema, size_t column_index, const char *value)
{
    return row_set_value(row, schema, column_index, value_string(value));
}

void table_init(Table *table)
{
    table_init_with_schema(table, employee_schema());
}

void table_init_with_schema(Table *table, Schema schema)
{
    table->schema = schema;
    table->values = NULL;
    table->size = 0;
    table->capacity = 0;
}

void table_free(Table *table)
{
    if (table == NULL) return;
    free(table->values);
    table->values = NULL;
    table->size = 0;
    table->capacity = 0;
    schema_init(&table->schema);
}

int table_reserve_rows(Table *table, size_t row_capacity)
{
    if (table == NULL) return 0;
    if (row_capacity <= table->capacity) return 1;
    return table_reserve(table, row_capacity);
}

int table_shrink_to_fit(Table *table)
{
    if (table == NULL) return 0;
    if (table->size == 0 || table->size == table->capacity) return 1;
    return table_reserve(table, table->size);
}

int table_append_values(Table *table, const Value *values)
{
    size_t width;
    if (table == NULL || values == NULL || table->schema.column_count == 0) return 0;
    if (table->size == table->capacity && !table_grow(table)) return 0;
    width = table->schema.column_count;
    memcpy(table->values + table->size * width, values, width * sizeof(*values));
    table->size++;
    return 1;
}

int table_append(Table *table, const Row *row)
{
    if (row == NULL) return 0;
    return table_append_values(table, row->values);
}

int table_get_row(const Table *table, size_t row_index, Row *row)
{
    size_t width;
    if (table == NULL || row == NULL || row_index >= table->size) return 0;
    width = table->schema.column_count;
    row_init(row, &table->schema);
    memcpy(row->values, table->values + row_index * width, width * sizeof(*row->values));
    return 1;
}

static void trim_newline(char *text)
{
    text[strcspn(text, "\r\n")] = '\0';
}

static int parse_int(const char *text, int *value)
{
    char *end;
    long parsed;
    errno = 0;
    parsed = strtol(text, &end, 10);
    if (errno != 0 || *text == '\0' || *end != '\0' || parsed < -2147483647L - 1L ||
        parsed > 2147483647L) return 0;
    *value = (int)parsed;
    return 1;
}

static int parse_double(const char *text, double *value)
{
    char *end;
    errno = 0;
    *value = strtod(text, &end);
    return errno == 0 && *text != '\0' && *end == '\0';
}

static int parse_csv_row(char *line, Row *row, const Schema *schema)
{
    size_t i;
    char *token = strtok(line, ",");
    row_init(row, schema);
    for (i = 0; i < schema->column_count; ++i) {
        int int_value;
        double double_value;
        if (token == NULL) return 0;
        if (schema->columns[i].type == DATA_TYPE_INT) {
            if (!parse_int(token, &int_value) || !row_set_int(row, schema, i, int_value)) return 0;
        } else if (schema->columns[i].type == DATA_TYPE_DOUBLE) {
            if (!parse_double(token, &double_value) || !row_set_double(row, schema, i, double_value)) return 0;
        } else if (strlen(token) >= VALUE_STRING_MAX_LEN ||
                   !row_set_string(row, schema, i, token)) {
            return 0;
        }
        token = strtok(NULL, ",");
    }
    return token == NULL;
}

int table_load_csv(Table *table, const char *path)
{
    FILE *file;
    char line[CSV_LINE_MAX_LEN];

    if (table == NULL || path == NULL || table->schema.column_count == 0) return 0;
    file = fopen(path, "r");
    if (file == NULL) return 0;
    if (fgets(line, sizeof(line), file) == NULL) {
        fclose(file);
        return 1;
    }
    while (fgets(line, sizeof(line), file) != NULL) {
        Row row;
        trim_newline(line);
        if (line[0] == '\0') continue;
        if (!parse_csv_row(line, &row, &table->schema) || !table_append(table, &row)) {
            fclose(file);
            table_free(table);
            return 0;
        }
    }
    fclose(file);
    (void)table_shrink_to_fit(table); /* a failed shrink keeps the larger, valid buffer */
    return 1;
}

size_t table_row_bytes(const Table *table)
{
    return table == NULL ? 0 : table->schema.column_count * sizeof(*table->values);
}

size_t table_memory_bytes(const Table *table)
{
    return table == NULL ? 0 : table->capacity * table_row_bytes(table);
}
