#ifndef QUERY_H
#define QUERY_H

#include "table.h"

/*
 * Execute a simple SQL-like query.
 *
 * Supported syntax:
 *   SELECT <col,...> | * FROM A [JOIN B ON A.<col> = B.<col>]
 *                              [WHERE <col> = "<value>"]
 *
 * Column names in SELECT and WHERE may use  A.<col>  or  B.<col>  prefixes
 * when a JOIN is active, or bare names otherwise.
 *
 * Returns a newly allocated Table (caller must table_free() it), or NULL.
 */
Table *run_query(const char *query_str, const Table *A, const Table *B);

#endif /* QUERY_H */
