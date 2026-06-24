// Package types defines MiniDB's value system and row/schema encoding.
//
// Trade-off: we support exactly two column types, INT (int64) and TEXT (string),
// plus SQL NULL. Two types are enough to demonstrate schemas, comparisons,
// indexing and joins while keeping serialization small and fully explainable.
package types

import (
	"bytes"
	"encoding/binary"
	"fmt"
	"strconv"
	"strings"
)

// Type is a column/value type tag.
type Type uint8

const (
	TypeInt  Type = iota // int64
	TypeText             // string
)

func (t Type) String() string {
	switch t {
	case TypeInt:
		return "INT"
	case TypeText:
		return "TEXT"
	default:
		return "UNKNOWN"
	}
}

// ParseType maps a SQL type keyword to a Type.
func ParseType(s string) (Type, error) {
	switch s {
	case "INT", "INTEGER", "BIGINT":
		return TypeInt, nil
	case "TEXT", "STRING", "VARCHAR":
		return TypeText, nil
	default:
		return 0, fmt.Errorf("unknown column type %q", s)
	}
}

// Value is a single typed value (or NULL).
type Value struct {
	Type Type
	Int  int64
	Str  string
	Null bool
}

func NewInt(v int64) Value   { return Value{Type: TypeInt, Int: v} }
func NewText(v string) Value { return Value{Type: TypeText, Str: v} }
func NewNull(t Type) Value   { return Value{Type: t, Null: true} }

func (v Value) String() string {
	if v.Null {
		return "NULL"
	}
	switch v.Type {
	case TypeInt:
		return strconv.FormatInt(v.Int, 10)
	default:
		return v.Str
	}
}

// Compare orders two values of the same type. NULLs sort before non-NULLs.
// Returns -1, 0, or +1.
func (v Value) Compare(o Value) int {
	switch {
	case v.Null && o.Null:
		return 0
	case v.Null:
		return -1
	case o.Null:
		return 1
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
	default:
		return strings.Compare(v.Str, o.Str)
	}
}

// Column is a named, typed column.
type Column struct {
	Name string
	Type Type
}

// Schema describes a table's columns and which column is the primary key.
type Schema struct {
	Columns []Column
	PKIndex int // index into Columns of the primary key
}

// ColIndex returns the position of a column by name, or -1.
func (s *Schema) ColIndex(name string) int {
	for i, c := range s.Columns {
		if c.Name == name {
			return i
		}
	}
	return -1
}

// PKColumn returns the primary-key column.
func (s *Schema) PKColumn() Column { return s.Columns[s.PKIndex] }

// Row is an ordered list of values matching a schema's columns.
type Row []Value

// PK returns the primary-key value of a row for the given schema.
func (r Row) PK(s *Schema) Value { return r[s.PKIndex] }

// Encode serializes a row to bytes: for each value a 1-byte tag, then payload.
// INT -> 8 bytes big-endian; TEXT -> 4-byte length + UTF-8 bytes; a null bit
// rides in the high bit of the tag byte.
func (r Row) Encode() []byte {
	var buf bytes.Buffer
	for _, v := range r {
		tag := byte(v.Type)
		if v.Null {
			tag |= 0x80
		}
		buf.WriteByte(tag)
		if v.Null {
			continue
		}
		switch v.Type {
		case TypeInt:
			var b [8]byte
			binary.BigEndian.PutUint64(b[:], uint64(v.Int))
			buf.Write(b[:])
		case TypeText:
			var b [4]byte
			binary.BigEndian.PutUint32(b[:], uint32(len(v.Str)))
			buf.Write(b[:])
			buf.WriteString(v.Str)
		}
	}
	return buf.Bytes()
}

// DecodeRow reverses Encode for a known schema.
func DecodeRow(s *Schema, data []byte) (Row, error) {
	row := make(Row, len(s.Columns))
	off := 0
	for i := range s.Columns {
		if off >= len(data) {
			return nil, fmt.Errorf("row decode: truncated at column %d", i)
		}
		tag := data[off]
		off++
		null := tag&0x80 != 0
		t := Type(tag & 0x7f)
		if null {
			row[i] = Value{Type: t, Null: true}
			continue
		}
		switch t {
		case TypeInt:
			if off+8 > len(data) {
				return nil, fmt.Errorf("row decode: truncated int at column %d", i)
			}
			row[i] = NewInt(int64(binary.BigEndian.Uint64(data[off : off+8])))
			off += 8
		case TypeText:
			if off+4 > len(data) {
				return nil, fmt.Errorf("row decode: truncated text length at column %d", i)
			}
			n := int(binary.BigEndian.Uint32(data[off : off+4]))
			off += 4
			if off+n > len(data) {
				return nil, fmt.Errorf("row decode: truncated text body at column %d", i)
			}
			row[i] = NewText(string(data[off : off+n]))
			off += n
		default:
			return nil, fmt.Errorf("row decode: bad type tag %d", t)
		}
	}
	return row, nil
}

// EncodeKey serializes a single value into an order-preserving-ish key for the
// B+Tree. INT uses a sign-flipped big-endian encoding so byte order matches
// numeric order; TEXT uses raw UTF-8 bytes.
func EncodeKey(v Value) []byte {
	switch v.Type {
	case TypeInt:
		var b [8]byte
		// flip sign bit so negative numbers sort before positive ones
		binary.BigEndian.PutUint64(b[:], uint64(v.Int)^(1<<63))
		return b[:]
	default:
		return []byte(v.Str)
	}
}
