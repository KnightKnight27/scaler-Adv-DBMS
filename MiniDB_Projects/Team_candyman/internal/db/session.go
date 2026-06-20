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
	cur *txn.Transaction
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
		s.cur = s.db.txnMgr.Begin()
		return Result{Message: "BEGIN"}, nil
	case *sql.Commit:
		if s.cur == nil {
			return Result{}, fmt.Errorf("no transaction in progress")
		}
		s.cur.Commit()
		s.cur = nil
		return Result{Message: "COMMIT"}, nil
	case *sql.Rollback:
		if s.cur == nil {
			return Result{}, fmt.Errorf("no transaction in progress")
		}
		s.cur.Abort()
		s.cur = nil
		return Result{Message: "ROLLBACK"}, nil
	default:
		return s.runLocked(stmt)
	}
}

// runLocked acquires the locks a statement needs under 2PL, runs it, and (for
// auto-committed statements) commits or aborts.
func (s *Session) runLocked(stmt sql.Statement) (Result, error) {
	tx := s.cur
	autocommit := tx == nil
	if autocommit {
		tx = s.db.txnMgr.Begin()
	}

	if err := s.acquire(tx, stmt); err != nil {
		// On deadlock the transaction is already aborted and its locks released.
		if err == txn.ErrDeadlock && !autocommit {
			s.cur = nil
		}
		return Result{}, fmt.Errorf("transaction %d aborted: %w", tx.ID, err)
	}

	res, err := s.dispatch(stmt)
	if err != nil {
		if autocommit {
			tx.Abort()
		}
		return Result{}, err
	}
	if autocommit {
		tx.Commit()
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

func (s *Session) dispatch(stmt sql.Statement) (Result, error) {
	switch st := stmt.(type) {
	case *sql.CreateTable:
		return s.db.execCreate(st)
	case *sql.Insert:
		return s.db.execInsert(st)
	case *sql.Delete:
		return s.db.execDelete(st)
	case *sql.Select:
		return s.db.execSelect(st)
	default:
		return Result{}, fmt.Errorf("statement type %T not supported", stmt)
	}
}
