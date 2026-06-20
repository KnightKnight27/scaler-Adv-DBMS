package storage

import (
	"path/filepath"
	"testing"
)

func newTestBP(t *testing.T, capacity int) (*BufferPool, *DiskManager) {
	t.Helper()
	dir := t.TempDir()
	dm, err := OpenDiskManager(filepath.Join(dir, "minidb.db"), filepath.Join(dir, "minidb.idx"))
	if err != nil {
		t.Fatalf("open disk manager: %v", err)
	}
	t.Cleanup(func() { dm.Close() })
	return NewBufferPool(dm, capacity), dm
}

func TestPageInsertReadDelete(t *testing.T) {
	p := NewPage()
	s1, err := p.Insert([]byte("hello"))
	if err != nil {
		t.Fatal(err)
	}
	s2, err := p.Insert([]byte("world!"))
	if err != nil {
		t.Fatal(err)
	}
	if got, _ := p.Read(s1); string(got) != "hello" {
		t.Fatalf("slot1 = %q", got)
	}
	if got, _ := p.Read(s2); string(got) != "world!" {
		t.Fatalf("slot2 = %q", got)
	}
	if err := p.Delete(s1); err != nil {
		t.Fatal(err)
	}
	if !p.IsTombstone(s1) {
		t.Fatal("slot1 should be tombstoned")
	}
	if _, err := p.Read(s1); err != ErrBadSlot {
		t.Fatalf("reading tombstone should fail, got %v", err)
	}
}

func TestPageFull(t *testing.T) {
	p := NewPage()
	big := make([]byte, PageSize) // cannot possibly fit with header+slot
	if _, err := p.Insert(big); err != ErrPageFull {
		t.Fatalf("expected ErrPageFull, got %v", err)
	}
}

func TestPagePersistRoundTrip(t *testing.T) {
	bp, dm := newTestBP(t, 8)
	id, p, err := bp.NewPage(FileData)
	if err != nil {
		t.Fatal(err)
	}
	slot, _ := p.Insert([]byte("durable"))
	p.SetLSN(42)
	bp.Unpin(FileData, id, true)
	if err := bp.FlushAll(); err != nil {
		t.Fatal(err)
	}
	// read straight from disk into a fresh page
	var fresh Page
	if err := dm.ReadPage(FileData, id, &fresh); err != nil {
		t.Fatal(err)
	}
	if got, _ := fresh.Read(slot); string(got) != "durable" {
		t.Fatalf("after flush, got %q", got)
	}
	if fresh.LSN() != 42 {
		t.Fatalf("pageLSN not persisted: %d", fresh.LSN())
	}
}

func TestBufferPoolEviction(t *testing.T) {
	bp, dm := newTestBP(t, 3)
	// allocate and dirty more pages than capacity; eviction must flush them.
	var ids []PageID
	for i := 0; i < 10; i++ {
		id, p, err := bp.NewPage(FileData)
		if err != nil {
			t.Fatalf("alloc %d: %v", i, err)
		}
		p.Insert([]byte{byte('A' + i)})
		bp.Unpin(FileData, id, true)
		ids = append(ids, id)
	}
	frames, capacity := bp.Stats()
	if frames > capacity {
		t.Fatalf("pool exceeded capacity: %d > %d", frames, capacity)
	}
	// every page should be retrievable with correct contents (evicted ones from disk)
	for i, id := range ids {
		p, err := bp.Fetch(FileData, id)
		if err != nil {
			t.Fatalf("fetch %d: %v", i, err)
		}
		got, _ := p.Read(0)
		if len(got) != 1 || got[0] != byte('A'+i) {
			t.Fatalf("page %d corrupted after eviction: %q", i, got)
		}
		bp.Unpin(FileData, id, false)
	}
	_ = dm
}

func TestBufferPoolAllPinned(t *testing.T) {
	bp, _ := newTestBP(t, 2)
	id1, _, _ := bp.NewPage(FileData)
	id2, _, _ := bp.NewPage(FileData)
	// both pinned; a third admit must fail to evict
	if _, _, err := bp.NewPage(FileData); err == nil {
		t.Fatal("expected eviction failure when all frames pinned")
	}
	bp.Unpin(FileData, id1, false)
	bp.Unpin(FileData, id2, false)
}

func TestHeapFileInsertScanDelete(t *testing.T) {
	bp, _ := newTestBP(t, 4)
	h, _, err := NewHeapFile(bp)
	if err != nil {
		t.Fatal(err)
	}
	// insert enough records to span multiple pages
	const n = 500
	rids := make([]RID, 0, n)
	for i := 0; i < n; i++ {
		rec := []byte("record-payload-" + string(rune('0'+i%10)))
		rid, err := h.Insert(rec)
		if err != nil {
			t.Fatalf("insert %d: %v", i, err)
		}
		rids = append(rids, rid)
	}
	// scan counts all live records
	count := 0
	h.Scan(func(_ RID, _ []byte) bool { count++; return true })
	if count != n {
		t.Fatalf("scan saw %d records, want %d", count, n)
	}
	// delete half, re-scan
	for i := 0; i < n; i += 2 {
		if err := h.Delete(rids[i], 0); err != nil {
			t.Fatalf("delete %d: %v", i, err)
		}
	}
	count = 0
	h.Scan(func(_ RID, _ []byte) bool { count++; return true })
	if count != n/2 {
		t.Fatalf("after deletes scan saw %d, want %d", count, n/2)
	}
}

func TestRIDRoundTrip(t *testing.T) {
	r := RID{PageID: 12345, Slot: 678}
	if got := DecodeRID(r.Encode()); got != r {
		t.Fatalf("RID round-trip: got %+v want %+v", got, r)
	}
}
