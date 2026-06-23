// Package catalog holds table metadata (schemas) and owns the encoding of rows
// into the opaque byte tuples that the storage engine persists. Centralising the
// encoding here means the storage layer never needs to understand column types,
// and every other layer agrees on a single on-disk record format.
package catalog

import (
	"encoding/binary"
	"encoding/json"
	"fmt"
	"os"
	"sync"

	"minidb/internal/types"
)

// Column is a named, typed attribute of a table.
type Column struct {
	Name string           `json:"name"`
	Type types.ColumnType `json:"type"`
}

// Table describes one relation: its columns and which column is the primary
// key. The primary key column is automatically indexed by the engine.
type Table struct {
	Name      string   `json:"name"`
	Columns   []Column `json:"columns"`
	PKColumn  int      `json:"pk_column"`  // index into Columns; -1 if none
	HasSecond bool     `json:"has_second"` // whether a secondary index exists
	SecondCol int      `json:"second_col"` // column index for the secondary index
}

// ColumnIndex returns the position of the named column, or -1 if absent.
func (t *Table) ColumnIndex(name string) int {
	for i, c := range t.Columns {
		if c.Name == name {
			return i
		}
	}
	return -1
}

// EncodeRow serialises a row into the heap-file tuple format. The layout is a
// sequence of per-column fields:
//
//	INT  : 1 null byte + 8 bytes little-endian
//	BOOL : 1 null byte + 1 byte
//	TEXT : 1 null byte + 4 byte length + raw UTF-8 bytes
//
// A leading null byte per column keeps the format self-describing for NULLs
// without a separate null bitmap, which is plenty for MiniDB's scale.
func (t *Table) EncodeRow(row types.Row) []byte {
	var buf []byte
	for i, col := range t.Columns {
		v := row[i]
		if v.Null {
			buf = append(buf, 1)
		} else {
			buf = append(buf, 0)
		}
		switch col.Type {
		case types.TypeInt:
			var b [8]byte
			binary.LittleEndian.PutUint64(b[:], uint64(v.Int))
			buf = append(buf, b[:]...)
		case types.TypeBool:
			if v.Bool {
				buf = append(buf, 1)
			} else {
				buf = append(buf, 0)
			}
		case types.TypeText:
			var b [4]byte
			binary.LittleEndian.PutUint32(b[:], uint32(len(v.Str)))
			buf = append(buf, b[:]...)
			buf = append(buf, []byte(v.Str)...)
		}
	}
	return buf
}

// DecodeRow reverses EncodeRow using the table schema to drive parsing.
func (t *Table) DecodeRow(b []byte) types.Row {
	row := make(types.Row, len(t.Columns))
	pos := 0
	for i, col := range t.Columns {
		isNull := b[pos] == 1
		pos++
		switch col.Type {
		case types.TypeInt:
			v := int64(binary.LittleEndian.Uint64(b[pos:]))
			pos += 8
			row[i] = types.Value{Type: types.TypeInt, Int: v, Null: isNull}
		case types.TypeBool:
			v := b[pos] == 1
			pos++
			row[i] = types.Value{Type: types.TypeBool, Bool: v, Null: isNull}
		case types.TypeText:
			n := int(binary.LittleEndian.Uint32(b[pos:]))
			pos += 4
			s := string(b[pos : pos+n])
			pos += n
			row[i] = types.Value{Type: types.TypeText, Str: s, Null: isNull}
		}
	}
	return row
}

// Catalog is the in-memory registry of all tables, with simple JSON persistence
// so schemas survive restarts and are available to crash recovery.
type Catalog struct {
	mu     sync.RWMutex
	path   string
	Tables map[string]*Table `json:"tables"`
}

// NewCatalog loads an existing catalog from path, or starts an empty one if the
// file does not exist yet.
func NewCatalog(path string) (*Catalog, error) {
	c := &Catalog{path: path, Tables: make(map[string]*Table)}
	data, err := os.ReadFile(path)
	if os.IsNotExist(err) {
		return c, nil
	}
	if err != nil {
		return nil, err
	}
	if err := json.Unmarshal(data, &struct {
		Tables *map[string]*Table `json:"tables"`
	}{Tables: &c.Tables}); err != nil {
		return nil, fmt.Errorf("catalog: parse %q: %w", path, err)
	}
	return c, nil
}

// CreateTable registers a new table and persists the catalog.
func (c *Catalog) CreateTable(t *Table) error {
	c.mu.Lock()
	defer c.mu.Unlock()
	if _, ok := c.Tables[t.Name]; ok {
		return fmt.Errorf("catalog: table %q already exists", t.Name)
	}
	c.Tables[t.Name] = t
	return c.saveLocked()
}

// Get returns the table by name.
func (c *Catalog) Get(name string) (*Table, bool) {
	c.mu.RLock()
	defer c.mu.RUnlock()
	t, ok := c.Tables[name]
	return t, ok
}

// List returns all table names (unordered).
func (c *Catalog) List() []string {
	c.mu.RLock()
	defer c.mu.RUnlock()
	out := make([]string, 0, len(c.Tables))
	for name := range c.Tables {
		out = append(out, name)
	}
	return out
}

// Save persists the catalog after an in-place mutation of a table (such as
// adding a secondary index).
func (c *Catalog) Save() error {
	c.mu.Lock()
	defer c.mu.Unlock()
	return c.saveLocked()
}

func (c *Catalog) saveLocked() error {
	if c.path == "" {
		return nil
	}
	data, err := json.MarshalIndent(struct {
		Tables map[string]*Table `json:"tables"`
	}{Tables: c.Tables}, "", "  ")
	if err != nil {
		return err
	}
	return os.WriteFile(c.path, data, 0o644)
}
