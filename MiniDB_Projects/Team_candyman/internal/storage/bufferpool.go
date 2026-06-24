package storage

import (
	"container/list"
	"fmt"
	"sync"
)

// PageKey identifies a cached page across files.
type PageKey struct {
	File FileID
	ID   PageID
}

type frame struct {
	key   PageKey
	page  *Page
	pins  int
	dirty bool
}

// BufferPool caches pages in memory with a fixed capacity and LRU eviction.
//
// Trade-off: pages are pinned while in use so they are never evicted mid-operation.
// Eviction only considers unpinned frames; dirty victims are written back first.
// Before any dirty page is written to disk we flush the WAL up to that page's LSN
// (the write-ahead rule), wired via SetLogFlush.
type BufferPool struct {
	mu       sync.Mutex
	dm       *DiskManager
	capacity int
	frames   map[PageKey]*list.Element // key -> *list.Element holding *frame
	lru      *list.List                // front = most recently used
	logFlush func(uint64) error
}

// NewBufferPool creates a pool over dm holding up to capacity frames.
func NewBufferPool(dm *DiskManager, capacity int) *BufferPool {
	if capacity < 2 {
		capacity = 2
	}
	return &BufferPool{
		dm:       dm,
		capacity: capacity,
		frames:   make(map[PageKey]*list.Element),
		lru:      list.New(),
	}
}

// SetLogFlush installs the write-ahead-log flush hook. The pool calls fn(lsn)
// before writing any dirty page so its log records reach disk first.
func (bp *BufferPool) SetLogFlush(fn func(uint64) error) { bp.logFlush = fn }

// NewPage allocates a fresh page in a file and returns it pinned.
func (bp *BufferPool) NewPage(file FileID) (PageID, *Page, error) {
	id := bp.dm.Allocate(file)
	bp.mu.Lock()
	defer bp.mu.Unlock()
	key := PageKey{file, id}
	p := NewPage()
	if err := bp.admit(key, p); err != nil {
		return 0, nil, err
	}
	return id, p, nil
}

// Fetch returns the requested page, pinned. Caller must Unpin when done.
func (bp *BufferPool) Fetch(file FileID, id PageID) (*Page, error) {
	key := PageKey{file, id}
	bp.mu.Lock()
	defer bp.mu.Unlock()
	if el, ok := bp.frames[key]; ok {
		fr := el.Value.(*frame)
		fr.pins++
		bp.lru.MoveToFront(el)
		return fr.page, nil
	}
	p := NewPage()
	if err := bp.dm.ReadPage(file, id, p); err != nil {
		return nil, fmt.Errorf("buffer fetch %v: %w", key, err)
	}
	if err := bp.admit(key, p); err != nil {
		return nil, err
	}
	return p, nil
}

// admit inserts a freshly loaded/allocated page as a pinned frame, evicting if
// necessary. Caller holds bp.mu.
func (bp *BufferPool) admit(key PageKey, p *Page) error {
	if len(bp.frames) >= bp.capacity {
		if err := bp.evict(); err != nil {
			return err
		}
	}
	fr := &frame{key: key, page: p, pins: 1}
	bp.frames[key] = bp.lru.PushFront(fr)
	return nil
}

// evict removes the least-recently-used unpinned frame, flushing it if dirty.
func (bp *BufferPool) evict() error {
	for el := bp.lru.Back(); el != nil; el = el.Prev() {
		fr := el.Value.(*frame)
		if fr.pins > 0 {
			continue
		}
		if fr.dirty {
			if err := bp.flushFrame(fr); err != nil {
				return err
			}
		}
		bp.lru.Remove(el)
		delete(bp.frames, fr.key)
		return nil
	}
	return fmt.Errorf("buffer pool: all %d frames pinned, cannot evict", bp.capacity)
}

// flushFrame writes a dirty frame to disk, honoring the write-ahead rule.
func (bp *BufferPool) flushFrame(fr *frame) error {
	// Write-ahead rule applies to data (heap) pages only; index pages carry no
	// meaningful LSN because indexes are rebuilt from the heap on recovery.
	if bp.logFlush != nil && fr.key.File == FileData {
		if err := bp.logFlush(fr.page.LSN()); err != nil {
			return err
		}
	}
	if err := bp.dm.WritePage(fr.key.File, fr.key.ID, fr.page); err != nil {
		return err
	}
	fr.dirty = false
	return nil
}

// Unpin releases one pin on a page and records whether it was modified.
func (bp *BufferPool) Unpin(file FileID, id PageID, dirty bool) {
	key := PageKey{file, id}
	bp.mu.Lock()
	defer bp.mu.Unlock()
	el, ok := bp.frames[key]
	if !ok {
		return
	}
	fr := el.Value.(*frame)
	if dirty {
		fr.dirty = true
	}
	if fr.pins > 0 {
		fr.pins--
	}
}

// FlushAll writes every dirty frame to disk (used at checkpoint / shutdown).
func (bp *BufferPool) FlushAll() error {
	bp.mu.Lock()
	defer bp.mu.Unlock()
	for _, el := range bp.frames {
		fr := el.Value.(*frame)
		if fr.dirty {
			if err := bp.flushFrame(fr); err != nil {
				return err
			}
		}
	}
	return nil
}

// Stats reports cache size for diagnostics/benchmarks.
func (bp *BufferPool) Stats() (frames, capacity int) {
	bp.mu.Lock()
	defer bp.mu.Unlock()
	return len(bp.frames), bp.capacity
}
