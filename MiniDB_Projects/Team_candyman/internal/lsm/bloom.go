// Package lsm implements MiniDB's Track C extension: a log-structured merge-tree
// storage engine (MemTable + SSTables + compaction) behind the same
// storage.StorageEngine interface as the default heap engine, so the two can be
// benchmarked head to head.
package lsm

import "hash/fnv"

// Bloom is a small Bloom filter used to skip SSTables that cannot contain a key.
// It is rebuilt by scanning an SSTable's keys when the table is opened, so it is
// never persisted.
type Bloom struct {
	bits []uint64
	m    uint // number of bits
	k    int  // number of hash functions
}

// NewBloom sizes a filter for about n keys at ~1% false-positive rate.
func NewBloom(n int) *Bloom {
	if n < 1 {
		n = 1
	}
	m := uint(n * 10) // ~10 bits/key
	if m < 64 {
		m = 64
	}
	return &Bloom{bits: make([]uint64, (m+63)/64), m: m, k: 4}
}

func (b *Bloom) hashes(key []byte) (uint32, uint32) {
	h := fnv.New64a()
	h.Write(key)
	sum := h.Sum64()
	return uint32(sum), uint32(sum >> 32)
}

// Add records a key.
func (b *Bloom) Add(key []byte) {
	h1, h2 := b.hashes(key)
	for i := 0; i < b.k; i++ {
		pos := (uint(h1) + uint(i)*uint(h2)) % b.m
		b.bits[pos/64] |= 1 << (pos % 64)
	}
}

// Test reports whether a key may be present (false = definitely absent).
func (b *Bloom) Test(key []byte) bool {
	h1, h2 := b.hashes(key)
	for i := 0; i < b.k; i++ {
		pos := (uint(h1) + uint(i)*uint(h2)) % b.m
		if b.bits[pos/64]&(1<<(pos%64)) == 0 {
			return false
		}
	}
	return true
}
