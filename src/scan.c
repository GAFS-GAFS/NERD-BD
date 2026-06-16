#include "materialized.h"

const Table *scan_table(const Table *table)
{
    /* Scan exposes the full input table for the next materialized operator. */
    return table;
}
