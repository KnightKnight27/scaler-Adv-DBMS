package executor

import (
	"minidb/internal/sql"
	"minidb/internal/storage"
	"minidb/internal/types"
)

// Operator is a pull-based physical operator. Parents call Open once, then Next
// repeatedly until ok=false, then Close.
type Operator interface {
	Open() error
	Next() (types.Row, bool, error)
	Close() error
	Columns() Schema
}

// SeqScan reads every live row of a table via the engine's sequential cursor.
type SeqScan struct {
	Engine storage.StorageEngine
	Table  string
	Alias  string

	schema Schema
	cur    storage.Cursor
}

func (s *SeqScan) Open() error {
	s.ensureSchema()
	cur, err := s.Engine.Scan(s.Table)
	if err != nil {
		return err
	}
	s.cur = cur
	return nil
}

func (s *SeqScan) ensureSchema() {
	if s.schema == nil {
		sch, _ := s.Engine.Schema(s.Table)
		s.schema = schemaFor(s.Alias, sch)
	}
}

func (s *SeqScan) Next() (types.Row, bool, error) { return s.cur.Next() }
func (s *SeqScan) Close() error {
	if s.cur != nil {
		return s.cur.Close()
	}
	return nil
}

// Columns is valid before Open so parent operators (e.g. joins) can build their
// output schema without first opening a cursor.
func (s *SeqScan) Columns() Schema { s.ensureSchema(); return s.schema }

// schemaFor builds an output schema from a table schema qualified by alias.
func schemaFor(alias string, ts *types.Schema) Schema {
	out := make(Schema, len(ts.Columns))
	for i, c := range ts.Columns {
		out[i] = ColumnInfo{Table: alias, Name: c.Name, Type: c.Type}
	}
	return out
}

// Filter passes through only rows satisfying a predicate.
type Filter struct {
	Child Operator
	Pred  sql.Expr
}

func (f *Filter) Open() error     { return f.Child.Open() }
func (f *Filter) Columns() Schema { return f.Child.Columns() }
func (f *Filter) Close() error    { return f.Child.Close() }

func (f *Filter) Next() (types.Row, bool, error) {
	for {
		row, ok, err := f.Child.Next()
		if err != nil || !ok {
			return nil, false, err
		}
		keep, err := EvalBool(f.Pred, row, f.Child.Columns())
		if err != nil {
			return nil, false, err
		}
		if keep {
			return row, true, nil
		}
	}
}

// Project evaluates a list of output expressions per row. If Star is set it
// passes child rows through unchanged.
type Project struct {
	Child Operator
	Items []sql.SelectItem
	Star  bool

	schema Schema
}

func (p *Project) Open() error {
	if err := p.Child.Open(); err != nil {
		return err
	}
	if p.Star {
		p.schema = p.Child.Columns()
		return nil
	}
	cs := p.Child.Columns()
	p.schema = make(Schema, len(p.Items))
	for i, it := range p.Items {
		name := it.Alias
		typ := types.TypeText
		switch e := it.Expr.(type) {
		case *sql.ColumnRef:
			if name == "" {
				name = e.Name
			}
			if idx, err := cs.resolve(e); err == nil {
				typ = cs[idx].Type
			}
		case *sql.Literal:
			if name == "" {
				name = "?column?"
			}
			typ = e.Value.Type
		default:
			if name == "" {
				name = "?column?"
			}
		}
		p.schema[i] = ColumnInfo{Name: name, Type: typ}
	}
	return nil
}

func (p *Project) Columns() Schema { return p.schema }
func (p *Project) Close() error    { return p.Child.Close() }

func (p *Project) Next() (types.Row, bool, error) {
	row, ok, err := p.Child.Next()
	if err != nil || !ok {
		return nil, false, err
	}
	if p.Star {
		return row, true, nil
	}
	out := make(types.Row, len(p.Items))
	for i, it := range p.Items {
		v, err := Eval(it.Expr, row, p.Child.Columns())
		if err != nil {
			return nil, false, err
		}
		out[i] = v
	}
	return out, true, nil
}
