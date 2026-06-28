# Query Execution

M3 implements heap-backed query execution on top of the storage, parser, and
primary-key index modules.

## Supported Statements

- `INSERT INTO <table> VALUES (...)`
- `SELECT <columns> FROM <table>`
- `SELECT <columns> FROM <table> WHERE <column> <op> <value>`
- `DELETE FROM <table>`
- `DELETE FROM <table> WHERE <column> <op> <value>`
- `SELECT <columns> FROM <left> JOIN <right> ON <left.col> = <right.col>`
- `SELECT COUNT(*) FROM <table>`
- `SELECT COUNT(*) FROM <left> JOIN <right> ON <left.col> = <right.col>`

## Execution Model

- Tables are registered with an in-memory catalog inside `ExecutionEngine`.
- Rows are encoded into heap pages using the storage layer from M1.
- Primary-key entries are maintained in the M2 B+ tree.
- `WHERE primary_key = value` uses the primary-key index for direct row lookup.
- Other predicates use heap scans.
- Joins use nested-loop execution.
- `COUNT(*)` is implemented as a simple aggregation over matching rows.

## Current Limitations

- No cost-based plan selection yet; that belongs to the optimizer milestone.
- Joins are equality joins only.
- Aggregation is currently limited to `COUNT(*)`.
