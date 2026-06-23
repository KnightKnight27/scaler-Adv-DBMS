// Package executor runs physical plans using the Volcano (iterator) model: every
// operator exposes Open/Next/Close and pulls rows from its children one at a
// time. Scans materialise the locked/visible tuple set from the storage layer
// and feed it through filters, joins, projections and aggregates.
package executor

import (
	"fmt"

	"minidb/internal/sql"
	"minidb/internal/types"
)

// colMeta describes one column position in a (possibly joined) row: the column
// name plus the table name and alias it came from, used to resolve qualified
// references such as `t.id`.
type colMeta struct {
	table string
	alias string
	name  string
}

// RowSchema names the columns of the rows flowing out of an operator and maps
// SQL column references to positions within a row.
type RowSchema struct {
	cols []colMeta
}

func (s *RowSchema) width() int { return len(s.cols) }

// concat builds the schema of a join output: left columns followed by right.
func (s *RowSchema) concat(other *RowSchema) *RowSchema {
	out := &RowSchema{cols: make([]colMeta, 0, len(s.cols)+len(other.cols))}
	out.cols = append(out.cols, s.cols...)
	out.cols = append(out.cols, other.cols...)
	return out
}

// resolve returns the row position of ref. An unqualified reference matches by
// column name; a qualified reference must also match the table name or alias.
func (s *RowSchema) resolve(ref sql.ColumnRef) (int, error) {
	for i, c := range s.cols {
		if c.name != ref.Name {
			continue
		}
		if ref.Table == "" || ref.Table == c.table || ref.Table == c.alias {
			return i, nil
		}
	}
	return -1, fmt.Errorf("executor: unknown column %s", refString(ref))
}

func refString(r sql.ColumnRef) string {
	if r.Table != "" {
		return r.Table + "." + r.Name
	}
	return r.Name
}

// evalScalar evaluates an expression to a single value against row.
func evalScalar(e sql.Expr, schema *RowSchema, row types.Row) (types.Value, error) {
	switch v := e.(type) {
	case sql.Literal:
		return v.Value, nil
	case sql.ColumnRef:
		i, err := schema.resolve(v)
		if err != nil {
			return types.Value{}, err
		}
		return row[i], nil
	default:
		return types.Value{}, fmt.Errorf("executor: %T is not a scalar expression", e)
	}
}

// evalPredicate evaluates a boolean expression (AND of comparisons) against row.
// A nil expression is treated as always-true so callers can pass an absent
// WHERE clause directly.
func evalPredicate(e sql.Expr, schema *RowSchema, row types.Row) (bool, error) {
	if e == nil {
		return true, nil
	}
	be, ok := e.(sql.BinaryExpr)
	if !ok {
		return false, fmt.Errorf("executor: expected boolean expression, got %T", e)
	}
	if be.Op == "AND" {
		l, err := evalPredicate(be.Left, schema, row)
		if err != nil || !l {
			return false, err
		}
		return evalPredicate(be.Right, schema, row)
	}
	left, err := evalScalar(be.Left, schema, row)
	if err != nil {
		return false, err
	}
	right, err := evalScalar(be.Right, schema, row)
	if err != nil {
		return false, err
	}
	c := left.Compare(right)
	switch be.Op {
	case "=":
		return c == 0, nil
	case "!=":
		return c != 0, nil
	case "<":
		return c < 0, nil
	case "<=":
		return c <= 0, nil
	case ">":
		return c > 0, nil
	case ">=":
		return c >= 0, nil
	default:
		return false, fmt.Errorf("executor: unknown operator %q", be.Op)
	}
}

// EvalPredicate is the exported entry point used by the engine's DELETE path to
// re-check predicates against materialised rows.
func EvalPredicate(e sql.Expr, schema *RowSchema, row types.Row) (bool, error) {
	return evalPredicate(e, schema, row)
}
