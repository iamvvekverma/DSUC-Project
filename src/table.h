#ifndef TABLE_H
#define TABLE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -----------------------------------------------------------------------
 * Table  –  in-memory relation (header row + data rows, all strings)
 * ----------------------------------------------------------------------- */
typedef struct {
    int    ncols;
    char **headers;   /* [ncols]          */
    int    nrows;
    char ***rows;     /* [nrows][ncols]   */
} Table;

/* Lifecycle */
Table *table_create    (int ncols, char **headers);
void   table_free      (Table *t);

/* Mutation (all in RAM – files are NOT touched) */
int    table_add_row         (Table *t, char **values);
int    table_sort_by         (Table *t, const char *header);
int    table_delete_by_value (Table *t, int col, const char *value);
int    table_update_by_value (Table *t,
                              int cond_col, const char *cond_value,
                              int target_col, const char *new_value);

/* Output */
int    table_print     (const Table *t);
int    table_store_csv (const Table *t, const char *filename);

/* Joins */
Table *table_join_inner (const Table *A, const Table *B, int keyA, int keyB);
Table *table_join_left  (const Table *A, const Table *B, int keyA, int keyB);
Table *table_join_full  (const Table *A, const Table *B, int keyA, int keyB);

/* Utility */
int    table_get_col_index (const Table *t, const char *header);

#endif /* TABLE_H */
