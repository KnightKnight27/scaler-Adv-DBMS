"""Basic smoke test for MiniDB."""
import os
import shutil
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'src'))

DATA_DIR = '/tmp/minidb_test'

def clean():
    if os.path.exists(DATA_DIR):
        shutil.rmtree(DATA_DIR)

def test_create_insert_select():
    clean()
    from db import MiniDB
    db = MiniDB(DATA_DIR)

    db.execute("CREATE TABLE users (id INT PRIMARY KEY, name TEXT, age INT)")
    print("✓ CREATE TABLE")

    txn = db.begin()
    db.execute("INSERT INTO users (id, name, age) VALUES (1, 'Alice', 30)", txn)
    db.execute("INSERT INTO users (id, name, age) VALUES (2, 'Bob', 25)", txn)
    db.execute("INSERT INTO users (id, name, age) VALUES (3, 'Carol', 35)", txn)
    db.commit(txn)
    print("✓ INSERT (committed)")

    rows = db.execute("SELECT * FROM users")
    assert len(rows) == 3, f"Expected 3 rows, got {len(rows)}"
    print(f"✓ SELECT * → {rows}")

    rows = db.execute("SELECT name FROM users WHERE age > 28")
    names = {r['name'] for r in rows}
    assert names == {'Alice', 'Carol'}, f"Got {names}"
    print(f"✓ SELECT with WHERE → {rows}")

    db.close()

def test_delete():
    clean()
    from db import MiniDB
    db = MiniDB(DATA_DIR)
    db.execute("CREATE TABLE items (id INT PRIMARY KEY, val TEXT)")

    txn = db.begin()
    for i in range(5):
        db.execute(f"INSERT INTO items (id, val) VALUES ({i}, 'item{i}')", txn)
    db.commit(txn)

    txn = db.begin()
    result = db.execute("DELETE FROM items WHERE id = 2", txn)
    db.commit(txn)
    print(f"✓ DELETE → {result}")

    rows = db.execute("SELECT * FROM items")
    ids = {r['id'] for r in rows}
    assert 2 not in ids, f"id=2 should be deleted, got {ids}"
    assert len(rows) == 4, f"Expected 4 rows, got {len(rows)}"
    print(f"✓ SELECT after DELETE → {ids}")

    db.close()

def test_rollback():
    clean()
    from db import MiniDB
    db = MiniDB(DATA_DIR)
    db.execute("CREATE TABLE t (id INT PRIMARY KEY, v TEXT)")

    txn = db.begin()
    db.execute("INSERT INTO t (id, v) VALUES (1, 'hello')", txn)
    db.commit(txn)

    txn = db.begin()
    db.execute("INSERT INTO t (id, v) VALUES (2, 'world')", txn)
    db.rollback(txn)

    rows = db.execute("SELECT * FROM t")
    assert len(rows) == 1, f"Rollback failed, got {len(rows)} rows"
    print(f"✓ ROLLBACK → only committed rows visible: {rows}")
    db.close()

def test_mvcc_snapshot_isolation():
    """Two concurrent transactions — reader sees snapshot, not uncommitted writes."""
    clean()
    from db import MiniDB
    db = MiniDB(DATA_DIR)
    db.execute("CREATE TABLE acct (id INT PRIMARY KEY, balance INT)")

    # seed data
    txn = db.begin()
    db.execute("INSERT INTO acct (id, balance) VALUES (1, 1000)", txn)
    db.commit(txn)

    # T1 starts reading
    t1 = db.begin()

    # T2 starts and modifies (not yet committed)
    t2 = db.begin()
    db.execute("INSERT INTO acct (id, balance) VALUES (2, 500)", t2)
    # T2 not committed yet

    # T1 should NOT see T2's uncommitted insert
    rows = db.executor.execute(
        __import__('sql.parser', fromlist=['parse']).parse("SELECT * FROM acct"),
        t1
    )
    ids = {r['id'] for r in rows}
    assert 2 not in ids, f"T1 should not see T2's uncommitted row! Got ids: {ids}"
    print(f"✓ MVCC snapshot isolation: T1 sees {ids}, not T2's uncommitted row")

    # now commit T2
    db.commit(t2)
    db.rollback(t1)  # clean up T1

    # fresh txn should see both rows
    rows = db.execute("SELECT * FROM acct")
    assert len(rows) == 2, f"Expected 2 rows after T2 commit, got {len(rows)}"
    print(f"✓ After T2 commit, fresh SELECT sees both rows: {[r['id'] for r in rows]}")
    db.close()

def test_wal_recovery():
    clean()
    from db import MiniDB

    # first session: insert and commit
    db = MiniDB(DATA_DIR)
    db.execute("CREATE TABLE log_test (id INT PRIMARY KEY, msg TEXT)")
    txn = db.begin()
    db.execute("INSERT INTO log_test (id, msg) VALUES (1, 'committed')", txn)
    db.commit(txn)

    # simulate crash: don't call db.close() cleanly — but WAL is fsynced
    # second session: should recover via WAL
    db2 = MiniDB(DATA_DIR)
    rows = db2.execute("SELECT * FROM log_test")
    assert len(rows) == 1, f"Recovery failed, got {len(rows)} rows"
    assert rows[0]['msg'] == 'committed'
    print(f"✓ WAL recovery → {rows}")
    db2.close()

def test_join():
    clean()
    from db import MiniDB
    db = MiniDB(DATA_DIR)
    db.execute("CREATE TABLE orders (id INT PRIMARY KEY, user_id INT, amount INT)")
    db.execute("CREATE TABLE customers (id INT PRIMARY KEY, name TEXT)")

    txn = db.begin()
    db.execute("INSERT INTO customers (id, name) VALUES (1, 'Alice')", txn)
    db.execute("INSERT INTO customers (id, name) VALUES (2, 'Bob')", txn)
    db.execute("INSERT INTO orders (id, user_id, amount) VALUES (1, 1, 100)", txn)
    db.execute("INSERT INTO orders (id, user_id, amount) VALUES (2, 2, 200)", txn)
    db.execute("INSERT INTO orders (id, user_id, amount) VALUES (3, 1, 150)", txn)
    db.commit(txn)

    rows = db.execute("SELECT name, amount FROM orders JOIN customers ON orders.user_id = customers.id")
    print(f"✓ JOIN → {rows}")
    assert len(rows) == 3, f"Expected 3 joined rows, got {len(rows)}"
    db.close()

if __name__ == '__main__':
    print("=== MiniDB Smoke Tests ===\n")
    tests = [
        test_create_insert_select,
        test_delete,
        test_rollback,
        test_mvcc_snapshot_isolation,
        test_wal_recovery,
        test_join,
    ]
    passed = 0
    for t in tests:
        print(f"\n--- {t.__name__} ---")
        try:
            t()
            passed += 1
        except Exception as e:
            import traceback
            print(f"✗ FAILED: {e}")
            traceback.print_exc()

    print(f"\n=== {passed}/{len(tests)} passed ===")
