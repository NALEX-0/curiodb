# CurioDB

CurioDB is a small relational database built from first principles
in modern C++20. It is an educational project focused on the internals behind a
SQL database: parsing, planning, execution, page storage, buffering, and B+ tree
indexes.

CurioDB does not use a database or parser library. The SQL frontend, catalog,
row format, storage engine, buffer pool, query operators, planner, and B+ tree
are implemented in this repository.

## Highlights

- Interactive SQL command-line interface
- Persistent databases, schemas, rows, constraints, and indexes
- Custom-build lexer, recursive-descent parser, and AST
- Typed values: `INT`, `DOUBLE`, `VARCHAR(n)`, and `NULL`
- `INSERT`, `SELECT`, `UPDATE`, and `DELETE`
- Comparisons, parentheses, `AND`, `OR`, `IS NULL`, and `IS NOT NULL`
- `PRIMARY KEY`, `UNIQUE`, and `NOT NULL` constraints
- Fixed-size 4 KiB pages with a slotted-page row layout
- LRU buffer pool with pinning, dirty-page tracking, and RAII page guards
- Query plans using sequential scan, filter, projection, and index scan nodes
- B+ tree indexes
- `EXPLAIN SELECT` for inspecting the chosen query plan
- GoogleTest test suite with compiler warnings

## Requirements

- CMake 3.20 or newer
- A C++20 compiler, Apple Clang is the primary compiler


On macOS, install the command-line developer tools if they are not already
available:

```sh
xcode-select --install
```

## Build and test

From the repository root:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

To build the CLI without downloading or compiling GoogleTest:

```sh
cmake -S . -B build -DCURIODB_BUILD_TESTS=OFF
cmake --build build
```

Run CurioDB from the repository root:

```sh
./build/curiodb
```

The launch directory matters: CurioDB stores database files in a `.curiodb/`
directory beneath the current working directory. Each database is persisted in
its own `.cdb` file. The directory is ignored by Git.

## Quick tour

SQL statements must end with a semicolon. Multiline statements are supported.

```sql
CREATE DATABASE company;
USE company;

CREATE TABLE employees (
    id INT PRIMARY KEY,
    name VARCHAR(100) NOT NULL,
    email VARCHAR(150) UNIQUE,
    salary DOUBLE
);

INSERT INTO employees VALUES
    (1, 'Alice', 'alice@example.com', 72000.0);
INSERT INTO employees VALUES
    (2, 'Bob', NULL, 65000.0);

SELECT id, name
FROM employees
WHERE salary >= 70000.0 OR email IS NULL;

UPDATE employees
SET salary = 68000.0
WHERE id = 2;
```

An integer index can be created explicitly with

```sql
CREATE INDEX employees_id_idx ON employees (id);
EXPLAIN SELECT name FROM employees WHERE id = 2;
```

In this example the explicit index is redundant: an integer primary key already
creates an internal index. Therefore, the `EXPLAIN` query can use an `IndexScan`
even if `CREATE INDEX` is omitted. B+ tree indexes currently support `INT`
columns only.

Supported shell commands:

```text
.databases       List databases
.tables          List tables in the active database
.schema <table>  Show a table definition
.help            Show CLI help
.quit            Exit CurioDB
```

## Supported SQL

CurioDB supports the following SQL subset:

```sql
CREATE DATABASE database_name;
USE database_name;

CREATE TABLE table_name (
    column_name INT [PRIMARY KEY | UNIQUE | NOT NULL],
    column_name DOUBLE [UNIQUE | NOT NULL],
    column_name VARCHAR(length) [UNIQUE | NOT NULL]
);

INSERT INTO table_name VALUES (value, ...);

SELECT * FROM table_name [WHERE expression];
SELECT column, ... FROM table_name [WHERE expression];

UPDATE table_name SET column = value [WHERE expression];
DELETE FROM table_name [WHERE expression];

CREATE INDEX index_name ON table_name (integer_column);
EXPLAIN SELECT ...;
```

Expressions support `=`, `!=`, `<>`, `<`, `<=`, `>`, `>=`, `AND`, `OR`, and
parentheses. `AND` has higher precedence than `OR`. Null checks use `IS NULL`
and `IS NOT NULL`; ordinary comparisons involving `NULL` do not match rows.

`PRIMARY KEY` implies both uniqueness and non-nullability. Nullable `UNIQUE`
columns may contain multiple `NULL` values.

## Architecture

The SQL path is deliberately layered:

```text
SQL text
   |
   v
Lexer -> tokens -> Parser -> AST
                            |
                            v
                    Binder / Planner
                            |
                            v
              Scan -> Filter -> Projection
                            |
                            v
              Table heap or B+ tree index
                            |
                            v
             Buffer pool -> Disk manager
```

Detailed PlantUML models are available in the
[`UML diagrams`](UML%20diagrams) directory.

The major components are:

- **SQL frontend:** recognizes keywords and literals, tracks source locations,
  builds typed statements, and parses recursive boolean expressions.
- **Catalog:** tracks databases, table schemas, column constraints, page lists,
  index names, and persistent B+ tree root page identifiers.
- **Planner:** binds column names and literal types, and then creates a small physical plan. A simple equality predicate on an indexed `INT` column uses an index scan. Other predicates use a sequential scan.
- **Execution:** runs sequential scans, filters, projections, inserts, updates,
  and deletes against in-memory or disk-backed storage.
- **Storage:** serializes typed rows into records, places records in slotted
  pages, and groups pages into table heaps.
- **Buffer pool:** caches up to 16 pages per open database, tracks pins and dirty
  pages, and evicts the least recently used unpinned frame.
- **B+ tree:** stores unique signed 64-bit integer keys mapped to row IDs, splits
  leaf and internal nodes, and links leaves for ordered range traversal.

## On-disk layout

Every database file is divided into fixed 4096-byte pages.

```text
page 0       catalog metadata
page 1..N    table-heap pages and B+ tree pages
```

Table pages use a slotted layout. Records grow backward from the end of the
page, while the slot directory grows forward from the header. A row ID consists
of a page ID and slot ID. An update keeps its row ID
when the replacement fits and relocates the row when it must grow.

The catalog stores the database name, schemas, constraints, table page lists,
and index metadata.

## Project structure

```text
curiodb/
├── cmake/                 CMake helper modules and warning configuration
├── UML diagrams/          PlantUML architecture documentation
├── include/curiodb/
│   ├── catalog/           Schemas and persistent catalog metadata
│   ├── cli/               Interactive shell interface
│   ├── execution/         Operators, plans, and statement execution
│   ├── sql/               Tokens, lexer, AST, and parser
│   ├── storage/           Pages, heap, buffer pool, disk, and B+ tree
│   └── types/             SQL data types and runtime values
├── src/                   Implementations matching the public include tree
├── tests/                 GoogleTest suites organized by subsystem
├── CMakeLists.txt         Main build definition
└── README.md              Project documentation
```

## Design choices

- The project favors understandable implementations over production-level features
- Identifiers and SQL keywords are matched case-insensitively.
- Explicit and automatically created indexes are unique.
- Automatic B+ tree indexes are created for integer primary-key and unique
  columns. Non-integer uniqueness is enforced with a scan.
- Null keys are not stored in B+ tree indexes.
- Query plans currently choose an index only for a simple top-level equality
  predicate on an indexed integer column.

## Current limitations

CurioDB was never intended to be a production-grade database, so some features are deliberately omitted. In particular, it currently has:

- no transactions, concurrency control, write-ahead log, or crash recovery
- no joins, grouping, aggregation, ordering, limits, or subqueries
- no `DROP`, `ALTER`, foreign keys, or composite constraints
- no explicit column list in `INSERT`
- no expressions in projected or updated values
- no string or floating-point B+ tree keys
- no B+ tree merge or redistribution after deletion
- no optimizer statistics or cost-based planning
- no page reclamation or vacuum process
- a catalog that must fit in one 4 KiB page


## Tests

The test suite covers the catalog, lexer, parser, AST, values, serialization,
slotted pages, disk manager, table heap, buffer pool, B+ tree, planner,
operators, statement execution, persistence, and CLI behavior.

To run one test by name:

```sh
./build/tests/curiodb_tests \
  --gtest_filter=BPlusTreeTest.SplitsLeavesAndInternalNodesForShuffledKeys
```

Strict warnings are enabled for Clang and GCC, including conversion, shadowing,
format, null-dereference, and other useful diagnostics.
