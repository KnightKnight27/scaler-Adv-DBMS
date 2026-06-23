// Package planner turns a parsed SELECT into a physical query plan. It contains
// a small cost-based optimizer that estimates selectivity from table statistics,
// chooses between a sequential scan and an index scan, and picks a join
// algorithm and order. The resulting Plan tree is consumed by the executor and
// can be rendered by EXPLAIN.
package planner

import (
	"fmt"
	"strings"

	"minidb/internal/catalog"
	"minidb/internal/sql"
	"minidb/internal/types"
)

// TableInfo exposes the schema and statistics the optimizer needs about one
// table. The engine's runtime tables implement it.
type TableInfo interface {
	Schema() *catalog.Table
	RowCount() int
	DistinctValues(col int) int
	HasIndex(col int) bool
}

// Provider resolves table names to TableInfo.
type Provider interface {
	TableInfo(name string) (TableInfo, bool)
}

// Plan is a node in the physical plan tree. EstRows and EstCost are the
// optimizer's estimates, surfaced by EXPLAIN so the chosen plan can be defended.
type Plan interface {
	EstRows() float64
	EstCost() float64
	explain(sb *strings.Builder, indent int)
}

// SeqScan reads every tuple of a table, optionally applying a residual filter.
type SeqScan struct {
	Table  string
	Alias  string
	Filter sql.Expr
	rows   float64
	cost   float64
}

// IndexScan probes a B+Tree on Col for Key, then applies any residual filter
// that the index could not satisfy.
type IndexScan struct {
	Table    string
	Alias    string
	Col      int
	ColName  string
	Key      types.Value
	Residual sql.Expr
	rows     float64
	cost     float64
}

// HashJoin builds a hash table on the (smaller) right input keyed by RightKey
// and probes it with the left input on LeftKey.
type HashJoin struct {
	Left, Right       Plan
	LeftKey, RightKey sql.ColumnRef
	rows, cost        float64
}

// NestedLoopJoin scans the outer (Left) once and the inner (Right) per outer
// row, matching on the equality condition.
type NestedLoopJoin struct {
	Left, Right       Plan
	LeftKey, RightKey sql.ColumnRef
	rows, cost        float64
}

// Projection narrows or reorders the columns produced by its child.
type Projection struct {
	Child Plan
	Items []sql.ResultColumn
}

// CountAgg collapses its child to a single COUNT(*) row.
type CountAgg struct{ Child Plan }

func (p *SeqScan) EstRows() float64        { return p.rows }
func (p *SeqScan) EstCost() float64        { return p.cost }
func (p *IndexScan) EstRows() float64      { return p.rows }
func (p *IndexScan) EstCost() float64      { return p.cost }
func (p *HashJoin) EstRows() float64       { return p.rows }
func (p *HashJoin) EstCost() float64       { return p.cost }
func (p *NestedLoopJoin) EstRows() float64 { return p.rows }
func (p *NestedLoopJoin) EstCost() float64 { return p.cost }
func (p *Projection) EstRows() float64     { return p.Child.EstRows() }
func (p *Projection) EstCost() float64     { return p.Child.EstCost() }
func (p *CountAgg) EstRows() float64       { return 1 }
func (p *CountAgg) EstCost() float64       { return p.Child.EstCost() }

// Explain renders the plan as an indented tree with row/cost estimates.
func Explain(p Plan) string {
	var sb strings.Builder
	p.explain(&sb, 0)
	return sb.String()
}

func line(sb *strings.Builder, indent int, format string, args ...any) {
	sb.WriteString(strings.Repeat("  ", indent))
	sb.WriteString("-> ")
	fmt.Fprintf(sb, format, args...)
	sb.WriteByte('\n')
}

func (p *SeqScan) explain(sb *strings.Builder, indent int) {
	line(sb, indent, "Seq Scan on %s  (est_rows=%.0f cost=%.1f)", p.Table, p.rows, p.cost)
	if p.Filter != nil {
		line(sb, indent+1, "Filter: %s", exprString(p.Filter))
	}
}

func (p *IndexScan) explain(sb *strings.Builder, indent int) {
	line(sb, indent, "Index Scan on %s using %s=%s  (est_rows=%.0f cost=%.1f)",
		p.Table, p.ColName, p.Key.String(), p.rows, p.cost)
	if p.Residual != nil {
		line(sb, indent+1, "Filter: %s", exprString(p.Residual))
	}
}

func (p *HashJoin) explain(sb *strings.Builder, indent int) {
	line(sb, indent, "Hash Join on %s = %s  (est_rows=%.0f cost=%.1f)",
		refString(p.LeftKey), refString(p.RightKey), p.rows, p.cost)
	p.Left.explain(sb, indent+1)
	p.Right.explain(sb, indent+1)
}

func (p *NestedLoopJoin) explain(sb *strings.Builder, indent int) {
	line(sb, indent, "Nested Loop Join on %s = %s  (est_rows=%.0f cost=%.1f)",
		refString(p.LeftKey), refString(p.RightKey), p.rows, p.cost)
	p.Left.explain(sb, indent+1)
	p.Right.explain(sb, indent+1)
}

func (p *Projection) explain(sb *strings.Builder, indent int) {
	line(sb, indent, "Projection")
	p.Child.explain(sb, indent+1)
}

func (p *CountAgg) explain(sb *strings.Builder, indent int) {
	line(sb, indent, "Aggregate: COUNT(*)")
	p.Child.explain(sb, indent+1)
}

func refString(r sql.ColumnRef) string {
	if r.Table != "" {
		return r.Table + "." + r.Name
	}
	return r.Name
}

func exprString(e sql.Expr) string {
	switch v := e.(type) {
	case sql.ColumnRef:
		return refString(v)
	case sql.Literal:
		return v.Value.String()
	case sql.BinaryExpr:
		return fmt.Sprintf("%s %s %s", exprString(v.Left), v.Op, exprString(v.Right))
	default:
		return "?"
	}
}
