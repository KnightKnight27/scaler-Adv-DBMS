package executor

import (
	"fmt"
	"strings"

	"minidb/internal/sql"
	"minidb/internal/storage"
	"minidb/internal/types"
)

// IndexScan performs a primary-key point lookup via the engine's index (Get).
// The optimizer chooses it over SeqScan when a query has an equality predicate
// on the primary key.
type IndexScan struct {
	Engine storage.StorageEngine
	Table  string
	Alias  string
	Key    types.Value

	schema Schema
	done   bool
}

func (s *IndexScan) Open() error {
	s.ensureSchema()
	s.done = false
	return nil
}

func (s *IndexScan) ensureSchema() {
	if s.schema == nil {
		sch, _ := s.Engine.Schema(s.Table)
		s.schema = schemaFor(s.Alias, sch)
	}
}

func (s *IndexScan) Next() (types.Row, bool, error) {
	if s.done {
		return nil, false, nil
	}
	s.done = true
	row, ok, err := s.Engine.Get(s.Table, s.Key)
	if err != nil || !ok {
		return nil, false, err
	}
	return row, true, nil
}

func (s *IndexScan) Close() error    { return nil }
func (s *IndexScan) Columns() Schema { s.ensureSchema(); return s.schema }

// NestedLoopJoin joins two operators. For each outer row it re-scans the inner
// operator and emits combined rows that satisfy the join predicate.
type NestedLoopJoin struct {
	Outer Operator
	Inner Operator
	Pred  sql.Expr

	schema      Schema
	curOuter    types.Row
	innerActive bool
}

func (j *NestedLoopJoin) Open() error {
	if err := j.Outer.Open(); err != nil {
		return err
	}
	j.schema = append(append(Schema{}, j.Outer.Columns()...), j.Inner.Columns()...)
	return nil
}

func (j *NestedLoopJoin) Columns() Schema { return j.schema }

func (j *NestedLoopJoin) Next() (types.Row, bool, error) {
	for {
		if !j.innerActive {
			row, ok, err := j.Outer.Next()
			if err != nil || !ok {
				return nil, false, err
			}
			j.curOuter = row
			if err := j.Inner.Open(); err != nil {
				return nil, false, err
			}
			j.innerActive = true
		}
		irow, ok, err := j.Inner.Next()
		if err != nil {
			return nil, false, err
		}
		if !ok {
			j.Inner.Close()
			j.innerActive = false
			continue
		}
		combined := make(types.Row, 0, len(j.curOuter)+len(irow))
		combined = append(combined, j.curOuter...)
		combined = append(combined, irow...)
		keep, err := EvalBool(j.Pred, combined, j.schema)
		if err != nil {
			return nil, false, err
		}
		if keep {
			return combined, true, nil
		}
	}
}

func (j *NestedLoopJoin) Close() error {
	if j.innerActive {
		j.Inner.Close()
		j.innerActive = false
	}
	return j.Outer.Close()
}

// HashAgg implements grouped and whole-table aggregation. Items is the SELECT
// list: a mix of group-by column references and aggregate calls. With no GroupBy
// the whole input is a single group.
type HashAgg struct {
	Child   Operator
	GroupBy []sql.Expr
	Items   []sql.SelectItem

	schema Schema
	out    []types.Row
	pos    int
}

type aggState struct {
	groupVals types.Row
	count     int64
	sum       int64
	min       types.Value
	max       types.Value
	seen      bool
}

func (a *HashAgg) Open() error {
	if err := a.Child.Open(); err != nil {
		return err
	}
	cs := a.Child.Columns()
	a.buildSchema(cs)

	groups := map[string]*aggState{}
	var order []string
	for {
		row, ok, err := a.Child.Next()
		if err != nil {
			return err
		}
		if !ok {
			break
		}
		key, gv, err := a.groupKey(row, cs)
		if err != nil {
			return err
		}
		st, exists := groups[key]
		if !exists {
			st = &aggState{groupVals: gv}
			groups[key] = st
			order = append(order, key)
		}
		if err := a.accumulate(st, row, cs); err != nil {
			return err
		}
	}
	// whole-table aggregate with no rows still yields one row (e.g. COUNT(*) = 0)
	if len(a.GroupBy) == 0 && len(order) == 0 {
		st := &aggState{}
		groups[""] = st
		order = append(order, "")
	}
	for _, k := range order {
		out, err := a.emit(groups[k])
		if err != nil {
			return err
		}
		a.out = append(a.out, out)
	}
	return nil
}

func (a *HashAgg) buildSchema(cs Schema) {
	a.schema = make(Schema, len(a.Items))
	for i, it := range a.Items {
		name := it.Alias
		typ := types.TypeInt
		switch e := it.Expr.(type) {
		case *sql.ColumnRef:
			if name == "" {
				name = e.Name
			}
			if idx, err := cs.resolve(e); err == nil {
				typ = cs[idx].Type
			}
		case *sql.AggCall:
			if name == "" {
				name = aggName(e)
			}
			if e.Func == "MIN" || e.Func == "MAX" {
				if cr, ok := e.Arg.(*sql.ColumnRef); ok {
					if idx, err := cs.resolve(cr); err == nil {
						typ = cs[idx].Type
					}
				}
			}
		}
		a.schema[i] = ColumnInfo{Name: name, Type: typ}
	}
}

func (a *HashAgg) groupKey(row types.Row, cs Schema) (string, types.Row, error) {
	var sb strings.Builder
	gv := make(types.Row, len(a.GroupBy))
	for i, g := range a.GroupBy {
		v, err := Eval(g, row, cs)
		if err != nil {
			return "", nil, err
		}
		gv[i] = v
		sb.WriteString(v.String())
		sb.WriteByte('\x1f')
	}
	return sb.String(), gv, nil
}

func (a *HashAgg) accumulate(st *aggState, row types.Row, cs Schema) error {
	st.count++
	for _, it := range a.Items {
		ag, ok := it.Expr.(*sql.AggCall)
		if !ok || ag.Star {
			continue
		}
		v, err := Eval(ag.Arg, row, cs)
		if err != nil {
			return err
		}
		if v.Null {
			continue
		}
		if v.Type == types.TypeInt {
			st.sum += v.Int
		}
		if !st.seen {
			st.min, st.max, st.seen = v, v, true
		} else {
			if v.Compare(st.min) < 0 {
				st.min = v
			}
			if v.Compare(st.max) > 0 {
				st.max = v
			}
		}
	}
	return nil
}

func (a *HashAgg) emit(st *aggState) (types.Row, error) {
	out := make(types.Row, len(a.Items))
	gi := 0
	for i, it := range a.Items {
		switch e := it.Expr.(type) {
		case *sql.AggCall:
			out[i] = aggResult(e, st)
		case *sql.ColumnRef:
			if gi < len(st.groupVals) {
				out[i] = st.groupVals[gi]
				gi++
			} else {
				out[i] = types.Value{Null: true}
			}
		default:
			return nil, fmt.Errorf("unsupported aggregate-query expression %T", it.Expr)
		}
	}
	return out, nil
}

func aggResult(e *sql.AggCall, st *aggState) types.Value {
	switch e.Func {
	case "COUNT":
		return types.NewInt(st.count)
	case "SUM":
		return types.NewInt(st.sum)
	case "AVG":
		if st.count == 0 {
			return types.Value{Null: true}
		}
		return types.NewInt(st.sum / st.count)
	case "MIN":
		if !st.seen {
			return types.Value{Null: true}
		}
		return st.min
	case "MAX":
		if !st.seen {
			return types.Value{Null: true}
		}
		return st.max
	default:
		return types.Value{Null: true}
	}
}

func aggName(e *sql.AggCall) string {
	if e.Star {
		return e.Func + "(*)"
	}
	if cr, ok := e.Arg.(*sql.ColumnRef); ok {
		return e.Func + "(" + cr.Name + ")"
	}
	return e.Func
}

func (a *HashAgg) Columns() Schema { return a.schema }
func (a *HashAgg) Close() error    { return a.Child.Close() }

func (a *HashAgg) Next() (types.Row, bool, error) {
	if a.pos >= len(a.out) {
		return nil, false, nil
	}
	r := a.out[a.pos]
	a.pos++
	return r, true, nil
}
