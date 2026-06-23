"""demo_optimizer.py — cost-based plan choices: IndexScan vs SeqScan, join order."""

import _demo
from _demo import banner, sql, step

from minidb import Database


def main() -> None:
    banner("COST-BASED OPTIMIZER: EXPLAIN the chosen physical plan")
    db = Database(":memory:")
    sql(db, "CREATE TABLE users (id INT PRIMARY KEY, name TEXT, age INT)", echo=False)
    vals = ",".join(f"({i},'u{i}',{20 + i % 50})" for i in range(500))
    sql(db, f"INSERT INTO users VALUES {vals}", echo=False)
    print("  (loaded 500 users)")

    step("Predicate on the PRIMARY KEY -> optimizer picks an IndexScan (~1 row)")
    sql(db, "EXPLAIN SELECT name FROM users WHERE id = 42")

    step("Predicate on a NON-indexed column -> SeqScan + Filter")
    sql(db, "EXPLAIN SELECT name FROM users WHERE age = 30")

    step("Add a secondary index on age, re-analyze, and EXPLAIN again")
    sql(db, "CREATE INDEX ON users (age)")
    db.catalog.get_table("users").analyze()
    sql(db, "EXPLAIN SELECT name FROM users WHERE age = 30")

    step("Join ordering: small table should be scanned on the outer side")
    sql(db, "CREATE TABLE vip (uid INT PRIMARY KEY, tier TEXT)", echo=False)
    sql(db, "INSERT INTO vip VALUES (1,'gold'),(2,'silver')", echo=False)
    sql(db, "EXPLAIN SELECT u.name, v.tier FROM users u JOIN vip v ON u.id = v.uid")

    db.close()
    print("\nTakeaway: the optimizer estimates selectivity from catalog statistics")
    print("and chooses the cheaper access path and join order before execution.")


if __name__ == "__main__":
    main()
