package storage

import (
	"errors"
	"sync"
)

// ErrPoolFull is returned when every frame in the pool is pinned and no page
// can be evicted to make room.
var ErrPoolFull = errors.New("storage: buffer pool is full (all frames pinned)")

// frame is one slot in the buffer pool. The reference bit drives the Clock
// (second-chance) replacement policy.
type frame struct {
	page *Page
	ref  bool
}

// BufferPool caches a fixed number of pages from a single DiskManager. It
// implements the Clock replacement policy: a circular scan over frames that
// gives each recently used, unpinned page a "second chance" before eviction.
// Pinned pages are never evicted, which lets callers hold a page stable while
// they read or mutate it.
type BufferPool struct {
	mu        sync.Mutex
	disk      *DiskManager
	frames    []*frame
	pageTable map[PageID]int // page id -> frame index
	clockHand int
}

// NewBufferPool creates a pool backed by disk with the given number of frames.
func NewBufferPool(disk *DiskManager, capacity int) *BufferPool {
	if capacity < 1 {
		capacity = 1
	}
	return &BufferPool{
		disk:      disk,
		frames:    make([]*frame, capacity),
		pageTable: make(map[PageID]int),
	}
}

// FetchPage returns the requested page, pinned. Callers must pair every
// FetchPage/NewPage with exactly one Unpin once they are done with the page.
func (bp *BufferPool) FetchPage(id PageID) (*Page, error) {
	bp.mu.Lock()
	defer bp.mu.Unlock()

	if idx, ok := bp.pageTable[id]; ok {
		fr := bp.frames[idx]
		fr.ref = true
		fr.page.pins++
		return fr.page, nil
	}

	data, err := bp.disk.ReadPage(id)
	if err != nil {
		return nil, err
	}
	page := newPage(id, data)
	if err := bp.admit(page); err != nil {
		return nil, err
	}
	return page, nil
}

// NewPage allocates a fresh page on disk, formats it as an empty slotted page,
// and returns it pinned in the pool.
func (bp *BufferPool) NewPage() (*Page, error) {
	bp.mu.Lock()
	defer bp.mu.Unlock()

	id, err := bp.disk.AllocatePage()
	if err != nil {
		return nil, err
	}
	page := initEmptyPage(id)
	page.dirty = true // a freshly formatted page must be persisted
	if err := bp.admit(page); err != nil {
		return nil, err
	}
	return page, nil
}

// admit installs a page into a free or evicted frame and pins it. Caller holds
// the lock.
func (bp *BufferPool) admit(page *Page) error {
	idx, err := bp.victimFrame()
	if err != nil {
		return err
	}
	if fr := bp.frames[idx]; fr != nil && fr.page != nil {
		if fr.page.dirty {
			if err := bp.disk.WritePage(fr.page.id, fr.page.data); err != nil {
				return err
			}
		}
		delete(bp.pageTable, fr.page.id)
	}
	page.pins++
	bp.frames[idx] = &frame{page: page, ref: true}
	bp.pageTable[page.id] = idx
	return nil
}

// victimFrame returns the index of a frame to (re)use, running the Clock hand
// until it finds an empty or unpinned, unreferenced frame. Caller holds lock.
func (bp *BufferPool) victimFrame() (int, error) {
	n := len(bp.frames)
	for i := 0; i < n; i++ {
		if bp.frames[i] == nil {
			return i, nil
		}
	}
	// Two full sweeps are sufficient: the first clears reference bits, the
	// second is guaranteed to find a victim unless every frame is pinned.
	for scanned := 0; scanned < 2*n; scanned++ {
		idx := bp.clockHand
		bp.clockHand = (bp.clockHand + 1) % n
		fr := bp.frames[idx]
		if fr.page.pins > 0 {
			continue
		}
		if fr.ref {
			fr.ref = false
			continue
		}
		return idx, nil
	}
	return 0, ErrPoolFull
}

// Unpin releases one pin held on a page and records whether the caller dirtied
// it. A page only becomes eligible for eviction once its pin count reaches zero.
func (bp *BufferPool) Unpin(id PageID, dirty bool) {
	bp.mu.Lock()
	defer bp.mu.Unlock()
	idx, ok := bp.pageTable[id]
	if !ok {
		return
	}
	fr := bp.frames[idx]
	if dirty {
		fr.page.dirty = true
	}
	if fr.page.pins > 0 {
		fr.page.pins--
	}
}

// FlushPage writes a single page to disk if it is dirty and clears the flag.
func (bp *BufferPool) FlushPage(id PageID) error {
	bp.mu.Lock()
	defer bp.mu.Unlock()
	idx, ok := bp.pageTable[id]
	if !ok {
		return nil
	}
	return bp.flushLocked(bp.frames[idx])
}

// FlushAll persists every dirty page. The recovery layer uses this when taking
// a checkpoint and on clean shutdown.
func (bp *BufferPool) FlushAll() error {
	bp.mu.Lock()
	defer bp.mu.Unlock()
	for _, fr := range bp.frames {
		if fr == nil {
			continue
		}
		if err := bp.flushLocked(fr); err != nil {
			return err
		}
	}
	return bp.disk.Sync()
}

func (bp *BufferPool) flushLocked(fr *frame) error {
	if fr == nil || fr.page == nil || !fr.page.dirty {
		return nil
	}
	if err := bp.disk.WritePage(fr.page.id, fr.page.data); err != nil {
		return err
	}
	fr.page.dirty = false
	return nil
}

// Disk exposes the underlying disk manager for components (such as heap file
// scans) that need the current page count.
func (bp *BufferPool) Disk() *DiskManager { return bp.disk }
