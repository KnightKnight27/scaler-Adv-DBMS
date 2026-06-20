// Package sql implements MiniDB's SQL front end: a hand-written lexer and a
// recursive-descent parser producing a small AST. The supported grammar is
// deliberately compact (CREATE TABLE, INSERT, DELETE, SELECT with WHERE/JOIN/
// GROUP BY, and BEGIN/COMMIT/ROLLBACK) so every production is explainable.
package sql

import "minidb/internal/types"

// Statement is any top-level SQL statement.
type Statement interface{ stmt() }

// ColumnDef is a column declaration in CREATE TABLE.
type ColumnDef struct {
	Name       string
	Type       types.Type
	PrimaryKey bool
}

// CreateTable is `CREATE TABLE name (cols...)`.
type CreateTable struct {
	Table   string
	Columns []ColumnDef
}

// Insert is `INSERT INTO name [(cols)] VALUES (...) [, (...)]`.
type Insert struct {
	Table   string
	Columns []string // optional explicit column list; empty means all, in order
	Rows    [][]Expr
}

// Delete is `DELETE FROM name [WHERE expr]`.
type Delete struct {
	Table string
	Where Expr
}

// TableRef names a table with an optional alias.
type TableRef struct {
	Name  string
	Alias string // defaults to Name
}

// SelectItem is one projected expression with an optional output name.
type SelectItem struct {
	Expr  Expr
	Alias string
}

// Select is the query statement. Star is true for `SELECT *`. All join and where
// predicates are collected into Predicates (ANDed); the planner decides join order.
type Select struct {
	Star       bool
	Items      []SelectItem
	Tables     []TableRef
	Predicates []Expr
	GroupBy    []Expr
	Explain    bool // EXPLAIN prefix: print the chosen plan instead of running it
}

// Begin / Commit / Rollback control explicit transactions.
type Begin struct{}
type Commit struct{}
type Rollback struct{}

func (*CreateTable) stmt() {}
func (*Insert) stmt()      {}
func (*Delete) stmt()      {}
func (*Select) stmt()      {}
func (*Begin) stmt()       {}
func (*Commit) stmt()      {}
func (*Rollback) stmt()    {}

// Expr is an expression node.
type Expr interface{ expr() }

// ColumnRef references a column, optionally qualified by a table/alias.
type ColumnRef struct {
	Table string // optional qualifier
	Name  string
}

// Literal is a constant value.
type Literal struct{ Value types.Value }

// BinaryExpr covers comparisons (=, !=, <, <=, >, >=) and the AND connective.
type BinaryExpr struct {
	Op    string
	Left  Expr
	Right Expr
}

// AggCall is an aggregate function call, e.g. COUNT(*), SUM(col).
type AggCall struct {
	Func string // COUNT, SUM, AVG, MIN, MAX
	Star bool    // COUNT(*)
	Arg  Expr    // argument when not star
}

func (*ColumnRef) expr()  {}
func (*Literal) expr()    {}
func (*BinaryExpr) expr() {}
func (*AggCall) expr()    {}
