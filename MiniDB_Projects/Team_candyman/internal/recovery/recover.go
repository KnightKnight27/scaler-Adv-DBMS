package recovery

import (
	"minidb/internal/storage"
	"minidb/internal/types"
)

// Recover replays a WAL against an engine using the redo/undo scheme:
//
//  1. analysis  — a transaction is "committed" iff it has a COMMIT record;
//  2. redo      — re-apply, in log order, every change of a committed transaction;
//  3. undo      — reverse, in reverse log order, every change of a transaction
//     that did not commit (a loser).
//
// Both phases are idempotent (Put is an upsert; Delete is a no-op when the key is
// absent), so recovery is safe to run repeatedly — e.g. after a crash during
// recovery itself.
func Recover(recs []Record, eng storage.StorageEngine) error {
	committed := map[int]bool{}
	for _, r := range recs {
		if r.Type == RecCommit {
			committed[r.Txn] = true
		}
	}

	// redo committed changes, forward
	for _, r := range recs {
		if !committed[r.Txn] {
			continue
		}
		if err := apply(eng, r, false); err != nil {
			return err
		}
	}

	// undo loser changes, backward
	for i := len(recs) - 1; i >= 0; i-- {
		r := recs[i]
		if committed[r.Txn] {
			continue
		}
		if err := apply(eng, r, true); err != nil {
			return err
		}
	}
	return nil
}

// apply performs (or reverses) a single insert/delete record against the engine.
func apply(eng storage.StorageEngine, r Record, undo bool) error {
	schema, ok := eng.Schema(r.Table)
	if !ok {
		return nil // table no longer exists; nothing to do
	}
	switch r.Type {
	case RecInsert:
		row, err := types.DecodeRow(schema, r.After)
		if err != nil {
			return err
		}
		pk := row.PK(schema)
		if undo {
			_, err = eng.Delete(r.Table, pk) // undo an insert -> delete
			return err
		}
		return eng.Put(r.Table, pk, row) // redo an insert
	case RecDelete:
		row, err := types.DecodeRow(schema, r.Before)
		if err != nil {
			return err
		}
		pk := row.PK(schema)
		if undo {
			return eng.Put(r.Table, pk, row) // undo a delete -> reinsert
		}
		_, err = eng.Delete(r.Table, pk) // redo a delete
		return err
	default:
		return nil // BEGIN/COMMIT/ABORT/CHECKPOINT: no data effect
	}
}
