// Package engine provides the default MiniDB storage engine: a heap file for
// rows plus a B+Tree primary-key index, wired through the buffer pool and a
// persisted catalog. It satisfies storage.StorageEngine, so it is interchangeable
// with the LSM engine.
//
// Trade-off: B+Tree indexes are treated as derived data. They live in the index
// file during a run but are rebuilt from the heap whenever the database opens.
// This removes index durability/recovery from the critical path entirely — the
// WAL only protects the heap, and indexes simply follow.
package engine

import (
	"fmt"
	"sync"

	"minidb/internal/catalog"
	"minidb/internal/index"
	"minidb/internal/storage"
	"minidb/internal/types"
)

type heapTable struct {
	meta  *catalog.TableMeta
	heap  *storage.HeapFile
	index *index.BTree // primary-key index: pk -> RID
}

// HeapEngine is the heap-file + B+Tree storage engine.
//
// The mu RWMutex guards the tables map only. Per-table data races are prevented
// a level up by table-granularity 2PL (one writer or many readers per table);
// mu just makes the map itself safe when DDL adds a table concurrently.
type HeapEngine struct {
	mu     sync.RWMutex
	bp     *storage.BufferPool
	dm     *storage.DiskManager
	cat    *catalog.Catalog
	tables map[string]*heapTable
}

// OpenHeap opens (or creates) a heap engine. dm/bp/cat are shared with the WAL
// layer so checkpoints can flush the same buffer pool. It rebuilds all indexes
// from the heap before returning.
func OpenHeap(dm *storage.DiskManager, bp *storage.BufferPool, cat *catalog.Catalog) (*HeapEngine, error) {
	e := &HeapEngine{bp: bp, dm: dm, cat: cat, tables: map[string]*heapTable{}}
	if err := dm.TruncateIndex(); err != nil {
		return nil, fmt.Errorf("reset index file: %w", err)
	}
	for _, name := range cat.Names() {
		meta, _ := cat.Get(name)
		if err := e.openTable(meta); err != nil {
			return nil, err
		}
	}
	return e, nil
}

// openTable attaches a heap + freshly built index for an existing table.
func (e *HeapEngine) openTable(meta *catalog.TableMeta) error {
	h := storage.OpenHeapFile(e.bp, meta.HeapRoot)
	bt, err := index.NewBTree(e.bp)
	if err != nil {
		return err
	}
	// bulk-load the index from the heap
	cur := h.Cursor()
	for {
		rid, rec, ok, err := cur.Next()
		if err != nil {
			return err
		}
		if !ok {
			break
		}
		row, err := types.DecodeRow(meta.Schema, rec)
		if err != nil {
			return err
		}
		if err := bt.Insert(row.PK(meta.Schema), rid); err != nil {
			return err
		}
	}
	e.tables[meta.Name] = &heapTable{meta: meta, heap: h, index: bt}
	return nil
}

// CreateTable registers a new table with an empty heap and index.
func (e *HeapEngine) CreateTable(name string, schema *types.Schema) error {
	if _, ok := e.tables[name]; ok {
		return fmt.Errorf("table %q already exists", name)
	}
	h, root, err := storage.NewHeapFile(e.bp)
	if err != nil {
		return err
	}
	bt, err := index.NewBTree(e.bp)
	if err != nil {
		return err
	}
	meta := &catalog.TableMeta{Name: name, Schema: schema, HeapRoot: root}
	if err := e.cat.Add(meta); err != nil {
		return err
	}
	e.mu.Lock()
	e.tables[name] = &heapTable{meta: meta, heap: h, index: bt}
	e.mu.Unlock()
	return nil
}

func (e *HeapEngine) table(name string) (*heapTable, error) {
	e.mu.RLock()
	t, ok := e.tables[name]
	e.mu.RUnlock()
	if !ok {
		return nil, fmt.Errorf("unknown table %q", name)
	}
	return t, nil
}

// Schema returns a table's schema.
func (e *HeapEngine) Schema(name string) (*types.Schema, bool) {
	e.mu.RLock()
	t, ok := e.tables[name]
	e.mu.RUnlock()
	if !ok {
		return nil, false
	}
	return t.meta.Schema, true
}

// Tables lists table names.
func (e *HeapEngine) Tables() []string { return e.cat.Names() }

// Put upserts a row by primary key.
func (e *HeapEngine) Put(name string, pk types.Value, row types.Row) error {
	t, err := e.table(name)
	if err != nil {
		return err
	}
	// if the key already exists, tombstone the old record first (upsert)
	if oldRID, ok, err := t.index.Search(pk); err != nil {
		return err
	} else if ok {
		if err := t.heap.Delete(oldRID, 0); err != nil {
			return err
		}
	}
	rid, err := t.heap.Insert(row.Encode())
	if err != nil {
		return err
	}
	return t.index.Insert(pk, rid) // upsert in the index too
}

// Get fetches a row by primary key via the index.
func (e *HeapEngine) Get(name string, pk types.Value) (types.Row, bool, error) {
	t, err := e.table(name)
	if err != nil {
		return nil, false, err
	}
	rid, ok, err := t.index.Search(pk)
	if err != nil || !ok {
		return nil, false, err
	}
	rec, err := t.heap.Read(rid)
	if err != nil {
		return nil, false, err
	}
	row, err := types.DecodeRow(t.meta.Schema, rec)
	return row, true, err
}

// Delete removes a row by primary key.
func (e *HeapEngine) Delete(name string, pk types.Value) (bool, error) {
	t, err := e.table(name)
	if err != nil {
		return false, err
	}
	rid, ok, err := t.index.Search(pk)
	if err != nil || !ok {
		return false, err
	}
	if err := t.heap.Delete(rid, 0); err != nil {
		return false, err
	}
	if _, err := t.index.Delete(pk); err != nil {
		return false, err
	}
	return true, nil
}

// Scan returns a sequential cursor over all live rows.
func (e *HeapEngine) Scan(name string) (storage.Cursor, error) {
	t, err := e.table(name)
	if err != nil {
		return nil, err
	}
	return &heapEngineCursor{cur: t.heap.Cursor(), schema: t.meta.Schema}, nil
}

// RangeScan returns rows whose primary key is in [low, high], using the B+Tree.
func (e *HeapEngine) RangeScan(name string, low, high types.Value) (storage.Cursor, error) {
	t, err := e.table(name)
	if err != nil {
		return nil, err
	}
	var rows []types.Row
	err = t.index.Range(&low, &high, func(rid storage.RID) bool {
		rec, rerr := t.heap.Read(rid)
		if rerr != nil {
			return true
		}
		row, rerr := types.DecodeRow(t.meta.Schema, rec)
		if rerr == nil {
			rows = append(rows, row)
		}
		return true
	})
	if err != nil {
		return nil, err
	}
	return storage.NewSliceCursor(rows), nil
}

// Sync flushes the buffer pool and files (used at checkpoint/shutdown).
func (e *HeapEngine) Sync() error {
	if err := e.bp.FlushAll(); err != nil {
		return err
	}
	return e.dm.Sync()
}

// Close flushes and releases resources.
func (e *HeapEngine) Close() error {
	if err := e.Sync(); err != nil {
		return err
	}
	return e.dm.Close()
}

// heapEngineCursor decodes heap records into rows on demand.
type heapEngineCursor struct {
	cur    *storage.HeapCursor
	schema *types.Schema
}

func (c *heapEngineCursor) Next() (types.Row, bool, error) {
	_, rec, ok, err := c.cur.Next()
	if err != nil || !ok {
		return nil, false, err
	}
	row, err := types.DecodeRow(c.schema, rec)
	return row, true, err
}

func (c *heapEngineCursor) Close() error { return nil }
