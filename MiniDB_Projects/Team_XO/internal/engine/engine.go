// Package engine is the integration layer that assembles MiniDB from its parts:
// the catalog, per-table heap files and buffer pools, B+Tree indexes, the
// transaction manager, the write-ahead log, the optimizer and the executor. It
// presents a small session API used by the REPL and the benchmarks, and
// implements crash recovery on startup.
package engine

import (
	"fmt"
	"os"
	"path/filepath"

	"minidb/internal/catalog"
	"minidb/internal/executor"
	"minidb/internal/planner"
	"minidb/internal/recovery"
	"minidb/internal/storage"
	"minidb/internal/txn"
)

// indexOrder is the branching factor of every B+Tree index.
const indexOrder = 64

// Options configures a database instance.
type Options struct {
	Dir          string   // directory holding heap files, catalog and WAL
	Mode         txn.Mode // 2PL or MVCC
	BufferFrames int      // pages cached per table buffer pool
}

// DB is a running MiniDB instance.
type DB struct {
	dir          string
	mode         txn.Mode
	bufferFrames int

	catalog *catalog.Catalog
	txnMgr  *txn.Manager
	wal     *recovery.WAL
	opt     *planner.Optimizer

	tables       map[string]*Table
	lastRecovery *recovery.RecoveryResult
}

// Open starts (or reopens, running crash recovery) a database in opts.Dir.
func Open(opts Options) (*DB, error) {
	if opts.BufferFrames <= 0 {
		opts.BufferFrames = 64
	}
	if err := os.MkdirAll(opts.Dir, 0o755); err != nil {
		return nil, err
	}

	cat, err := catalog.NewCatalog(filepath.Join(opts.Dir, "catalog.json"))
	if err != nil {
		return nil, err
	}
	wal, err := recovery.OpenWAL(filepath.Join(opts.Dir, "wal.log"))
	if err != nil {
		return nil, err
	}

	db := &DB{
		dir:          opts.Dir,
		mode:         opts.Mode,
		bufferFrames: opts.BufferFrames,
		catalog:      cat,
		txnMgr:       txn.NewManager(opts.Mode),
		wal:          wal,
		tables:       make(map[string]*Table),
	}
	db.opt = planner.NewOptimizer(db)

	// Open heap files for every known table before recovery can apply to them.
	for _, name := range cat.List() {
		meta, _ := cat.Get(name)
		if err := db.openTable(meta); err != nil {
			return nil, err
		}
	}

	// Crash recovery: redo committed work and undo losers, then rebuild the
	// in-memory indexes from the recovered heaps and fast-forward the txn ids.
	res, err := recovery.Recover(filepath.Join(opts.Dir, "wal.log"), db)
	if err != nil {
		return nil, err
	}
	db.txnMgr.Advance(txn.TxnID(res.MaxTxnID))
	for _, t := range db.tables {
		if err := t.rebuildIndexes(); err != nil {
			return nil, err
		}
	}
	db.lastRecovery = res
	return db, nil
}

func (db *DB) openTable(meta *catalog.Table) error {
	path := filepath.Join(db.dir, meta.Name+".tbl")
	disk, err := storage.NewDiskManager(path)
	if err != nil {
		return err
	}
	bp := storage.NewBufferPool(disk, db.bufferFrames)
	t := &Table{
		db:   db,
		meta: meta,
		disk: disk,
		bp:   bp,
		heap: storage.NewHeapFile(bp),
	}
	db.tables[meta.Name] = t
	return nil
}

// Mode returns the active concurrency strategy.
func (db *DB) Mode() txn.Mode { return db.mode }

// TableNames lists the tables known to the catalog.
func (db *DB) TableNames() []string { return db.catalog.List() }

// TxnManager exposes the transaction manager (used by the demos to start
// concurrent transactions).
func (db *DB) TxnManager() *txn.Manager { return db.txnMgr }

// LastRecovery returns the summary produced by the most recent crash recovery.
func (db *DB) LastRecovery() *recovery.RecoveryResult { return db.lastRecovery }

// --- planner.Provider and executor.Tables -------------------------------------

// TableInfo resolves a table for the optimizer.
func (db *DB) TableInfo(name string) (planner.TableInfo, bool) {
	t, ok := db.tables[name]
	return t, ok
}

// Get resolves a table for the executor.
func (db *DB) Get(name string) (executor.Table, bool) {
	t, ok := db.tables[name]
	return t, ok
}

// --- recovery.Applier ---------------------------------------------------------

// PutTuple writes a tuple image at an exact RID during recovery redo/undo.
func (db *DB) PutTuple(table string, rid storage.RID, tuple []byte) error {
	t, ok := db.tables[table]
	if !ok {
		return fmt.Errorf("engine: recovery references unknown table %q", table)
	}
	return t.heap.PutAt(rid, tuple)
}

// RemoveTuple tombstones the tuple at rid during recovery redo/undo.
func (db *DB) RemoveTuple(table string, rid storage.RID) error {
	t, ok := db.tables[table]
	if !ok {
		return fmt.Errorf("engine: recovery references unknown table %q", table)
	}
	return t.heap.Delete(rid)
}

// --- lifecycle ----------------------------------------------------------------

// Close cleanly shuts down: it flushes every dirty data page and the WAL. After
// a clean close, recovery has no work to do on the next Open.
func (db *DB) Close() error {
	for _, t := range db.tables {
		if err := t.bp.FlushAll(); err != nil {
			return err
		}
		if err := t.disk.Close(); err != nil {
			return err
		}
	}
	return db.wal.Close()
}

// Crash simulates a power failure: the WAL is closed (its committed records are
// already durable) but dirty data pages in the buffer pools are discarded
// without flushing. Reopening the database then exercises crash recovery.
func (db *DB) Crash() error {
	for _, t := range db.tables {
		_ = t.disk.Close() // drop buffer pool contents without FlushAll
	}
	return db.wal.Close()
}
