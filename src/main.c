#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "table.h"
#include "loader.h"
#include "query.h"

/* -----------------------------------------------------------------------
 * Timing helpers
 * ----------------------------------------------------------------------- */
static long elapsed_ms(struct timespec *start, struct timespec *end) {
    return (end->tv_sec  - start->tv_sec)  * 1000L
         + (end->tv_nsec - start->tv_nsec) / 1000000L;
}

static long elapsed_us(struct timespec *start, struct timespec *end) {
    return (end->tv_sec  - start->tv_sec)  * 1000000L
         + (end->tv_nsec - start->tv_nsec) / 1000L;
}

/* -----------------------------------------------------------------------
 * read_line  –  safe fgets wrapper, returns 0 on EOF
 * ----------------------------------------------------------------------- */
static int read_line(char *buf, size_t sz) {
    if (!fgets(buf, (int)sz, stdin)) return 0;
    size_t len = strlen(buf);
    if (len > 0 && buf[len - 1] == '\n') buf[len - 1] = '\0';
    return 1;
}

/* -----------------------------------------------------------------------
 * Resolve a column name entered by the user into an index.
 * Prints an error if not found.
 * ----------------------------------------------------------------------- */
static int resolve_col(const Table *t, const char *name) {
    int idx = table_get_col_index(t, name);
    if (idx < 0)
        fprintf(stderr, "Column '%s' not found. Available: ", name);
    if (idx < 0 && t) {
        for (int i = 0; i < t->ncols; i++)
            fprintf(stderr, "%s%s", i ? ", " : "", t->headers[i]);
        fprintf(stderr, "\n");
    }
    return idx;
}

/* -----------------------------------------------------------------------
 * print_menu
 *
 * BUG FIXED: original code listed Exit=13 BEFORE the query option=14,
 *  but intercepted choice==13 before entering the switch, making the
 *  ordering confusing and the query option hard to discover.
 *  Menu is now cleanly numbered 0-15 with Exit at the end.
 * ----------------------------------------------------------------------- */
static void print_menu(void) {
    printf("\n╔══════════════════════════════════════╗\n");
    printf("║       DSUC Data Engine  v2.0         ║\n");
    printf("╠══════════════════════════════════════╣\n");
    printf("║  [1]  Load directory/file into A     ║\n");
    printf("║  [2]  Load directory/file into B     ║\n");
    printf("║  [3]  Show table A                   ║\n");
    printf("║  [4]  Show table B                   ║\n");
    printf("╠──────────────────────────────────────╣\n");
    printf("║  [5]  Insert row into A              ║\n");
    printf("║  [6]  Update row(s) in A             ║\n");
    printf("║  [7]  Delete row(s) from A           ║\n");
    printf("║  [8]  Sort A by column               ║\n");
    printf("╠──────────────────────────────────────╣\n");
    printf("║  [9]  Inner join  A ⋈ B              ║\n");
    printf("║  [10] Left join   A ⟕ B              ║\n");
    printf("║  [11] Full outer  A ⟗ B              ║\n");
    printf("╠──────────────────────────────────────╣\n");
    printf("║  [12] Run custom query               ║\n");
    printf("╠──────────────────────────────────────╣\n");
    printf("║  [13] Save A to CSV                  ║\n");
    printf("║  [14] Save B to CSV                  ║\n");
    printf("╠──────────────────────────────────────╣\n");
    printf("║  [0]  Exit                           ║\n");
    printf("╚══════════════════════════════════════╝\n");
    printf("Choice: ");
}

/* -----------------------------------------------------------------------
 * Interactive menu
 * ----------------------------------------------------------------------- */
static void run_menu(Table **Ap, Table **Bp) {
    char line[1024];
    struct timespec t0, t1;

    while (1) {
        print_menu();
        if (!read_line(line, sizeof(line))) break;
        int choice = atoi(line);

        switch (choice) {
        case 0:
            return;

        /* ---- Load ---- */
        case 1:
            printf("Path to directory or file for A: ");
            if (!read_line(line, sizeof(line))) break;
            if (*Ap) table_free(*Ap);
            clock_gettime(CLOCK_MONOTONIC, &t0);
            *Ap = load_kv_path(line);
            clock_gettime(CLOCK_MONOTONIC, &t1);
            if (*Ap) printf("Loaded A: %d columns, %d rows  (%ld ms)\n",
                            (*Ap)->ncols, (*Ap)->nrows, elapsed_ms(&t0, &t1));
            else     printf("Failed to load '%s'\n", line);
            break;

        case 2:
            printf("Path to directory or file for B: ");
            if (!read_line(line, sizeof(line))) break;
            if (*Bp) table_free(*Bp);
            clock_gettime(CLOCK_MONOTONIC, &t0);
            *Bp = load_kv_path(line);
            clock_gettime(CLOCK_MONOTONIC, &t1);
            if (*Bp) printf("Loaded B: %d columns, %d rows  (%ld ms)\n",
                            (*Bp)->ncols, (*Bp)->nrows, elapsed_ms(&t0, &t1));
            else     printf("Failed to load '%s'\n", line);
            break;

        /* ---- Print ---- */
        case 3:
            if (*Ap) table_print(*Ap);
            else printf("A is not loaded.\n");
            break;

        case 4:
            if (*Bp) table_print(*Bp);
            else printf("B is not loaded.\n");
            break;

        /* ---- Insert ---- */
        case 5: {
            if (!*Ap) { printf("Load A first.\n"); break; }
            char **row = malloc((*Ap)->ncols * sizeof(char *));
            for (int i = 0; i < (*Ap)->ncols; i++) {
                printf("  %s: ", (*Ap)->headers[i]);
                read_line(line, sizeof(line));
                row[i] = strdup(line);
            }
            clock_gettime(CLOCK_MONOTONIC, &t0);
            table_add_row(*Ap, row);
            clock_gettime(CLOCK_MONOTONIC, &t1);
            for (int i = 0; i < (*Ap)->ncols; i++) free(row[i]);
            free(row);
            printf("Row inserted. (%ld µs)\n", elapsed_us(&t0, &t1));
            break;
        }

        /* ---- Update ---- */
        case 6: {
            if (!*Ap) { printf("Load A first.\n"); break; }
            char cond_col_name[128], cond_val[256], tgt_col_name[128], new_val[256];
            printf("Condition column: "); read_line(cond_col_name, sizeof(cond_col_name));
            printf("Condition value:  "); read_line(cond_val, sizeof(cond_val));
            printf("Target column:    "); read_line(tgt_col_name, sizeof(tgt_col_name));
            printf("New value:        "); read_line(new_val, sizeof(new_val));
            int cc = resolve_col(*Ap, cond_col_name);
            int tc = resolve_col(*Ap, tgt_col_name);
            if (cc < 0 || tc < 0) break;
            clock_gettime(CLOCK_MONOTONIC, &t0);
            int n = table_update_by_value(*Ap, cc, cond_val, tc, new_val);
            clock_gettime(CLOCK_MONOTONIC, &t1);
            printf("Updated %d row(s). (%ld µs)\n", n, elapsed_us(&t0, &t1));
            break;
        }

        /* ---- Delete ---- */
        case 7: {
            if (!*Ap) { printf("Load A first.\n"); break; }
            char col_name[128], del_val[256];
            printf("Column to match: "); read_line(col_name, sizeof(col_name));
            printf("Value to delete: "); read_line(del_val, sizeof(del_val));
            int cc = resolve_col(*Ap, col_name);
            if (cc < 0) break;
            clock_gettime(CLOCK_MONOTONIC, &t0);
            table_delete_by_value(*Ap, cc, del_val);
            clock_gettime(CLOCK_MONOTONIC, &t1);
            printf("Delete done. A now has %d rows. (%ld µs)\n",
                   (*Ap)->nrows, elapsed_us(&t0, &t1));
            break;
        }

        /* ---- Sort ---- */
        case 8: {
            if (!*Ap) { printf("Load A first.\n"); break; }
            printf("Sort A by column: "); read_line(line, sizeof(line));
            clock_gettime(CLOCK_MONOTONIC, &t0);
            int rc = table_sort_by(*Ap, line);
            clock_gettime(CLOCK_MONOTONIC, &t1);
            if (rc == 0) printf("Sorted. (%ld µs)\n", elapsed_us(&t0, &t1));
            break;
        }

        /* ---- Joins ---- */
        case 9: case 10: case 11: {
            if (!*Ap || !*Bp) { printf("Load both A and B first.\n"); break; }
            char cola[128], colb[128];
            printf("Key column in A: "); read_line(cola, sizeof(cola));
            printf("Key column in B: "); read_line(colb, sizeof(colb));
            int ka = resolve_col(*Ap, cola);
            int kb = resolve_col(*Bp, colb);
            if (ka < 0 || kb < 0) break;
            Table *J = NULL;
            clock_gettime(CLOCK_MONOTONIC, &t0);
            if      (choice == 9)  J = table_join_inner(*Ap, *Bp, ka, kb);
            else if (choice == 10) J = table_join_left (*Ap, *Bp, ka, kb);
            else                   J = table_join_full (*Ap, *Bp, ka, kb);
            clock_gettime(CLOCK_MONOTONIC, &t1);
            const char *jname[] = {"inner","left","full outer"};
            printf("Join (%s) result – %d rows  (%ld µs):\n",
                   jname[choice - 9], J ? J->nrows : 0, elapsed_us(&t0, &t1));
            if (J) { table_print(J); table_free(J); }
            break;
        }

        /* ---- Query ---- */
        case 12: {
            if (!*Ap && !*Bp) { printf("Load at least one table first.\n"); break; }
            printf("\nQuery language syntax:\n");
            printf("  SELECT <col,...> | * FROM A [JOIN B ON A.<col> = B.<col>] [WHERE <col> = \"value\"]\n");
            printf("Examples:\n");
            printf("  SELECT * FROM A\n");
            printf("  SELECT A.Name, A.RollNo FROM A WHERE A.RollNo = \"250231001\"\n");
            printf("  SELECT A.Name, B.Physics FROM A JOIN B ON A.RollNo = B.RollNo\n\n");
            printf("Query> ");
            char qbuf[2048];
            if (!read_line(qbuf, sizeof(qbuf))) break;
            clock_gettime(CLOCK_MONOTONIC, &t0);
            Table *R = run_query(qbuf, *Ap, *Bp);
            clock_gettime(CLOCK_MONOTONIC, &t1);
            if (R) {
                printf("Result – %d rows  (%ld µs):\n", R->nrows, elapsed_us(&t0, &t1));
                table_print(R);
                printf("Save result to CSV? (leave blank to skip): ");
                read_line(line, sizeof(line));
                if (strlen(line) > 0) {
                    table_store_csv(R, line);
                    printf("Saved to '%s'\n", line);
                }
                table_free(R);
            } else {
                printf("Query failed or returned no result.\n");
            }
            break;
        }

        /* ---- Save ---- */
        case 13:
            if (!*Ap) { printf("Load A first.\n"); break; }
            printf("Save A to CSV path: "); read_line(line, sizeof(line));
            if (table_store_csv(*Ap, line) == 0) printf("Saved to '%s'\n", line);
            break;

        case 14:
            if (!*Bp) { printf("Load B first.\n"); break; }
            printf("Save B to CSV path: "); read_line(line, sizeof(line));
            if (table_store_csv(*Bp, line) == 0) printf("Saved to '%s'\n", line);
            break;

        default:
            printf("Unknown option '%d'. Try again.\n", choice);
            break;
        }
    }
}

/* -----------------------------------------------------------------------
 * Demonstration mode  –  called when two paths are given on argv
 * ----------------------------------------------------------------------- */
static void demo_mode(const char *pathA, const char *pathB) {
    struct timespec t0, t1;

    printf("\n=== Loading datasets ===\n");
    clock_gettime(CLOCK_MONOTONIC, &t0);
    Table *A = load_kv_path(pathA);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    printf("A loaded in %ld ms\n", elapsed_ms(&t0, &t1));

    clock_gettime(CLOCK_MONOTONIC, &t0);
    Table *B = load_kv_path(pathB);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    printf("B loaded in %ld ms\n", elapsed_ms(&t0, &t1));

    if (!A || !B) {
        fprintf(stderr, "Failed to load one or both data sets.\n");
        if (A) table_free(A);
        if (B) table_free(B);
        return;
    }

    printf("\nA: %d cols, %d rows\n", A->ncols, A->nrows);
    printf("B: %d cols, %d rows\n", B->ncols, B->nrows);

    printf("\n=== Dataset A ===\n");
    table_print(A);
    printf("\n=== Dataset B ===\n");
    table_print(B);

    /* --- Insert --- */
    printf("\n=== Insert demo (adding one row to A) ===\n");
    char **newrow = malloc(A->ncols * sizeof(char *));
    for (int i = 0; i < A->ncols; i++)
        newrow[i] = strdup(i == 0 ? "Demo Student" : "99");
    clock_gettime(CLOCK_MONOTONIC, &t0);
    table_add_row(A, newrow);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    for (int i = 0; i < A->ncols; i++) free(newrow[i]);
    free(newrow);
    printf("Insert took %ld µs. A now has %d rows.\n",
           elapsed_us(&t0, &t1), A->nrows);
    table_print(A);

    /* --- Sort --- */
    printf("\n=== Sort A by '%s' ===\n", A->headers[0]);
    clock_gettime(CLOCK_MONOTONIC, &t0);
    table_sort_by(A, A->headers[0]);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    printf("Sort took %ld µs.\n", elapsed_us(&t0, &t1));
    table_print(A);

    /* --- Update --- */
    if (A->ncols >= 1) {
        printf("\n=== Update demo (rename 'Demo Student' → 'Updated Student') ===\n");
        clock_gettime(CLOCK_MONOTONIC, &t0);
        int n = table_update_by_value(A, 0, "Demo Student", 0, "Updated Student");
        clock_gettime(CLOCK_MONOTONIC, &t1);
        printf("Updated %d row(s) in %ld µs.\n", n, elapsed_us(&t0, &t1));
        table_print(A);
    }

    /* --- Delete --- */
    printf("\n=== Delete demo (remove 'Updated Student') ===\n");
    clock_gettime(CLOCK_MONOTONIC, &t0);
    table_delete_by_value(A, 0, "Updated Student");
    clock_gettime(CLOCK_MONOTONIC, &t1);
    printf("Delete took %ld µs. A now has %d rows.\n",
           elapsed_us(&t0, &t1), A->nrows);
    table_print(A);

    /* --- Save --- */
    printf("\n=== Saving A to 'A_snapshot.csv' ===\n");
    table_store_csv(A, "A_snapshot.csv");
    printf("Written.\n");

    /* --- Inner Join --- */
    printf("\n=== Inner Join: A ⋈ B on '%s' / '%s' ===\n",
           A->headers[0], B->headers[0]);
    clock_gettime(CLOCK_MONOTONIC, &t0);
    Table *J = table_join_inner(A, B, 0, 0);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    printf("Inner join took %ld µs. Result: %d rows.\n",
           elapsed_us(&t0, &t1), J ? J->nrows : 0);
    if (J) { table_print(J); table_free(J); }

    /* --- Full Outer Join --- */
    printf("\n=== Full Outer Join: A ⟗ B on '%s' / '%s' ===\n",
           A->headers[0], B->headers[0]);
    clock_gettime(CLOCK_MONOTONIC, &t0);
    Table *FJ = table_join_full(A, B, 0, 0);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    printf("Full outer join took %ld µs. Result: %d rows.\n",
           elapsed_us(&t0, &t1), FJ ? FJ->nrows : 0);
    if (FJ) { table_print(FJ); table_free(FJ); }

    /* --- Sample Query --- */
    printf("\n=== Sample Query ===\n");
    char q[256];
    snprintf(q, sizeof(q),
             "SELECT A.%s, A.%s, B.%s FROM A JOIN B ON A.%s = B.%s",
             A->headers[0], A->headers[1],
             B->ncols > 2 ? B->headers[2] : B->headers[0],
             A->headers[0], B->headers[0]);
    printf("Query: %s\n", q);
    clock_gettime(CLOCK_MONOTONIC, &t0);
    Table *QR = run_query(q, A, B);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    printf("Query took %ld µs.\n", elapsed_us(&t0, &t1));
    if (QR) { table_print(QR); table_free(QR); }

    table_free(A);
    table_free(B);
}

/* -----------------------------------------------------------------------
 * main
 * ----------------------------------------------------------------------- */
int main(int argc, char **argv) {
    if (argc >= 3) {
        demo_mode(argv[1], argv[2]);
        return 0;
    }

    /* Interactive mode */
    Table *A = NULL, *B = NULL;
    run_menu(&A, &B);
    if (A) table_free(A);
    if (B) table_free(B);
    return 0;
}
