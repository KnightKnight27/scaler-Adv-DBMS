package db

import (
	"strings"
	"testing"
)

// open a fresh heap database in a temp dir.
func openTest(t *testing.T) *Database {
	t.Helper()
	d, err := Open(t.TempDir(), "heap")
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { d.Close() })
	return d
}

func mustExec(t *testing.T, d *Database, sql string) Result {
	t.Helper()
	res, err := d.Execute(sql)
	if err != nil {
		t.Fatalf("exec %q: %v", sql, err)
	}
	return res
}

func TestEndToEndCRUD(t *testing.T) {
	d := openTest(t)
	mustExec(t, d, "CREATE TABLE t (id INT PRIMARY KEY, name TEXT)")
	mustExec(t, d, "INSERT INTO t VALUES (1, 'a'), (2, 'b'), (3, 'c')")

	res := mustExec(t, d, "SELECT id, name FROM t WHERE id >= 2")
	if len(res.Rows) != 2 {
		t.Fatalf("WHERE id>=2 returned %d rows", len(res.Rows))
	}

	mustExec(t, d, "DELETE FROM t WHERE id = 2")
	res = mustExec(t, d, "SELECT id FROM t")
	if len(res.Rows) != 2 {
		t.Fatalf("after delete, %d rows", len(res.Rows))
	}

	// duplicate primary key must fail
	if _, err := d.Execute("INSERT INTO t VALUES (1, 'dup')"); err == nil {
		t.Fatal("expected duplicate PK error")
	}
}

func TestEndToEndJoin(t *testing.T) {
	d := openTest(t)
	mustExec(t, d, "CREATE TABLE users (id INT PRIMARY KEY, name TEXT)")
	mustExec(t, d, "CREATE TABLE orders (oid INT PRIMARY KEY, uid INT, total INT)")
	mustExec(t, d, "INSERT INTO users VALUES (1,'alice'),(2,'bob')")
	mustExec(t, d, "INSERT INTO orders VALUES (10,1,100),(11,1,50),(12,2,200)")

	res := mustExec(t, d, "SELECT u.name, o.total FROM users u JOIN orders o ON u.id = o.uid WHERE o.total >= 100")
	if len(res.Rows) != 2 {
		t.Fatalf("join returned %d rows, want 2", len(res.Rows))
	}
}

func TestEndToEndAggregate(t *testing.T) {
	d := openTest(t)
	mustExec(t, d, "CREATE TABLE emp (id INT PRIMARY KEY, dept TEXT, sal INT)")
	mustExec(t, d, "INSERT INTO emp VALUES (1,'eng',100),(2,'eng',200),(3,'ops',150)")

	res := mustExec(t, d, "SELECT dept, COUNT(*), SUM(sal), AVG(sal) FROM emp GROUP BY dept")
	if len(res.Rows) != 2 {
		t.Fatalf("group by returned %d rows", len(res.Rows))
	}
	res = mustExec(t, d, "SELECT COUNT(*) FROM emp")
	if res.Rows[0][0] != "3" {
		t.Fatalf("COUNT(*) = %s, want 3", res.Rows[0][0])
	}
}

func TestOptimizerChoosesIndexScan(t *testing.T) {
	d := openTest(t)
	mustExec(t, d, "CREATE TABLE t (id INT PRIMARY KEY, v TEXT)")
	mustExec(t, d, "INSERT INTO t VALUES (1,'x'),(2,'y'),(3,'z')")

	idx := mustExec(t, d, "EXPLAIN SELECT v FROM t WHERE id = 2")
	if !strings.Contains(idx.Message, "IndexScan") {
		t.Fatalf("PK equality should use IndexScan, got:\n%s", idx.Message)
	}
	seq := mustExec(t, d, "EXPLAIN SELECT v FROM t WHERE v = 'y'")
	if !strings.Contains(seq.Message, "SeqScan") {
		t.Fatalf("non-PK predicate should use SeqScan, got:\n%s", seq.Message)
	}
}

func TestReopenPersistsData(t *testing.T) {
	dir := t.TempDir()
	d, err := Open(dir, "heap")
	if err != nil {
		t.Fatal(err)
	}
	mustExec(t, d, "CREATE TABLE t (id INT PRIMARY KEY, n INT)")
	mustExec(t, d, "INSERT INTO t VALUES (1,10),(2,20)")
	d.Close()

	d2, err := Open(dir, "heap")
	if err != nil {
		t.Fatal(err)
	}
	defer d2.Close()
	res := mustExec(t, d2, "SELECT n FROM t WHERE id = 2")
	if len(res.Rows) != 1 || res.Rows[0][0] != "20" {
		t.Fatalf("after reopen got %+v", res.Rows)
	}
}
