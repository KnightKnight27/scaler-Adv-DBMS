# Query Execution

M2 adds the SQL parser used by the later execution engine. It currently parses:

- `INSERT INTO <table> VALUES (...)`
- `SELECT <columns> FROM <table>`
- `SELECT <columns> FROM <table> WHERE <column> <op> <value>`
- `DELETE FROM <table>`
- `DELETE FROM <table> WHERE <column> <op> <value>`

Parsed statements are structured into statement-specific objects so M3 can route
table operations and index lookups without reparsing SQL text.

M3 will add physical execution for `INSERT`, `DELETE`, `SELECT`, `WHERE`, joins,
and aggregation.
