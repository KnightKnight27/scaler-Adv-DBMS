package planner

import (
	"fmt"
	"math"

	"minidb/internal/sql"
	"minidb/internal/types"
)

// Optimizer builds physical plans using table statistics from a Provider.
type Optimizer struct {
	provider Provider
}

// NewOptimizer constructs an optimizer over the given table provider.
func NewOptimizer(p Provider) *Optimizer { return &Optimizer{provider: p} }

type relation struct {
	name  string
	alias string
	info  TableInfo
}

func (r relation) matches(ref sql.ColumnRef) bool {
	if ref.Table != "" {
		return ref.Table == r.alias || ref.Table == r.name
	}
	return r.info.Schema().ColumnIndex(ref.Name) >= 0
}

func aliasOr(name, alias string) string {
	if alias != "" {
		return alias
	}
	return name
}

// BuildSelect produces a physical plan for sel, choosing scan methods, join
// algorithm and join order by estimated cost.
func (o *Optimizer) BuildSelect(sel *sql.Select) (Plan, error) {
	info0, ok := o.provider.TableInfo(sel.From)
	if !ok {
		return nil, fmt.Errorf("planner: unknown table %q", sel.From)
	}
	rel0 := relation{name: sel.From, alias: aliasOr(sel.From, sel.FromAlias), info: info0}

	conjuncts := flatten(sel.Where)

	var plan Plan
	if sel.Join == nil {
		plan = o.buildScan(rel0, assignConjuncts(conjuncts, []relation{rel0})[0])
	} else {
		info1, ok := o.provider.TableInfo(sel.Join.Table)
		if !ok {
			return nil, fmt.Errorf("planner: unknown table %q", sel.Join.Table)
		}
		rel1 := relation{name: sel.Join.Table, alias: aliasOr(sel.Join.Table, sel.Join.Alias), info: info1}
		buckets := assignConjuncts(conjuncts, []relation{rel0, rel1})
		left := o.buildScan(rel0, buckets[0])
		right := o.buildScan(rel1, buckets[1])
		plan = o.buildJoin(left, right, rel0, rel1, sel.Join)
	}

	return o.topLevel(sel, plan), nil
}

// topLevel wraps the scan/join plan in an aggregate or projection as the SELECT
// list dictates.
func (o *Optimizer) topLevel(sel *sql.Select, child Plan) Plan {
	if len(sel.Items) == 1 && sel.Items[0].IsCount {
		return &CountAgg{Child: child}
	}
	if len(sel.Items) == 1 && sel.Items[0].Star {
		return child
	}
	return &Projection{Child: child, Items: sel.Items}
}

// buildScan chooses between a sequential scan and an index scan for one relation
// given the predicates that reference only that relation.
func (o *Optimizer) buildScan(rel relation, conjuncts []sql.Expr) Plan {
	rows := float64(rel.info.RowCount())
	if rows < 1 {
		rows = 1
	}

	sel := 1.0
	for _, c := range conjuncts {
		sel *= selectivity(c, rel)
	}

	seq := &SeqScan{
		Table:  rel.name,
		Alias:  rel.alias,
		Filter: foldAnd(conjuncts),
		rows:   rows * sel,
		cost:   rows, // a sequential scan must read every tuple
	}

	// Look for an equality predicate on an indexed column to consider an index
	// scan instead.
	for i, c := range conjuncts {
		col, key, ok := equalityOnColumn(c, rel)
		if !ok || !rel.info.HasIndex(col) {
			continue
		}
		eqSel := selectivity(c, rel)
		residual := foldAnd(removeAt(conjuncts, i))
		idxRows := rows * sel
		idxCost := rows*eqSel + 1 // touch only matching tuples via the B+Tree
		if idxCost < seq.cost {
			return &IndexScan{
				Table:    rel.name,
				Alias:    rel.alias,
				Col:      col,
				ColName:  rel.info.Schema().Columns[col].Name,
				Key:      key,
				Residual: residual,
				rows:     idxRows,
				cost:     idxCost,
			}
		}
	}
	return seq
}

// buildJoin estimates output size and selects a join algorithm and orientation.
func (o *Optimizer) buildJoin(left, right Plan, lRel, rRel relation, j *sql.JoinClause) Plan {
	lRef, rRef := orientKeys(j, lRel, rRel)
	lCol := lRel.info.Schema().ColumnIndex(lRef.Name)
	rCol := rRel.info.Schema().ColumnIndex(rRef.Name)

	lRows := left.EstRows()
	rRows := right.EstRows()
	dl := math.Max(float64(lRel.info.DistinctValues(lCol)), 1)
	dr := math.Max(float64(rRel.info.DistinctValues(rCol)), 1)
	joinSel := 1.0 / math.Max(dl, dr)
	outRows := lRows * rRows * joinSel

	hashCost := lRows + rRows + outRows

	// Index nested-loop join is attractive when the inner side is indexed on the
	// join key, turning the inner scan into a point lookup.
	bestNLJCost := math.Inf(1)
	var nljOuter, nljInner Plan
	var nljOuterRef, nljInnerRef sql.ColumnRef
	if rRel.info.HasIndex(rCol) {
		if c := lRows + lRows; c < bestNLJCost {
			bestNLJCost = c
			nljOuter, nljInner = left, right
			nljOuterRef, nljInnerRef = lRef, rRef
		}
	}
	if lRel.info.HasIndex(lCol) {
		if c := rRows + rRows; c < bestNLJCost {
			bestNLJCost = c
			nljOuter, nljInner = right, left
			nljOuterRef, nljInnerRef = rRef, lRef
		}
	}

	if bestNLJCost < hashCost {
		return &NestedLoopJoin{
			Left: nljOuter, Right: nljInner,
			LeftKey: nljOuterRef, RightKey: nljInnerRef,
			rows: outRows, cost: bestNLJCost,
		}
	}

	// Hash join: build the hash table on the smaller input.
	bl, br := left, right
	blRef, brRef := lRef, rRef
	if rRows > lRows {
		bl, br = right, left
		blRef, brRef = rRef, lRef
	}
	return &HashJoin{
		Left: bl, Right: br,
		LeftKey: blRef, RightKey: brRef,
		rows: outRows, cost: hashCost,
	}
}

// orientKeys returns the join condition refs ordered as (left-relation ref,
// right-relation ref).
func orientKeys(j *sql.JoinClause, lRel, rRel relation) (sql.ColumnRef, sql.ColumnRef) {
	if lRel.matches(j.Left) {
		return j.Left, j.Right
	}
	return j.Right, j.Left
}

// selectivity estimates the fraction of rows a predicate keeps, using distinct
// counts for equality and fixed heuristics otherwise.
func selectivity(e sql.Expr, rel relation) float64 {
	be, ok := e.(sql.BinaryExpr)
	if !ok {
		return 1.0
	}
	switch be.Op {
	case "=":
		if col, _, ok := equalityOnColumn(be, rel); ok {
			d := rel.info.DistinctValues(col)
			if d < 1 {
				d = 1
			}
			return 1.0 / float64(d)
		}
		return 0.1
	case "!=":
		return 0.9
	case "<", "<=", ">", ">=":
		return 0.33
	default:
		return 1.0
	}
}

// equalityOnColumn recognises predicates of the form `column = literal` (in
// either operand order) on a column of rel, returning its index and the key.
func equalityOnColumn(e sql.Expr, rel relation) (int, types.Value, bool) {
	be, ok := e.(sql.BinaryExpr)
	if !ok || be.Op != "=" {
		return 0, types.Value{}, false
	}
	if ref, ok := be.Left.(sql.ColumnRef); ok {
		if lit, ok := be.Right.(sql.Literal); ok && rel.matches(ref) {
			return rel.info.Schema().ColumnIndex(ref.Name), lit.Value, true
		}
	}
	if ref, ok := be.Right.(sql.ColumnRef); ok {
		if lit, ok := be.Left.(sql.Literal); ok && rel.matches(ref) {
			return rel.info.Schema().ColumnIndex(ref.Name), lit.Value, true
		}
	}
	return 0, types.Value{}, false
}

// flatten splits an AND-chain into its conjuncts.
func flatten(e sql.Expr) []sql.Expr {
	if e == nil {
		return nil
	}
	be, ok := e.(sql.BinaryExpr)
	if !ok || be.Op != "AND" {
		return []sql.Expr{e}
	}
	return append(flatten(be.Left), flatten(be.Right)...)
}

// foldAnd recombines conjuncts into a single expression (nil if empty).
func foldAnd(cs []sql.Expr) sql.Expr {
	if len(cs) == 0 {
		return nil
	}
	out := cs[0]
	for _, c := range cs[1:] {
		out = sql.BinaryExpr{Op: "AND", Left: out, Right: c}
	}
	return out
}

func removeAt(cs []sql.Expr, i int) []sql.Expr {
	out := make([]sql.Expr, 0, len(cs)-1)
	out = append(out, cs[:i]...)
	return append(out, cs[i+1:]...)
}

// assignConjuncts routes each conjunct to the single relation it references.
// Cross-relation predicates (which our grammar expresses through JOIN ... ON)
// are dropped from per-table buckets.
func assignConjuncts(conjuncts []sql.Expr, rels []relation) [][]sql.Expr {
	buckets := make([][]sql.Expr, len(rels))
	for _, c := range conjuncts {
		refs := columnRefs(c)
		matched := -1
		single := true
		for _, ref := range refs {
			for ri, rel := range rels {
				if rel.matches(ref) {
					if matched == -1 {
						matched = ri
					} else if matched != ri {
						single = false
					}
				}
			}
		}
		if single && matched >= 0 {
			buckets[matched] = append(buckets[matched], c)
		}
	}
	return buckets
}

func columnRefs(e sql.Expr) []sql.ColumnRef {
	switch v := e.(type) {
	case sql.ColumnRef:
		return []sql.ColumnRef{v}
	case sql.BinaryExpr:
		return append(columnRefs(v.Left), columnRefs(v.Right)...)
	default:
		return nil
	}
}
