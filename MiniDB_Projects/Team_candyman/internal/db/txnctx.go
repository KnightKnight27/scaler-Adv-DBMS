package db

import (
	"minidb/internal/recovery"
	"minidb/internal/txn"
	"minidb/internal/types"
)

// txnCtx is a transaction's runtime context: its lock-holding transaction plus
// the ordered list of data changes it has made, used for runtime ROLLBACK
// (reverse the in-memory list) and mirrored to the WAL for crash recovery.
type txnCtx struct {
	tx  *txn.Transaction
	ops []mutation
}

// mutation records one applied change. before==nil means an insert; after==nil
// means a delete.
type mutation struct {
	table  string
	before types.Row
	after  types.Row
}

// beginCtx starts a transaction and logs a BEGIN record.
func (d *Database) beginCtx() *txnCtx {
	tx := d.txnMgr.Begin()
	d.wal.Append(recovery.Record{Type: recovery.RecBegin, Txn: tx.ID})
	return &txnCtx{tx: tx}
}

// applyMutation applies a change to the engine, appends a WAL record, and records
// it for rollback. Exactly one of (insert) after / (delete) before is non-nil.
func (d *Database) applyMutation(ctx *txnCtx, table string, before, after types.Row) error {
	schema, _ := d.eng.Schema(table)
	var rec recovery.Record
	if before == nil { // insert
		pk := after.PK(schema)
		if err := d.eng.Put(table, pk, after); err != nil {
			return err
		}
		rec = recovery.Record{Type: recovery.RecInsert, Txn: ctx.tx.ID, Table: table, After: after.Encode()}
	} else { // delete
		pk := before.PK(schema)
		if _, err := d.eng.Delete(table, pk); err != nil {
			return err
		}
		rec = recovery.Record{Type: recovery.RecDelete, Txn: ctx.tx.ID, Table: table, Before: before.Encode()}
	}
	if _, err := d.wal.Append(rec); err != nil {
		return err
	}
	ctx.ops = append(ctx.ops, mutation{table: table, before: before, after: after})
	return nil
}

// rollbackTo reverses the context's mutations down to index mark (used to undo a
// failed statement, or the whole transaction when mark == 0).
func (d *Database) rollbackTo(ctx *txnCtx, mark int) {
	for i := len(ctx.ops) - 1; i >= mark; i-- {
		m := ctx.ops[i]
		schema, _ := d.eng.Schema(m.table)
		if m.before == nil { // undo insert
			d.eng.Delete(m.table, m.after.PK(schema))
		} else { // undo delete
			d.eng.Put(m.table, m.before.PK(schema), m.before)
		}
	}
	ctx.ops = ctx.ops[:mark]
}

// commitCtx logs and fsyncs a COMMIT, then releases the transaction's locks.
func (d *Database) commitCtx(ctx *txnCtx) error {
	if _, err := d.wal.Append(recovery.Record{Type: recovery.RecCommit, Txn: ctx.tx.ID}); err != nil {
		return err
	}
	if err := d.wal.Flush(); err != nil {
		return err
	}
	ctx.tx.Commit()
	return nil
}

// abortCtx reverses all the context's mutations, logs an ABORT, and releases locks.
func (d *Database) abortCtx(ctx *txnCtx) {
	d.rollbackTo(ctx, 0)
	d.wal.Append(recovery.Record{Type: recovery.RecAbort, Txn: ctx.tx.ID})
	ctx.tx.Abort()
}
