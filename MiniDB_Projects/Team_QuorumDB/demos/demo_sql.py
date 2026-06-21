"""Demo: end-to-end SQL — storage, indexing, joins, and the optimizer.

Shows the optimizer choosing an index scan for a key lookup but a sequential
scan for a non-indexed predicate, plus a join across two tables.
"""

from _bootstrap import rule, scratch

from minidb.engine import Database


def main() -> None:
    db = Database(scratch("sql"))
    c = db.connect()

    rule("1. CREATE tables (primary keys build B+Tree indexes)")
    c.execute("CREATE TABLE users (id INT PRIMARY KEY, name TEXT, age INT)")
    c.execute("CREATE TABLE orders (oid INT PRIMARY KEY, uid INT, total INT)")
    print("created users, orders")

    rule("2. INSERT data")
    c.execute("INSERT INTO users VALUES " +
              ",".join(f"({i},'user{i}',{20 + i % 30})" for i in range(1, 201)))
    c.execute("INSERT INTO orders VALUES " +
              ",".join(f"({i},{1 + i % 200},{(i * 37) % 500})" for i in range(1, 401)))
    print("inserted 200 users, 400 orders")

    rule("3. Optimizer: PK equality -> IndexScan")
    print(c.execute("EXPLAIN SELECT name FROM users WHERE id = 42").message)

    rule("4. Optimizer: non-indexed predicate -> SeqScan")
    print(c.execute("EXPLAIN SELECT name FROM users WHERE age = 25").message)

    rule("5. Point query via the index")
    res = c.execute("SELECT id, name, age FROM users WHERE id = 42")
    print(res.columns, res.rows)

    rule("6. JOIN users x orders with a filter")
    res = c.execute("SELECT u.name, o.total FROM users u JOIN orders o "
                    "ON u.id = o.uid WHERE u.id = 7")
    for row in res.rows:
        print(row)

    rule("7. Buffer pool stats after the workload")
    bp = db.buffer_pool
    print(f"pages={db.disk.num_pages}  hit_ratio={bp.hit_ratio():.2%}  {bp.stats}")
    db.close()


if __name__ == "__main__":
    main()
