package lsm

import (
	"fmt"
	"os"
	"path/filepath"
	"sort"
	"strconv"
	"strings"
	"sync"

	"minidb/internal/catalog"
	"minidb/internal/storage"
	"minidb/internal/types"
)

// flush thresholds: flush the memtable to an SSTable once it grows past this many
// bytes, and compact a table's SSTables once it accumulates this many of them.
const (
	flushBytes          = 64 * 1024
	compactionThreshold = 4
)

type lsmTable struct {
	name   string
	schema *types.Schema
	mem    *MemTable
	ssts   []*SSTable // newest first
	seq    int        // next SSTable sequence number
}

// Engine is the LSM-tree storage engine. It satisfies storage.StorageEngine.
type Engine struct {
	mu     sync.Mutex
	dir    string
	cat    *catalog.Catalog
	tables map[string]*lsmTable
}

// Open opens (or creates) an LSM engine in dir, loading existing SSTables for
// every table in the catalog.
func Open(dir string, cat *catalog.Catalog) (*Engine, error) {
	e := &Engine{dir: dir, cat: cat, tables: map[string]*lsmTable{}}
	for _, name := range cat.Names() {
		meta, _ := cat.Get(name)
		t := &lsmTable{name: name, schema: meta.Schema, mem: NewMemTable()}
		if err := e.loadSSTables(t); err != nil {
			return nil, err
		}
		e.tables[name] = t
	}
	return e, nil
}

// loadSSTables discovers a table's on-disk SSTables, newest (highest seq) first.
func (e *Engine) loadSSTables(t *lsmTable) error {
	prefix := "lsm_" + t.name + "_"
	ents, err := os.ReadDir(e.dir)
	if err != nil {
		return err
	}
	type fileSeq struct {
		path string
		seq  int
	}
	var files []fileSeq
	maxSeq := -1
	for _, de := range ents {
		n := de.Name()
		if !strings.HasPrefix(n, prefix) || !strings.HasSuffix(n, ".sst") {
			continue
		}
		seqStr := strings.TrimSuffix(strings.TrimPrefix(n, prefix), ".sst")
		seq, err := strconv.Atoi(seqStr)
		if err != nil {
			continue
		}
		files = append(files, fileSeq{filepath.Join(e.dir, n), seq})
		if seq > maxSeq {
			maxSeq = seq
		}
	}
	sort.Slice(files, func(i, j int) bool { return files[i].seq > files[j].seq }) // newest first
	for _, f := range files {
		s, err := OpenSSTable(f.path)
		if err != nil {
			return err
		}
		t.ssts = append(t.ssts, s)
	}
	t.seq = maxSeq + 1
	return nil
}

// CreateTable registers a table and persists its schema in the catalog.
func (e *Engine) CreateTable(name string, schema *types.Schema) error {
	e.mu.Lock()
	defer e.mu.Unlock()
	if _, ok := e.tables[name]; ok {
		return fmt.Errorf("table %q already exists", name)
	}
	if err := e.cat.Add(&catalog.TableMeta{Name: name, Schema: schema}); err != nil {
		return err
	}
	e.tables[name] = &lsmTable{name: name, schema: schema, mem: NewMemTable()}
	return nil
}

func (e *Engine) table(name string) (*lsmTable, error) {
	t, ok := e.tables[name]
	if !ok {
		return nil, fmt.Errorf("unknown table %q", name)
	}
	return t, nil
}

// Schema returns a table's schema.
func (e *Engine) Schema(name string) (*types.Schema, bool) {
	e.mu.Lock()
	defer e.mu.Unlock()
	t, ok := e.tables[name]
	if !ok {
		return nil, false
	}
	return t.schema, true
}

// Tables lists table names.
func (e *Engine) Tables() []string { return e.cat.Names() }

// Put inserts or replaces a row by primary key.
func (e *Engine) Put(name string, pk types.Value, row types.Row) error {
	e.mu.Lock()
	defer e.mu.Unlock()
	t, err := e.table(name)
	if err != nil {
		return err
	}
	t.mem.Put(string(types.EncodeKey(pk)), row.Encode())
	return e.maybeFlush(t)
}

// Get fetches a row by primary key, consulting the memtable then SSTables.
func (e *Engine) Get(name string, pk types.Value) (types.Row, bool, error) {
	e.mu.Lock()
	defer e.mu.Unlock()
	t, err := e.table(name)
	if err != nil {
		return nil, false, err
	}
	key := string(types.EncodeKey(pk))
	if kv, ok := t.mem.Get(key); ok {
		if kv.Tomb {
			return nil, false, nil
		}
		return decodeRow(t.schema, kv.Val)
	}
	for _, s := range t.ssts { // newest first
		kv, ok, err := s.Get(key)
		if err != nil {
			return nil, false, err
		}
		if ok {
			if kv.Tomb {
				return nil, false, nil
			}
			return decodeRow(t.schema, kv.Val)
		}
	}
	return nil, false, nil
}

// Delete writes a tombstone for a primary key, reporting whether it existed.
func (e *Engine) Delete(name string, pk types.Value) (bool, error) {
	existed, _, err := e.Get(name, pk)
	if err != nil {
		return false, err
	}
	e.mu.Lock()
	defer e.mu.Unlock()
	t, err := e.table(name)
	if err != nil {
		return false, err
	}
	t.mem.Delete(string(types.EncodeKey(pk)))
	return existed != nil, e.maybeFlush(t)
}

// Scan returns a cursor over all live rows (merged, tombstones removed).
func (e *Engine) Scan(name string) (storage.Cursor, error) {
	rows, err := e.materialize(name, nil, nil)
	if err != nil {
		return nil, err
	}
	return storage.NewSliceCursor(rows), nil
}

// RangeScan returns rows whose primary key is in [low, high].
func (e *Engine) RangeScan(name string, low, high types.Value) (storage.Cursor, error) {
	lo := string(types.EncodeKey(low))
	hi := string(types.EncodeKey(high))
	rows, err := e.materialize(name, &lo, &hi)
	if err != nil {
		return nil, err
	}
	return storage.NewSliceCursor(rows), nil
}

// materialize merges the memtable and all SSTables into the current set of live
// rows, optionally restricted to a key range.
func (e *Engine) materialize(name string, lo, hi *string) ([]types.Row, error) {
	e.mu.Lock()
	defer e.mu.Unlock()
	t, err := e.table(name)
	if err != nil {
		return nil, err
	}
	runs := [][]KV{t.mem.Sorted()} // newest first
	for _, s := range t.ssts {
		all, err := s.All()
		if err != nil {
			return nil, err
		}
		runs = append(runs, all)
	}
	merged := mergeRuns(runs, true)
	var rows []types.Row
	for _, kv := range merged {
		if lo != nil && kv.Key < *lo {
			continue
		}
		if hi != nil && kv.Key > *hi {
			continue
		}
		row, _, err := decodeRow(t.schema, kv.Val)
		if err != nil {
			return nil, err
		}
		rows = append(rows, row)
	}
	return rows, nil
}

// maybeFlush flushes the memtable to a new SSTable once it is large enough, then
// compacts if too many SSTables have accumulated. Caller holds e.mu.
func (e *Engine) maybeFlush(t *lsmTable) error {
	if t.mem.Bytes() < flushBytes || t.mem.Len() == 0 {
		return nil
	}
	return e.flush(t)
}

func (e *Engine) flush(t *lsmTable) error {
	if t.mem.Len() == 0 {
		return nil
	}
	path := filepath.Join(e.dir, fmt.Sprintf("lsm_%s_%d.sst", t.name, t.seq))
	if err := WriteSSTable(path, t.mem.Sorted()); err != nil {
		return err
	}
	s, err := OpenSSTable(path)
	if err != nil {
		return err
	}
	t.ssts = append([]*SSTable{s}, t.ssts...) // newest first
	t.seq++
	t.mem = NewMemTable()
	if len(t.ssts) >= compactionThreshold {
		return e.compact(t)
	}
	return nil
}

// compact merges all of a table's SSTables into a single new one and removes the
// old files (size-tiered compaction down to the base level).
func (e *Engine) compact(t *lsmTable) error {
	runs := make([][]KV, 0, len(t.ssts))
	for _, s := range t.ssts { // newest first
		all, err := s.All()
		if err != nil {
			return err
		}
		runs = append(runs, all)
	}
	merged := mergeRuns(runs, true) // bottom level: drop tombstones
	path := filepath.Join(e.dir, fmt.Sprintf("lsm_%s_%d.sst", t.name, t.seq))
	if err := WriteSSTable(path, merged); err != nil {
		return err
	}
	newS, err := OpenSSTable(path)
	if err != nil {
		return err
	}
	for _, s := range t.ssts {
		s.Remove()
	}
	t.ssts = []*SSTable{newS}
	t.seq++
	return nil
}

// Sync flushes every memtable so all committed data is durable on disk.
func (e *Engine) Sync() error {
	e.mu.Lock()
	defer e.mu.Unlock()
	for _, t := range e.tables {
		if err := e.flush(t); err != nil {
			return err
		}
	}
	return nil
}

// Close flushes and closes all SSTable files.
func (e *Engine) Close() error {
	if err := e.Sync(); err != nil {
		return err
	}
	e.mu.Lock()
	defer e.mu.Unlock()
	for _, t := range e.tables {
		for _, s := range t.ssts {
			s.Close()
		}
	}
	return nil
}

func decodeRow(schema *types.Schema, val []byte) (types.Row, bool, error) {
	row, err := types.DecodeRow(schema, val)
	if err != nil {
		return nil, false, err
	}
	return row, true, nil
}
