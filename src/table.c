#include "table.h"

/* -----------------------------------------------------------------------
 * Internal helpers
 * ----------------------------------------------------------------------- */
static char *xstrdup(const char *s) {
    if (!s) return NULL;
    char *d = malloc(strlen(s) + 1);
    if (d) strcpy(d, s);
    return d;
}

static void free_row(char **row, int ncols) {
    if (!row) return;
    for (int c = 0; c < ncols; c++) free(row[c]);
    free(row);
}

/* -----------------------------------------------------------------------
 * table_create / table_free
 * ----------------------------------------------------------------------- */
Table *table_create(int ncols, char **headers) {
    Table *t = calloc(1, sizeof(Table));
    if (!t) return NULL;
    t->ncols = ncols;
    if (ncols > 0 && headers) {
        t->headers = malloc(ncols * sizeof(char *));
        if (!t->headers) { free(t); return NULL; }
        for (int i = 0; i < ncols; i++)
            t->headers[i] = xstrdup(headers[i]);
    }
    return t;
}

void table_free(Table *t) {
    if (!t) return;
    if (t->headers) {
        for (int i = 0; i < t->ncols; i++) free(t->headers[i]);
        free(t->headers);
    }
    if (t->rows) {
        for (int r = 0; r < t->nrows; r++)
            free_row(t->rows[r], t->ncols);
        free(t->rows);
    }
    free(t);
}

/* -----------------------------------------------------------------------
 * table_add_row  –  copies every value string
 * ----------------------------------------------------------------------- */
int table_add_row(Table *t, char **values) {
    if (!t || !values) return -1;

    char ***newrows = realloc(t->rows, (t->nrows + 1) * sizeof(char **));
    if (!newrows) return -1;
    t->rows = newrows;

    t->rows[t->nrows] = malloc(t->ncols * sizeof(char *));
    if (!t->rows[t->nrows]) return -1;

    for (int i = 0; i < t->ncols; i++)
        t->rows[t->nrows][i] = xstrdup(values[i] ? values[i] : "");

    t->nrows++;
    return 0;
}

/* -----------------------------------------------------------------------
 * table_sort_by  –  insertion sort by header (stable, in-place)
 *
 * BUG FIXED: the original code had two dead stub functions
 *  (compare_rows_by_col / comparator_factory) that did nothing, but the
 *  sort itself accidentally worked via insertion sort directly.
 *  Those stubs are now removed and sort logic is kept + cleaned up.
 * ----------------------------------------------------------------------- */
int table_sort_by(Table *t, const char *header) {
    if (!t || !header) return -1;
    int idx = table_get_col_index(t, header);
    if (idx < 0) {
        fprintf(stderr, "sort_by: column '%s' not found\n", header);
        return -1;
    }
    /* Insertion sort – good enough for assignment-scale data */
    for (int i = 1; i < t->nrows; i++) {
        char **row = t->rows[i];
        int j = i - 1;
        while (j >= 0 && strcmp(t->rows[j][idx], row[idx]) > 0) {
            t->rows[j + 1] = t->rows[j];
            j--;
        }
        t->rows[j + 1] = row;
    }
    return 0;
}

/* -----------------------------------------------------------------------
 * table_delete_by_value
 *
 * BUG FIXED: realloc(ptr, 0) is implementation-defined (may return NULL
 *  or a unique pointer).  We now guard against nrows==0.
 * ----------------------------------------------------------------------- */
int table_delete_by_value(Table *t, int col, const char *value) {
    if (!t || col < 0 || col >= t->ncols || !value) return -1;

    int write = 0;
    for (int r = 0; r < t->nrows; r++) {
        if (strcmp(t->rows[r][col], value) != 0) {
            t->rows[write++] = t->rows[r];
        } else {
            free_row(t->rows[r], t->ncols);
        }
    }
    t->nrows = write;

    if (write == 0) {
        free(t->rows);
        t->rows = NULL;
    } else {
        char ***tmp = realloc(t->rows, write * sizeof(char **));
        if (tmp) t->rows = tmp;
    }
    return 0;
}

/* -----------------------------------------------------------------------
 * table_update_by_value
 * ----------------------------------------------------------------------- */
int table_update_by_value(Table *t,
                          int cond_col,  const char *cond_value,
                          int target_col, const char *new_value) {
    if (!t || !cond_value || !new_value) return -1;
    if (cond_col  < 0 || cond_col  >= t->ncols) return -1;
    if (target_col < 0 || target_col >= t->ncols) return -1;

    int count = 0;
    for (int r = 0; r < t->nrows; r++) {
        if (strcmp(t->rows[r][cond_col], cond_value) == 0) {
            free(t->rows[r][target_col]);
            t->rows[r][target_col] = xstrdup(new_value);
            count++;
        }
    }
    return count;   /* number of rows updated */
}

/* -----------------------------------------------------------------------
 * table_print  –  aligned columnar output
 * ----------------------------------------------------------------------- */
int table_print(const Table *t) {
    if (!t) return -1;

    /* Compute column widths */
    int *widths = calloc(t->ncols, sizeof(int));
    for (int c = 0; c < t->ncols; c++) {
        widths[c] = (int)strlen(t->headers[c] ? t->headers[c] : "");
        for (int r = 0; r < t->nrows; r++) {
            int len = (int)strlen(t->rows[r][c] ? t->rows[r][c] : "");
            if (len > widths[c]) widths[c] = len;
        }
    }

    /* Separator line */
    printf("+");
    for (int c = 0; c < t->ncols; c++) {
        for (int k = 0; k < widths[c] + 2; k++) putchar('-');
        printf("+");
    }
    printf("\n");

    /* Header row */
    printf("|");
    for (int c = 0; c < t->ncols; c++)
        printf(" %-*s |", widths[c], t->headers[c] ? t->headers[c] : "");
    printf("\n");

    /* Separator */
    printf("+");
    for (int c = 0; c < t->ncols; c++) {
        for (int k = 0; k < widths[c] + 2; k++) putchar('-');
        printf("+");
    }
    printf("\n");

    /* Data rows */
    for (int r = 0; r < t->nrows; r++) {
        printf("|");
        for (int c = 0; c < t->ncols; c++)
            printf(" %-*s |", widths[c], t->rows[r][c] ? t->rows[r][c] : "");
        printf("\n");
    }

    /* Bottom border */
    printf("+");
    for (int c = 0; c < t->ncols; c++) {
        for (int k = 0; k < widths[c] + 2; k++) putchar('-');
        printf("+");
    }
    printf("\n");

    free(widths);
    return 0;
}

/* -----------------------------------------------------------------------
 * table_store_csv  –  persist to disk on user demand
 * ----------------------------------------------------------------------- */
int table_store_csv(const Table *t, const char *filename) {
    if (!t || !filename) return -1;
    FILE *f = fopen(filename, "w");
    if (!f) { perror(filename); return -1; }

    for (int c = 0; c < t->ncols; c++) {
        if (c) fprintf(f, ",");
        fprintf(f, "%s", t->headers[c] ? t->headers[c] : "");
    }
    fprintf(f, "\n");

    for (int r = 0; r < t->nrows; r++) {
        for (int c = 0; c < t->ncols; c++) {
            if (c) fprintf(f, ",");
            /* Quote fields that contain commas */
            const char *v = t->rows[r][c] ? t->rows[r][c] : "";
            if (strchr(v, ',') || strchr(v, '"') || strchr(v, '\n'))
                fprintf(f, "\"%s\"", v);
            else
                fprintf(f, "%s", v);
        }
        fprintf(f, "\n");
    }
    fclose(f);
    return 0;
}

/* -----------------------------------------------------------------------
 * Join helpers – build combined header array
 * ----------------------------------------------------------------------- */
static Table *make_join_table(const Table *A, const Table *B) {
    int total = A->ncols + B->ncols;
    char **hdrs = malloc(total * sizeof(char *));
    char buf[256];
    for (int i = 0; i < A->ncols; i++) {
        snprintf(buf, sizeof(buf), "A.%s", A->headers[i] ? A->headers[i] : "");
        hdrs[i] = xstrdup(buf);
    }
    for (int i = 0; i < B->ncols; i++) {
        snprintf(buf, sizeof(buf), "B.%s", B->headers[i] ? B->headers[i] : "");
        hdrs[A->ncols + i] = xstrdup(buf);
    }
    Table *J = table_create(total, hdrs);
    for (int i = 0; i < total; i++) free(hdrs[i]);
    free(hdrs);
    return J;
}

static void add_join_row(Table *J, const Table *A, int rowA,
                                   const Table *B, int rowB) {
    int total = A->ncols + B->ncols;
    char **row = malloc(total * sizeof(char *));
    for (int c = 0; c < A->ncols; c++)
        row[c] = xstrdup(rowA >= 0 ? A->rows[rowA][c] : "NULL");
    for (int c = 0; c < B->ncols; c++)
        row[A->ncols + c] = xstrdup(rowB >= 0 ? B->rows[rowB][c] : "NULL");
    table_add_row(J, row);
    for (int c = 0; c < total; c++) free(row[c]);
    free(row);
}

/* -----------------------------------------------------------------------
 * table_join_inner
 * ----------------------------------------------------------------------- */
Table *table_join_inner(const Table *A, const Table *B, int keyA, int keyB) {
    if (!A || !B) return NULL;
    if (keyA < 0 || keyA >= A->ncols || keyB < 0 || keyB >= B->ncols) return NULL;

    Table *J = make_join_table(A, B);
    for (int i = 0; i < A->nrows; i++)
        for (int j = 0; j < B->nrows; j++)
            if (strcmp(A->rows[i][keyA], B->rows[j][keyB]) == 0)
                add_join_row(J, A, i, B, j);
    return J;
}

/* -----------------------------------------------------------------------
 * table_join_left  –  all rows from A, matched or NULL from B
 * ----------------------------------------------------------------------- */
Table *table_join_left(const Table *A, const Table *B, int keyA, int keyB) {
    if (!A || !B) return NULL;
    if (keyA < 0 || keyA >= A->ncols || keyB < 0 || keyB >= B->ncols) return NULL;

    Table *J = make_join_table(A, B);
    for (int i = 0; i < A->nrows; i++) {
        int matched = 0;
        for (int j = 0; j < B->nrows; j++) {
            if (strcmp(A->rows[i][keyA], B->rows[j][keyB]) == 0) {
                add_join_row(J, A, i, B, j);
                matched = 1;
            }
        }
        if (!matched) add_join_row(J, A, i, B, -1);
    }
    return J;
}

/* -----------------------------------------------------------------------
 * table_join_full  –  full outer join
 *
 * BUG FIXED: original code used bare A/B header names (no A./B. prefix)
 *  which caused silent column-name collisions.  Now uses A./B. prefixes
 *  consistent with inner join.
 * ----------------------------------------------------------------------- */
Table *table_join_full(const Table *A, const Table *B, int keyA, int keyB) {
    if (!A || !B) return NULL;
    if (keyA < 0 || keyA >= A->ncols || keyB < 0 || keyB >= B->ncols) return NULL;

    Table *J = make_join_table(A, B);

    int *matchedB = calloc(B->nrows, sizeof(int));

    for (int i = 0; i < A->nrows; i++) {
        int any = 0;
        for (int j = 0; j < B->nrows; j++) {
            if (strcmp(A->rows[i][keyA], B->rows[j][keyB]) == 0) {
                add_join_row(J, A, i, B, j);
                matchedB[j] = 1;
                any = 1;
            }
        }
        if (!any) add_join_row(J, A, i, B, -1);
    }
    for (int j = 0; j < B->nrows; j++)
        if (!matchedB[j]) add_join_row(J, A, -1, B, j);

    free(matchedB);
    return J;
}

/* -----------------------------------------------------------------------
 * table_get_col_index
 * ----------------------------------------------------------------------- */
int table_get_col_index(const Table *t, const char *header) {
    if (!t || !header) return -1;
    for (int i = 0; i < t->ncols; i++)
        if (t->headers[i] && strcmp(t->headers[i], header) == 0) return i;
    return -1;
}
