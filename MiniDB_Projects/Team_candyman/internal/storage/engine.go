package storage

import "minidb/internal/types"

// StorageEngine is the central abstraction of MiniDB: a transactional-agnostic,
// primary-key-addressed table store. Both the default heap engine (heap file +
// B+Tree) and the LSM engine (Track C extension) implement it, so the same SQL,
// optimizer and recovery layer run unchanged on either, and the two can be
// benchmarked head to head.
//
// Put has upsert semantics (insert-or-replace by primary key); this keeps WAL
// redo idempotent. The SQL layer enforces INSERT's "fail on duplicate key" rule
// above the engine using Get.
type StorageEngine interface {
	// CreateTable registers a table and its schema. Idempotent on reopen.
	CreateTable(name string, schema *types.Schema) error

	// Schema returns a registered table's schema.
	Schema(table string) (*types.Schema, bool)

	// Tables lists registered table names.
	Tables() []string

	// Put inserts or replaces the row for pk.
	Put(table string, pk types.Value, row types.Row) error

	// Get fetches a row by primary key (the engine's index/point-lookup path).
	Get(table string, pk types.Value) (types.Row, bool, error)

	// Delete removes a row by primary key, reporting whether it existed.
	Delete(table string, pk types.Value) (bool, error)

	// Scan returns a cursor over all live rows of a table (heap/seq order).
	Scan(table string) (Cursor, error)

	// RangeScan returns a cursor over rows whose primary key is in [low, high].
	// Engines back this with their ordered structure (B+Tree / sorted runs).
	RangeScan(table string, low, high types.Value) (Cursor, error)

	// Sync forces durable state (used at checkpoint/shutdown).
	Sync() error

	// Close releases resources.
	Close() error
}

// Cursor is a pull-based (volcano-style) iterator over rows.
type Cursor interface {
	Next() (types.Row, bool, error)
	Close() error
}

// sliceCursor serves rows from an in-memory slice (used by the LSM engine and as
// a simple building block).
type sliceCursor struct {
	rows []types.Row
	i    int
}

// NewSliceCursor returns a Cursor over a fixed slice of rows.
func NewSliceCursor(rows []types.Row) Cursor { return &sliceCursor{rows: rows} }

func (c *sliceCursor) Next() (types.Row, bool, error) {
	if c.i >= len(c.rows) {
		return nil, false, nil
	}
	r := c.rows[c.i]
	c.i++
	return r, true, nil
}

func (c *sliceCursor) Close() error { return nil }
