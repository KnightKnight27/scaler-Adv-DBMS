// Package types defines the value and row primitives shared by every layer of
// MiniDB. Keeping these in a dependency-free leaf package lets the storage,
// indexing, planning and execution layers agree on a single representation of a
// tuple without creating import cycles.
package types

import (
	"fmt"
	"strconv"
	"strings"
)

// ColumnType enumerates the scalar types MiniDB understands. The set is
// deliberately small; the focus of the project is engine internals rather than
// a rich type system.
type ColumnType uint8

const (
	TypeInt ColumnType = iota
	TypeText
	TypeBool
)

func (t ColumnType) String() string {
	switch t {
	case TypeInt:
		return "INT"
	case TypeText:
		return "TEXT"
	case TypeBool:
		return "BOOL"
	default:
		return "UNKNOWN"
	}
}

// Value is a tagged union representing a single column value. A struct (rather
// than an interface) is used so that values are cheap to copy and trivial to
// serialise with encoding/binary in the storage layer.
type Value struct {
	Type ColumnType
	Int  int64
	Str  string
	Bool bool
	Null bool
}

// NewInt, NewText and NewBool are the canonical constructors.
func NewInt(v int64) Value   { return Value{Type: TypeInt, Int: v} }
func NewText(v string) Value { return Value{Type: TypeText, Str: v} }
func NewBool(v bool) Value   { return Value{Type: TypeBool, Bool: v} }
func NewNull(t ColumnType) Value {
	return Value{Type: t, Null: true}
}

// String renders a value for the REPL and EXPLAIN output.
func (v Value) String() string {
	if v.Null {
		return "NULL"
	}
	switch v.Type {
	case TypeInt:
		return strconv.FormatInt(v.Int, 10)
	case TypeText:
		return v.Str
	case TypeBool:
		if v.Bool {
			return "true"
		}
		return "false"
	default:
		return "?"
	}
}

// Compare returns -1, 0 or 1 following the usual ordering convention. NULLs sort
// before any non-null value, which keeps index ordering deterministic. Comparing
// values of different types is a programming error and panics, surfacing schema
// bugs early rather than silently mis-ordering data.
func (v Value) Compare(o Value) int {
	switch {
	case v.Null && o.Null:
		return 0
	case v.Null:
		return -1
	case o.Null:
		return 1
	}
	if v.Type != o.Type {
		panic(fmt.Sprintf("type mismatch in comparison: %s vs %s", v.Type, o.Type))
	}
	switch v.Type {
	case TypeInt:
		switch {
		case v.Int < o.Int:
			return -1
		case v.Int > o.Int:
			return 1
		default:
			return 0
		}
	case TypeText:
		return strings.Compare(v.Str, o.Str)
	case TypeBool:
		switch {
		case v.Bool == o.Bool:
			return 0
		case !v.Bool:
			return -1
		default:
			return 1
		}
	default:
		return 0
	}
}

// Equal is a convenience wrapper over Compare.
func (v Value) Equal(o Value) bool { return v.Compare(o) == 0 }

// Row is an ordered list of column values. Row positions correspond to the
// column order declared in the catalog for the owning table.
type Row []Value

// String renders a row as a comma separated list, used by the REPL.
func (r Row) String() string {
	parts := make([]string, len(r))
	for i, v := range r {
		parts[i] = v.String()
	}
	return strings.Join(parts, ", ")
}

// Clone returns a deep copy of the row. Values are value types, so a shallow
// copy of the slice is already a deep copy of the contents.
func (r Row) Clone() Row {
	out := make(Row, len(r))
	copy(out, r)
	return out
}
