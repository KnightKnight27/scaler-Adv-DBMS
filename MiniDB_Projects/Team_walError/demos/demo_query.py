"""demo_query.py — SQL end to end: CREATE, INSERT, SELECT, WHERE, JOIN, DELETE."""

import _demo
from _demo import banner, sql, step

from minidb import Database


def main() -> None:
    banner("SQL EXECUTION: parser -> planner -> Volcano executor")
    db = Database(":memory:")

    step("Define a schema and load rows")
    sql(db, "CREATE TABLE users (id INT PRIMARY KEY, name TEXT, city TEXT)")
    sql(db, "CREATE TABLE orders (oid INT PRIMARY KEY, uid INT, total INT)")
    sql(db, "INSERT INTO users VALUES (1,'ada','london'),(2,'grace','nyc'),(3,'linus','helsinki')")
    sql(db, "INSERT INTO orders VALUES (10,1,250),(11,1,80),(12,2,500),(13,3,40)")

    step("Filtered projection (WHERE + AND/OR)")
    sql(db, "SELECT name, city FROM users WHERE id = 2 OR city = 'london'")

    step("Inner join across two tables with a predicate")
    sql(db, "SELECT u.name, o.total FROM users u JOIN orders o ON u.id = o.uid "
            "WHERE o.total > 100")

    step("Delete rows, then confirm they are gone")
    sql(db, "DELETE FROM orders WHERE total < 100")
    sql(db, "SELECT oid, total FROM orders")

    db.close()
    print("\nTakeaway: a SQL string becomes tokens -> an AST -> a physical operator")
    print("tree whose iterators pull tuples from the heap/index through the engine.")


if __name__ == "__main__":
    main()
