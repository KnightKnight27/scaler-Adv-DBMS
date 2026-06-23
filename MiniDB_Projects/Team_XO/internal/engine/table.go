package engine

import (
	"encoding/binary"
	"sync"

	"minidb/internal/catalog"
	"minidb/internal/executor"
	"minidb/internal/index"
	"minidb/internal/recovery"
	"minidb/internal/storage"
	"minidb/internal/txn"
	"minidb/internal/types"
)

// mvccHeaderSize is the per-tuple version header used only in MVCC mode: the
// creating transaction id (xmin) and the deleting transaction id (xmax, 0 while
// the version is live).
const mvccHeaderSize = 16

func encodeMVCC(xmin, xmax uint64, body []byte) []byte {
	out := make([]byte, mvccHeaderSize+len(body))
	binary.LittleEndian.PutUint64(out[0:], xmin)
	binary.LittleEndian.PutUint64(out[8:], xmax)
	copy(out[mvccHeaderSize:], body)
	return out
}

func mvccXmin(tuple []byte) uint64 { return binary.LittleEndian.Uint64(tuple[0:]) }
func mvccXmax(tuple []byte) uint64 { return binary.LittleEndian.Uint64(tuple[8:]) }
func mvccBody(tuple []byte) []byte { return tuple[mvccHeaderSize:] }

// Table is the runtime representation of a relation: its schema, heap file,
// buffer pool and in-memory indexes, plus the glue that applies the active
// concurrency-control strategy. It satisfies planner.TableInfo (for the
// optimizer) and executor.Table (for execution).
//
// latch is a short-duration physical latch that protects the heap and index
// structures during a single operation. It is deliberately distinct from the
// logical locks of the lock manager (held for a whole transaction): logical
// locks are never acquired while the latch is held, so latches stay brief and
// never participate in transaction-level deadlocks.
type Table struct {
	db   *DB
	meta *catalog.Table

	disk *storage.DiskManager
	bp   *storage.BufferPool
	heap *storage.HeapFile

	latch    sync.RWMutex
	pkIndex  *index.BPlusTree
	secIndex *index.BPlusTree

	// Cached table statistics for the optimizer. Recomputed lazily after writes
	// rather than on every query, so planning a point lookup does not cost a full
	// table scan.
	statsMu       sync.Mutex
	statsDirty    bool
	rowCountCache int
	distinctCache map[int]int
}

// Schema returns the table's catalog definition.
func (t *Table) Schema() *catalog.Table { return t.meta }

// HasIndex reports whether column col is indexed.
func (t *Table) HasIndex(col int) bool {
	if t.pkIndex != nil && col == t.meta.PKColumn {
		return true
	}
	return t.secIndex != nil && t.meta.HasSecond && col == t.meta.SecondCol
}

func (t *Table) indexFor(col int) *index.BPlusTree {
	if col == t.meta.PKColumn {
		return t.pkIndex
	}
	if t.meta.HasSecond && col == t.meta.SecondCol {
		return t.secIndex
	}
	return nil
}

type rawRef struct {
	rid   storage.RID
	tuple []byte
}

// collectAll reads every physical tuple in the heap into memory. Callers must
// hold the latch (read or write); it copies tuple bytes so the results stay
// valid after the latch is released.
func (t *Table) collectAll() ([]rawRef, error) {
	var out []rawRef
	err := t.heap.Scan(func(rid storage.RID, tuple []byte) bool {
		out = append(out, rawRef{rid: rid, tuple: tuple})
		return true
	})
	return out, err
}

// Scan returns the rows visible to transaction tr. Under 2PL it acquires a
// shared lock on each row (held until end of transaction); under MVCC it applies
// snapshot visibility with no locking. The physical read happens under the read
// latch, which is released before any logical lock is acquired.
func (t *Table) Scan(tr *txn.Transaction) ([]executor.RowRef, error) {
	t.latch.RLock()
	raws, err := t.collectAll()
	t.latch.RUnlock()
	if err != nil {
		return nil, err
	}
	return t.materialiseAll(tr, raws)
}

// IndexLookup returns the visible rows whose column col equals key, using the
// B+Tree to fetch only the matching RIDs.
func (t *Table) IndexLookup(tr *txn.Transaction, col int, key types.Value) ([]executor.RowRef, error) {
	tree := t.indexFor(col)
	if tree == nil {
		return t.Scan(tr)
	}
	t.latch.RLock()
	rids := tree.Search(key)
	raws := make([]rawRef, 0, len(rids))
	for _, rid := range rids {
		if tuple, err := t.heap.Get(rid); err == nil {
			raws = append(raws, rawRef{rid: rid, tuple: tuple})
		}
	}
	t.latch.RUnlock()
	return t.materialiseAll(tr, raws)
}

// materialiseAll applies the concurrency strategy to a batch of physical tuples.
func (t *Table) materialiseAll(tr *txn.Transaction, raws []rawRef) ([]executor.RowRef, error) {
	out := make([]executor.RowRef, 0, len(raws))
	for _, r := range raws {
		if t.db.mode == txn.ModeMVCC {
			if !t.db.txnMgr.Visible(tr, txn.TxnID(mvccXmin(r.tuple)), txn.TxnID(mvccXmax(r.tuple))) {
				continue
			}
			out = append(out, executor.RowRef{RID: r.rid, Row: t.meta.DecodeRow(mvccBody(r.tuple))})
			continue
		}
		// 2PL: take a shared lock (held until commit) before exposing the row.
		if err := t.db.txnMgr.Locks().Acquire(tr.ID, r.rid, txn.Shared); err != nil {
			return nil, err
		}
		out = append(out, executor.RowRef{RID: r.rid, Row: t.meta.DecodeRow(r.tuple)})
	}
	return out, nil
}

// Insert adds row on behalf of tr and maintains indexes, the WAL and undo state.
func (t *Table) Insert(tr *txn.Transaction, row types.Row) (storage.RID, error) {
	body := t.meta.EncodeRow(row)
	var stored []byte
	if t.db.mode == txn.ModeMVCC {
		stored = encodeMVCC(uint64(tr.ID), 0, body)
	} else {
		stored = body
	}

	t.latch.Lock()
	rid, err := t.heap.Insert(stored)
	if err != nil {
		t.latch.Unlock()
		return storage.RID{}, err
	}
	if t.db.mode == txn.Mode2PL {
		// A brand-new RID has no other holders, so this never blocks.
		_ = t.db.txnMgr.Locks().Acquire(tr.ID, rid, txn.Exclusive)
	}
	if err := t.db.wal.LogUpdate(uint64(tr.ID), recovery.OpInsert, t.meta.Name, rid, nil, stored); err != nil {
		t.latch.Unlock()
		return storage.RID{}, err
	}
	t.indexInsert(row, rid)
	t.latch.Unlock()
	t.markStatsDirty()

	tr.AddUndo(func() {
		t.latch.Lock()
		t.indexDelete(row, rid)
		_ = t.heap.Delete(rid)
		t.latch.Unlock()
		t.markStatsDirty()
	})
	return rid, nil
}

// DeleteByRID removes the tuple at rid on behalf of tr. Under 2PL the logical
// exclusive lock is taken first (it may block, but no latch is held while we
// wait); under MVCC the current version's xmax is stamped with tr's id so the
// deletion becomes visible only after tr commits.
func (t *Table) DeleteByRID(tr *txn.Transaction, rid storage.RID) error {
	if t.db.mode == txn.Mode2PL {
		if err := t.db.txnMgr.Locks().Acquire(tr.ID, rid, txn.Exclusive); err != nil {
			return err
		}
	}

	t.latch.Lock()
	defer t.latch.Unlock()

	tuple, err := t.heap.Get(rid)
	if err != nil {
		return nil // already gone
	}
	if t.db.mode == txn.ModeMVCC {
		return t.mvccDelete(tr, rid, tuple)
	}

	row := t.meta.DecodeRow(tuple)
	if err := t.db.wal.LogUpdate(uint64(tr.ID), recovery.OpDelete, t.meta.Name, rid, tuple, nil); err != nil {
		return err
	}
	t.indexDelete(row, rid)
	if err := t.heap.Delete(rid); err != nil {
		return err
	}
	t.markStatsDirty()
	before := tuple
	tr.AddUndo(func() {
		t.latch.Lock()
		_ = t.heap.PutAt(rid, before)
		t.indexInsert(row, rid)
		t.latch.Unlock()
		t.markStatsDirty()
	})
	return nil
}

// mvccDelete stamps xmax on a live version, with first-committer-wins conflict
// detection. Caller holds the write latch.
func (t *Table) mvccDelete(tr *txn.Transaction, rid storage.RID, tuple []byte) error {
	xmin, xmax := mvccXmin(tuple), mvccXmax(tuple)
	if !t.db.txnMgr.Visible(tr, txn.TxnID(xmin), txn.TxnID(xmax)) {
		return nil // not visible to us; nothing to delete
	}
	if xmax != 0 && uint64(tr.ID) != xmax && t.db.txnMgr.Status(txn.TxnID(xmax)) != txn.Aborted {
		return txn.ErrWriteConflict
	}
	before := make([]byte, len(tuple))
	copy(before, tuple)
	after := make([]byte, len(tuple))
	copy(after, tuple)
	binary.LittleEndian.PutUint64(after[8:], uint64(tr.ID)) // set xmax

	if err := t.db.wal.LogUpdate(uint64(tr.ID), recovery.OpUpdate, t.meta.Name, rid, before, after); err != nil {
		return err
	}
	if _, err := t.heap.Update(rid, after); err != nil {
		return err
	}
	tr.AddUndo(func() {
		t.latch.Lock()
		_, _ = t.heap.Update(rid, before)
		t.latch.Unlock()
	})
	return nil
}

// indexInsert/indexDelete maintain the primary and optional secondary indexes.
// Caller holds the write latch.
func (t *Table) indexInsert(row types.Row, rid storage.RID) {
	if t.pkIndex != nil && t.meta.PKColumn >= 0 {
		t.pkIndex.Insert(row[t.meta.PKColumn], rid)
	}
	if t.secIndex != nil && t.meta.HasSecond {
		t.secIndex.Insert(row[t.meta.SecondCol], rid)
	}
}

func (t *Table) indexDelete(row types.Row, rid storage.RID) {
	if t.pkIndex != nil && t.meta.PKColumn >= 0 {
		t.pkIndex.Delete(row[t.meta.PKColumn], rid)
	}
	if t.secIndex != nil && t.meta.HasSecond {
		t.secIndex.Delete(row[t.meta.SecondCol], rid)
	}
}

// markStatsDirty invalidates the cached statistics after a write.
func (t *Table) markStatsDirty() {
	t.statsMu.Lock()
	t.statsDirty = true
	t.statsMu.Unlock()
}

// ensureStats recomputes the row count and per-column distinct counts with a
// single heap scan, but only if the cache has been invalidated since the last
// computation. The scan runs under the read latch with statsMu released, so the
// two locks are never held together.
func (t *Table) ensureStats() {
	t.statsMu.Lock()
	dirty := t.statsDirty || t.distinctCache == nil
	t.statsMu.Unlock()
	if !dirty {
		return
	}

	t.latch.RLock()
	raws, _ := t.collectAll()
	t.latch.RUnlock()

	dist := make(map[int]int, len(t.meta.Columns))
	seen := make([]map[string]struct{}, len(t.meta.Columns))
	for i := range seen {
		seen[i] = make(map[string]struct{})
	}
	for _, r := range raws {
		body := r.tuple
		if t.db.mode == txn.ModeMVCC {
			body = mvccBody(body)
		}
		row := t.meta.DecodeRow(body)
		for col := range t.meta.Columns {
			seen[col][row[col].String()] = struct{}{}
		}
	}
	for col := range t.meta.Columns {
		d := len(seen[col])
		if d < 1 {
			d = 1
		}
		dist[col] = d
	}

	t.statsMu.Lock()
	t.rowCountCache = len(raws)
	t.distinctCache = dist
	t.statsDirty = false
	t.statsMu.Unlock()
}

// RowCount returns an approximate live row count for the optimizer. Under MVCC
// it counts physical versions, which is an acceptable over-estimate for costing.
func (t *Table) RowCount() int {
	t.ensureStats()
	t.statsMu.Lock()
	defer t.statsMu.Unlock()
	return t.rowCountCache
}

// DistinctValues estimates the number of distinct values in column col, used for
// selectivity estimation.
func (t *Table) DistinctValues(col int) int {
	t.ensureStats()
	t.statsMu.Lock()
	defer t.statsMu.Unlock()
	if d, ok := t.distinctCache[col]; ok {
		return d
	}
	return 1
}

// rebuildIndexes reconstructs the in-memory indexes from the current heap
// contents. It runs at startup after crash recovery, since indexes are not
// persisted and must be derived from the recovered heap.
func (t *Table) rebuildIndexes() error {
	t.latch.Lock()
	defer t.latch.Unlock()
	t.pkIndex = index.New(indexOrder)
	if t.meta.HasSecond {
		t.secIndex = index.New(indexOrder)
	}
	raws, err := t.collectAll()
	if err != nil {
		return err
	}
	for _, r := range raws {
		body := r.tuple
		if t.db.mode == txn.ModeMVCC {
			body = mvccBody(body)
		}
		row := t.meta.DecodeRow(body)
		t.indexInsert(row, r.rid)
	}
	t.statsDirty = true
	return nil
}
