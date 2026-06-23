package main

import (
	"fmt"
	"os"
	"time"

	"minidb/internal/engine"
	"minidb/internal/storage"
	"minidb/internal/txn"
)

func runDemo(name string) {
	switch name {
	case "crash":
		demoCrash()
	case "deadlock":
		demoDeadlock()
	case "mvcc":
		demoMVCC()
	default:
		fmt.Fprintf(os.Stderr, "unknown demo %q (choose crash, deadlock or mvcc)\n", name)
		os.Exit(1)
	}
}

func section(title string) {
	fmt.Printf("\n=== %s ===\n", title)
}

func mustExec(s *engine.Session, q string) {
	res, err := s.Execute(q)
	if err != nil {
		fmt.Printf("  ! %s -> error: %v\n", q, err)
		return
	}
	if res.IsQuery {
		if len(res.Rows) == 1 && len(res.Rows[0]) == 1 {
			fmt.Printf("  %s -> %s\n", q, res.Rows[0][0].String())
		} else {
			fmt.Printf("  %s -> %d row(s)\n", q, len(res.Rows))
		}
	} else {
		fmt.Printf("  %s -> %s\n", q, res.Message)
	}
}

// demoCrash shows that committed work survives a crash and uncommitted work does
// not, by relying solely on the WAL for durability (the buffer pool is discarded
// on the simulated crash).
func demoCrash() {
	section("Crash Recovery Demo (WAL: redo committed, undo uncommitted)")
	dir, _ := os.MkdirTemp("", "minidb-crash")
	defer os.RemoveAll(dir)

	db, err := engine.Open(engine.Options{Dir: dir, Mode: txn.Mode2PL})
	if err != nil {
		fmt.Println("open:", err)
		return
	}
	s := db.NewSession()
	fmt.Println("Committed work (autocommit):")
	mustExec(s, "CREATE TABLE accounts (id INT PRIMARY KEY, balance INT)")
	mustExec(s, "INSERT INTO accounts VALUES (1, 100), (2, 200), (3, 300)")

	fmt.Println("Uncommitted work in an explicit transaction:")
	u := db.NewSession()
	mustExec(u, "BEGIN")
	mustExec(u, "INSERT INTO accounts VALUES (4, 999)")
	fmt.Println("  (transaction 4 is left OPEN, then the process 'crashes')")

	if err := db.Crash(); err != nil {
		fmt.Println("crash:", err)
		return
	}
	fmt.Println("** simulated crash: buffer pool discarded, only the WAL is durable **")

	db, err = engine.Open(engine.Options{Dir: dir, Mode: txn.Mode2PL})
	if err != nil {
		fmt.Println("reopen:", err)
		return
	}
	defer db.Close()
	r := db.LastRecovery()
	fmt.Printf("Recovery summary: redone=%d undone=%d committed_txns=%v\n", r.Redone, r.Undone, r.Committed)

	s = db.NewSession()
	fmt.Println("State after recovery:")
	mustExec(s, "SELECT * FROM accounts")
	mustExec(s, "SELECT COUNT(*) FROM accounts WHERE id = 4")
	fmt.Println("Committed rows (1,2,3) survive; the uncommitted row (4) was rolled back.")
}

// demoDeadlock drives the lock manager directly to show shared/exclusive lock
// acquisition, a wait-for cycle between two transactions, and the eager deadlock
// detection that aborts a victim so the other transaction can finish.
func demoDeadlock() {
	section("Deadlock Detection Demo (Strict 2PL + wait-for graph)")
	lm := txn.NewLockManager()
	ridA := storage.RID{Page: 0, Slot: 0}
	ridB := storage.RID{Page: 0, Slot: 1}

	const t1, t2 = txn.TxnID(1), txn.TxnID(2)
	_ = lm.Acquire(t1, ridA, txn.Exclusive)
	fmt.Println("  T1 acquires X lock on row A")
	_ = lm.Acquire(t2, ridB, txn.Exclusive)
	fmt.Println("  T2 acquires X lock on row B")

	// T1 now wants B (held by T2): it will block.
	t1GotB := make(chan error, 1)
	go func() {
		fmt.Println("  T1 requests X lock on row B  (held by T2 -> T1 waits)")
		t1GotB <- lm.Acquire(t1, ridB, txn.Exclusive)
	}()
	time.Sleep(100 * time.Millisecond) // let T1 register as a waiter

	// T2 wants A (held by T1): this closes the cycle T1->T2->T1.
	fmt.Println("  T2 requests X lock on row A  (held by T1 -> cycle!)")
	err := lm.Acquire(t2, ridA, txn.Exclusive)
	if err == txn.ErrDeadlock {
		fmt.Println("  >> deadlock detected: T2 is chosen as victim and aborts")
	} else {
		fmt.Println("  unexpected:", err)
	}

	// The victim releases its locks; T1 can now proceed.
	lm.ReleaseAll(t2)
	if err := <-t1GotB; err == nil {
		fmt.Println("  T1 proceeds and acquires row B after T2 releases its locks")
	}
	lm.ReleaseAll(t1)
	fmt.Println("Outcome: one transaction committed, the other was safely aborted and can retry.")
}

// demoMVCC shows snapshot isolation: a reader that started before a writer's
// commit keeps seeing the old state, and the writer never blocks on the reader.
func demoMVCC() {
	section("MVCC Snapshot Isolation Demo (Extension Track B)")
	dir, _ := os.MkdirTemp("", "minidb-mvcc")
	defer os.RemoveAll(dir)

	db, err := engine.Open(engine.Options{Dir: dir, Mode: txn.ModeMVCC})
	if err != nil {
		fmt.Println("open:", err)
		return
	}
	defer db.Close()

	setup := db.NewSession()
	mustExec(setup, "CREATE TABLE items (id INT PRIMARY KEY, qty INT)")
	mustExec(setup, "INSERT INTO items VALUES (1, 10), (2, 20), (3, 30)")

	reader := db.NewSession()
	writer := db.NewSession()
	fmt.Println("Reader and Writer both BEGIN (reader's snapshot is taken now):")
	mustExec(reader, "BEGIN")
	mustExec(writer, "BEGIN")

	fmt.Println("Writer deletes a row and commits (no blocking, no locks held by reader):")
	mustExec(writer, "DELETE FROM items WHERE id = 2")
	mustExec(writer, "COMMIT")

	fmt.Println("Reader, under its original snapshot, still sees all 3 rows:")
	mustExec(reader, "SELECT COUNT(*) FROM items")
	mustExec(reader, "COMMIT")

	fmt.Println("A new transaction sees the committed delete (2 rows):")
	after := db.NewSession()
	mustExec(after, "SELECT COUNT(*) FROM items")
	fmt.Println("Reads never blocked the writer and the writer never blocked the reader.")
}
