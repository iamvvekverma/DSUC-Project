#include "loader.h"
#include <sys/stat.h>
#include <dirent.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* -----------------------------------------------------------------------
 * Internal helpers
 * ----------------------------------------------------------------------- */
static char *xstrdup(const char *s) {
    if (!s) return NULL;
    char *d = malloc(strlen(s) + 1);
    if (d) strcpy(d, s);
    return d;
}

/* Trim leading/trailing whitespace in-place; returns pointer into s */
static char *trim(char *s) {
    while (isspace((unsigned char)*s)) s++;
    if (*s == '\0') return s;
    char *end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) *end-- = '\0';
    return s;
}

/*
 * Strip a trailing comma that the sample data files use:
 *   "Rahul Kumar ,"  →  "Rahul Kumar"
 * We only strip ONE trailing comma (after trimming spaces).
 */
static void strip_trailing_comma(char *s) {
    size_t len = strlen(s);
    if (len > 0 && s[len - 1] == ',') {
        s[len - 1] = '\0';
        /* re-trim any spaces that were before the comma */
        size_t l2 = strlen(s);
        while (l2 > 0 && isspace((unsigned char)s[l2 - 1])) s[--l2] = '\0';
    }
}

/* BUG FIXED: was defined TWICE (at line 34 and again at 134 in original) */
static int is_dir(const char *path) {
    struct stat st;
    return (stat(path, &st) == 0 && S_ISDIR(st.st_mode));
}

static int is_regular_file(const char *path) {
    struct stat st;
    return (stat(path, &st) == 0 && S_ISREG(st.st_mode));
}

/* -----------------------------------------------------------------------
 * TempRec  –  key/value pairs collected while parsing one record block
 * ----------------------------------------------------------------------- */
typedef struct {
    char **keys;
    char **values;
    int    n;
} TempRec;

static void temprec_free(TempRec *r) {
    for (int i = 0; i < r->n; i++) { free(r->keys[i]); free(r->values[i]); }
    free(r->keys); free(r->values);
    r->keys = r->values = NULL; r->n = 0;
}

static void temprec_push(TempRec *r, const char *key, const char *val) {
    r->keys   = realloc(r->keys,   (r->n + 1) * sizeof(char *));
    r->values = realloc(r->values, (r->n + 1) * sizeof(char *));
    r->keys[r->n]   = xstrdup(key);
    r->values[r->n] = xstrdup(val);
    r->n++;
}

/* -----------------------------------------------------------------------
 * Detect whether a TempRec is a "schema record" (key == value for every
 * field, e.g.  Name:Name,  RollNo:Roll No, …).  File 0.txt in the
 * sample data is purely a header-definition record; loading it as a
 * data row would add a spurious row with header names as values.
 *
 * BUG FIXED: original code loaded 0.txt as a normal data row.
 * ----------------------------------------------------------------------- */
static int temprec_is_schema(const TempRec *r) {
    /* A schema record: every value equals the corresponding key */
    for (int i = 0; i < r->n; i++)
        if (strcmp(r->keys[i], r->values[i]) != 0) return 0;
    return (r->n > 0);
}

/* -----------------------------------------------------------------------
 * load_kv_file  –  parse one KV file into a Table
 * ----------------------------------------------------------------------- */
Table *load_kv_file(const char *filename) {
    FILE *f = fopen(filename, "r");
    if (!f) return NULL;

    char    line[4096];
    int     nheaders = 0;
    char  **headers  = NULL;

    /* Accumulate rows as raw arrays first, then build the Table */
    int      nrows   = 0;
    char ***rows    = NULL;

    TempRec cur = {NULL, NULL, 0};

    /* flush_record: commit the current TempRec as one row */
    /* (implemented as a local-state lambda via captured pointers) */

    while (fgets(line, sizeof(line), f)) {
        /* Remove newline */
        char *nl = strchr(line, '\n'); if (nl) *nl = '\0';
        char *p = trim(line);

        if (strlen(p) == 0) {
            /* --- flush current record --- */
            if (cur.n > 0 && !temprec_is_schema(&cur)) {
                if (nheaders == 0) {
                    nheaders = cur.n;
                    headers  = malloc((size_t)nheaders * sizeof(char *));
                    for (int _i = 0; _i < nheaders; _i++)
                        headers[_i] = xstrdup(cur.keys[_i]);
                }
                if (nheaders > 0) {
                    char **row = calloc((size_t)nheaders, sizeof(char *));
                    for (int _h = 0; _h < nheaders; _h++) {
                        char *val = NULL;
                        for (int _k = 0; _k < cur.n; _k++)
                            if (strcmp(cur.keys[_k], headers[_h]) == 0)
                                { val = cur.values[_k]; break; }
                        row[_h] = xstrdup(val ? val : "");
                    }
                    rows = realloc(rows, (size_t)(nrows + 1) * sizeof(char **));
                    rows[nrows++] = row;
                }
            }
            temprec_free(&cur);
            continue;
        }

        char *colon = strchr(p, ':');
        if (!colon) continue;     /* malformed line, skip */

        *colon = '\0';
        char *key = trim(p);
        char *val = trim(colon + 1);

        /* BUG FIXED: strip trailing comma from value */
        strip_trailing_comma(val);

        temprec_push(&cur, key, val);
    }
    /* flush last record if file doesn't end with blank line */
    if (cur.n > 0 && !temprec_is_schema(&cur)) {
        if (nheaders == 0) {
            nheaders = cur.n;
            headers  = malloc((size_t)nheaders * sizeof(char *));
            for (int _i = 0; _i < nheaders; _i++)
                headers[_i] = xstrdup(cur.keys[_i]);
        }
        if (nheaders > 0) {
            char **row = calloc((size_t)nheaders, sizeof(char *));
            for (int _h = 0; _h < nheaders; _h++) {
                char *val = NULL;
                for (int _k = 0; _k < cur.n; _k++)
                    if (strcmp(cur.keys[_k], headers[_h]) == 0)
                        { val = cur.values[_k]; break; }
                row[_h] = xstrdup(val ? val : "");
            }
            rows = realloc(rows, (size_t)(nrows + 1) * sizeof(char **));
            rows[nrows++] = row;
        }
    }
    temprec_free(&cur);

    fclose(f);

    if (nheaders == 0) {
        /* File had no usable data */
        for (int r = 0; r < nrows; r++) free(rows[r]);
        free(rows);
        return NULL;
    }

    Table *tbl = table_create(nheaders, headers);
    for (int i = 0; i < nheaders; i++) free(headers[i]);
    free(headers);

    if (tbl) {
        for (int r = 0; r < nrows; r++) {
            table_add_row(tbl, rows[r]);
            for (int c = 0; c < tbl->ncols; c++) free(rows[r][c]);
            free(rows[r]);
        }
    }
    free(rows);
    return tbl;
}

/* -----------------------------------------------------------------------
 * Numeric sort comparator for filenames like "1.txt", "10.txt", "9.txt"
 *
 * BUG FIXED: original sorted lexicographically so "10.txt" < "2.txt"
 * ----------------------------------------------------------------------- */
static int cmp_filenames(const void *a, const void *b) {
    const char *fa = *(const char **)a;
    const char *fb = *(const char **)b;
    /* Extract the basename's numeric prefix */
    const char *ba = strrchr(fa, '/'); ba = ba ? ba + 1 : fa;
    const char *bb = strrchr(fb, '/'); bb = bb ? bb + 1 : fb;
    long na = strtol(ba, NULL, 10);
    long nb = strtol(bb, NULL, 10);
    if (na != nb) return (na < nb) ? -1 : 1;
    return strcmp(ba, bb);
}

/* -----------------------------------------------------------------------
 * load_kv_path  –  file or directory
 * ----------------------------------------------------------------------- */
Table *load_kv_path(const char *path) {
    if (!path) return NULL;

    if (!is_dir(path)) return load_kv_file(path);

    /* Collect .txt file paths */
    DIR *d = opendir(path);
    if (!d) { perror(path); return NULL; }

    char   **files   = NULL;
    size_t   nfiles  = 0;

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.') continue;   /* skip . .. and hidden */
        char full[1024];
        snprintf(full, sizeof(full), "%s/%s", path, ent->d_name);
        if (!is_regular_file(full)) continue;
        files = realloc(files, (nfiles + 1) * sizeof(char *));
        files[nfiles++] = xstrdup(full);
    }
    closedir(d);

    if (nfiles == 0) { free(files); return NULL; }

    /* BUG FIXED: numeric sort so 1.txt < 2.txt < 10.txt */
    qsort(files, nfiles, sizeof(char *), cmp_filenames);

    Table *base = NULL;
    for (size_t i = 0; i < nfiles; i++) {
        Table *cur = load_kv_file(files[i]);
        if (!cur) { free(files[i]); continue; }

        if (!base) {
            base = cur;
        } else {
            /* Merge cur into base, aligning by header name */
            for (int r = 0; r < cur->nrows; r++) {
                char **row = calloc(base->ncols, sizeof(char *));
                for (int h = 0; h < base->ncols; h++) {
                    /* BUG FIXED: original code over-wrote row[h] twice when idx >= 0 */
                    int idx = table_get_col_index(cur, base->headers[h]);
                    row[h] = xstrdup(idx >= 0 ? cur->rows[r][idx] : "");
                }
                table_add_row(base, row);
                for (int h = 0; h < base->ncols; h++) free(row[h]);
                free(row);
            }
            table_free(cur);
        }
        free(files[i]);
    }
    free(files);
    return base;
}
