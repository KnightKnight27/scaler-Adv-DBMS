"""
Demo 3 — Query optimizer: index utilization, selectivity, join ordering.

Shows the cost-based optimizer:
  * picking a SeqScan on a tiny table (scan is cheaper than an index probe)
  * switching to an IndexScan once the table is large and the predicate is
    selective (primary-key equality / range)
  * choosing a scan for a low-selectivity predicate even when a column could
    be filtered, because most rows match
  * ordering a multi-table join (smaller build side first, hash join)

Run:
    python demos/demo_optimizer.py
"""
import os
import shutil
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "src"))

from minidb import Database

DB_DIR = "/tmp/minidb_demo_optimizer"


def banner(t):
    print("\n" + "=" * 64 + f"\n  {t}\n" + "=" * 64)


def show(db, sql):
    print(f"\nSQL> {sql}")
    print(db.explain(sql))


def main():
    shutil.rmtree(DB_DIR, ignore_errors=True)
    db = Database(DB_DIR, isolation="2PL")

    banner("Tiny table — SeqScan beats an index probe")
    db.execute("CREATE TABLE small (id INT PRIMARY KEY, v INT)")
    db.execute("INSERT INTO small VALUES (1,1),(2,2),(3,3)")
    db.analyze()
    show(db, "SELECT v FROM small WHERE id = 2")

    banner("Large table — IndexScan wins for a selective predicate")
    db.execute("CREATE TABLE big (id INT PRIMARY KEY, bucket INT)")
    db.execute("INSERT INTO big VALUES " +
               ",".join(f"({i},{i % 50})" for i in range(1, 2001)))
    db.analyze()
    show(db, "SELECT bucket FROM big WHERE id = 1234")     # PK equality -> index
    show(db, "SELECT id FROM big WHERE id >= 10 AND id <= 20")  # range -> index
    print("\nexecuted range query result:")
    print(db.execute("SELECT id, bucket FROM big WHERE id >= 10 AND id <= 13"))

    banner("Low-selectivity predicate on a non-indexed column — SeqScan")
    show(db, "SELECT id FROM big WHERE bucket = 7")  # ~2% * 2000 = 40 rows, no index

    banner("Join ordering — smaller relation becomes the hash build side")
    db.execute("CREATE TABLE dept (did INT PRIMARY KEY, dname TEXT)")
    db.execute("INSERT INTO dept VALUES (1,'eng'),(2,'sales'),(3,'ops')")
    db.execute("CREATE TABLE emp (eid INT PRIMARY KEY, did INT, name TEXT)")
    db.execute("INSERT INTO emp VALUES " +
               ",".join(f"({i},{(i % 3) + 1},'e{i}')" for i in range(1, 301)))
    db.analyze()
    show(db, "SELECT emp.name, dept.dname FROM emp "
             "JOIN dept ON emp.did = dept.did")
    print("\njoin result (first rows):")
    res = db.execute("SELECT emp.name, dept.dname FROM emp "
                     "JOIN dept ON emp.did = dept.did")
    for row in list(res)[:5]:
        print("   ", row)
    print(f"   ... {len(res)} rows total")

    db.close()
    banner("OPTIMIZER DEMO COMPLETE")


if __name__ == "__main__":
    main()
