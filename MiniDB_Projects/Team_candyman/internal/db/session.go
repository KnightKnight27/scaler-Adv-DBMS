package db

import (
	"fmt"

	"minidb/internal/sql"
	"minidb/internal/txn"
)

// catalogResource is the lock name guarding DDL (table creation) so concurrent
// CREATE TABLE statements are serialized.
const catalogResource = "\x00catalog"

// Session is a single connection's view of the database. It owns the current
// explicit transaction (if any); statements outside an explicit transaction run
// in their own auto-committed transaction.
type Session struct {
	db  *Database
	cur *txnCtx
}

// NewSession creates an independent session (used by the REPL and by concurrency
// demos that drive several sessions from goroutines).
func (d *Database) NewSession() *Session { return &Session{db: d} }

// Execute parses and runs one SQL statement within this session.
func (s *Session) Execute(input string) (Result, error) {
	stmt, err := sql.Parse(input)
	if err != nil {
		return Result{}, err
	}
	switch stmt.(type) {
	case *sql.Begin:
		if s.cur != nil {
			return Result{}, fmt.Errorf("already in a transaction")
		}
		s.cur = s.db.beginCtx()
		return Result{Message: "BEGIN"}, nil
	case *sql.Commit:
		if s.cur == nil {
			return Result{}, fmt.Errorf("no transaction in progress")
		}
		if err := s.db.commitCtx(s.cur); err != nil {
			return Result{}, err
		}
		s.cur = nil
		return Result{Message: "COMMIT"}, nil
	case *sql.Rollback:
		if s.cur == nil {
			return Result{}, fmt.Errorf("no transaction in progress")
		}
		s.db.abortCtx(s.cur)
		s.cur = nil
		return Result{Message: "ROLLBACK"}, nil
	default:
		return s.runLocked(stmt)
	}
}

// runLocked acquires the locks a statement needs under 2PL, runs it, and (for
// auto-committed statements) commits or aborts. A failed statement inside an
// explicit transaction is undone on its own (statement-level atomicity) while the
// transaction stays open.
func (s *Session) runLocked(stmt sql.Statement) (Result, error) {
	ctx := s.cur
	autocommit := ctx == nil
	if autocommit {
		ctx = s.db.beginCtx()
	}

	if err := s.acquire(ctx.tx, stmt); err != nil {
		// On deadlock the transaction is already aborted and its locks released;
		// reverse any changes it made and drop it.
		s.db.rollbackTo(ctx, 0)
		if err == txn.ErrDeadlock && !autocommit {
			s.cur = nil
		}
		return Result{}, fmt.Errorf("transaction %d aborted: %w", ctx.tx.ID, err)
	}

	mark := len(ctx.ops)
	res, err := s.dispatch(stmt, ctx)
	if err != nil {
		if autocommit {
			s.db.abortCtx(ctx)
		} else {
			s.db.rollbackTo(ctx, mark) // undo just this statement
		}
		return Result{}, err
	}
	if autocommit {
		if err := s.db.commitCtx(ctx); err != nil {
			return Result{}, err
		}
	}
	return res, nil
}

// acquire takes the locks a statement requires (table-granularity 2PL).
func (s *Session) acquire(tx *txn.Transaction, stmt sql.Statement) error {
	switch st := stmt.(type) {
	case *sql.Select:
		for _, tr := range st.Tables {
			if err := tx.Lock(tr.Name, txn.Shared); err != nil {
				return err
			}
		}
	case *sql.Insert:
		return tx.Lock(st.Table, txn.Exclusive)
	case *sql.Delete:
		return tx.Lock(st.Table, txn.Exclusive)
	case *sql.CreateTable:
		if err := tx.Lock(catalogResource, txn.Exclusive); err != nil {
			return err
		}
		return tx.Lock(st.Table, txn.Exclusive)
	}
	return nil
}

func (s *Session) dispatch(stmt sql.Statement, ctx *txnCtx) (Result, error) {
	switch st := stmt.(type) {
	case *sql.CreateTable:
		return s.db.execCreate(st)
	case *sql.Insert:
		return s.db.execInsert(st, ctx)
	case *sql.Delete:
		return s.db.execDelete(st, ctx)
	case *sql.Select:
		return s.db.execSelect(st)
	default:
		return Result{}, fmt.Errorf("statement type %T not supported", stmt)
	}
}
