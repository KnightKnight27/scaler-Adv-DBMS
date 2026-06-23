// Command bench measures MiniDB throughput under a read-heavy, contended
// workload and contrasts the two concurrency-control strategies, which is the
// experiment required by Extension Track B (MVCC vs 2PL).
//
// Each "reader" runs a transaction that scans the whole table and holds it open
// briefly (simulating an analytical read); each "writer" deletes and re-inserts
// a random row. Under strict 2PL a long reader holds shared locks on every row
// it touched until commit, so writers block and may deadlock; under MVCC readers
// take a snapshot and no locks, so writers proceed without blocking.
package main

import (
	"flag"
	"fmt"
	"math/rand"
	"os"
	"sync"
	"sync/atomic"
	"time"

	"minidb/internal/engine"
	"minidb/internal/txn"
)

type result struct {
	mode    string
	reads   int64
	writes  int64
	aborts  int64
	elapsed time.Duration
}

func main() {
	rows := flag.Int("rows", 200, "number of seed rows")
	readers := flag.Int("readers", 4, "concurrent reader transactions")
	writers := flag.Int("writers", 4, "concurrent writer transactions")
	seconds := flag.Float64("seconds", 2.0, "duration of each run")
	flag.Parse()

	dur := time.Duration(*seconds * float64(time.Second))
	fmt.Printf("MiniDB Track B benchmark: %d rows, %d readers, %d writers, %.1fs per mode\n\n",
		*rows, *readers, *writers, *seconds)

	r2pl := runWorkload(txn.Mode2PL, *rows, *readers, *writers, dur)
	rmvcc := runWorkload(txn.ModeMVCC, *rows, *readers, *writers, dur)

	printComparison(r2pl, rmvcc)
}

func runWorkload(mode txn.Mode, rows, readers, writers int, dur time.Duration) result {
	dir, _ := os.MkdirTemp("", "minidb-bench")
	defer os.RemoveAll(dir)

	db, err := engine.Open(engine.Options{Dir: dir, Mode: mode, BufferFrames: 256})
	if err != nil {
		fmt.Println("open:", err)
		os.Exit(1)
	}
	defer db.Close()

	seed(db, rows)

	var reads, writes, aborts int64
	deadline := time.Now().Add(dur)
	var wg sync.WaitGroup

	for i := 0; i < readers; i++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			s := db.NewSession()
			for time.Now().Before(deadline) {
				if runReaderTxn(s) {
					atomic.AddInt64(&reads, 1)
				} else {
					atomic.AddInt64(&aborts, 1)
				}
			}
		}()
	}

	for i := 0; i < writers; i++ {
		wg.Add(1)
		go func(seed int64) {
			defer wg.Done()
			s := db.NewSession()
			rng := rand.New(rand.NewSource(seed))
			for time.Now().Before(deadline) {
				if runWriterTxn(s, rng.Intn(rows)+1) {
					atomic.AddInt64(&writes, 1)
				} else {
					atomic.AddInt64(&aborts, 1)
				}
			}
		}(int64(i + 1))
	}

	start := time.Now()
	wg.Wait()
	return result{mode: mode.String(), reads: reads, writes: writes, aborts: aborts, elapsed: time.Since(start)}
}

func seed(db *engine.DB, rows int) {
	s := db.NewSession()
	must(s, "CREATE TABLE accounts (id INT PRIMARY KEY, balance INT)")
	for i := 1; i <= rows; i++ {
		must(s, fmt.Sprintf("INSERT INTO accounts VALUES (%d, %d)", i, i*10))
	}
}

// runReaderTxn runs one read transaction and returns whether it committed.
func runReaderTxn(s *engine.Session) bool {
	if _, err := s.Execute("BEGIN"); err != nil {
		return false
	}
	if _, err := s.Execute("SELECT COUNT(*) FROM accounts"); err != nil {
		s.Execute("ROLLBACK")
		return false
	}
	time.Sleep(time.Millisecond) // hold the read open to create contention
	_, err := s.Execute("COMMIT")
	return err == nil
}

// runWriterTxn deletes and re-inserts row id, returning whether it committed.
func runWriterTxn(s *engine.Session, id int) bool {
	if _, err := s.Execute("BEGIN"); err != nil {
		return false
	}
	if _, err := s.Execute(fmt.Sprintf("DELETE FROM accounts WHERE id = %d", id)); err != nil {
		s.Execute("ROLLBACK")
		return false
	}
	if _, err := s.Execute(fmt.Sprintf("INSERT INTO accounts VALUES (%d, %d)", id, id*10)); err != nil {
		s.Execute("ROLLBACK")
		return false
	}
	if _, err := s.Execute("COMMIT"); err != nil {
		s.Execute("ROLLBACK")
		return false
	}
	return true
}

func must(s *engine.Session, q string) {
	if _, err := s.Execute(q); err != nil {
		fmt.Printf("setup error on %q: %v\n", q, err)
		os.Exit(1)
	}
}

func printComparison(a, b result) {
	fmt.Printf("%-6s  %12s  %12s  %12s  %12s\n", "mode", "reads", "writes", "aborts", "txns/sec")
	fmt.Println("------------------------------------------------------------------------")
	for _, r := range []result{a, b} {
		total := float64(r.reads+r.writes) / r.elapsed.Seconds()
		fmt.Printf("%-6s  %12d  %12d  %12d  %12.0f\n", r.mode, r.reads, r.writes, r.aborts, total)
	}
	fmt.Println()
	if a.writes >= 5 {
		fmt.Printf("MVCC write throughput vs 2PL: %.2fx\n", float64(b.writes)/float64(a.writes))
	} else {
		fmt.Printf("MVCC writes=%d vs 2PL writes=%d (2PL writers starved by long readers' shared locks)\n", b.writes, a.writes)
	}
	fmt.Printf("Deadlock/conflict aborts: 2PL=%d, MVCC=%d\n", a.aborts, b.aborts)
}
