package recovery

import "minidb/internal/storage"

// Applier is the bridge between recovery and the storage engine. Recovery calls
// these to physically reinstate or remove tuple images during redo and undo.
// The engine implements them against the appropriate heap file and rebuilds
// in-memory indexes afterwards.
type Applier interface {
	// PutTuple writes tuple at an exact RID (creating pages as needed).
	PutTuple(table string, rid storage.RID, tuple []byte) error
	// RemoveTuple tombstones the tuple at rid.
	RemoveTuple(table string, rid storage.RID) error
}

// RecoveryResult summarises what recovery did, for display in the demo.
type RecoveryResult struct {
	Committed []uint64
	Aborted   []uint64
	Redone    int
	Undone    int
	MaxTxnID  uint64
}

// Recover replays the WAL at path against the applier following the standard
// three phases:
//
//  1. Analysis  - classify transactions as committed or losers (no commit
//     record, or an explicit abort).
//  2. Redo      - replay every update's after-image forward, repeating history
//     so the database reaches its pre-crash state.
//  3. Undo      - for loser transactions, restore before-images in reverse log
//     order, rolling back work that must not survive.
//
// Committed transactions therefore persist and uncommitted ones vanish, which is
// exactly the durability/atomicity guarantee the WAL exists to provide.
func Recover(path string, applier Applier) (*RecoveryResult, error) {
	recs, err := ReadRecords(path)
	if err != nil {
		return nil, err
	}

	committed := make(map[uint64]bool)
	seen := make(map[uint64]bool)
	res := &RecoveryResult{}
	for _, r := range recs {
		if r.Txn > res.MaxTxnID {
			res.MaxTxnID = r.Txn
		}
		switch r.Type {
		case RecBegin:
			seen[r.Txn] = true
		case RecCommit:
			committed[r.Txn] = true
		case RecAbort:
			committed[r.Txn] = false
		}
	}

	// Redo phase: repeat history for all logged updates.
	for _, r := range recs {
		if r.Type != RecUpdate {
			continue
		}
		if err := redo(r, applier); err != nil {
			return nil, err
		}
		res.Redone++
	}

	// Undo phase: walk backwards, reversing updates of loser transactions.
	for i := len(recs) - 1; i >= 0; i-- {
		r := recs[i]
		if r.Type != RecUpdate || committed[r.Txn] {
			continue
		}
		if err := undo(r, applier); err != nil {
			return nil, err
		}
		res.Undone++
	}

	for txn := range seen {
		if committed[txn] {
			res.Committed = append(res.Committed, txn)
		} else {
			res.Aborted = append(res.Aborted, txn)
		}
	}
	return res, nil
}

// redo brings a record's post-state into existence.
func redo(r *Record, applier Applier) error {
	switch r.Op {
	case OpInsert, OpUpdate:
		return applier.PutTuple(r.Table, r.RID, r.After)
	case OpDelete:
		return applier.RemoveTuple(r.Table, r.RID)
	}
	return nil
}

// undo restores a record's pre-state for a loser transaction.
func undo(r *Record, applier Applier) error {
	switch r.Op {
	case OpInsert:
		return applier.RemoveTuple(r.Table, r.RID)
	case OpDelete, OpUpdate:
		return applier.PutTuple(r.Table, r.RID, r.Before)
	}
	return nil
}
