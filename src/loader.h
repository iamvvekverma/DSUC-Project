#ifndef LOADER_H
#define LOADER_H

#include "table.h"

/*
 * Load one KV-style file into a Table.
 *   Format: each record is a block of  Key: Value,  lines separated by
 *   blank lines.  The first record defines the schema (headers).
 *   File "0.txt" (index 0) is the schema-definition file and is skipped
 *   as a data record.
 */
Table *load_kv_file(const char *filename);

/*
 * Load from a directory (merges all *.txt files, sorted numerically)
 * or from a single file.
 */
Table *load_kv_path(const char *path);

#endif /* LOADER_H */
