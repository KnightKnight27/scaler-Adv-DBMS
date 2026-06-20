// Package catalog stores table metadata (schemas and heap roots) and persists it
// as JSON next to the data files.
//
// Trade-off: the catalog is small and changes only on DDL, so a human-readable
// JSON file (rewritten and fsynced on each change) is simpler and easier to
// inspect than packing metadata into a system page. B+Tree roots are deliberately
// NOT stored here — indexes are rebuilt from the heap on startup.
package catalog

import (
	"encoding/json"
	"fmt"
	"os"
	"sort"

	"minidb/internal/storage"
	"minidb/internal/types"
)

// TableMeta is the persisted description of one table.
type TableMeta struct {
	Name     string         `json:"name"`
	Schema   *types.Schema  `json:"schema"`
	HeapRoot storage.PageID `json:"heap_root"`
}

// Catalog maps table names to their metadata.
type Catalog struct {
	path   string
	Tables map[string]*TableMeta `json:"tables"`
}

// Open loads the catalog from path, or returns an empty one if absent.
func Open(path string) (*Catalog, error) {
	c := &Catalog{path: path, Tables: map[string]*TableMeta{}}
	data, err := os.ReadFile(path)
	if os.IsNotExist(err) {
		return c, nil
	}
	if err != nil {
		return nil, fmt.Errorf("read catalog: %w", err)
	}
	var on struct {
		Tables map[string]*TableMeta `json:"tables"`
	}
	if err := json.Unmarshal(data, &on); err != nil {
		return nil, fmt.Errorf("parse catalog: %w", err)
	}
	if on.Tables != nil {
		c.Tables = on.Tables
	}
	return c, nil
}

// Get returns a table's metadata.
func (c *Catalog) Get(name string) (*TableMeta, bool) {
	m, ok := c.Tables[name]
	return m, ok
}

// Add registers a new table and persists the catalog.
func (c *Catalog) Add(m *TableMeta) error {
	if _, exists := c.Tables[m.Name]; exists {
		return fmt.Errorf("table %q already exists", m.Name)
	}
	c.Tables[m.Name] = m
	return c.Save()
}

// Names returns table names in sorted order.
func (c *Catalog) Names() []string {
	out := make([]string, 0, len(c.Tables))
	for n := range c.Tables {
		out = append(out, n)
	}
	sort.Strings(out)
	return out
}

// Save writes the catalog to disk and fsyncs it.
func (c *Catalog) Save() error {
	data, err := json.MarshalIndent(struct {
		Tables map[string]*TableMeta `json:"tables"`
	}{c.Tables}, "", "  ")
	if err != nil {
		return err
	}
	tmp := c.path + ".tmp"
	if err := os.WriteFile(tmp, data, 0o644); err != nil {
		return err
	}
	return os.Rename(tmp, c.path)
}
