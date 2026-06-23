package storage

import "fmt"

// HeapFile presents an unordered collection of variable-length tuples stored
// across the pages of one file. It sits directly on top of the buffer pool, so
// every page access is cached and obeys the Clock replacement policy. Tuples
// are addressed by RID and there is no inherent ordering; ordered access is the
// job of the B+Tree index layer.
type HeapFile struct {
	bp *BufferPool
}

// NewHeapFile wraps a buffer pool as a heap file.
func NewHeapFile(bp *BufferPool) *HeapFile { return &HeapFile{bp: bp} }

// Insert stores a tuple and returns its RID. It uses a first-fit search over
// existing pages and only grows the file when no page can hold the tuple. The
// returned RID is what the caller logs to the WAL and inserts into indexes.
func (h *HeapFile) Insert(tuple []byte) (RID, error) {
	if len(tuple) > PageSize-pageHeaderSize-slotEntrySize {
		return RID{}, fmt.Errorf("storage: tuple of %d bytes too large for a page", len(tuple))
	}
	n := h.bp.Disk().NumPages()
	for pid := PageID(0); pid < n; pid++ {
		page, err := h.bp.FetchPage(pid)
		if err != nil {
			return RID{}, err
		}
		slot, err := page.Insert(tuple)
		if err == nil {
			h.bp.Unpin(pid, true)
			return RID{Page: pid, Slot: slot}, nil
		}
		h.bp.Unpin(pid, false)
		if err != ErrPageFull {
			return RID{}, err
		}
	}

	page, err := h.bp.NewPage()
	if err != nil {
		return RID{}, err
	}
	slot, err := page.Insert(tuple)
	h.bp.Unpin(page.ID(), true)
	if err != nil {
		return RID{}, err
	}
	return RID{Page: page.ID(), Slot: slot}, nil
}

// Get returns the tuple at rid.
func (h *HeapFile) Get(rid RID) ([]byte, error) {
	page, err := h.bp.FetchPage(rid.Page)
	if err != nil {
		return nil, err
	}
	defer h.bp.Unpin(rid.Page, false)
	return page.Get(rid.Slot)
}

// Update overwrites the tuple at rid. When the new tuple fits in place the RID
// is preserved; otherwise the old slot is deleted and the tuple is re-inserted,
// returning a new RID that the caller must repoint indexes at.
func (h *HeapFile) Update(rid RID, tuple []byte) (RID, error) {
	page, err := h.bp.FetchPage(rid.Page)
	if err != nil {
		return rid, err
	}
	inPlace, err := page.Update(rid.Slot, tuple)
	if err != nil {
		h.bp.Unpin(rid.Page, false)
		return rid, err
	}
	if inPlace {
		h.bp.Unpin(rid.Page, true)
		return rid, nil
	}
	_ = page.Delete(rid.Slot)
	h.bp.Unpin(rid.Page, true)
	return h.Insert(tuple)
}

// Delete tombstones the tuple at rid.
func (h *HeapFile) Delete(rid RID) error {
	page, err := h.bp.FetchPage(rid.Page)
	if err != nil {
		return err
	}
	defer h.bp.Unpin(rid.Page, true)
	return page.Delete(rid.Slot)
}

// Scan visits every live tuple in the file in physical (page, slot) order,
// calling fn for each. Returning false from fn stops the scan early. This is the
// access path used by sequential table scans.
func (h *HeapFile) Scan(fn func(rid RID, tuple []byte) bool) error {
	n := h.bp.Disk().NumPages()
	for pid := PageID(0); pid < n; pid++ {
		page, err := h.bp.FetchPage(pid)
		if err != nil {
			return err
		}
		slots := page.NumSlots()
		stop := false
		for s := uint16(0); s < slots; s++ {
			tuple, err := page.Get(s)
			if err != nil {
				continue // tombstone
			}
			if !fn(RID{Page: pid, Slot: s}, tuple) {
				stop = true
				break
			}
		}
		h.bp.Unpin(pid, false)
		if stop {
			break
		}
	}
	return nil
}

// PutAt writes tuple at an exact RID, growing the file and padding the slot
// directory as needed. It is used exclusively by recovery to physically redo
// logged inserts and updates at the precise location they originally occupied.
func (h *HeapFile) PutAt(rid RID, tuple []byte) error {
	for h.bp.Disk().NumPages() <= rid.Page {
		if _, err := h.bp.NewPage(); err != nil {
			return err
		}
		// NewPage pins the page; unpin the freshly allocated frame.
		h.bp.Unpin(h.bp.Disk().NumPages()-1, true)
	}
	page, err := h.bp.FetchPage(rid.Page)
	if err != nil {
		return err
	}
	defer h.bp.Unpin(rid.Page, true)
	// A page that was allocated on disk but never flushed in a formatted state
	// reads back as all zeros (free-space pointer 0). Recovery may be the first
	// writer to touch it, so format it before use.
	if page.numSlots() == 0 && page.freeSpaceTo() == 0 {
		page.setFreeSpaceTo(PageSize)
	}
	page.putAtSlot(rid.Slot, tuple)
	return nil
}

// putAtSlot is the page-level primitive behind HeapFile.PutAt: it ensures slot
// exists (padding earlier slots as tombstones) and writes tuple into the page's
// free region, updating the directory entry. Caller holds the page pinned.
func (p *Page) putAtSlot(slot uint16, tuple []byte) {
	for p.numSlots() <= slot {
		p.writeSlot(p.numSlots(), 0, 0)
		p.setNumSlots(p.numSlots() + 1)
	}
	newTo := int(p.freeSpaceTo()) - len(tuple)
	if newTo < p.slotDirEnd() {
		// Should not happen for a faithful replay, but guard against corruption.
		return
	}
	copy(p.data[newTo:], tuple)
	p.writeSlot(slot, uint16(newTo), uint16(len(tuple)))
	p.setFreeSpaceTo(uint16(newTo))
	p.dirty = true
}
