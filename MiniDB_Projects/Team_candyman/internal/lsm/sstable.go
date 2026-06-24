package lsm

import (
	"bufio"
	"encoding/binary"
	"io"
	"os"
	"sort"
)

// SSTable is an immutable, sorted run of key/value entries on disk. Each entry is
// framed as keyLen(4) | key | flag(1) | valLen(4) | val, where flag=1 marks a
// tombstone. On open the table is scanned once to build an in-memory sorted key
// index (key -> file offset) and a Bloom filter, so point lookups do a Bloom
// check, a binary search, then a single seek+read.
type SSTable struct {
	path  string
	f     *os.File
	index []idxEntry
	bloom *Bloom
}

type idxEntry struct {
	key string
	off int64
}

// WriteSSTable writes sorted entries to path. The caller passes entries already
// ordered by key with duplicates resolved (newest wins).
func WriteSSTable(path string, entries []KV) error {
	f, err := os.Create(path)
	if err != nil {
		return err
	}
	w := bufio.NewWriter(f)
	var hdr [4]byte
	for _, e := range entries {
		binary.BigEndian.PutUint32(hdr[:], uint32(len(e.Key)))
		w.Write(hdr[:])
		w.WriteString(e.Key)
		if e.Tomb {
			w.WriteByte(1)
		} else {
			w.WriteByte(0)
		}
		binary.BigEndian.PutUint32(hdr[:], uint32(len(e.Val)))
		w.Write(hdr[:])
		w.Write(e.Val)
	}
	if err := w.Flush(); err != nil {
		f.Close()
		return err
	}
	if err := f.Sync(); err != nil {
		f.Close()
		return err
	}
	return f.Close()
}

// OpenSSTable opens an SSTable and builds its index and Bloom filter.
func OpenSSTable(path string) (*SSTable, error) {
	f, err := os.Open(path)
	if err != nil {
		return nil, err
	}
	s := &SSTable{path: path, f: f}
	r := bufio.NewReader(f)
	var off int64
	var hdr [4]byte
	var keys [][]byte
	for {
		if _, err := io.ReadFull(r, hdr[:]); err == io.EOF {
			break
		} else if err != nil {
			f.Close()
			return nil, err
		}
		kl := int(binary.BigEndian.Uint32(hdr[:]))
		key := make([]byte, kl)
		if _, err := io.ReadFull(r, key); err != nil {
			f.Close()
			return nil, err
		}
		flag := make([]byte, 1)
		io.ReadFull(r, flag)
		if _, err := io.ReadFull(r, hdr[:]); err != nil {
			f.Close()
			return nil, err
		}
		vl := int(binary.BigEndian.Uint32(hdr[:]))
		if _, err := r.Discard(vl); err != nil {
			f.Close()
			return nil, err
		}
		s.index = append(s.index, idxEntry{key: string(key), off: off})
		keys = append(keys, key)
		off += int64(4 + kl + 1 + 4 + vl)
	}
	s.bloom = NewBloom(len(keys))
	for _, k := range keys {
		s.bloom.Add(k)
	}
	return s, nil
}

// readAt reads the entry stored at file offset.
func (s *SSTable) readAt(off int64) (KV, error) {
	var hdr [4]byte
	if _, err := s.f.ReadAt(hdr[:], off); err != nil {
		return KV{}, err
	}
	kl := int(binary.BigEndian.Uint32(hdr[:]))
	buf := make([]byte, kl+1+4)
	if _, err := s.f.ReadAt(buf, off+4); err != nil {
		return KV{}, err
	}
	key := string(buf[:kl])
	tomb := buf[kl] == 1
	vl := int(binary.BigEndian.Uint32(buf[kl+1:]))
	val := make([]byte, vl)
	if _, err := s.f.ReadAt(val, off+4+int64(kl)+1+4); err != nil {
		return KV{}, err
	}
	return KV{Key: key, Val: val, Tomb: tomb}, nil
}

// Get returns the entry for key (value or tombstone), or ok=false if absent.
func (s *SSTable) Get(key string) (KV, bool, error) {
	if !s.bloom.Test([]byte(key)) {
		return KV{}, false, nil
	}
	i := sort.Search(len(s.index), func(i int) bool { return s.index[i].key >= key })
	if i >= len(s.index) || s.index[i].key != key {
		return KV{}, false, nil
	}
	kv, err := s.readAt(s.index[i].off)
	if err != nil {
		return KV{}, false, err
	}
	return kv, true, nil
}

// All returns every entry in key order (used by compaction and scans).
func (s *SSTable) All() ([]KV, error) {
	out := make([]KV, 0, len(s.index))
	for _, ie := range s.index {
		kv, err := s.readAt(ie.off)
		if err != nil {
			return nil, err
		}
		out = append(out, kv)
	}
	return out, nil
}

// Close closes the underlying file.
func (s *SSTable) Close() error { return s.f.Close() }

// Remove closes and deletes the SSTable file (after compaction).
func (s *SSTable) Remove() error {
	s.f.Close()
	return os.Remove(s.path)
}
