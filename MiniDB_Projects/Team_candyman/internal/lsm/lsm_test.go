package lsm

import (
	"fmt"
	"path/filepath"
	"testing"

	"minidb/internal/catalog"
	"minidb/internal/types"
)

func TestBloomNoFalseNegatives(t *testing.T) {
	b := NewBloom(1000)
	for i := 0; i < 1000; i++ {
		b.Add([]byte(fmt.Sprintf("key-%d", i)))
	}
	for i := 0; i < 1000; i++ {
		if !b.Test([]byte(fmt.Sprintf("key-%d", i))) {
			t.Fatalf("bloom false negative for key-%d", i)
		}
	}
}

func TestMemTablePutDeleteSorted(t *testing.T) {
	m := NewMemTable()
	m.Put("b", []byte("2"))
	m.Put("a", []byte("1"))
	m.Delete("c")
	got := m.Sorted()
	if len(got) != 3 || got[0].Key != "a" || got[1].Key != "b" || !got[2].Tomb {
		t.Fatalf("sorted memtable wrong: %+v", got)
	}
}

func TestSSTableRoundTrip(t *testing.T) {
	path := filepath.Join(t.TempDir(), "x.sst")
	entries := []KV{
		{Key: "a", Val: []byte("1")},
		{Key: "b", Tomb: true},
		{Key: "c", Val: []byte("333")},
	}
	if err := WriteSSTable(path, entries); err != nil {
		t.Fatal(err)
	}
	s, err := OpenSSTable(path)
	if err != nil {
		t.Fatal(err)
	}
	defer s.Close()
	if kv, ok, _ := s.Get("a"); !ok || string(kv.Val) != "1" {
		t.Fatalf("get a: %+v ok=%v", kv, ok)
	}
	if kv, ok, _ := s.Get("b"); !ok || !kv.Tomb {
		t.Fatalf("get b should be tombstone: %+v", kv)
	}
	if _, ok, _ := s.Get("zzz"); ok {
		t.Fatal("get of absent key returned ok")
	}
	all, _ := s.All()
	if len(all) != 3 {
		t.Fatalf("All returned %d", len(all))
	}
}

func openEngine(t *testing.T) (*Engine, *catalog.Catalog) {
	t.Helper()
	dir := t.TempDir()
	cat, err := catalog.Open(filepath.Join(dir, "catalog.json"))
	if err != nil {
		t.Fatal(err)
	}
	e, err := Open(dir, cat)
	if err != nil {
		t.Fatal(err)
	}
	return e, cat
}

func tableSchema() *types.Schema {
	return &types.Schema{
		Columns: []types.Column{{Name: "id", Type: types.TypeInt}, {Name: "v", Type: types.TypeText}},
		PKIndex: 0,
	}
}

func TestEngineFlushCompactPersist(t *testing.T) {
	e, cat := openEngine(t)
	if err := e.CreateTable("t", tableSchema()); err != nil {
		t.Fatal(err)
	}
	const n = 20000 // enough bytes to trigger several flushes and a compaction
	for i := 0; i < n; i++ {
		row := types.Row{types.NewInt(int64(i)), types.NewText(fmt.Sprintf("value-%d", i))}
		if err := e.Put("t", types.NewInt(int64(i)), row); err != nil {
			t.Fatalf("put %d: %v", i, err)
		}
	}
	// overwrite + delete to exercise newest-wins and tombstones
	e.Put("t", types.NewInt(5), types.Row{types.NewInt(5), types.NewText("OVERWRITTEN")})
	if _, err := e.Delete("t", types.NewInt(7)); err != nil {
		t.Fatal(err)
	}

	check := func(e *Engine, label string) {
		if row, ok, _ := e.Get("t", types.NewInt(12345)); !ok || row[1].Str != "value-12345" {
			t.Fatalf("%s: get 12345 = %+v ok=%v", label, row, ok)
		}
		if row, ok, _ := e.Get("t", types.NewInt(5)); !ok || row[1].Str != "OVERWRITTEN" {
			t.Fatalf("%s: overwrite not visible: %+v", label, row)
		}
		if _, ok, _ := e.Get("t", types.NewInt(7)); ok {
			t.Fatalf("%s: deleted key 7 still present", label)
		}
	}
	check(e, "in-memory")

	// reopen and verify everything persisted across SSTables
	if err := e.Close(); err != nil {
		t.Fatal(err)
	}
	e2, err := Open(e.dir, cat)
	if err != nil {
		t.Fatal(err)
	}
	defer e2.Close()
	check(e2, "after-reopen")

	// full scan should see n live rows (n inserted, 1 deleted)
	cur, _ := e2.Scan("t")
	count := 0
	for {
		_, ok, err := cur.Next()
		if err != nil {
			t.Fatal(err)
		}
		if !ok {
			break
		}
		count++
	}
	if count != n-1 {
		t.Fatalf("scan after reopen: %d rows, want %d", count, n-1)
	}
}
