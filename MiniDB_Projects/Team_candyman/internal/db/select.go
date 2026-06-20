package db

import (
	"fmt"

	"minidb/internal/executor"
	"minidb/internal/sql"
)

// execSelect plans and runs a SELECT. M2 supports single-table queries
// (SeqScan -> Filter -> Project). Joins, index scans, the cost optimizer and
// aggregation are added in M3 and replace this with the planner package.
func (d *Database) execSelect(s *sql.Select) (Result, error) {
	if len(s.Tables) != 1 {
		return Result{}, fmt.Errorf("multi-table queries are added in a later milestone")
	}
	if len(s.GroupBy) > 0 || containsAgg(s) {
		return Result{}, fmt.Errorf("aggregation is added in a later milestone")
	}
	tr := s.Tables[0]
	if _, ok := d.eng.Schema(tr.Name); !ok {
		return Result{}, fmt.Errorf("unknown table %q", tr.Name)
	}

	var op executor.Operator = &executor.SeqScan{Engine: d.eng, Table: tr.Name, Alias: tr.Alias}
	if pred := andAll(s.Predicates); pred != nil {
		op = &executor.Filter{Child: op, Pred: pred}
	}
	proj := &executor.Project{Child: op, Items: s.Items, Star: s.Star}

	return runQuery(proj)
}

// runQuery drives an operator tree to completion and formats the rows.
func runQuery(op executor.Operator) (Result, error) {
	if err := op.Open(); err != nil {
		return Result{}, err
	}
	defer op.Close()

	res := Result{}
	for _, c := range op.Columns() {
		res.Columns = append(res.Columns, c.Name)
	}
	for {
		row, ok, err := op.Next()
		if err != nil {
			return Result{}, err
		}
		if !ok {
			break
		}
		formatted := make([]string, len(row))
		for i, v := range row {
			formatted[i] = v.String()
		}
		res.Rows = append(res.Rows, formatted)
	}
	return res, nil
}

// andAll combines predicates with AND, or returns nil if there are none.
func andAll(preds []sql.Expr) sql.Expr {
	if len(preds) == 0 {
		return nil
	}
	out := preds[0]
	for _, p := range preds[1:] {
		out = &sql.BinaryExpr{Op: "AND", Left: out, Right: p}
	}
	return out
}

func containsAgg(s *sql.Select) bool {
	for _, it := range s.Items {
		if hasAgg(it.Expr) {
			return true
		}
	}
	return false
}

func hasAgg(e sql.Expr) bool {
	switch ex := e.(type) {
	case *sql.AggCall:
		_ = ex
		return true
	case *sql.BinaryExpr:
		return hasAgg(ex.Left) || hasAgg(ex.Right)
	default:
		return false
	}
}
