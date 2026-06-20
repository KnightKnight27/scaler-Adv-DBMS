// Package db is MiniDB's top-level façade: it opens the storage stack (disk
// manager, buffer pool, catalog, engine) and executes parsed SQL statements,
// returning tabular results. The query-planning and transaction layers plug in
// here as they are built.
package db

import (
	"fmt"
	"path/filepath"

	"minidb/internal/catalog"
	"minidb/internal/engine"
	"minidb/internal/executor"
	"minidb/internal/sql"
	"minidb/internal/storage"
	"minidb/internal/types"
)

// Result is the outcome of executing a statement.
type Result struct {
	Columns []string   // column headers (SELECT)
	Rows    [][]string // formatted result rows (SELECT)
	Message string     // human-readable status (DDL/DML)
}

// Database is an open MiniDB instance.
type Database struct {
	dir    string
	engine string
	dm     *storage.DiskManager
	bp     *storage.BufferPool
	cat    *catalog.Catalog
	eng    storage.StorageEngine
}

// Open opens (or creates) a database in dir using the named engine ("heap" or
// "lsm").
func Open(dir, engineKind string) (*Database, error) {
	dataPath := filepath.Join(dir, "minidb.db")
	indexPath := filepath.Join(dir, "minidb.idx")
	catPath := filepath.Join(dir, "catalog.json")

	dm, err := storage.OpenDiskManager(dataPath, indexPath)
	if err != nil {
		return nil, err
	}
	bp := storage.NewBufferPool(dm, 256)
	cat, err := catalog.Open(catPath)
	if err != nil {
		dm.Close()
		return nil, err
	}

	d := &Database{dir: dir, engine: engineKind, dm: dm, bp: bp, cat: cat}
	if err := d.openEngine(); err != nil {
		dm.Close()
		return nil, err
	}
	return d, nil
}

func (d *Database) openEngine() error {
	switch d.engine {
	case "heap":
		eng, err := engine.OpenHeap(d.dm, d.bp, d.cat)
		if err != nil {
			return err
		}
		d.eng = eng
		return nil
	case "lsm":
		return fmt.Errorf("lsm engine is wired in a later milestone")
	default:
		return fmt.Errorf("unknown engine %q", d.engine)
	}
}

// Close flushes and closes the database.
func (d *Database) Close() error { return d.eng.Close() }

// Tables lists table names (for the \dt meta-command).
func (d *Database) Tables() []string { return d.eng.Tables() }

// Execute parses and runs one SQL statement.
func (d *Database) Execute(input string) (Result, error) {
	stmt, err := sql.Parse(input)
	if err != nil {
		return Result{}, err
	}
	switch s := stmt.(type) {
	case *sql.CreateTable:
		return d.execCreate(s)
	case *sql.Insert:
		return d.execInsert(s)
	case *sql.Delete:
		return d.execDelete(s)
	case *sql.Select:
		return d.execSelect(s)
	default:
		return Result{}, fmt.Errorf("statement type %T not yet supported", stmt)
	}
}

func (d *Database) execCreate(s *sql.CreateTable) (Result, error) {
	schema := &types.Schema{PKIndex: -1}
	for i, c := range s.Columns {
		schema.Columns = append(schema.Columns, types.Column{Name: c.Name, Type: c.Type})
		if c.PrimaryKey {
			schema.PKIndex = i
		}
	}
	if schema.PKIndex == -1 {
		return Result{}, fmt.Errorf("table %s has no primary key", s.Table)
	}
	if err := d.eng.CreateTable(s.Table, schema); err != nil {
		return Result{}, err
	}
	return Result{Message: fmt.Sprintf("table %s created", s.Table)}, nil
}

func (d *Database) execInsert(s *sql.Insert) (Result, error) {
	schema, ok := d.eng.Schema(s.Table)
	if !ok {
		return Result{}, fmt.Errorf("unknown table %q", s.Table)
	}
	// resolve target column order
	colPos := make([]int, len(schema.Columns))
	if len(s.Columns) == 0 {
		for i := range colPos {
			colPos[i] = i
		}
	} else {
		for i := range colPos {
			colPos[i] = -1
		}
		for vi, name := range s.Columns {
			ci := schema.ColIndex(name)
			if ci == -1 {
				return Result{}, fmt.Errorf("unknown column %q", name)
			}
			colPos[vi] = ci
		}
	}
	count := 0
	for _, exprRow := range s.Rows {
		row, err := d.buildRow(schema, s.Columns, colPos, exprRow)
		if err != nil {
			return Result{}, err
		}
		pk := row.PK(schema)
		if _, exists, err := d.eng.Get(s.Table, pk); err != nil {
			return Result{}, err
		} else if exists {
			return Result{}, fmt.Errorf("duplicate primary key %s", pk)
		}
		if err := d.eng.Put(s.Table, pk, row); err != nil {
			return Result{}, err
		}
		count++
	}
	return Result{Message: fmt.Sprintf("%d row(s) inserted", count)}, nil
}

// buildRow constructs a typed row from VALUES expressions.
func (d *Database) buildRow(schema *types.Schema, names []string, colPos []int, exprs []sql.Expr) (types.Row, error) {
	row := make(types.Row, len(schema.Columns))
	for i := range row {
		row[i] = types.Value{Type: schema.Columns[i].Type, Null: true}
	}
	limit := len(exprs)
	if len(names) == 0 && limit != len(schema.Columns) {
		return nil, fmt.Errorf("expected %d values, got %d", len(schema.Columns), limit)
	}
	for vi := 0; vi < limit; vi++ {
		lit, ok := exprs[vi].(*sql.Literal)
		if !ok {
			return nil, fmt.Errorf("only literal values are supported in INSERT")
		}
		ci := colPos[vi]
		col := schema.Columns[ci]
		v := lit.Value
		if v.Null {
			row[ci] = types.Value{Type: col.Type, Null: true}
			continue
		}
		if v.Type != col.Type {
			return nil, fmt.Errorf("column %q expects %s, got %s", col.Name, col.Type, v.Type)
		}
		row[ci] = v
	}
	return row, nil
}

func (d *Database) execDelete(s *sql.Delete) (Result, error) {
	schema, ok := d.eng.Schema(s.Table)
	if !ok {
		return Result{}, fmt.Errorf("unknown table %q", s.Table)
	}
	scan := &executor.SeqScan{Engine: d.eng, Table: s.Table, Alias: s.Table}
	if err := scan.Open(); err != nil {
		return Result{}, err
	}
	var pks []types.Value
	for {
		row, ok, err := scan.Next()
		if err != nil {
			scan.Close()
			return Result{}, err
		}
		if !ok {
			break
		}
		keep, err := executor.EvalBool(s.Where, row, scan.Columns())
		if err != nil {
			scan.Close()
			return Result{}, err
		}
		if keep {
			pks = append(pks, row.PK(schema))
		}
	}
	scan.Close()
	count := 0
	for _, pk := range pks {
		if ok, err := d.eng.Delete(s.Table, pk); err != nil {
			return Result{}, err
		} else if ok {
			count++
		}
	}
	return Result{Message: fmt.Sprintf("%d row(s) deleted", count)}, nil
}
