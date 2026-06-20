package main

import (
	"fmt"
	"os"
	"time"

	"minidb/internal/db"
)

// runDemo drives several sessions of one Database from goroutines to show the
// 2PL concurrency control in action. These scenarios are referenced by the
// project's transaction demonstration.
func runDemo(name string) error {
	switch name {
	case "locking":
		return demoLocking()
	case "deadlock":
		return demoDeadlock()
	default:
		fmt.Println("usage: minidb demo <locking|deadlock>")
		fmt.Println("  locking  - a reader blocks behind an uncommitted writer, then proceeds")
		fmt.Println("  deadlock - two transactions form a cycle; one is aborted as the victim")
		return nil
	}
}

func openDemoDB() (*db.Database, error) {
	dir, err := os.MkdirTemp("", "minidb-demo-")
	if err != nil {
		return nil, err
	}
	return db.Open(dir, "heap")
}

func ts() string { return time.Now().Format("15:04:05.000") }

func step(who, msg string) { fmt.Printf("[%s] %-8s %s\n", ts(), who, msg) }

// demoLocking shows a SELECT (shared lock) blocking behind an uncommitted
// INSERT (exclusive lock) on the same table, then proceeding once it commits.
func demoLocking() error {
	database, err := openDemoDB()
	if err != nil {
		return err
	}
	defer database.Close()

	exec(database.NewSession(), "CREATE TABLE accounts (id INT PRIMARY KEY, bal INT)")
	exec(database.NewSession(), "INSERT INTO accounts VALUES (1, 100)")

	writer := database.NewSession()
	reader := database.NewSession()

	step("writer", "BEGIN")
	exec(writer, "BEGIN")
	step("writer", "INSERT INTO accounts VALUES (2, 50)  -> acquires X lock on accounts")
	exec(writer, "INSERT INTO accounts VALUES (2, 50)")

	done := make(chan struct{})
	go func() {
		step("reader", "BEGIN; SELECT * FROM accounts  -> wants S lock, must wait")
		exec(reader, "BEGIN")
		res, err := reader.Execute("SELECT id FROM accounts")
		if err != nil {
			step("reader", "ERROR: "+err.Error())
		} else {
			step("reader", fmt.Sprintf("SELECT returned %d row(s) (now sees committed insert)", len(res.Rows)))
		}
		exec(reader, "COMMIT")
		close(done)
	}()

	time.Sleep(300 * time.Millisecond)
	step("main", "reader is still blocked while writer holds the X lock")
	step("writer", "COMMIT  -> releases X lock")
	exec(writer, "COMMIT")

	<-done
	fmt.Println("\nResult: the reader blocked under 2PL until the writer committed, then saw a consistent snapshot.")
	return nil
}

// demoDeadlock forms a wait-for cycle between two transactions and shows the
// detector aborting one of them.
func demoDeadlock() error {
	database, err := openDemoDB()
	if err != nil {
		return err
	}
	defer database.Close()

	exec(database.NewSession(), "CREATE TABLE a (id INT PRIMARY KEY, v INT)")
	exec(database.NewSession(), "CREATE TABLE b (id INT PRIMARY KEY, v INT)")

	t1 := database.NewSession()
	t2 := database.NewSession()

	exec(t1, "BEGIN")
	exec(t2, "BEGIN")
	step("txn1", "INSERT INTO a ...  -> X lock on a")
	exec(t1, "INSERT INTO a VALUES (1, 1)")
	step("txn2", "INSERT INTO b ...  -> X lock on b")
	exec(t2, "INSERT INTO b VALUES (1, 1)")

	type r struct {
		who string
		err error
	}
	out := make(chan r, 2)
	go func() {
		step("txn1", "INSERT INTO b ...  -> wants X lock on b (held by txn2): waits")
		_, err := t1.Execute("INSERT INTO b VALUES (2, 2)")
		out <- r{"txn1", err}
	}()
	go func() {
		step("txn2", "INSERT INTO a ...  -> wants X lock on a (held by txn1): waits  [cycle!]")
		_, err := t2.Execute("INSERT INTO a VALUES (2, 2)")
		out <- r{"txn2", err}
	}()

	first := <-out
	if first.err != nil {
		step(first.who, "ABORTED by deadlock detector: "+first.err.Error())
	} else {
		step(first.who, "committed its write")
	}
	second := <-out
	if second.err != nil {
		step(second.who, "ABORTED by deadlock detector: "+second.err.Error())
	} else {
		step(second.who, "proceeded after the victim released its locks")
	}
	fmt.Println("\nResult: the wait-for graph detected the cycle and aborted one transaction as the victim.")
	return nil
}

func exec(s *db.Session, sql string) {
	if _, err := s.Execute(sql); err != nil {
		fmt.Printf("[%s] error: %s -> %v\n", ts(), sql, err)
	}
}
