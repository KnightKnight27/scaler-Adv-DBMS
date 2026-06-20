package db

import (
	"minidb/internal/executor"
	"minidb/internal/planner"
	"minidb/internal/sql"
)

// execSelect plans a SELECT through the cost-based planner and runs the operator
// tree. EXPLAIN returns the chosen plan instead of executing it.
func (d *Database) execSelect(s *sql.Select) (Result, error) {
	p := planner.New(d.eng)
	op, explain, err := p.Plan(s)
	if err != nil {
		return Result{}, err
	}
	if s.Explain {
		return Result{Message: explain}, nil
	}
	return runQuery(op)
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
