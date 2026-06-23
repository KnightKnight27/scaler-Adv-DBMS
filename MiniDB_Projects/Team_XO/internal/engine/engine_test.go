package engine

import (
	"strings"
	"testing"

	"minidb/internal/txn"
)

func open(t *testing.T, dir string, mode txn.Mode) *DB {
	t.Helper()
	db, err := Open(Options{Dir: dir, Mode: mode, BufferFrames: 16})
	if err != nil {
		t.Fatalf("open: %v", err)
	}
	return db
}

func exec(t *testing.T, s *Session, q string) *Result {
	t.Helper()
	res, err := s.Execute(q)
	if err != nil {
		t.Fatalf("execute %q: %v", q, err)
	}
	return res
}

func seedUsers(t *testing.T, s *Session) {
	exec(t, s, "CREATE TABLE users (id INT PRIMARY KEY, name TEXT, age INT)")
	exec(t, s, "INSERT INTO users VALUES (1, 'alice', 30), (2, 'bob', 25), (3, 'carol', 30), (4, 'dave', 41), (5, 'erin', 25)")
}

func TestCoreQueries(t *testing.T) {
	db := open(t, t.TempDir(), txn.Mode2PL)
	defer db.Close()
	s := db.NewSession()
	seedUsers(t, s)

	// Point lookup on the primary key.
	res := exec(t, s, "SELECT id, name FROM users WHERE id = 3")
	if len(res.Rows) != 1 || res.Rows[0][1].Str != "carol" {
		t.Fatalf("point lookup wrong: %+v", res.Rows)
	}

	// COUNT(*) with a non-indexed filter.
	res = exec(t, s, "SELECT COUNT(*) FROM users WHERE age = 30")
	if res.Rows[0][0].Int != 2 {
		t.Fatalf("count wrong: got %d want 2", res.Rows[0][0].Int)
	}

	// The optimizer must pick an index scan for the PK and a seq scan for age.
	if plan := exec(t, s, "EXPLAIN SELECT * FROM users WHERE id = 3").Message; !strings.Contains(plan, "Index Scan") {
		t.Fatalf("expected index scan, got:\n%s", plan)
	}
	if plan := exec(t, s, "EXPLAIN SELECT * FROM users WHERE age = 30").Message; !strings.Contains(plan, "Seq Scan") {
		t.Fatalf("expected seq scan, got:\n%s", plan)
	}
}

func TestJoinAndDelete(t *testing.T) {
	db := open(t, t.TempDir(), txn.Mode2PL)
	defer db.Close()
	s := db.NewSession()
	seedUsers(t, s)
	exec(t, s, "CREATE TABLE orders (oid INT PRIMARY KEY, uid INT, item TEXT)")
	exec(t, s, "INSERT INTO orders VALUES (10, 1, 'book'), (11, 1, 'pen'), (12, 3, 'mug')")

	res := exec(t, s, "SELECT users.name, orders.item FROM users JOIN orders ON users.id = orders.uid")
	if len(res.Rows) != 3 {
		t.Fatalf("join expected 3 rows, got %d", len(res.Rows))
	}

	exec(t, s, "DELETE FROM users WHERE id = 2")
	res = exec(t, s, "SELECT COUNT(*) FROM users")
	if res.Rows[0][0].Int != 4 {
		t.Fatalf("after delete expected 4 users, got %d", res.Rows[0][0].Int)
	}
}

func TestCrashRecovery(t *testing.T) {
	dir := t.TempDir()
	db := open(t, dir, txn.Mode2PL)
	s := db.NewSession()
	seedUsers(t, s) // committed via autocommit

	// An explicit transaction that inserts but never commits.
	s2 := db.NewSession()
	exec(t, s2, "BEGIN")
	exec(t, s2, "INSERT INTO users VALUES (99, 'ghost', 99)")
	// Simulate a crash: dirty data pages are discarded; only the WAL is durable.
	if err := db.Crash(); err != nil {
		t.Fatalf("crash: %v", err)
	}

	// Reopen and run recovery.
	db = open(t, dir, txn.Mode2PL)
	defer db.Close()
	s = db.NewSession()

	res := exec(t, s, "SELECT COUNT(*) FROM users")
	if res.Rows[0][0].Int != 5 {
		t.Fatalf("recovery: expected 5 committed users, got %d", res.Rows[0][0].Int)
	}
	// The uncommitted ghost row must not survive.
	res = exec(t, s, "SELECT COUNT(*) FROM users WHERE id = 99")
	if res.Rows[0][0].Int != 0 {
		t.Fatalf("recovery: uncommitted row survived")
	}
}

func TestMVCCSnapshotIsolation(t *testing.T) {
	db := open(t, t.TempDir(), txn.ModeMVCC)
	defer db.Close()
	setup := db.NewSession()
	seedUsers(t, setup)

	// Two concurrent transactions; A begins first, then B snapshots while A is
	// still active, so A's later commit is invisible to B.
	a := db.NewSession()
	b := db.NewSession()
	exec(t, a, "BEGIN")
	exec(t, b, "BEGIN")

	exec(t, a, "DELETE FROM users WHERE id = 1")
	exec(t, a, "COMMIT")

	// B's snapshot predates A's commit: it must still see all 5 rows.
	res := exec(t, b, "SELECT COUNT(*) FROM users")
	if res.Rows[0][0].Int != 5 {
		t.Fatalf("MVCC: B should see 5 rows under its snapshot, got %d", res.Rows[0][0].Int)
	}
	exec(t, b, "COMMIT")

	// A fresh transaction sees A's committed delete.
	c := db.NewSession()
	res = exec(t, c, "SELECT COUNT(*) FROM users")
	if res.Rows[0][0].Int != 4 {
		t.Fatalf("MVCC: new txn should see 4 rows, got %d", res.Rows[0][0].Int)
	}
}
