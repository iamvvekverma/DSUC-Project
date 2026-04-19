# DSUC — Data Structures + DBMS Assignment (Fixed)
**MCA Assignment 1 · HBTU Kanpur · Dr. Siddharth Srivastava**

---

## What This Program Does

A generic, in-RAM data engine written in C that:

- Loads KV-format student record files (any schema) from a directory
- Performs **Insert, Update, Delete, Sort** entirely in RAM (files are unchanged until you explicitly save)
- Performs **Inner Join, Left Join, Full Outer Join** across two loaded datasets
- Runs a simple **SQL-like query language** (SELECT / FROM / JOIN ON / WHERE)
- Reports **execution time** (µs/ms) for every operation
- Saves results to CSV on user demand

---

## Build

```sh
make          # → ./dsuc  (optimised)
make debug    # → ./dsuc_debug  (AddressSanitizer, for testing)
make clean
```

Requires: `gcc`, standard C99 headers.

---

## Run

### Interactive menu
```sh
./dsuc
```

### Demo mode (auto-runs all operations)
```sh
./dsuc data/MCASampleData1 data/MCASampleData2
```

---

## Menu Options

| Key | Action |
|-----|--------|
| 1 | Load directory or file → table A |
| 2 | Load directory or file → table B |
| 3 | Show table A |
| 4 | Show table B |
| 5 | Insert a row into A |
| 6 | Update rows in A (WHERE col = val) |
| 7 | Delete rows from A (WHERE col = val) |
| 8 | Sort A by a column |
| 9 | Inner join A ⋈ B |
| 10 | Left join A ⟕ B |
| 11 | Full outer join A ⟗ B |
| 12 | Run a custom query |
| 13 | Save A to CSV |
| 14 | Save B to CSV |
| 0 | Exit |

---

## Query Language

```
SELECT <col,...> | * FROM A [JOIN B ON A.<col> = B.<col>] [WHERE <col> = "value"]
```

**Examples:**
```sql
SELECT * FROM A
SELECT A.Name, A.RollNo FROM A WHERE A.Name = "Rahul Kumar"
SELECT A.Name, A.Maths, B.Physics FROM A JOIN B ON A.Name = B.Name
SELECT A.Name, B.Physics FROM A JOIN B ON A.RollNo = B.RollNo WHERE A.Name = "Tanveer Khan"
```

Column names in WHERE and SELECT accept both bare names (`Name`) and prefixed names (`A.Name`).

---

## Data Format

Each record file contains one student record as `Key: Value,` pairs separated by a blank line:

```
Name: Rahul Kumar,
RollNo: 250231048,
Science: 82,
Maths: 93,
```

File `0.txt` in each directory is the schema definition (key == value) and is automatically skipped.

---

## Bugs Fixed (vs. GPT-generated original)

| # | File | Bug |
|---|------|-----|
| 1 | `loader.c` | `is_dir()` defined **twice** — caused compile error |
| 2 | `loader.c` | Trailing commas on values (`"Rahul Kumar ,"`) never stripped |
| 3 | `loader.c` | `0.txt` (schema file) loaded as a spurious data row |
| 4 | `loader.c` | File sort was lexicographic: `10.txt` < `2.txt` (fixed to numeric) |
| 5 | `loader.c` | Memory leak: `row[h]` assigned `strdup` twice when column matched |
| 6 | `table.c` | `compare_rows_by_col` / `comparator_factory` were dead stubs |
| 7 | `table.c` | `realloc(ptr, 0)` on empty table → undefined behaviour |
| 8 | `table.c` | `table_join_full` used bare header names → silent column collisions |
| 9 | `table.c` | No aligned output; added box-style table printer |
| 10 | `query.c` | WHERE clause couldn't resolve `A.`/`B.` prefixed column names |
| 11 | `query.c` | Double-free: `working` and `base` aliased but both had `owned=1` |
| 12 | `main.c` | Menu option 13 (Exit) intercepted *before* the switch, making option 14 unreachable |

All fixes verified with **GCC AddressSanitizer** (`-fsanitize=address`) — zero errors.

---

## Time Complexity

| Operation | Complexity |
|-----------|------------|
| Load (n files) | O(n × k) where k = fields/file |
| Insert | O(1) amortised |
| Delete | O(r) rows |
| Update | O(r) rows |
| Sort (insertion) | O(r²) worst — fine for assignment scale |
| Inner/Left/Full join | O(r₁ × r₂) |
| Query (join + filter + project) | O(r₁ × r₂) |

---

## Architecture

```
main.c          — interactive menu + demo mode + timing harness
├── loader.c/h  — KV file parser, directory walker, schema detection
├── table.c/h   — in-RAM Table struct, all CRUD + join operations
└── query.c/h   — SQL-like query parser (SELECT/FROM/JOIN/WHERE)
```

All computation is in RAM. Files are never modified unless the user explicitly chooses Save (options 13/14) or the query engine saves a result.
