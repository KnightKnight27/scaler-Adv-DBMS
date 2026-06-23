package main

import (
	"fmt"
	"testing"

	"minidb/internal/engine"
	"minidb/internal/txn"
)

// benchDB creates a throwaway database seeded with n rows in table `t(id INT
// PRIMARY KEY, val INT, label TEXT)`.
func benchDB(b *testing.B, n int) (*engine.DB, *engine.Session) {
	b.Helper()
	db, err := engine.Open(engine.Options{Dir: b.TempDir(), Mode: txn.Mode2PL, BufferFrames: 512})
	if err != nil {
		b.Fatal(err)
	}
	s := db.NewSession()
	mustB(b, s, "CREATE TABLE t (id INT PRIMARY KEY, val INT, label TEXT)")
	for i := 1; i <= n; i++ {
		mustB(b, s, fmt.Sprintf("INSERT INTO t VALUES (%d, %d, 'row-%d')", i, i%100, i))
	}
	return db, s
}

func mustB(b *testing.B, s *engine.Session, q string) {
	if _, err := s.Execute(q); err != nil {
		b.Fatalf("%q: %v", q, err)
	}
}

// BenchmarkPointLookupIndex measures a primary-key equality query, which the
// optimizer satisfies with a B+Tree index scan.
func BenchmarkPointLookupIndex(b *testing.B) {
	const n = 5000
	db, s := benchDB(b, n)
	defer db.Close()
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		k := (i % n) + 1
		if _, err := s.Execute(fmt.Sprintf("SELECT id, label FROM t WHERE id = %d", k)); err != nil {
			b.Fatal(err)
		}
	}
}

// BenchmarkPointLookupSeqScan measures the same selectivity on a non-indexed
// column, forcing a sequential scan, to quantify the index's benefit.
func BenchmarkPointLookupSeqScan(b *testing.B) {
	const n = 5000
	db, s := benchDB(b, n)
	defer db.Close()
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		k := (i % n) + 1
		if _, err := s.Execute(fmt.Sprintf("SELECT id, label FROM t WHERE label = 'row-%d'", k)); err != nil {
			b.Fatal(err)
		}
	}
}

// BenchmarkInsert measures sustained single-row insert throughput including WAL
// logging and index maintenance.
func BenchmarkInsert(b *testing.B) {
	db, err := engine.Open(engine.Options{Dir: b.TempDir(), Mode: txn.Mode2PL, BufferFrames: 512})
	if err != nil {
		b.Fatal(err)
	}
	defer db.Close()
	s := db.NewSession()
	mustB(b, s, "CREATE TABLE t (id INT PRIMARY KEY, val INT, label TEXT)")
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		if _, err := s.Execute(fmt.Sprintf("INSERT INTO t VALUES (%d, %d, 'x')", i, i)); err != nil {
			b.Fatal(err)
		}
	}
}
