#include "query.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -----------------------------------------------------------------------
 * String utilities
 * ----------------------------------------------------------------------- */
static char *xstrdup(const char *s) {
    if (!s) return NULL;
    char *d = malloc(strlen(s) + 1);
    if (d) strcpy(d, s);
    return d;
}

static char *trim(char *s) {
    while (isspace((unsigned char)*s)) s++;
    if (*s == '\0') return s;
    char *e = s + strlen(s) - 1;
    while (e > s && isspace((unsigned char)*e)) *e-- = '\0';
    return s;
}

/* Case-insensitive prefix check */
static int ci_startswith(const char *s, const char *prefix) {
    while (*prefix)
        if (tolower((unsigned char)*s++) != tolower((unsigned char)*prefix++)) return 0;
    return 1;
}

/* -----------------------------------------------------------------------
 * find_col  –  locate a column by name in a table.
 *
 * Accepts:   "Name"      exact match
 *            "A.Name"    strips "A." then exact match
 *            "B.Name"    strips "B." then exact match
 *
 * BUG FIXED: original WHERE clause only did exact match, ignoring
 *  A./B. prefixed column names that are the natural result of a JOIN.
 * ----------------------------------------------------------------------- */
static int find_col(const Table *t, const char *name) {
    if (!t || !name) return -1;
    /* Try exact match first */
    int idx = table_get_col_index(t, name);
    if (idx >= 0) return idx;
    /* Strip one level of "X." prefix */
    const char *dot = strchr(name, '.');
    if (dot) {
        idx = table_get_col_index(t, dot + 1);
        if (idx >= 0) return idx;
    }
    return -1;
}

/* -----------------------------------------------------------------------
 * parse_col_list  –  split "A.Name, B.Roll, ..." into tokens
 * ----------------------------------------------------------------------- */
static void parse_col_list(const char *s, char ***out, int *n) {
    char *tmp = xstrdup(s);
    int cap = 8; *n = 0;
    *out = malloc(cap * sizeof(char *));
    char *tok = strtok(tmp, ",");
    while (tok) {
        char *t = trim(tok);
        if (strlen(t) > 0) {
            if (*n >= cap) { cap *= 2; *out = realloc(*out, cap * sizeof(char *)); }
            (*out)[(*n)++] = xstrdup(t);
        }
        tok = strtok(NULL, ",");
    }
    free(tmp);
}

/* -----------------------------------------------------------------------
 * project  –  build a result table keeping only the requested columns
 * ----------------------------------------------------------------------- */
static Table *project(const Table *src, char **cols, int ncols) {
    /* Resolve each column name to an index once */
    int *idxs = malloc(ncols * sizeof(int));
    for (int i = 0; i < ncols; i++)
        idxs[i] = find_col(src, cols[i]);

    char **hdrs = malloc(ncols * sizeof(char *));
    for (int i = 0; i < ncols; i++)
        hdrs[i] = xstrdup(cols[i]);

    Table *dst = table_create(ncols, hdrs);
    for (int i = 0; i < ncols; i++) free(hdrs[i]);
    free(hdrs);

    for (int r = 0; r < src->nrows; r++) {
        char **row = malloc(ncols * sizeof(char *));
        for (int i = 0; i < ncols; i++) {
            int ci = idxs[i];
            row[i] = xstrdup(ci >= 0 ? src->rows[r][ci] : "");
        }
        table_add_row(dst, row);
        for (int i = 0; i < ncols; i++) free(row[i]);
        free(row);
    }
    free(idxs);
    return dst;
}

/* -----------------------------------------------------------------------
 * filter  –  return a new table keeping only rows where col==value
 * ----------------------------------------------------------------------- */
static Table *filter(const Table *src, int col, const char *value) {
    Table *dst = table_create(src->ncols, src->headers);
    for (int r = 0; r < src->nrows; r++) {
        if (col >= 0 && strcmp(src->rows[r][col], value) == 0) {
            table_add_row(dst, src->rows[r]);
        }
    }
    return dst;
}

/* -----------------------------------------------------------------------
 * run_query
 *
 * Grammar (whitespace-tolerant, case-insensitive keywords):
 *
 *   SELECT  ( * | col [, col]* )
 *   FROM    ( A | B )
 *   [ JOIN  ( A | B )  ON  col = col ]
 *   [ WHERE col = "value" ]
 *
 * BUG FIXED (multiple):
 *   1. WHERE clause now resolves A./B. prefixed column names.
 *   2. Cleanup (table_free) is now safe: intermediate tables that were
 *      allocated here are always freed exactly once; the original A/B
 *      pointers are never freed.
 *   3. "base != A" const-cast comparison was unreliable; now we use an
 *      explicit boolean to track ownership.
 * ----------------------------------------------------------------------- */
Table *run_query(const char *query_str, const Table *A, const Table *B) {
    if (!query_str) return NULL;

    char q[2048];
    strncpy(q, query_str, sizeof(q) - 1);
    q[sizeof(q) - 1] = '\0';

    /* ---- Locate mandatory keywords ---- */
    char *sel_kw = NULL, *from_kw = NULL;
    /* Case-insensitive search */
    for (char *p = q; *p; p++) {
        if (!sel_kw  && ci_startswith(p, "SELECT ")) sel_kw  = p;
        if (!from_kw && ci_startswith(p, "FROM "))   from_kw = p;
    }
    if (!sel_kw || !from_kw || from_kw <= sel_kw) {
        fprintf(stderr, "Query error: missing SELECT or FROM\n");
        return NULL;
    }

    /* ---- Extract SELECT columns ---- */
    int sel_len = (int)(from_kw - (sel_kw + 7));
    char selbuf[512];
    strncpy(selbuf, sel_kw + 7, sel_len);
    selbuf[sel_len < 511 ? sel_len : 511] = '\0';
    trim(selbuf);

    char **sel_cols = NULL; int sel_n = 0;
    int select_star = (strcmp(trim(selbuf), "*") == 0);
    if (!select_star)
        parse_col_list(selbuf, &sel_cols, &sel_n);

    /* ---- Detect JOIN ---- */
    char *join_kw  = strstr(q, "JOIN ");
    if (!join_kw) join_kw = strstr(q, "join ");

    char *where_kw = strstr(q, "WHERE ");
    if (!where_kw) where_kw = strstr(q, "where ");

    /* ---- Build base table ---- */
    Table *base = NULL;
    int    base_owned = 0;   /* 1 if we allocated it and must free it */

    if (join_kw) {
        if (!A || !B) {
            fprintf(stderr, "Query error: JOIN requires both A and B loaded\n");
            goto cleanup_cols;
        }
        /* Parse ON clause: ON A.<col> = B.<col>  (or B./A. order) */
        char *on_kw = strstr(join_kw, " ON ");
        if (!on_kw) on_kw = strstr(join_kw, " on ");

        int idxA = 0, idxB = 0;   /* default to first column */
        if (on_kw) {
            char left[128], right[128];
            if (sscanf(on_kw + 4, "%127s = %127s", left, right) == 2) {
                /* Determine which side belongs to A and which to B */
                if (ci_startswith(left, "A.")) {
                    idxA = table_get_col_index(A, left + 2);
                    idxB = table_get_col_index(B, right + (ci_startswith(right, "B.") ? 2 : 0));
                } else if (ci_startswith(left, "B.")) {
                    idxB = table_get_col_index(B, left + 2);
                    idxA = table_get_col_index(A, right + (ci_startswith(right, "A.") ? 2 : 0));
                } else {
                    idxA = find_col(A, left);
                    idxB = find_col(B, right);
                }
            }
        }
        if (idxA < 0) idxA = 0;
        if (idxB < 0) idxB = 0;

        base = table_join_inner(A, B, idxA, idxB);
        base_owned = 1;
    } else {
        /* Single table: FROM A or FROM B */
        char *from_arg = from_kw + 5;
        while (*from_arg == ' ') from_arg++;
        if (tolower((unsigned char)*from_arg) == 'b' && B)
            base = (Table *)B;
        else
            base = (Table *)A;
        base_owned = 0;
    }

    if (!base) {
        fprintf(stderr, "Query error: base table is NULL\n");
        goto cleanup_cols;
    }

    /* ---- WHERE filter ---- */
    /*
     * Ownership model (simplified):
     *   'scratch' is the one table we may need to free on the way out.
     *   It is either:
     *     - base     (if base_owned and no WHERE produced a new table), or
     *     - filtered (a newly allocated table produced by the WHERE clause, base freed already)
     *     - NULL     (base not owned; nothing to free)
     */
    Table *scratch       = base_owned ? base : NULL;
    Table *working       = base;   /* the table we actually query against */

    if (where_kw) {
        char cond_col[128], cond_val[512];
        int parsed = (sscanf(where_kw + 6, "%127s = \"%511[^\"]\"", cond_col, cond_val) == 2);
        if (!parsed)
            parsed = (sscanf(where_kw + 6, "%127s = %511s", cond_col, cond_val) == 2);

        if (parsed) {
            trim(cond_col); trim(cond_val);
            int widx = find_col(base, cond_col);
            if (widx < 0) {
                fprintf(stderr, "Query warning: WHERE column '%s' not found\n", cond_col);
            } else {
                Table *filtered = filter(base, widx, cond_val);
                /* Free the old scratch (=base) if we owned it */
                if (scratch) table_free(scratch);
                scratch = filtered;   /* now own filtered */
                working = filtered;
            }
        }
    }

    /* ---- Projection ---- */
    Table *result = NULL;
    if (working) {
        if (select_star || sel_n == 0) {
            /* Transfer ownership: result takes scratch, we must not free it below */
            result  = working;
            scratch = NULL;   /* relinquish ownership */
        } else {
            result = project(working, sel_cols, sel_n);
            /* scratch still owned; will be freed below */
        }
    }

    /* ---- Cleanup: free any intermediate table we still own ---- */
    if (scratch) table_free(scratch);

    /* Free column token list */
    for (int i = 0; i < sel_n; i++) free(sel_cols[i]);
    free(sel_cols);

    return result;

cleanup_cols:
    for (int i = 0; i < sel_n; i++) free(sel_cols[i]);
    free(sel_cols);
    return NULL;
}
