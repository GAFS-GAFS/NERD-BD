# Materialized Execution Model

This project implements only Group A: materialization.

## Concept

In a materialized execution model, each operator produces a complete result
before the next operator consumes it. The intermediate result is stored in
memory as a new table.

This is simple to understand and easy to inspect, but it can use more memory
than models that pass rows directly between operators.

## Implemented Flow

The current fixed query is:

```sql
WHERE salary > 5000
```

There is no SQL parser. The query is hardcoded on purpose.

Execution steps:

1. Load `data/funcionarios.csv` into the original `Table`.
2. Run a full scan over the original `Table`.
3. Apply the materialized filter `salary > 5000`.
4. Copy approved rows into a new intermediate `Table`.
5. Run aggregations over the materialized `Table`.

## Why There Is An Intermediate Table

The intermediate table makes the materialized model explicit. The filter does
not only decide whether a row passes; it also stores every approved row in a new
table before aggregation starts.

This makes the execution easy to explain in a meeting:

```text
CSV -> original Table -> scan -> materialized filter -> intermediate Table -> aggregate
```

## Metrics

The program prints:

- `rows_in`: number of rows scanned from the original table.
- `rows_out`: number of rows stored in the materialized table.
- `avg_salary`: average salary in the materialized table.
- `time_ms`: elapsed time for the materialized filter operator.

## Not Implemented

The project intentionally does not implement:

- SQL parser.
- Pipeline execution.
- Vectorized execution.
- Dynamic query selection.
