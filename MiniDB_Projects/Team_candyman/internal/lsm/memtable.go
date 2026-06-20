package lsm

import "sort"

// KV is a key/value pair (possibly a tombstone) flowing between the memtable,
// SSTables and compaction.
type KV struct {
	Key  string
	Val  []byte
	Tomb bool
}

// MemTable is the in-memory write buffer of the LSM-tree. Writes are O(1) map
// inserts (the LSM's write-throughput advantage); the table is kept sorted only
// when it is flushed or range-scanned.
type MemTable struct {
	data  map[string]KV
	bytes int
}

// NewMemTable creates an empty memtable.
func NewMemTable() *MemTable { return &MemTable{data: map[string]KV{}} }

// Put inserts or overwrites a live value.
func (m *MemTable) Put(key string, val []byte) {
	m.bytes += len(key) + len(val)
	m.data[key] = KV{Key: key, Val: val}
}

// Delete records a tombstone for key.
func (m *MemTable) Delete(key string) {
	m.bytes += len(key)
	m.data[key] = KV{Key: key, Tomb: true}
}

// Get returns the entry for key, if the memtable has one.
func (m *MemTable) Get(key string) (KV, bool) {
	kv, ok := m.data[key]
	return kv, ok
}

// Len returns the number of entries (including tombstones).
func (m *MemTable) Len() int { return len(m.data) }

// Bytes returns the approximate in-memory size, used to trigger flushes.
func (m *MemTable) Bytes() int { return m.bytes }

// Sorted returns all entries ordered by key (for flushing/scanning).
func (m *MemTable) Sorted() []KV {
	out := make([]KV, 0, len(m.data))
	for _, kv := range m.data {
		out = append(out, kv)
	}
	sort.Slice(out, func(i, j int) bool { return out[i].Key < out[j].Key })
	return out
}
