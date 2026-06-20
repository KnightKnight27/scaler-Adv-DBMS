// Package planner turns a SELECT AST into a physical operator tree, making the
// cost-based decisions the project requires:
//
//   - scan choice: an equality predicate on a primary key becomes an IndexScan
//     (point lookup, ~1 row) instead of a SeqScan (N rows);
//   - selectivity estimation: per-predicate selectivity refines each relation's
//     estimated cardinality;
//   - join ordering: relations are joined smallest-estimated-first (a greedy
//     left-deep order) to keep the nested-loop inner side small.
//
// EXPLAIN renders the chosen plan with the estimates that drove it.
package planner

import (
	"fmt"
	"sort"
	"strings"

	"minidb/internal/executor"
	"minidb/internal/sql"
	"minidb/internal/storage"
	"minidb/internal/types"
)

// Planner builds plans against a storage engine.
type Planner struct {
	eng    storage.StorageEngine
	counts map[string]int // cached base-table row counts
}

// New returns a planner for an engine.
func New(eng storage.StorageEngine) *Planner {
	return &Planner{eng: eng, counts: map[string]int{}}
}

// relation is one base table after local filtering, with its chosen access path.
type relation struct {
	alias   string
	op      executor.Operator
	estCard float64
	explain string
}

// Plan produces the operator tree and an EXPLAIN string for a SELECT.
func (p *Planner) Plan(sel *sql.Select) (executor.Operator, string, error) {
	schemas := map[string]*types.Schema{}
	tableOf := map[string]string{}
	for _, tr := range sel.Tables {
		sch, ok := p.eng.Schema(tr.Name)
		if !ok {
			return nil, "", fmt.Errorf("unknown table %q", tr.Name)
		}
		schemas[tr.Alias] = sch
		tableOf[tr.Alias] = tr.Name
	}

	// classify predicates into single-table locals and multi-table joins
	locals := map[string][]sql.Expr{}
	var joins []sql.Expr
	var residual []sql.Expr
	for _, pred := range sel.Predicates {
		aliases := p.predAliases(pred, schemas)
		switch len(aliases) {
		case 1:
			a := aliases[0]
			locals[a] = append(locals[a], pred)
		case 2:
			joins = append(joins, pred)
		default:
			residual = append(residual, pred)
		}
	}

	// build a relation (access path + filters) per table
	rels := make([]*relation, 0, len(sel.Tables))
	for _, tr := range sel.Tables {
		rel, err := p.buildRelation(tr, schemas[tr.Alias], locals[tr.Alias])
		if err != nil {
			return nil, "", err
		}
		rels = append(rels, rel)
	}

	// join ordering: greedily smallest estimated relation first
	sort.SliceStable(rels, func(i, j int) bool { return rels[i].estCard < rels[j].estCard })

	root := rels[0].op
	joinedAliases := map[string]bool{rels[0].alias: true}
	var explainLines []string
	explainLines = append(explainLines, rels[0].explain)
	for _, rel := range rels[1:] {
		cond, used := pickJoinPreds(joins, joinedAliases, rel.alias, schemas, p)
		joins = used.remaining
		root = &executor.NestedLoopJoin{Outer: root, Inner: rel.op, Pred: cond}
		joinedAliases[rel.alias] = true
		detail := "NestedLoopJoin"
		if cond != nil {
			detail += " on " + exprString(cond)
		}
		explainLines = append(explainLines, fmt.Sprintf("%s\n    %s", detail, rel.explain))
	}

	// predicates not consumed by joins/access paths become a top filter
	leftover := append(residual, joins...)
	if f := andAll(leftover); f != nil {
		root = &executor.Filter{Child: root, Pred: f}
		explainLines = append(explainLines, "Filter: "+exprString(f))
	}

	// aggregation vs projection
	if len(sel.GroupBy) > 0 || hasAggItems(sel.Items) {
		root = &executor.HashAgg{Child: root, GroupBy: sel.GroupBy, Items: sel.Items}
		explainLines = append(explainLines, fmt.Sprintf("HashAggregate (group by %d expr)", len(sel.GroupBy)))
	} else {
		root = &executor.Project{Child: root, Items: sel.Items, Star: sel.Star}
		explainLines = append(explainLines, "Project")
	}

	return root, renderExplain(explainLines), nil
}

func (p *Planner) buildRelation(tr sql.TableRef, schema *types.Schema, preds []sql.Expr) (*relation, error) {
	n := float64(p.rowCount(tr.Name))
	rel := &relation{alias: tr.Alias}

	// look for a primary-key equality predicate to drive an index scan
	pkName := schema.PKColumn().Name
	var indexKey *types.Value
	var consumed = -1
	for i, pred := range preds {
		if v, ok := pkEqualityKey(pred, tr.Alias, pkName); ok {
			indexKey = &v
			consumed = i
			break
		}
	}

	var remaining []sql.Expr
	if indexKey != nil {
		for i, pred := range preds {
			if i != consumed {
				remaining = append(remaining, pred)
			}
		}
		rel.op = &executor.IndexScan{Engine: p.eng, Table: tr.Name, Alias: tr.Alias, Key: *indexKey}
		rel.estCard = 1
		rel.explain = fmt.Sprintf("IndexScan %s (pk = %s)  [est rows 1, of %d]", tr.Name, indexKey.String(), int(n))
	} else {
		remaining = preds
		rel.op = &executor.SeqScan{Engine: p.eng, Table: tr.Name, Alias: tr.Alias}
		rel.estCard = n * selectivity(remaining)
		rel.explain = fmt.Sprintf("SeqScan %s  [est rows %d, of %d]", tr.Name, int(rel.estCard+0.5), int(n))
	}

	if f := andAll(remaining); f != nil {
		rel.op = &executor.Filter{Child: rel.op, Pred: f}
		rel.explain += "\n  Filter: " + exprString(f)
	}
	return rel, nil
}

// rowCount returns the (cached) number of live rows in a table.
func (p *Planner) rowCount(table string) int {
	if n, ok := p.counts[table]; ok {
		return n
	}
	n := 0
	cur, err := p.eng.Scan(table)
	if err == nil {
		for {
			_, ok, err := cur.Next()
			if err != nil || !ok {
				break
			}
			n++
		}
		cur.Close()
	}
	p.counts[table] = n
	return n
}

// selectivity estimates the combined selectivity of a predicate list using
// standard textbook defaults (equality 0.1, range 0.3).
func selectivity(preds []sql.Expr) float64 {
	s := 1.0
	for _, pred := range preds {
		s *= predSelectivity(pred)
	}
	return s
}

func predSelectivity(e sql.Expr) float64 {
	be, ok := e.(*sql.BinaryExpr)
	if !ok {
		return 1.0
	}
	switch be.Op {
	case "=":
		return 0.1
	case "!=":
		return 0.9
	case "<", "<=", ">", ">=":
		return 0.3
	case "AND":
		return predSelectivity(be.Left) * predSelectivity(be.Right)
	default:
		return 1.0
	}
}

// predAliases returns the distinct table aliases a predicate references.
func (p *Planner) predAliases(e sql.Expr, schemas map[string]*types.Schema) []string {
	set := map[string]bool{}
	collectAliases(e, schemas, set)
	out := make([]string, 0, len(set))
	for a := range set {
		out = append(out, a)
	}
	sort.Strings(out)
	return out
}

func collectAliases(e sql.Expr, schemas map[string]*types.Schema, set map[string]bool) {
	switch ex := e.(type) {
	case *sql.ColumnRef:
		if ex.Table != "" {
			set[ex.Table] = true
			return
		}
		// unqualified: resolve to the unique table that has this column
		for alias, sch := range schemas {
			if sch.ColIndex(ex.Name) != -1 {
				set[alias] = true
			}
		}
	case *sql.BinaryExpr:
		collectAliases(ex.Left, schemas, set)
		collectAliases(ex.Right, schemas, set)
	}
}

type usedJoins struct{ remaining []sql.Expr }

// pickJoinPreds selects join predicates connecting newAlias to already-joined
// tables, returning their conjunction and the still-unused predicates.
func pickJoinPreds(joins []sql.Expr, joined map[string]bool, newAlias string, schemas map[string]*types.Schema, p *Planner) (sql.Expr, usedJoins) {
	var picked, rest []sql.Expr
	for _, j := range joins {
		aliases := p.predAliases(j, schemas)
		if len(aliases) != 2 {
			rest = append(rest, j)
			continue
		}
		a, b := aliases[0], aliases[1]
		connects := (a == newAlias && joined[b]) || (b == newAlias && joined[a])
		if connects {
			picked = append(picked, j)
		} else {
			rest = append(rest, j)
		}
	}
	return andAll(picked), usedJoins{remaining: rest}
}

// pkEqualityKey returns the literal key if pred is `alias.pk = literal`.
func pkEqualityKey(pred sql.Expr, alias, pkName string) (types.Value, bool) {
	be, ok := pred.(*sql.BinaryExpr)
	if !ok || be.Op != "=" {
		return types.Value{}, false
	}
	if v, ok := matchColLit(be.Left, be.Right, alias, pkName); ok {
		return v, true
	}
	return matchColLit(be.Right, be.Left, alias, pkName)
}

func matchColLit(col, lit sql.Expr, alias, pkName string) (types.Value, bool) {
	cr, ok := col.(*sql.ColumnRef)
	if !ok || cr.Name != pkName || (cr.Table != "" && cr.Table != alias) {
		return types.Value{}, false
	}
	l, ok := lit.(*sql.Literal)
	if !ok {
		return types.Value{}, false
	}
	return l.Value, true
}

func hasAggItems(items []sql.SelectItem) bool {
	for _, it := range items {
		if containsAgg(it.Expr) {
			return true
		}
	}
	return false
}

func containsAgg(e sql.Expr) bool {
	switch ex := e.(type) {
	case *sql.AggCall:
		return true
	case *sql.BinaryExpr:
		return containsAgg(ex.Left) || containsAgg(ex.Right)
	default:
		return false
	}
}

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

func renderExplain(lines []string) string {
	var sb strings.Builder
	sb.WriteString("QUERY PLAN\n")
	for i := len(lines) - 1; i >= 0; i-- {
		sb.WriteString("-> ")
		sb.WriteString(lines[i])
		sb.WriteByte('\n')
	}
	return strings.TrimRight(sb.String(), "\n")
}

func exprString(e sql.Expr) string {
	switch ex := e.(type) {
	case *sql.ColumnRef:
		if ex.Table != "" {
			return ex.Table + "." + ex.Name
		}
		return ex.Name
	case *sql.Literal:
		return ex.Value.String()
	case *sql.BinaryExpr:
		return exprString(ex.Left) + " " + ex.Op + " " + exprString(ex.Right)
	case *sql.AggCall:
		if ex.Star {
			return ex.Func + "(*)"
		}
		return ex.Func + "(" + exprString(ex.Arg) + ")"
	default:
		return "?"
	}
}
