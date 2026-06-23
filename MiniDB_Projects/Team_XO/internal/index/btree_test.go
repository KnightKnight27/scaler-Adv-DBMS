package index

import (
	"math/rand"
	"sort"
	"testing"

	"minidb/internal/storage"
	"minidb/internal/types"
)

// ridFor maps a key to a deterministic RID so tests can use (key -> single rid).
func ridFor(k int64) storage.RID {
	return storage.RID{Page: storage.PageID(k / 100), Slot: uint16(k % 100)}
}

// TestBPlusTreeAgainstMap fuzzes insert/delete/search against a reference map to
// validate the split, merge and redistribute paths across many tree shapes.
func TestBPlusTreeAgainstMap(t *testing.T) {
	for _, order := range []int{3, 4, 8} {
		tree := New(order)
		ref := map[int64]storage.RID{}
		rng := rand.New(rand.NewSource(int64(order) * 7))

		for op := 0; op < 5000; op++ {
			k := rng.Int63n(200)
			key := types.NewInt(k)
			if rng.Intn(2) == 0 {
				rid := ridFor(k)
				tree.Insert(key, rid)
				ref[k] = rid
			} else {
				tree.Delete(key, ridFor(k))
				delete(ref, k)
			}

			if got := tree.Search(key); len(got) != boolToInt(refHas(ref, k)) {
				t.Fatalf("order=%d key=%d: search returned %d rids, ref present=%v", order, k, len(got), refHas(ref, k))
			}
		}

		// Full range scan must return exactly the reference set in sorted order.
		got := tree.SearchRange(types.NewInt(-1), types.NewInt(1_000))
		want := make([]int64, 0, len(ref))
		for k := range ref {
			want = append(want, k)
		}
		sort.Slice(want, func(i, j int) bool { return want[i] < want[j] })
		if len(got) != len(want) {
			t.Fatalf("order=%d: range scan size %d, want %d", order, len(got), len(want))
		}
	}
}

func refHas(m map[int64]storage.RID, k int64) bool { _, ok := m[k]; return ok }
func boolToInt(b bool) int {
	if b {
		return 1
	}
	return 0
}

// TestBPlusTreeDuplicateKeys verifies the index supports non-unique keys, as a
// secondary index requires.
func TestBPlusTreeDuplicateKeys(t *testing.T) {
	tree := New(4)
	key := types.NewInt(42)
	rids := []storage.RID{{Page: 1, Slot: 1}, {Page: 1, Slot: 2}, {Page: 2, Slot: 0}}
	for _, r := range rids {
		tree.Insert(key, r)
	}
	if got := tree.Search(key); len(got) != 3 {
		t.Fatalf("expected 3 rids for duplicate key, got %d", len(got))
	}
	tree.Delete(key, rids[1])
	if got := tree.Search(key); len(got) != 2 {
		t.Fatalf("expected 2 rids after delete, got %d", len(got))
	}
}
