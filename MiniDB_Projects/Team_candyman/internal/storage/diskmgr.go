package storage

import (
	"fmt"
	"io"
	"os"
	"sync"
)

// FileID identifies one of the on-disk files managed by the DiskManager.
type FileID uint8

const (
	// FileData holds heap (table) pages.
	FileData FileID = iota
	// FileIndex holds B+Tree index pages.
	FileIndex
	numFiles
)

// DiskManager reads and writes fixed-size pages to one or more files. Pages are
// addressed by (FileID, PageID); PageID is simply the page's ordinal in the file.
//
// Trade-off: keeping heap pages and index pages in separate files lets recovery
// rebuild indexes by truncating the index file, without touching table data.
type DiskManager struct {
	mu    sync.Mutex
	files [numFiles]*os.File
	pages [numFiles]PageID // next page id to allocate per file
}

// OpenDiskManager opens (creating if needed) the data and index files in dir.
func OpenDiskManager(dataPath, indexPath string) (*DiskManager, error) {
	dm := &DiskManager{}
	paths := [numFiles]string{FileData: dataPath, FileIndex: indexPath}
	for id := FileID(0); id < numFiles; id++ {
		f, err := os.OpenFile(paths[id], os.O_RDWR|os.O_CREATE, 0o644)
		if err != nil {
			dm.Close()
			return nil, fmt.Errorf("open %s: %w", paths[id], err)
		}
		dm.files[id] = f
		info, err := f.Stat()
		if err != nil {
			dm.Close()
			return nil, err
		}
		dm.pages[id] = PageID(info.Size() / PageSize)
	}
	return dm, nil
}

// NumPages returns how many pages currently exist in a file.
func (dm *DiskManager) NumPages(file FileID) PageID {
	dm.mu.Lock()
	defer dm.mu.Unlock()
	return dm.pages[file]
}

// Allocate reserves a fresh page id at the end of a file.
func (dm *DiskManager) Allocate(file FileID) PageID {
	dm.mu.Lock()
	defer dm.mu.Unlock()
	id := dm.pages[file]
	dm.pages[file]++
	return id
}

// ReadPage loads a page image from disk. Reading a never-written page returns a
// zeroed page (it has been allocated but not yet flushed).
func (dm *DiskManager) ReadPage(file FileID, id PageID, p *Page) error {
	dm.mu.Lock()
	defer dm.mu.Unlock()
	off := int64(id) * PageSize
	n, err := dm.files[file].ReadAt(p.data[:], off)
	if err == io.EOF && n < PageSize {
		// page allocated but not yet persisted: zero-fill the remainder
		for i := n; i < PageSize; i++ {
			p.data[i] = 0
		}
		return nil
	}
	return err
}

// WritePage persists a page image to disk (without fsync).
func (dm *DiskManager) WritePage(file FileID, id PageID, p *Page) error {
	dm.mu.Lock()
	defer dm.mu.Unlock()
	off := int64(id) * PageSize
	_, err := dm.files[file].WriteAt(p.data[:], off)
	return err
}

// Sync flushes OS buffers for both files to stable storage.
func (dm *DiskManager) Sync() error {
	dm.mu.Lock()
	defer dm.mu.Unlock()
	for _, f := range dm.files {
		if f != nil {
			if err := f.Sync(); err != nil {
				return err
			}
		}
	}
	return nil
}

// TruncateIndex resets the index file (used by recovery before rebuilding it).
func (dm *DiskManager) TruncateIndex() error {
	dm.mu.Lock()
	defer dm.mu.Unlock()
	if err := dm.files[FileIndex].Truncate(0); err != nil {
		return err
	}
	dm.pages[FileIndex] = 0
	return nil
}

// Close closes all open files.
func (dm *DiskManager) Close() error {
	dm.mu.Lock()
	defer dm.mu.Unlock()
	var err error
	for i, f := range dm.files {
		if f != nil {
			if e := f.Close(); e != nil && err == nil {
				err = e
			}
			dm.files[i] = nil
		}
	}
	return err
}
