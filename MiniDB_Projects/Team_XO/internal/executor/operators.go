package executor

import (
	"fmt"

	"minidb/internal/catalog"
	"minidb/internal/planner"
	"minidb/internal/sql"
	"minidb/internal/storage"
	"minidb/internal/txn"
	"minidb/internal/types"
)

// RowRef pairs a tuple with its physical address, returned by scans so the
// DELETE path can address rows for removal.
type RowRef struct {
	RID storage.RID
	Row types.Row
}

// Table is the access-method interface the executor needs from a stored
// relation. The engine's runtime tables implement it for both the 2PL and MVCC
// strategies, so the executor is agnostic to the concurrency model.
type Table interface {
	Schema() *catalog.Table
	Scan(t *txn.Transaction) ([]RowRef, error)
	IndexLookup(t *txn.Transaction, col int, key types.Value) ([]RowRef, error)
	HasIndex(col int) bool
	Insert(t *txn.Transaction, row types.Row) (storage.RID, error)
	DeleteByRID(t *txn.Transaction, rid storage.RID) error
}

// Tables resolves table names to their runtime access objects.
type Tables interface {
	Get(name string) (Table, bool)
}

// Operator is a node in the execution pipeline following the Volcano model.
type Operator interface {
	Open() error
	Next() (types.Row, bool, error)
	Close() error
	Schema() *RowSchema
}

// BuildTableSchema produces the row schema for a base table scan.
func BuildTableSchema(name, alias string, t Table) *RowSchema {
	cols := t.Schema().Columns
	meta := make([]colMeta, len(cols))
	for i, c := range cols {
		meta[i] = colMeta{table: name, alias: alias, name: c.Name}
	}
	return &RowSchema{cols: meta}
}

// ColumnNames returns display headers for a schema.
func ColumnNames(s *RowSchema) []string {
	out := make([]string, len(s.cols))
	for i, c := range s.cols {
		out[i] = c.name
	}
	return out
}

// scanOp iterates a materialised slice of rows produced by a base-table access
// method (sequential or index).
type scanOp struct {
	rows   []RowRef
	schema *RowSchema
	pos    int
}

func (o *scanOp) Open() error        { o.pos = 0; return nil }
func (o *scanOp) Schema() *RowSchema { return o.schema }
func (o *scanOp) Close() error       { return nil }
func (o *scanOp) Next() (types.Row, bool, error) {
	if o.pos >= len(o.rows) {
		return nil, false, nil
	}
	r := o.rows[o.pos].Row
	o.pos++
	return r, true, nil
}

// filterOp passes through only the child rows satisfying a predicate.
type filterOp struct {
	child Operator
	pred  sql.Expr
}

func (o *filterOp) Open() error        { return o.child.Open() }
func (o *filterOp) Schema() *RowSchema { return o.child.Schema() }
func (o *filterOp) Close() error       { return o.child.Close() }
func (o *filterOp) Next() (types.Row, bool, error) {
	for {
		row, ok, err := o.child.Next()
		if err != nil || !ok {
			return nil, false, err
		}
		keep, err := evalPredicate(o.pred, o.child.Schema(), row)
		if err != nil {
			return nil, false, err
		}
		if keep {
			return row, true, nil
		}
	}
}

// hashJoinOp builds an in-memory hash table on its right (build) input keyed by
// the right join column, then streams the left (probe) input, emitting the
// concatenation of every matching pair.
type hashJoinOp struct {
	left, right Operator
	leftKey     sql.ColumnRef
	rightKey    sql.ColumnRef
	schema      *RowSchema

	leftKeyIdx int
	table      map[string][]types.Row
	curLeft    types.Row
	matches    []types.Row
	matchPos   int
}

func (o *hashJoinOp) Schema() *RowSchema { return o.schema }

func (o *hashJoinOp) Open() error {
	if err := o.right.Open(); err != nil {
		return err
	}
	rightIdx, err := o.right.Schema().resolve(o.rightKey)
	if err != nil {
		return err
	}
	o.table = make(map[string][]types.Row)
	for {
		row, ok, err := o.right.Next()
		if err != nil {
			return err
		}
		if !ok {
			break
		}
		k := row[rightIdx].String()
		o.table[k] = append(o.table[k], row.Clone())
	}
	if err := o.right.Close(); err != nil {
		return err
	}
	if err := o.left.Open(); err != nil {
		return err
	}
	o.leftKeyIdx, err = o.left.Schema().resolve(o.leftKey)
	return err
}

func (o *hashJoinOp) Next() (types.Row, bool, error) {
	for {
		if o.matchPos < len(o.matches) {
			right := o.matches[o.matchPos]
			o.matchPos++
			joined := make(types.Row, 0, len(o.curLeft)+len(right))
			joined = append(joined, o.curLeft...)
			joined = append(joined, right...)
			return joined, true, nil
		}
		row, ok, err := o.left.Next()
		if err != nil || !ok {
			return nil, false, err
		}
		o.curLeft = row
		o.matches = o.table[row[o.leftKeyIdx].String()]
		o.matchPos = 0
	}
}

func (o *hashJoinOp) Close() error { return o.left.Close() }

// nestedLoopJoinOp scans the outer input once and, for each outer row, scans a
// materialised copy of the inner input, emitting matching pairs. It is the plan
// the optimizer selects when the inner side can be probed cheaply (e.g. via an
// index), or when inputs are tiny.
type nestedLoopJoinOp struct {
	outer, inner Operator
	outerKey     sql.ColumnRef
	innerKey     sql.ColumnRef
	schema       *RowSchema

	innerRows   []types.Row
	outerKeyIdx int
	innerKeyIdx int
	curOuter    types.Row
	innerPos    int
}

func (o *nestedLoopJoinOp) Schema() *RowSchema { return o.schema }

func (o *nestedLoopJoinOp) Open() error {
	if err := o.inner.Open(); err != nil {
		return err
	}
	var err error
	o.innerKeyIdx, err = o.inner.Schema().resolve(o.innerKey)
	if err != nil {
		return err
	}
	for {
		row, ok, err := o.inner.Next()
		if err != nil {
			return err
		}
		if !ok {
			break
		}
		o.innerRows = append(o.innerRows, row.Clone())
	}
	if err := o.inner.Close(); err != nil {
		return err
	}
	if err := o.outer.Open(); err != nil {
		return err
	}
	o.outerKeyIdx, err = o.outer.Schema().resolve(o.outerKey)
	o.innerPos = len(o.innerRows) // force fetch of first outer row
	return err
}

func (o *nestedLoopJoinOp) Next() (types.Row, bool, error) {
	for {
		for o.innerPos < len(o.innerRows) {
			inner := o.innerRows[o.innerPos]
			o.innerPos++
			if o.curOuter[o.outerKeyIdx].Equal(inner[o.innerKeyIdx]) {
				joined := make(types.Row, 0, len(o.curOuter)+len(inner))
				joined = append(joined, o.curOuter...)
				joined = append(joined, inner...)
				return joined, true, nil
			}
		}
		row, ok, err := o.outer.Next()
		if err != nil || !ok {
			return nil, false, err
		}
		o.curOuter = row
		o.innerPos = 0
	}
}

func (o *nestedLoopJoinOp) Close() error { return o.outer.Close() }

// projectionOp emits a chosen subset/ordering of its child's columns.
type projectionOp struct {
	child   Operator
	indices []int
	schema  *RowSchema
}

func (o *projectionOp) Schema() *RowSchema { return o.schema }
func (o *projectionOp) Open() error        { return o.child.Open() }
func (o *projectionOp) Close() error       { return o.child.Close() }
func (o *projectionOp) Next() (types.Row, bool, error) {
	row, ok, err := o.child.Next()
	if err != nil || !ok {
		return nil, false, err
	}
	out := make(types.Row, len(o.indices))
	for i, idx := range o.indices {
		out[i] = row[idx]
	}
	return out, true, nil
}

// countOp consumes its child fully and emits a single COUNT(*) row.
type countOp struct {
	child  Operator
	schema *RowSchema
	done   bool
}

func (o *countOp) Schema() *RowSchema { return o.schema }
func (o *countOp) Open() error        { o.done = false; return o.child.Open() }
func (o *countOp) Close() error       { return o.child.Close() }
func (o *countOp) Next() (types.Row, bool, error) {
	if o.done {
		return nil, false, nil
	}
	var n int64
	for {
		_, ok, err := o.child.Next()
		if err != nil {
			return nil, false, err
		}
		if !ok {
			break
		}
		n++
	}
	o.done = true
	return types.Row{types.NewInt(n)}, true, nil
}

// Result is the outcome of executing a query plan.
type Result struct {
	Columns []string
	Rows    []types.Row
}

// Execute builds an operator tree from plan and drains it into a Result.
func Execute(plan planner.Plan, tables Tables, t *txn.Transaction) (*Result, error) {
	op, err := build(plan, tables, t)
	if err != nil {
		return nil, err
	}
	if err := op.Open(); err != nil {
		return nil, err
	}
	defer op.Close()

	res := &Result{Columns: ColumnNames(op.Schema())}
	for {
		row, ok, err := op.Next()
		if err != nil {
			return nil, err
		}
		if !ok {
			break
		}
		res.Rows = append(res.Rows, row.Clone())
	}
	return res, nil
}

// build recursively turns a plan node into an operator.
func build(plan planner.Plan, tables Tables, t *txn.Transaction) (Operator, error) {
	switch p := plan.(type) {
	case *planner.SeqScan:
		tbl, ok := tables.Get(p.Table)
		if !ok {
			return nil, fmt.Errorf("executor: unknown table %q", p.Table)
		}
		refs, err := tbl.Scan(t)
		if err != nil {
			return nil, err
		}
		op := Operator(&scanOp{rows: refs, schema: BuildTableSchema(p.Table, p.Alias, tbl)})
		if p.Filter != nil {
			op = &filterOp{child: op, pred: p.Filter}
		}
		return op, nil

	case *planner.IndexScan:
		tbl, ok := tables.Get(p.Table)
		if !ok {
			return nil, fmt.Errorf("executor: unknown table %q", p.Table)
		}
		refs, err := tbl.IndexLookup(t, p.Col, p.Key)
		if err != nil {
			return nil, err
		}
		op := Operator(&scanOp{rows: refs, schema: BuildTableSchema(p.Table, p.Alias, tbl)})
		if p.Residual != nil {
			op = &filterOp{child: op, pred: p.Residual}
		}
		return op, nil

	case *planner.HashJoin:
		left, err := build(p.Left, tables, t)
		if err != nil {
			return nil, err
		}
		right, err := build(p.Right, tables, t)
		if err != nil {
			return nil, err
		}
		return &hashJoinOp{
			left: left, right: right,
			leftKey: p.LeftKey, rightKey: p.RightKey,
			schema: left.Schema().concat(right.Schema()),
		}, nil

	case *planner.NestedLoopJoin:
		outer, err := build(p.Left, tables, t)
		if err != nil {
			return nil, err
		}
		inner, err := build(p.Right, tables, t)
		if err != nil {
			return nil, err
		}
		return &nestedLoopJoinOp{
			outer: outer, inner: inner,
			outerKey: p.LeftKey, innerKey: p.RightKey,
			schema: outer.Schema().concat(inner.Schema()),
		}, nil

	case *planner.Projection:
		child, err := build(p.Child, tables, t)
		if err != nil {
			return nil, err
		}
		return buildProjection(child, p.Items)

	case *planner.CountAgg:
		child, err := build(p.Child, tables, t)
		if err != nil {
			return nil, err
		}
		return &countOp{child: child, schema: &RowSchema{cols: []colMeta{{name: "count"}}}}, nil

	default:
		return nil, fmt.Errorf("executor: unsupported plan node %T", plan)
	}
}

func buildProjection(child Operator, items []sql.ResultColumn) (Operator, error) {
	indices := make([]int, 0, len(items))
	cols := make([]colMeta, 0, len(items))
	for _, item := range items {
		idx, err := child.Schema().resolve(item.Column)
		if err != nil {
			return nil, err
		}
		indices = append(indices, idx)
		cols = append(cols, child.Schema().cols[idx])
	}
	return &projectionOp{child: child, indices: indices, schema: &RowSchema{cols: cols}}, nil
}
