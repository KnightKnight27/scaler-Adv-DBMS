// Package sql turns SQL text into an abstract syntax tree. It contains a
// hand-written lexer and a recursive-descent parser supporting the subset of
// SQL MiniDB executes: CREATE TABLE / CREATE INDEX, INSERT, SELECT (with WHERE,
// a single JOIN and COUNT(*)), DELETE, EXPLAIN, and the transaction control
// statements BEGIN / COMMIT / ROLLBACK.
package sql

import "minidb/internal/types"

// Statement is implemented by every top-level parsed statement.
type Statement interface{ stmt() }

// Expr is implemented by every expression node used in WHERE clauses and INSERT
// value lists.
type Expr interface{ expr() }

// ColumnRef is a (possibly table-qualified) column reference such as `t.id`.
type ColumnRef struct {
	Table string // optional qualifier; "" when unqualified
	Name  string
}

// Literal is a constant value.
type Literal struct{ Value types.Value }

// BinaryExpr models comparisons (=, !=, <, <=, >, >=) and the logical AND used
// to chain predicates. Keeping the grammar to AND-of-comparisons keeps the
// optimizer's selectivity model tractable while still being genuinely useful.
type BinaryExpr struct {
	Op    string
	Left  Expr
	Right Expr
}

func (ColumnRef) expr()  {}
func (Literal) expr()    {}
func (BinaryExpr) expr() {}

// ColumnDef is one column in a CREATE TABLE statement.
type ColumnDef struct {
	Name       string
	Type       types.ColumnType
	PrimaryKey bool
}

// CreateTable defines a new relation.
type CreateTable struct {
	Table   string
	Columns []ColumnDef
}

// CreateIndex builds a secondary index on Table(Column).
type CreateIndex struct {
	Table  string
	Column string
}

// Insert adds one or more rows. Columns may be empty, meaning "all columns in
// declaration order".
type Insert struct {
	Table   string
	Columns []string
	Rows    [][]Expr
}

// ResultColumn is one item in a SELECT list: either *, COUNT(*), or a column.
type ResultColumn struct {
	Star    bool
	IsCount bool
	Column  ColumnRef
}

// JoinClause is a single inner join on an equality predicate.
type JoinClause struct {
	Table string
	Alias string
	Left  ColumnRef
	Right ColumnRef
}

// Select is a projection over a table, an optional join, and an optional filter.
type Select struct {
	Items     []ResultColumn
	From      string
	FromAlias string
	Join      *JoinClause
	Where     Expr
}

// Delete removes rows matching Where (or all rows when Where is nil).
type Delete struct {
	Table string
	Where Expr
}

// Explain wraps a SELECT and asks the engine to print the chosen plan rather
// than execute it.
type Explain struct{ Select *Select }

// Transaction control statements.
type Begin struct{}
type Commit struct{}
type Rollback struct{}

func (*CreateTable) stmt() {}
func (*CreateIndex) stmt() {}
func (*Insert) stmt()      {}
func (*Select) stmt()      {}
func (*Delete) stmt()      {}
func (*Explain) stmt()     {}
func (*Begin) stmt()       {}
func (*Commit) stmt()      {}
func (*Rollback) stmt()    {}
