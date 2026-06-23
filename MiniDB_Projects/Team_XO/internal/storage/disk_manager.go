package storage

import (
	"fmt"
	"os"
	"sync"
)

// DiskManager owns a single heap file on disk and performs page-granular reads
// and writes. It is intentionally dumb: it knows nothing about tuples or
// transactions, only how to move PageSize-sized blocks between memory and the
// file at page-aligned offsets. This separation keeps the buffer pool free to
// implement any caching policy on top.
type DiskManager struct {
	mu       sync.Mutex
	file     *os.File
	numPages PageID
}

// NewDiskManager opens (creating if necessary) the heap file at path and
// derives the current page count from the file size.
func NewDiskManager(path string) (*DiskManager, error) {
	f, err := os.OpenFile(path, os.O_RDWR|os.O_CREATE, 0o644)
	if err != nil {
		return nil, fmt.Errorf("storage: open %q: %w", path, err)
	}
	info, err := f.Stat()
	if err != nil {
		f.Close()
		return nil, err
	}
	if info.Size()%PageSize != 0 {
		f.Close()
		return nil, fmt.Errorf("storage: file %q size %d is not a multiple of page size", path, info.Size())
	}
	return &DiskManager{file: f, numPages: PageID(info.Size() / PageSize)}, nil
}

// NumPages returns the number of pages currently in the file.
func (d *DiskManager) NumPages() PageID {
	d.mu.Lock()
	defer d.mu.Unlock()
	return d.numPages
}

// AllocatePage extends the file by one page and returns the new page id. The
// page is zero-filled on disk; callers format it via the buffer pool.
func (d *DiskManager) AllocatePage() (PageID, error) {
	d.mu.Lock()
	defer d.mu.Unlock()
	id := d.numPages
	if err := d.writeAt(id, make([]byte, PageSize)); err != nil {
		return InvalidPageID, err
	}
	d.numPages++
	return id, nil
}

// ReadPage loads the raw bytes of page id into a fresh buffer.
func (d *DiskManager) ReadPage(id PageID) ([]byte, error) {
	d.mu.Lock()
	defer d.mu.Unlock()
	if id < 0 || id >= d.numPages {
		return nil, fmt.Errorf("storage: read page %d out of range [0,%d)", id, d.numPages)
	}
	buf := make([]byte, PageSize)
	if _, err := d.file.ReadAt(buf, int64(id)*PageSize); err != nil {
		return nil, fmt.Errorf("storage: read page %d: %w", id, err)
	}
	return buf, nil
}

// WritePage flushes a page image to disk at its page-aligned offset.
func (d *DiskManager) WritePage(id PageID, data []byte) error {
	d.mu.Lock()
	defer d.mu.Unlock()
	return d.writeAt(id, data)
}

func (d *DiskManager) writeAt(id PageID, data []byte) error {
	if len(data) != PageSize {
		return fmt.Errorf("storage: page write must be %d bytes, got %d", PageSize, len(data))
	}
	if _, err := d.file.WriteAt(data, int64(id)*PageSize); err != nil {
		return fmt.Errorf("storage: write page %d: %w", id, err)
	}
	return nil
}

// Sync forces buffered file writes to durable storage. The recovery layer calls
// this to honour the write-ahead logging rule.
func (d *DiskManager) Sync() error {
	d.mu.Lock()
	defer d.mu.Unlock()
	return d.file.Sync()
}

// Close flushes and closes the underlying file.
func (d *DiskManager) Close() error {
	d.mu.Lock()
	defer d.mu.Unlock()
	return d.file.Close()
}
