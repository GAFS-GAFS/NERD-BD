# miniquery-c

`miniquery-c` is a didactic C project about database query execution models.
The current version implements only Group A: materialization.

## Structure

```text
miniquery-c/
  src/
  include/
  data/
  tests/
  bench/
  docs/
  Makefile
  README.md
```

Main files:

- `include/table.h`: row, schema, column, and table definitions.
- `include/materialized.h`: scan, materialized filter, aggregate, and metrics APIs.
- `src/table.c`: schema helpers, table memory management, and CSV loading.
- `src/scan.c`: full table scan operator.
- `src/materialized.c`: materialized filter operator.
- `src/aggregate.c`: `COUNT(*)` and `AVG(salary)` aggregation.
- `tests/test_materialized.c`: simple unit tests for the current model.
- `docs/materialization.md`: explanation of the materialized model.

## Build

```sh
make
```

The project uses C11 and only the C standard library.

## Run

```sh
./miniquery
```

The program reads `data/funcionarios.csv`, loads all rows into memory, scans the
table, executes the fixed query `WHERE salary > 5000`, materializes approved
rows into a new table, and prints:

```text
rows_in=...
rows_out=...
avg_salary=...
time_ms=...
```

## Test

```sh
make test
```

## Clean

```sh
make clean
```

## Materialized Execution Model

Materialization executes an operator and stores its full result before the next
step consumes it.

In this project:

1. The CSV is loaded into an original `Table` with an associated `Schema`.
2. A full scan exposes the rows from the original table.
3. The fixed filter `salary > 5000` is applied.
4. Approved rows are copied into a new intermediate `Table`.
5. Aggregations run over the materialized table:
   - `COUNT(*)`
   - `AVG(salary)`

This intentionally creates an intermediate table to make the materialized model
easy to inspect and explain.

## Fixed Query

There is no SQL parser. The only supported query is hardcoded:

```sql
WHERE salary > 5000
```

The focus is the execution model, not query parsing.

## Next Steps

- Add larger CSV datasets for experiments.
- Add benchmark cases in `bench/`.
- Add more metrics, such as memory used by the materialized table.
- Compare this model later with pipeline and vectorized execution models.
