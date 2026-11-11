#ifndef MINIQUERY_MATERIALIZED_H
#define MINIQUERY_MATERIALIZED_H

#include <stddef.h>
#include <stdio.h>

#include "table.h"

typedef struct {
    size_t rows_in;
    size_t rows_out;
    size_t memory_bytes;
    double time_ms;
} QueryMetrics;

typedef enum {
    COMPARE_EQUAL,
    COMPARE_NOT_EQUAL,
    COMPARE_LESS,
    COMPARE_LESS_EQUAL,
    COMPARE_GREATER,
    COMPARE_GREATER_EQUAL
} Comparison;

typedef enum {
    AGG_COUNT,
    AGG_SUM,
    AGG_MIN,
    AGG_MAX,
    AGG_AVG
} AggregateFunction;

typedef struct {
    AggregateFunction function;
    size_t count;
    double value;
    int has_value;
} AggregateResult;

typedef struct {
    size_t build_rows;          /* Hash Join: BUILD input; Nested Loop: inner relation */
    size_t probe_rows;          /* Hash Join: PROBE input; Nested Loop: outer relation */
    size_t rows_out;
    size_t memory_bytes;        /* auxiliary structures + output buffer */
    size_t output_bytes;        /* reserved bytes of the materialized output */
    size_t hash_table_entries;
    size_t hash_table_bytes;
    size_t hash_buckets;
    size_t key_comparisons;     /* key equality tests executed */
    int build_is_left;          /* 1 when the left input was used as BUILD/inner */
    double build_time_ms;
    double probe_time_ms;
    double total_time_ms;
} JoinMetrics;

/*
 * Every operator below creates an independent materialized output table.
 * The output must arrive empty (initialized or freed); the caller owns it and
 * releases it with table_free after its last consumer finishes.
 */
int materialized_scan(const Table *input, Table *output, QueryMetrics *metrics);
int materialized_filter(const Table *input, const char *column_name,
                        Comparison comparison, Value value, Table *output,
                        QueryMetrics *metrics);
int materialized_filter_salary_gt(const Table *input, double min_salary,
                                  Table *output, QueryMetrics *metrics);
int materialized_project(const Table *input, const char *const *column_names,
                         size_t column_count, Table *output,
                         QueryMetrics *metrics);
int materialized_project_indices(const Table *input, const size_t *column_indexes,
                                 size_t column_count, Table *output,
                                 QueryMetrics *metrics);
int materialized_aggregate(const Table *input, AggregateFunction function,
                           const char *column_name, AggregateResult *result,
                           QueryMetrics *metrics);
int materialized_nested_loop_join(const Table *left, const char *left_key,
                                  const char *left_alias, const Table *right,
                                  const char *right_key, const char *right_alias,
                                  Table *output, JoinMetrics *metrics);
int materialized_hash_join(const Table *left, const char *left_key,
                           const char *left_alias, const Table *right,
                           const char *right_key, const char *right_alias,
                           Table *output, JoinMetrics *metrics);
/* Prints the metrics in the "[NAME]\nkey=value" layout of the stage 3 statement. */
void join_metrics_print(FILE *stream, const char *name, const JoinMetrics *metrics);
int materialized_group_by(const Table *input, const char *const *group_columns,
                          size_t group_column_count, AggregateFunction function,
                          const char *aggregate_column, const char *result_name,
                          Table *output, QueryMetrics *metrics);

#endif
