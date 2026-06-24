package db

import "testing"

// TestCrashRecovery simulates a crash by abandoning a Database without a clean
// Close (so committed data lives only in the WAL and the in-memory buffer pool,
// not yet flushed to the heap) and reopening, which must redo committed
// transactions and discard uncommitted ones.
func TestCrashRecovery(t *testing.T) {
	dir := t.TempDir()

	d, err := Open(dir, "heap")
	if err != nil {
		t.Fatal(err)
	}
	s := d.NewSession()
	mustExec(t, s, "CREATE TABLE t (id INT PRIMARY KEY, v TEXT)")
	mustExec(t, s, "INSERT INTO t VALUES (1, 'committed-A')")
	mustExec(t, s, "INSERT INTO t VALUES (2, 'committed-B')")

	// an explicit transaction that never commits
	u := d.NewSession()
	if _, err := u.Execute("BEGIN"); err != nil {
		t.Fatal(err)
	}
	if _, err := u.Execute("INSERT INTO t VALUES (3, 'uncommitted-C')"); err != nil {
		t.Fatal(err)
	}
	// *** crash: do NOT call d.Close() ***

	// reopen the same directory -> triggers recovery
	d2, err := Open(dir, "heap")
	if err != nil {
		t.Fatalf("reopen/recovery: %v", err)
	}
	defer d2.Close()

	res, err := d2.Execute("SELECT id, v FROM t")
	if err != nil {
		t.Fatal(err)
	}
	if len(res.Rows) != 2 {
		t.Fatalf("recovery: expected 2 committed rows, got %d: %v", len(res.Rows), res.Rows)
	}
	// committed rows must be present; the uncommitted one must be gone
	got := map[string]bool{}
	for _, r := range res.Rows {
		got[r[0]] = true
	}
	if !got["1"] || !got["2"] {
		t.Fatalf("committed rows missing after recovery: %v", res.Rows)
	}
	if got["3"] {
		t.Fatal("uncommitted row survived recovery")
	}
}

func TestRollbackUndoesChanges(t *testing.T) {
	d := openTest(t)
	mustExec(t, d, "CREATE TABLE t (id INT PRIMARY KEY, v INT)")
	mustExec(t, d, "INSERT INTO t VALUES (1, 10)")

	s := d.NewSession()
	if _, err := s.Execute("BEGIN"); err != nil {
		t.Fatal(err)
	}
	if _, err := s.Execute("INSERT INTO t VALUES (2, 20)"); err != nil {
		t.Fatal(err)
	}
	if _, err := s.Execute("DELETE FROM t WHERE id = 1"); err != nil {
		t.Fatal(err)
	}
	if _, err := s.Execute("ROLLBACK"); err != nil {
		t.Fatal(err)
	}

	res := mustExec(t, d, "SELECT id FROM t")
	if len(res.Rows) != 1 || res.Rows[0][0] != "1" {
		t.Fatalf("after rollback expected only row 1, got %v", res.Rows)
	}
}
