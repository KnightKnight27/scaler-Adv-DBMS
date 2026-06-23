package sql

import (
	"fmt"
	"strconv"

	"minidb/internal/types"
)

// Parse lexes and parses a single SQL statement. A trailing semicolon is
// optional.
func Parse(src string) (Statement, error) {
	toks, err := tokenize(src)
	if err != nil {
		return nil, err
	}
	p := &parser{toks: toks}
	stmt, err := p.parseStatement()
	if err != nil {
		return nil, err
	}
	p.acceptPunct(";")
	if p.cur().kind != tEOF {
		return nil, fmt.Errorf("sql: unexpected trailing input near %q", p.cur().text)
	}
	return stmt, nil
}

type parser struct {
	toks []token
	pos  int
}

func (p *parser) cur() token { return p.toks[p.pos] }
func (p *parser) advance() token {
	t := p.toks[p.pos]
	if p.pos < len(p.toks)-1 {
		p.pos++
	}
	return t
}

func (p *parser) isKeyword(kw string) bool {
	return p.cur().kind == tKeyword && p.cur().text == kw
}

func (p *parser) acceptKeyword(kw string) bool {
	if p.isKeyword(kw) {
		p.advance()
		return true
	}
	return false
}

func (p *parser) expectKeyword(kw string) error {
	if !p.acceptKeyword(kw) {
		return fmt.Errorf("sql: expected %s but found %q", kw, p.cur().text)
	}
	return nil
}

func (p *parser) acceptPunct(s string) bool {
	if p.cur().kind == tPunct && p.cur().text == s {
		p.advance()
		return true
	}
	return false
}

func (p *parser) expectPunct(s string) error {
	if !p.acceptPunct(s) {
		return fmt.Errorf("sql: expected %q but found %q", s, p.cur().text)
	}
	return nil
}

func (p *parser) expectIdent() (string, error) {
	if p.cur().kind != tIdent {
		return "", fmt.Errorf("sql: expected identifier but found %q", p.cur().text)
	}
	return p.advance().text, nil
}

func (p *parser) parseStatement() (Statement, error) {
	switch {
	case p.isKeyword("CREATE"):
		return p.parseCreate()
	case p.isKeyword("INSERT"):
		return p.parseInsert()
	case p.isKeyword("SELECT"):
		return p.parseSelect()
	case p.isKeyword("DELETE"):
		return p.parseDelete()
	case p.isKeyword("EXPLAIN"):
		return p.parseExplain()
	case p.acceptKeyword("BEGIN"):
		return &Begin{}, nil
	case p.acceptKeyword("COMMIT"):
		return &Commit{}, nil
	case p.isKeyword("ROLLBACK") || p.isKeyword("ABORT"):
		p.advance()
		return &Rollback{}, nil
	default:
		return nil, fmt.Errorf("sql: unsupported statement starting with %q", p.cur().text)
	}
}

func (p *parser) parseCreate() (Statement, error) {
	p.advance() // CREATE
	if p.acceptKeyword("TABLE") {
		return p.parseCreateTable()
	}
	if p.acceptKeyword("INDEX") {
		return p.parseCreateIndex()
	}
	return nil, fmt.Errorf("sql: expected TABLE or INDEX after CREATE, found %q", p.cur().text)
}

func (p *parser) parseCreateTable() (Statement, error) {
	name, err := p.expectIdent()
	if err != nil {
		return nil, err
	}
	if err := p.expectPunct("("); err != nil {
		return nil, err
	}
	ct := &CreateTable{Table: name}
	for {
		colName, err := p.expectIdent()
		if err != nil {
			return nil, err
		}
		colType, err := p.parseColumnType()
		if err != nil {
			return nil, err
		}
		def := ColumnDef{Name: colName, Type: colType}
		if p.acceptKeyword("PRIMARY") {
			if err := p.expectKeyword("KEY"); err != nil {
				return nil, err
			}
			def.PrimaryKey = true
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
	return ct, nil
}

func (p *parser) parseColumnType() (types.ColumnType, error) {
	if p.cur().kind != tKeyword {
		return 0, fmt.Errorf("sql: expected column type but found %q", p.cur().text)
	}
	switch p.advance().text {
	case "INT", "INTEGER":
		return types.TypeInt, nil
	case "TEXT":
		return types.TypeText, nil
	case "BOOL", "BOOLEAN":
		return types.TypeBool, nil
	default:
		return 0, fmt.Errorf("sql: unknown column type")
	}
}

func (p *parser) parseCreateIndex() (Statement, error) {
	if err := p.expectKeyword("ON"); err != nil {
		return nil, err
	}
	table, err := p.expectIdent()
	if err != nil {
		return nil, err
	}
	if err := p.expectPunct("("); err != nil {
		return nil, err
	}
	col, err := p.expectIdent()
	if err != nil {
		return nil, err
	}
	if err := p.expectPunct(")"); err != nil {
		return nil, err
	}
	return &CreateIndex{Table: table, Column: col}, nil
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
			lit, err := p.parseLiteral()
			if err != nil {
				return nil, err
			}
			row = append(row, lit)
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

func (p *parser) parseSelect() (Statement, error) {
	p.advance() // SELECT
	sel := &Select{}
	if p.cur().kind == tStar {
		p.advance()
		sel.Items = append(sel.Items, ResultColumn{Star: true})
	} else {
		for {
			item, err := p.parseResultColumn()
			if err != nil {
				return nil, err
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
	from, err := p.expectIdent()
	if err != nil {
		return nil, err
	}
	sel.From = from
	sel.FromAlias = p.parseOptionalAlias()

	if p.isKeyword("JOIN") || p.isKeyword("INNER") {
		join, err := p.parseJoin()
		if err != nil {
			return nil, err
		}
		sel.Join = join
	}

	if p.acceptKeyword("WHERE") {
		where, err := p.parseExpr()
		if err != nil {
			return nil, err
		}
		sel.Where = where
	}
	return sel, nil
}

func (p *parser) parseResultColumn() (ResultColumn, error) {
	if p.acceptKeyword("COUNT") {
		if err := p.expectPunct("("); err != nil {
			return ResultColumn{}, err
		}
		if p.cur().kind != tStar {
			return ResultColumn{}, fmt.Errorf("sql: only COUNT(*) is supported")
		}
		p.advance()
		if err := p.expectPunct(")"); err != nil {
			return ResultColumn{}, err
		}
		return ResultColumn{IsCount: true}, nil
	}
	col, err := p.parseColumnRef()
	if err != nil {
		return ResultColumn{}, err
	}
	return ResultColumn{Column: col}, nil
}

func (p *parser) parseOptionalAlias() string {
	if p.acceptKeyword("AS") {
		name, _ := p.expectIdent()
		return name
	}
	// A bare identifier following a table name is treated as an alias.
	if p.cur().kind == tIdent {
		return p.advance().text
	}
	return ""
}

func (p *parser) parseJoin() (*JoinClause, error) {
	p.acceptKeyword("INNER")
	if err := p.expectKeyword("JOIN"); err != nil {
		return nil, err
	}
	table, err := p.expectIdent()
	if err != nil {
		return nil, err
	}
	j := &JoinClause{Table: table}
	j.Alias = p.parseOptionalAlias()
	if err := p.expectKeyword("ON"); err != nil {
		return nil, err
	}
	left, err := p.parseColumnRef()
	if err != nil {
		return nil, err
	}
	if p.cur().kind != tOp || p.cur().text != "=" {
		return nil, fmt.Errorf("sql: JOIN currently supports only equality conditions")
	}
	p.advance()
	right, err := p.parseColumnRef()
	if err != nil {
		return nil, err
	}
	j.Left, j.Right = left, right
	return j, nil
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
		where, err := p.parseExpr()
		if err != nil {
			return nil, err
		}
		del.Where = where
	}
	return del, nil
}

func (p *parser) parseExplain() (Statement, error) {
	p.advance() // EXPLAIN
	if !p.isKeyword("SELECT") {
		return nil, fmt.Errorf("sql: EXPLAIN currently supports only SELECT")
	}
	sel, err := p.parseSelect()
	if err != nil {
		return nil, err
	}
	return &Explain{Select: sel.(*Select)}, nil
}

// parseExpr parses a chain of comparison predicates joined by AND.
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
		left = BinaryExpr{Op: "AND", Left: left, Right: right}
	}
	return left, nil
}

func (p *parser) parseComparison() (Expr, error) {
	left, err := p.parseOperand()
	if err != nil {
		return nil, err
	}
	if p.cur().kind != tOp {
		return nil, fmt.Errorf("sql: expected comparison operator but found %q", p.cur().text)
	}
	op := p.advance().text
	right, err := p.parseOperand()
	if err != nil {
		return nil, err
	}
	return BinaryExpr{Op: op, Left: left, Right: right}, nil
}

func (p *parser) parseOperand() (Expr, error) {
	if p.cur().kind == tIdent {
		return p.parseColumnRef()
	}
	return p.parseLiteral()
}

func (p *parser) parseColumnRef() (ColumnRef, error) {
	first, err := p.expectIdent()
	if err != nil {
		return ColumnRef{}, err
	}
	if p.acceptPunct(".") {
		second, err := p.expectIdent()
		if err != nil {
			return ColumnRef{}, err
		}
		return ColumnRef{Table: first, Name: second}, nil
	}
	return ColumnRef{Name: first}, nil
}

func (p *parser) parseLiteral() (Expr, error) {
	switch p.cur().kind {
	case tNumber:
		text := p.advance().text
		n, err := strconv.ParseInt(text, 10, 64)
		if err != nil {
			return nil, fmt.Errorf("sql: invalid integer literal %q", text)
		}
		return Literal{Value: types.NewInt(n)}, nil
	case tString:
		return Literal{Value: types.NewText(p.advance().text)}, nil
	case tKeyword:
		switch p.cur().text {
		case "TRUE":
			p.advance()
			return Literal{Value: types.NewBool(true)}, nil
		case "FALSE":
			p.advance()
			return Literal{Value: types.NewBool(false)}, nil
		}
	}
	return nil, fmt.Errorf("sql: expected a literal value but found %q", p.cur().text)
}
