// Package executor implements volcano-style (pull-based) physical operators and
// expression evaluation. Each operator exposes Open/Next/Close and an output
// schema (Columns); a parent pulls one row at a time from its child.
package executor

import (
	"fmt"

	"minidb/internal/sql"
	"minidb/internal/types"
)

// ColumnInfo describes one column of an operator's output tuple: the originating
// table alias, the column name, and its type.
type ColumnInfo struct {
	Table string
	Name  string
	Type  types.Type
}

// Schema is an operator's output column list.
type Schema []ColumnInfo

// resolve finds the index of a (possibly qualified) column reference.
func (s Schema) resolve(ref *sql.ColumnRef) (int, error) {
	idx := -1
	for i, c := range s {
		if c.Name != ref.Name {
			continue
		}
		if ref.Table != "" && c.Table != ref.Table {
			continue
		}
		if idx != -1 {
			return -1, fmt.Errorf("ambiguous column %q", ref.Name)
		}
		idx = i
	}
	if idx == -1 {
		return -1, fmt.Errorf("unknown column %q", columnName(ref))
	}
	return idx, nil
}

func columnName(ref *sql.ColumnRef) string {
	if ref.Table != "" {
		return ref.Table + "." + ref.Name
	}
	return ref.Name
}

// Eval evaluates a scalar expression against a row given the row's schema.
func Eval(e sql.Expr, row types.Row, schema Schema) (types.Value, error) {
	switch ex := e.(type) {
	case *sql.Literal:
		return ex.Value, nil
	case *sql.ColumnRef:
		i, err := schema.resolve(ex)
		if err != nil {
			return types.Value{}, err
		}
		return row[i], nil
	case *sql.BinaryExpr:
		return evalBinary(ex, row, schema)
	default:
		return types.Value{}, fmt.Errorf("cannot evaluate expression of type %T here", e)
	}
}

// EvalBool evaluates a predicate, treating NULL/non-boolean results as false.
func EvalBool(e sql.Expr, row types.Row, schema Schema) (bool, error) {
	if e == nil {
		return true, nil
	}
	v, err := Eval(e, row, schema)
	if err != nil {
		return false, err
	}
	return !v.Null && v.Type == types.TypeInt && v.Int != 0, nil
}

func evalBinary(ex *sql.BinaryExpr, row types.Row, schema Schema) (types.Value, error) {
	if ex.Op == "AND" {
		l, err := EvalBool(ex.Left, row, schema)
		if err != nil {
			return types.Value{}, err
		}
		r, err := EvalBool(ex.Right, row, schema)
		if err != nil {
			return types.Value{}, err
		}
		return boolValue(l && r), nil
	}
	l, err := Eval(ex.Left, row, schema)
	if err != nil {
		return types.Value{}, err
	}
	r, err := Eval(ex.Right, row, schema)
	if err != nil {
		return types.Value{}, err
	}
	if l.Null || r.Null {
		return types.Value{Null: true}, nil
	}
	cmp := l.Compare(r)
	var res bool
	switch ex.Op {
	case "=":
		res = cmp == 0
	case "!=":
		res = cmp != 0
	case "<":
		res = cmp < 0
	case "<=":
		res = cmp <= 0
	case ">":
		res = cmp > 0
	case ">=":
		res = cmp >= 0
	default:
		return types.Value{}, fmt.Errorf("unknown operator %q", ex.Op)
	}
	return boolValue(res), nil
}

func boolValue(b bool) types.Value {
	if b {
		return types.NewInt(1)
	}
	return types.NewInt(0)
}
