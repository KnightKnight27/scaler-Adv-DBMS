package engine

import (
	"fmt"

	"minidb/internal/catalog"
	"minidb/internal/executor"
	"minidb/internal/planner"
	"minidb/internal/sql"
	"minidb/internal/storage"
	"minidb/internal/txn"
	"minidb/internal/types"
)

// Result is what a session returns for one statement. Query results carry
// Columns and Rows; DDL/DML statements carry a human-readable Message; EXPLAIN
// carries the rendered Plan.
type Result struct {
	IsQuery bool
	Columns []string
	Rows    []types.Row
	Message string
	Plan    string
}

// Session is a single client's connection. It holds the current explicit
// transaction (nil when running in autocommit mode).
type Session struct {
	db *DB
	tx *txn.Transaction
}

// NewSession creates a session bound to the database.
func (db *DB) NewSession() *Session { return &Session{db: db} }

// InTransaction reports whether an explicit transaction is open.
func (s *Session) InTransaction() bool { return s.tx != nil }

// Execute parses and runs one SQL statement.
func (s *Session) Execute(query string) (*Result, error) {
	stmt, err := sql.Parse(query)
	if err != nil {
		return nil, err
	}

	switch st := stmt.(type) {
	case *sql.Begin:
		return s.begin()
	case *sql.Commit:
		return s.commit()
	case *sql.Rollback:
		return s.rollback()
	case *sql.CreateTable:
		return s.createTable(st)
	case *sql.CreateIndex:
		return s.createIndex(st)
	case *sql.Explain:
		return s.explain(st)
	case *sql.Insert:
		return s.runWrite(func(t *txn.Transaction) (*Result, error) { return s.insert(t, st) })
	case *sql.Delete:
		return s.runWrite(func(t *txn.Transaction) (*Result, error) { return s.delete(t, st) })
	case *sql.Select:
		return s.runWrite(func(t *txn.Transaction) (*Result, error) { return s.query(t, st) })
	default:
		return nil, fmt.Errorf("engine: unsupported statement %T", stmt)
	}
}

// --- transaction control ------------------------------------------------------

func (s *Session) begin() (*Result, error) {
	if s.tx != nil {
		return nil, fmt.Errorf("engine: already in a transaction")
	}
	s.tx = s.db.beginTxn()
	return &Result{Message: fmt.Sprintf("BEGIN (txn %d, %s)", s.tx.ID, s.db.mode)}, nil
}

func (s *Session) commit() (*Result, error) {
	if s.tx == nil {
		return nil, fmt.Errorf("engine: no transaction to commit")
	}
	if err := s.db.commitTxn(s.tx); err != nil {
		return nil, err
	}
	id := s.tx.ID
	s.tx = nil
	return &Result{Message: fmt.Sprintf("COMMIT (txn %d)", id)}, nil
}

func (s *Session) rollback() (*Result, error) {
	if s.tx == nil {
		return nil, fmt.Errorf("engine: no transaction to roll back")
	}
	s.db.abortTxn(s.tx)
	id := s.tx.ID
	s.tx = nil
	return &Result{Message: fmt.Sprintf("ROLLBACK (txn %d)", id)}, nil
}

// runWrite executes fn within the current explicit transaction, or wraps it in
// an autocommit transaction. On error the autocommit transaction is aborted; an
// error inside an explicit transaction leaves it open for the client to roll
// back (matching common SQL semantics where the client decides).
func (s *Session) runWrite(fn func(*txn.Transaction) (*Result, error)) (*Result, error) {
	if s.tx != nil {
		return fn(s.tx)
	}
	t := s.db.beginTxn()
	res, err := fn(t)
	if err != nil {
		s.db.abortTxn(t)
		return nil, err
	}
	if err := s.db.commitTxn(t); err != nil {
		return nil, err
	}
	return res, nil
}

// --- DDL ----------------------------------------------------------------------

func (s *Session) createTable(st *sql.CreateTable) (*Result, error) {
	meta := &catalog.Table{Name: st.Table, PKColumn: -1, SecondCol: -1}
	for i, c := range st.Columns {
		meta.Columns = append(meta.Columns, catalog.Column{Name: c.Name, Type: c.Type})
		if c.PrimaryKey {
			meta.PKColumn = i
		}
	}
	if err := s.db.catalog.CreateTable(meta); err != nil {
		return nil, err
	}
	if err := s.db.openTable(meta); err != nil {
		return nil, err
	}
	if err := s.db.tables[meta.Name].rebuildIndexes(); err != nil {
		return nil, err
	}
	return &Result{Message: "CREATE TABLE " + st.Table}, nil
}

func (s *Session) createIndex(st *sql.CreateIndex) (*Result, error) {
	t, ok := s.db.tables[st.Table]
	if !ok {
		return nil, fmt.Errorf("engine: unknown table %q", st.Table)
	}
	col := t.meta.ColumnIndex(st.Column)
	if col < 0 {
		return nil, fmt.Errorf("engine: unknown column %q on %q", st.Column, st.Table)
	}
	t.meta.HasSecond = true
	t.meta.SecondCol = col
	if err := s.db.catalog.Save(); err != nil {
		return nil, err
	}
	if err := t.rebuildIndexes(); err != nil {
		return nil, err
	}
	return &Result{Message: fmt.Sprintf("CREATE INDEX on %s(%s)", st.Table, st.Column)}, nil
}

// --- DML / queries ------------------------------------------------------------

func (s *Session) insert(t *txn.Transaction, st *sql.Insert) (*Result, error) {
	tbl, ok := s.db.tables[st.Table]
	if !ok {
		return nil, fmt.Errorf("engine: unknown table %q", st.Table)
	}
	meta := tbl.meta

	for _, valExprs := range st.Rows {
		row, err := buildInsertRow(meta, st.Columns, valExprs)
		if err != nil {
			return nil, err
		}
		if _, err := tbl.Insert(t, row); err != nil {
			return nil, err
		}
	}
	return &Result{Message: fmt.Sprintf("INSERT %d", len(st.Rows))}, nil
}

// buildInsertRow maps a VALUES tuple to a full row in schema column order,
// validating value types and filling unspecified columns with NULL.
func buildInsertRow(meta *catalog.Table, cols []string, exprs []sql.Expr) (types.Row, error) {
	row := make(types.Row, len(meta.Columns))
	for i, c := range meta.Columns {
		row[i] = types.NewNull(c.Type)
	}

	targets := make([]int, len(exprs))
	if len(cols) == 0 {
		if len(exprs) != len(meta.Columns) {
			return nil, fmt.Errorf("engine: table %s has %d columns but %d values supplied",
				meta.Name, len(meta.Columns), len(exprs))
		}
		for i := range exprs {
			targets[i] = i
		}
	} else {
		if len(cols) != len(exprs) {
			return nil, fmt.Errorf("engine: %d columns but %d values", len(cols), len(exprs))
		}
		for i, name := range cols {
			idx := meta.ColumnIndex(name)
			if idx < 0 {
				return nil, fmt.Errorf("engine: unknown column %q", name)
			}
			targets[i] = idx
		}
	}

	for i, e := range exprs {
		lit, ok := e.(sql.Literal)
		if !ok {
			return nil, fmt.Errorf("engine: INSERT values must be literals")
		}
		col := meta.Columns[targets[i]]
		if lit.Value.Type != col.Type {
			return nil, fmt.Errorf("engine: column %q expects %s but got %s",
				col.Name, col.Type, lit.Value.Type)
		}
		row[targets[i]] = lit.Value
	}
	return row, nil
}

func (s *Session) query(t *txn.Transaction, st *sql.Select) (*Result, error) {
	plan, err := s.db.opt.BuildSelect(st)
	if err != nil {
		return nil, err
	}
	res, err := executor.Execute(plan, s.db, t)
	if err != nil {
		return nil, err
	}
	return &Result{IsQuery: true, Columns: res.Columns, Rows: res.Rows}, nil
}

func (s *Session) explain(st *sql.Explain) (*Result, error) {
	plan, err := s.db.opt.BuildSelect(st.Select)
	if err != nil {
		return nil, err
	}
	return &Result{Message: "Query Plan (" + s.db.mode.String() + "):\n" + planner.Explain(plan)}, nil
}

func (s *Session) delete(t *txn.Transaction, st *sql.Delete) (*Result, error) {
	tbl, ok := s.db.tables[st.Table]
	if !ok {
		return nil, fmt.Errorf("engine: unknown table %q", st.Table)
	}
	refs, err := tbl.Scan(t) // acquires shared locks (2PL) / snapshot (MVCC)
	if err != nil {
		return nil, err
	}
	schema := executor.BuildTableSchema(st.Table, st.Table, tbl)

	var rids []storage.RID
	for _, r := range refs {
		match, err := executor.EvalPredicate(st.Where, schema, r.Row)
		if err != nil {
			return nil, err
		}
		if match {
			rids = append(rids, r.RID)
		}
	}
	for _, rid := range rids {
		if err := tbl.DeleteByRID(t, rid); err != nil {
			return nil, err
		}
	}
	return &Result{Message: fmt.Sprintf("DELETE %d", len(rids))}, nil
}

// --- transaction plumbing -----------------------------------------------------

func (db *DB) beginTxn() *txn.Transaction {
	t := db.txnMgr.Begin()
	_ = db.wal.LogBegin(uint64(t.ID))
	return t
}

func (db *DB) commitTxn(t *txn.Transaction) error {
	if err := db.wal.LogCommit(uint64(t.ID)); err != nil {
		return err
	}
	db.txnMgr.Commit(t)
	return nil
}

func (db *DB) abortTxn(t *txn.Transaction) {
	db.txnMgr.Abort(t)
	_ = db.wal.LogAbort(uint64(t.ID))
}
