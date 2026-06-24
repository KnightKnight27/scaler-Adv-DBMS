// Package benchmarks compares the two storage engines (heap+B+Tree vs LSM-tree)
// on write throughput, point-read latency and storage amplification, and shows
// the optimizer's index-scan vs sequential-scan trade-off.
//
// Run:
//
//	go test -bench . ./benchmarks/...          # Go micro-benchmarks
//	go test -run Report -v ./benchmarks/...    # formatted comparison table
package benchmarks

import (
	"fmt"
	"math/rand"
	"os"
	"path/filepath"
	"testing"
	"time"

	"minidb/internal/catalog"
	"minidb/internal/engine"
	"minidb/internal/executor"
	"minidb/internal/lsm"
	"minidb/internal/sql"
	"minidb/internal/storage"
	"minidb/internal/types"
)

func benchSchema() *types.Schema {
	return &types.Schema{
		Columns: []types.Column{
			{Name: "id", Type: types.TypeInt},
			{Name: "payload", Type: types.TypeText},
		},
		PKIndex: 0,
	}
}

func row(i int) types.Row {
	return types.Row{types.NewInt(int64(i)), types.NewText(fmt.Sprintf("payload-value-%08d", i))}
}

// newHeap builds a heap engine in dir with a table "t".
func newHeap(tb testing.TB, dir string) storage.StorageEngine {
	dm, err := storage.OpenDiskManager(filepath.Join(dir, "minidb.db"), filepath.Join(dir, "minidb.idx"))
	if err != nil {
		tb.Fatal(err)
	}
	bp := storage.NewBufferPool(dm, 4096)
	cat, _ := catalog.Open(filepath.Join(dir, "catalog.json"))
	e, err := engine.OpenHeap(dm, bp, cat)
	if err != nil {
		tb.Fatal(err)
	}
	if err := e.CreateTable("t", benchSchema()); err != nil {
		tb.Fatal(err)
	}
	return e
}

// newLSM builds an LSM engine in dir with a table "t".
func newLSM(tb testing.TB, dir string) storage.StorageEngine {
	cat, _ := catalog.Open(filepath.Join(dir, "catalog.json"))
	e, err := lsm.Open(dir, cat)
	if err != nil {
		tb.Fatal(err)
	}
	if err := e.CreateTable("t", benchSchema()); err != nil {
		tb.Fatal(err)
	}
	return e
}

func benchPut(b *testing.B, mk func(testing.TB, string) storage.StorageEngine) {
	e := mk(b, b.TempDir())
	defer e.Close()
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		r := row(i)
		if err := e.Put("t", r.PK(benchSchema()), r); err != nil {
			b.Fatal(err)
		}
	}
}

func benchGet(b *testing.B, mk func(testing.TB, string) storage.StorageEngine) {
	e := mk(b, b.TempDir())
	defer e.Close()
	const n = 100000
	for i := 0; i < n; i++ {
		r := row(i)
		e.Put("t", r.PK(benchSchema()), r)
	}
	e.Sync()
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		pk := types.NewInt(int64(rand.Intn(n)))
		if _, ok, err := e.Get("t", pk); err != nil || !ok {
			b.Fatalf("get miss: ok=%v err=%v", ok, err)
		}
	}
}

func BenchmarkHeapPut(b *testing.B) { benchPut(b, newHeap) }
func BenchmarkLSMPut(b *testing.B)  { benchPut(b, newLSM) }
func BenchmarkHeapGet(b *testing.B) { benchGet(b, newHeap) }
func BenchmarkLSMGet(b *testing.B)  { benchGet(b, newLSM) }

// dirSize sums the size of all files in dir (storage amplification).
func dirSize(dir string) int64 {
	var total int64
	ents, _ := os.ReadDir(dir)
	for _, de := range ents {
		if info, err := de.Info(); err == nil {
			total += info.Size()
		}
	}
	return total
}

// benchGate skips the heavy reporting tests during a normal `go test ./...`.
func benchGate(t *testing.T) {
	if os.Getenv("MINIDB_BENCH") == "" {
		t.Skip("set MINIDB_BENCH=1 to run the reporting benchmarks")
	}
}

// TestReport prints a human-readable comparison table for the README. It is not
// a pass/fail test; it always succeeds.
func TestReport(t *testing.T) {
	benchGate(t)
	const n = 200000
	type result struct {
		writeOps float64
		readLat  time.Duration
		bytes    int64
	}

	run := func(mk func(testing.TB, string) storage.StorageEngine) result {
		dir := t.TempDir()
		e := mk(t, dir)
		defer e.Close()

		start := time.Now()
		for i := 0; i < n; i++ {
			r := row(i)
			e.Put("t", r.PK(benchSchema()), r)
		}
		e.Sync()
		writeDur := time.Since(start)

		const reads = 50000
		start = time.Now()
		for i := 0; i < reads; i++ {
			e.Get("t", types.NewInt(int64(rand.Intn(n))))
		}
		readDur := time.Since(start)

		return result{
			writeOps: float64(n) / writeDur.Seconds(),
			readLat:  readDur / reads,
			bytes:    dirSize(dir),
		}
	}

	heap := run(newHeap)
	lsmr := run(newLSM)

	fmt.Printf("\n=== MiniDB storage-engine benchmark (n=%d rows) ===\n", n)
	fmt.Printf("%-22s %15s %15s\n", "metric", "heap (B+Tree)", "LSM-tree")
	fmt.Printf("%-22s %15s %15s\n", "----------------------", "---------------", "---------------")
	fmt.Printf("%-22s %12.0f/s %12.0f/s\n", "write throughput", heap.writeOps, lsmr.writeOps)
	fmt.Printf("%-22s %15s %15s\n", "point-read latency", heap.readLat.String(), lsmr.readLat.String())
	fmt.Printf("%-22s %12.2f MB %12.2f MB\n", "on-disk size", float64(heap.bytes)/1e6, float64(lsmr.bytes)/1e6)
	fmt.Println()
}

// TestIndexVsSeqScan compares a primary-key point lookup served by an IndexScan
// against the same lookup served by a full SeqScan + Filter, demonstrating why
// the optimizer prefers the index for equality predicates.
func TestIndexVsSeqScan(t *testing.T) {
	benchGate(t)
	dir := t.TempDir()
	e := newHeap(t, dir)
	defer e.Close()
	const n = 200000
	for i := 0; i < n; i++ {
		r := row(i)
		e.Put("t", r.PK(benchSchema()), r)
	}
	e.Sync()

	const reps = 2000
	target := types.NewInt(n / 2)

	// IndexScan path
	start := time.Now()
	for i := 0; i < reps; i++ {
		op := &executor.IndexScan{Engine: e, Table: "t", Alias: "t", Key: target}
		drain(op)
	}
	idxDur := time.Since(start) / reps

	// SeqScan + Filter path (id = target)
	pred := &sql.BinaryExpr{Op: "=", Left: &sql.ColumnRef{Name: "id"}, Right: &sql.Literal{Value: target}}
	start = time.Now()
	for i := 0; i < reps; i++ {
		op := &executor.Filter{Child: &executor.SeqScan{Engine: e, Table: "t", Alias: "t"}, Pred: pred}
		drain(op)
	}
	seqDur := time.Since(start) / reps

	fmt.Printf("\n=== Index vs sequential scan (point lookup, n=%d rows) ===\n", n)
	fmt.Printf("IndexScan : %v / lookup\n", idxDur)
	fmt.Printf("SeqScan   : %v / lookup\n", seqDur)
	if seqDur > 0 {
		fmt.Printf("speedup   : %.0fx\n\n", float64(seqDur)/float64(idxDur))
	}
}

func drain(op executor.Operator) {
	op.Open()
	for {
		_, ok, err := op.Next()
		if err != nil || !ok {
			break
		}
	}
	op.Close()
}
