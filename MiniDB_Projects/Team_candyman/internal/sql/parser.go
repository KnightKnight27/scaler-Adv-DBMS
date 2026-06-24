package sql

import (
	"fmt"
	"strconv"

	"minidb/internal/types"
)

// Parse parses a single SQL statement (without a trailing semicolon).
func Parse(input string) (Statement, error) {
	toks, err := newLexer(input).tokens()
	if err != nil {
		return nil, err
	}
	p := &parser{toks: toks}
	stmt, err := p.parseStatement()
	if err != nil {
		return nil, err
	}
	if p.peek().kind != tEOF {
		return nil, fmt.Errorf("sql: unexpected token %q after statement", p.peek().text)
	}
	return stmt, nil
}

type parser struct {
	toks []token
	pos  int
}

func (p *parser) peek() token { return p.toks[p.pos] }
func (p *parser) advance() token {
	t := p.toks[p.pos]
	if p.pos < len(p.toks)-1 {
		p.pos++
	}
	return t
}
func (p *parser) acceptKeyword(kw string) bool {
	if t := p.peek(); t.kind == tKeyword && t.text == kw {
		p.advance()
		return true
	}
	return false
}
func (p *parser) expectKeyword(kw string) error {
	if !p.acceptKeyword(kw) {
		return fmt.Errorf("sql: expected %s, got %q", kw, p.peek().text)
	}
	return nil
}
func (p *parser) acceptPunct(s string) bool {
	if t := p.peek(); t.kind == tPunct && t.text == s {
		p.advance()
		return true
	}
	return false
}
func (p *parser) expectPunct(s string) error {
	if !p.acceptPunct(s) {
		return fmt.Errorf("sql: expected %q, got %q", s, p.peek().text)
	}
	return nil
}
func (p *parser) expectIdent() (string, error) {
	t := p.peek()
	if t.kind != tIdent {
		return "", fmt.Errorf("sql: expected identifier, got %q", t.text)
	}
	p.advance()
	return t.text, nil
}

func (p *parser) parseStatement() (Statement, error) {
	t := p.peek()
	if t.kind != tKeyword {
		return nil, fmt.Errorf("sql: expected a statement keyword, got %q", t.text)
	}
	switch t.text {
	case "CREATE":
		return p.parseCreate()
	case "INSERT":
		return p.parseInsert()
	case "DELETE":
		return p.parseDelete()
	case "SELECT", "EXPLAIN":
		return p.parseSelect()
	case "BEGIN":
		p.advance()
		return &Begin{}, nil
	case "COMMIT":
		p.advance()
		return &Commit{}, nil
	case "ROLLBACK":
		p.advance()
		return &Rollback{}, nil
	default:
		return nil, fmt.Errorf("sql: unsupported statement %q", t.text)
	}
}

func (p *parser) parseCreate() (Statement, error) {
	p.advance() // CREATE
	if err := p.expectKeyword("TABLE"); err != nil {
		return nil, err
	}
	name, err := p.expectIdent()
	if err != nil {
		return nil, err
	}
	if err := p.expectPunct("("); err != nil {
		return nil, err
	}
	ct := &CreateTable{Table: name}
	seenPK := false
	for {
		col, err := p.expectIdent()
		if err != nil {
			return nil, err
		}
		tk := p.peek()
		if tk.kind != tKeyword {
			return nil, fmt.Errorf("sql: expected column type, got %q", tk.text)
		}
		colType, err := types.ParseType(tk.text)
		if err != nil {
			return nil, err
		}
		p.advance()
		def := ColumnDef{Name: col, Type: colType}
		if p.acceptKeyword("PRIMARY") {
			if err := p.expectKeyword("KEY"); err != nil {
				return nil, err
			}
			def.PrimaryKey = true
			seenPK = true
		}
		ct.Columns = append(ct.Columns, def)
		if p.acceptPunct(",") {
			continue
		}
		break
	}
	if err := p.expectPunct(")"); err != nil {
		return nil, err
	}
	if !seenPK {
		return nil, fmt.Errorf("sql: CREATE TABLE %s requires a PRIMARY KEY column", name)
	}
	return ct, nil
}

func (p *parser) parseInsert() (Statement, error) {
	p.advance() // INSERT
	if err := p.expectKeyword("INTO"); err != nil {
		return nil, err
	}
	name, err := p.expectIdent()
	if err != nil {
		return nil, err
	}
	ins := &Insert{Table: name}
	if p.acceptPunct("(") {
		for {
			col, err := p.expectIdent()
			if err != nil {
				return nil, err
			}
			ins.Columns = append(ins.Columns, col)
			if p.acceptPunct(",") {
				continue
			}
			break
		}
		if err := p.expectPunct(")"); err != nil {
			return nil, err
		}
	}
	if err := p.expectKeyword("VALUES"); err != nil {
		return nil, err
	}
	for {
		if err := p.expectPunct("("); err != nil {
			return nil, err
		}
		var row []Expr
		for {
			e, err := p.parsePrimary()
			if err != nil {
				return nil, err
			}
			row = append(row, e)
			if p.acceptPunct(",") {
				continue
			}
			break
		}
		if err := p.expectPunct(")"); err != nil {
			return nil, err
		}
		ins.Rows = append(ins.Rows, row)
		if p.acceptPunct(",") {
			continue
		}
		break
	}
	return ins, nil
}

func (p *parser) parseDelete() (Statement, error) {
	p.advance() // DELETE
	if err := p.expectKeyword("FROM"); err != nil {
		return nil, err
	}
	name, err := p.expectIdent()
	if err != nil {
		return nil, err
	}
	del := &Delete{Table: name}
	if p.acceptKeyword("WHERE") {
		w, err := p.parseExpr()
		if err != nil {
			return nil, err
		}
		del.Where = w
	}
	return del, nil
}

func (p *parser) parseSelect() (Statement, error) {
	sel := &Select{}
	if p.acceptKeyword("EXPLAIN") {
		sel.Explain = true
	}
	if err := p.expectKeyword("SELECT"); err != nil {
		return nil, err
	}
	if p.acceptPunct("*") {
		sel.Star = true
	} else {
		for {
			e, err := p.parseExpr()
			if err != nil {
				return nil, err
			}
			item := SelectItem{Expr: e}
			if p.acceptKeyword("AS") {
				alias, err := p.expectIdent()
				if err != nil {
					return nil, err
				}
				item.Alias = alias
			}
			sel.Items = append(sel.Items, item)
			if p.acceptPunct(",") {
				continue
			}
			break
		}
	}
	if err := p.expectKeyword("FROM"); err != nil {
		return nil, err
	}
	// table list with optional JOIN ... ON predicates
	tr, err := p.parseTableRef()
	if err != nil {
		return nil, err
	}
	sel.Tables = append(sel.Tables, tr)
	for {
		if p.acceptPunct(",") {
			tr, err := p.parseTableRef()
			if err != nil {
				return nil, err
			}
			sel.Tables = append(sel.Tables, tr)
			continue
		}
		if p.acceptKeyword("JOIN") {
			tr, err := p.parseTableRef()
			if err != nil {
				return nil, err
			}
			sel.Tables = append(sel.Tables, tr)
			if err := p.expectKeyword("ON"); err != nil {
				return nil, err
			}
			cond, err := p.parseExpr()
			if err != nil {
				return nil, err
			}
			sel.Predicates = append(sel.Predicates, cond)
			continue
		}
		break
	}
	if p.acceptKeyword("WHERE") {
		w, err := p.parseExpr()
		if err != nil {
			return nil, err
		}
		sel.Predicates = append(sel.Predicates, splitAnd(w)...)
	}
	if p.acceptKeyword("GROUP") {
		if err := p.expectKeyword("BY"); err != nil {
			return nil, err
		}
		for {
			e, err := p.parsePrimary()
			if err != nil {
				return nil, err
			}
			sel.GroupBy = append(sel.GroupBy, e)
			if p.acceptPunct(",") {
				continue
			}
			break
		}
	}
	return sel, nil
}

func (p *parser) parseTableRef() (TableRef, error) {
	name, err := p.expectIdent()
	if err != nil {
		return TableRef{}, err
	}
	ref := TableRef{Name: name, Alias: name}
	if p.acceptKeyword("AS") {
		alias, err := p.expectIdent()
		if err != nil {
			return TableRef{}, err
		}
		ref.Alias = alias
	} else if t := p.peek(); t.kind == tIdent {
		ref.Alias = t.text
		p.advance()
	}
	return ref, nil
}

// parseExpr parses AND-connected comparisons.
func (p *parser) parseExpr() (Expr, error) {
	left, err := p.parseComparison()
	if err != nil {
		return nil, err
	}
	for p.acceptKeyword("AND") {
		right, err := p.parseComparison()
		if err != nil {
			return nil, err
		}
		left = &BinaryExpr{Op: "AND", Left: left, Right: right}
	}
	return left, nil
}

func (p *parser) parseComparison() (Expr, error) {
	left, err := p.parsePrimary()
	if err != nil {
		return nil, err
	}
	if t := p.peek(); t.kind == tOp {
		p.advance()
		right, err := p.parsePrimary()
		if err != nil {
			return nil, err
		}
		op := t.text
		if op == "<>" {
			op = "!="
		}
		return &BinaryExpr{Op: op, Left: left, Right: right}, nil
	}
	return left, nil
}

func (p *parser) parsePrimary() (Expr, error) {
	t := p.peek()
	switch t.kind {
	case tNumber:
		p.advance()
		n, err := strconv.ParseInt(t.text, 10, 64)
		if err != nil {
			return nil, fmt.Errorf("sql: bad integer %q", t.text)
		}
		return &Literal{Value: types.NewInt(n)}, nil
	case tString:
		p.advance()
		return &Literal{Value: types.NewText(t.text)}, nil
	case tKeyword:
		switch t.text {
		case "NULL":
			p.advance()
			return &Literal{Value: types.Value{Null: true}}, nil
		case "COUNT", "SUM", "AVG", "MIN", "MAX":
			return p.parseAgg()
		}
		return nil, fmt.Errorf("sql: unexpected keyword %q in expression", t.text)
	case tIdent:
		p.advance()
		ref := &ColumnRef{Name: t.text}
		if p.acceptPunct(".") {
			col, err := p.expectIdent()
			if err != nil {
				return nil, err
			}
			ref.Table = t.text
			ref.Name = col
		}
		return ref, nil
	case tPunct:
		if t.text == "(" {
			p.advance()
			e, err := p.parseExpr()
			if err != nil {
				return nil, err
			}
			if err := p.expectPunct(")"); err != nil {
				return nil, err
			}
			return e, nil
		}
	}
	return nil, fmt.Errorf("sql: unexpected token %q in expression", t.text)
}

func (p *parser) parseAgg() (Expr, error) {
	fn := p.advance().text // COUNT/SUM/...
	if err := p.expectPunct("("); err != nil {
		return nil, err
	}
	agg := &AggCall{Func: fn}
	if p.acceptPunct("*") {
		agg.Star = true
	} else {
		arg, err := p.parseExpr()
		if err != nil {
			return nil, err
		}
		agg.Arg = arg
	}
	if err := p.expectPunct(")"); err != nil {
		return nil, err
	}
	return agg, nil
}

// splitAnd flattens an AND tree into a list of conjuncts.
func splitAnd(e Expr) []Expr {
	be, ok := e.(*BinaryExpr)
	if ok && be.Op == "AND" {
		return append(splitAnd(be.Left), splitAnd(be.Right)...)
	}
	return []Expr{e}
}
