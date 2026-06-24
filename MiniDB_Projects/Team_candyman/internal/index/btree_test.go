package index

import (
	"math/rand"
	"path/filepath"
	"testing"

	"minidb/internal/storage"
	"minidb/internal/types"
)

func newTestTree(t *testing.T) *BTree {
	t.Helper()
	dir := t.TempDir()
	dm, err := storage.OpenDiskManager(filepath.Join(dir, "d.db"), filepath.Join(dir, "d.idx"))
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { dm.Close() })
	bt, err := NewBTree(storage.NewBufferPool(dm, 64))
	if err != nil {
		t.Fatal(err)
	}
	return bt
}

func TestBTreeInsertSearch(t *testing.T) {
	bt := newTestTree(t)
	const n = 2000 // enough to force many splits
	for i := 0; i < n; i++ {
		rid := storage.RID{PageID: storage.PageID(i), Slot: uint16(i % 100)}
		if err := bt.Insert(types.NewInt(int64(i)), rid); err != nil {
			t.Fatalf("insert %d: %v", i, err)
		}
	}
	for i := 0; i < n; i++ {
		rid, ok, err := bt.Search(types.NewInt(int64(i)))
		if err != nil || !ok {
			t.Fatalf("search %d: ok=%v err=%v", i, ok, err)
		}
		want := storage.RID{PageID: storage.PageID(i), Slot: uint16(i % 100)}
		if rid != want {
			t.Fatalf("search %d: got %+v want %+v", i, rid, want)
		}
	}
	if _, ok, _ := bt.Search(types.NewInt(99999)); ok {
		t.Fatal("found a key that was never inserted")
	}
}

func TestBTreeRandomizedAndUpsert(t *testing.T) {
	bt := newTestTree(t)
	rng := rand.New(rand.NewSource(42))
	keys := rng.Perm(1500)
	for _, k := range keys {
		bt.Insert(types.NewInt(int64(k)), storage.RID{PageID: storage.PageID(k)})
	}
	// upsert: overwrite value for an existing key
	if err := bt.Insert(types.NewInt(7), storage.RID{PageID: 777}); err != nil {
		t.Fatal(err)
	}
	if rid, _, _ := bt.Search(types.NewInt(7)); rid.PageID != 777 {
		t.Fatalf("upsert failed: %+v", rid)
	}
}

func TestBTreeRange(t *testing.T) {
	bt := newTestTree(t)
	for i := 0; i < 1000; i++ {
		bt.Insert(types.NewInt(int64(i)), storage.RID{PageID: storage.PageID(i)})
	}
	lo, hi := types.NewInt(100), types.NewInt(199)
	var got []int
	bt.Range(&lo, &hi, func(rid storage.RID) bool {
		got = append(got, int(rid.PageID))
		return true
	})
	if len(got) != 100 {
		t.Fatalf("range [100,199] returned %d rows, want 100", len(got))
	}
	for i, v := range got {
		if v != 100+i {
			t.Fatalf("range not ascending at %d: %d", i, v)
		}
	}
}

func TestBTreeDelete(t *testing.T) {
	bt := newTestTree(t)
	for i := 0; i < 500; i++ {
		bt.Insert(types.NewInt(int64(i)), storage.RID{PageID: storage.PageID(i)})
	}
	for i := 0; i < 500; i += 2 {
		ok, err := bt.Delete(types.NewInt(int64(i)))
		if err != nil || !ok {
			t.Fatalf("delete %d: ok=%v err=%v", i, ok, err)
		}
	}
	for i := 0; i < 500; i++ {
		_, ok, _ := bt.Search(types.NewInt(int64(i)))
		want := i%2 == 1
		if ok != want {
			t.Fatalf("after delete key %d present=%v want=%v", i, ok, want)
		}
	}
}

func TestBTreeStringKeys(t *testing.T) {
	bt := newTestTree(t)
	words := []string{"delta", "alpha", "charlie", "echo", "bravo"}
	for i, w := range words {
		bt.Insert(types.NewText(w), storage.RID{PageID: storage.PageID(i)})
	}
	for i, w := range words {
		rid, ok, _ := bt.Search(types.NewText(w))
		if !ok || int(rid.PageID) != i {
			t.Fatalf("string search %q: ok=%v rid=%+v", w, ok, rid)
		}
	}
	// range scan should come back in lexicographic order
	var got []string
	bt.Range(nil, nil, func(rid storage.RID) bool {
		got = append(got, words[rid.PageID])
		return true
	})
	want := []string{"alpha", "bravo", "charlie", "delta", "echo"}
	for i := range want {
		if got[i] != want[i] {
			t.Fatalf("ordered scan = %v, want %v", got, want)
		}
	}
}
