package storage

import "encoding/binary"

// RID (record id) locates a record: a page and a slot within it. RIDs are stable
// across the record's lifetime because deletes tombstone slots rather than moving
// records. The B+Tree primary-key index maps key -> RID.
type RID struct {
	PageID PageID
	Slot   uint16
}

// Encode/DecodeRID serialize a RID to 6 bytes for storage as a B+Tree value.
func (r RID) Encode() []byte {
	b := make([]byte, 6)
	binary.BigEndian.PutUint32(b[0:], uint32(r.PageID))
	binary.BigEndian.PutUint16(b[4:], r.Slot)
	return b
}

func DecodeRID(b []byte) RID {
	return RID{
		PageID: PageID(binary.BigEndian.Uint32(b[0:])),
		Slot:   binary.BigEndian.Uint16(b[4:]),
	}
}

// HeapFile is an unordered collection of records spread across a chain of pages
// in the data file. Each table owns one HeapFile rooted at firstPage.
type HeapFile struct {
	bp        *BufferPool
	firstPage PageID
}

// NewHeapFile allocates the first page of a new heap and returns the heap plus
// its root page id (the catalog persists this id).
func NewHeapFile(bp *BufferPool) (*HeapFile, PageID, error) {
	id, _, err := bp.NewPage(FileData)
	if err != nil {
		return nil, 0, err
	}
	bp.Unpin(FileData, id, true)
	return &HeapFile{bp: bp, firstPage: id}, id, nil
}

// OpenHeapFile reopens an existing heap rooted at firstPage.
func OpenHeapFile(bp *BufferPool, firstPage PageID) *HeapFile {
	return &HeapFile{bp: bp, firstPage: firstPage}
}

// Insert appends a record, allocating and linking a new page if the chain is full.
// It returns the new record's RID and the LSN slot is left to the caller (the WAL
// layer sets pageLSN via SetPageLSN after logging).
func (h *HeapFile) Insert(rec []byte) (RID, error) {
	cur := h.firstPage
	for {
		page, err := h.bp.Fetch(FileData, cur)
		if err != nil {
			return RID{}, err
		}
		slot, err := page.Insert(rec)
		if err == nil {
			h.bp.Unpin(FileData, cur, true)
			return RID{PageID: cur, Slot: uint16(slot)}, nil
		}
		next := page.NextPage()
		if next != InvalidPageID {
			h.bp.Unpin(FileData, cur, false)
			cur = next
			continue
		}
		// end of chain and record didn't fit: append a fresh page
		newID, newPage, err := h.bp.NewPage(FileData)
		if err != nil {
			h.bp.Unpin(FileData, cur, false)
			return RID{}, err
		}
		slot, err = newPage.Insert(rec)
		if err != nil {
			h.bp.Unpin(FileData, newID, false)
			h.bp.Unpin(FileData, cur, false)
			return RID{}, err
		}
		page.SetNextPage(newID)
		h.bp.Unpin(FileData, cur, true)
		h.bp.Unpin(FileData, newID, true)
		return RID{PageID: newID, Slot: uint16(slot)}, nil
	}
}

// Read returns the record bytes at a RID.
func (h *HeapFile) Read(rid RID) ([]byte, error) {
	page, err := h.bp.Fetch(FileData, rid.PageID)
	if err != nil {
		return nil, err
	}
	defer h.bp.Unpin(FileData, rid.PageID, false)
	return page.Read(int(rid.Slot))
}

// Delete tombstones the record at a RID and bumps its page LSN.
func (h *HeapFile) Delete(rid RID, lsn uint64) error {
	page, err := h.bp.Fetch(FileData, rid.PageID)
	if err != nil {
		return err
	}
	defer func() { h.bp.Unpin(FileData, rid.PageID, true) }()
	if err := page.Delete(int(rid.Slot)); err != nil {
		return err
	}
	if lsn > page.LSN() {
		page.SetLSN(lsn)
	}
	return nil
}

// SetPageLSN records the LSN of the log record that produced the latest change
// to the page holding rid (used by the WAL layer right after Insert).
func (h *HeapFile) SetPageLSN(pid PageID, lsn uint64) error {
	page, err := h.bp.Fetch(FileData, pid)
	if err != nil {
		return err
	}
	defer h.bp.Unpin(FileData, pid, true)
	if lsn > page.LSN() {
		page.SetLSN(lsn)
	}
	return nil
}

// ScanFunc is called for every live record during a scan. Returning false stops.
type ScanFunc func(rid RID, rec []byte) bool

// Scan visits every live (non-tombstone) record in the heap.
func (h *HeapFile) Scan(fn ScanFunc) error {
	cur := h.firstPage
	for cur != InvalidPageID {
		page, err := h.bp.Fetch(FileData, cur)
		if err != nil {
			return err
		}
		n := page.SlotCount()
		next := page.NextPage()
		for s := 0; s < n; s++ {
			if page.IsTombstone(s) {
				continue
			}
			rec, err := page.Read(s)
			if err != nil {
				continue
			}
			if !fn(RID{PageID: cur, Slot: uint16(s)}, rec) {
				h.bp.Unpin(FileData, cur, false)
				return nil
			}
		}
		h.bp.Unpin(FileData, cur, false)
		cur = next
	}
	return nil
}

// FirstPage returns the heap's root page id (for catalog persistence).
func (h *HeapFile) FirstPage() PageID { return h.firstPage }

// HeapCursor is a pull-based iterator over a heap's live records. It buffers one
// page of records at a time, so memory use is bounded by a single page.
type HeapCursor struct {
	h       *HeapFile
	curPage PageID
	buf     []heapRec
	pos     int
	done    bool
}

type heapRec struct {
	rid RID
	rec []byte
}

// Cursor returns a streaming iterator over the heap.
func (h *HeapFile) Cursor() *HeapCursor {
	return &HeapCursor{h: h, curPage: h.firstPage}
}

// Next returns the next live record, or ok=false at the end.
func (c *HeapCursor) Next() (RID, []byte, bool, error) {
	for {
		if c.pos < len(c.buf) {
			r := c.buf[c.pos]
			c.pos++
			return r.rid, r.rec, true, nil
		}
		if c.done || c.curPage == InvalidPageID {
			return RID{}, nil, false, nil
		}
		if err := c.loadPage(); err != nil {
			return RID{}, nil, false, err
		}
	}
}

func (c *HeapCursor) loadPage() error {
	page, err := c.h.bp.Fetch(FileData, c.curPage)
	if err != nil {
		return err
	}
	n := page.SlotCount()
	next := page.NextPage()
	c.buf = c.buf[:0]
	c.pos = 0
	for s := 0; s < n; s++ {
		if page.IsTombstone(s) {
			continue
		}
		rec, err := page.Read(s)
		if err != nil {
			continue
		}
		c.buf = append(c.buf, heapRec{RID{PageID: c.curPage, Slot: uint16(s)}, rec})
	}
	c.h.bp.Unpin(FileData, c.curPage, false)
	if next == InvalidPageID {
		c.done = true
	}
	c.curPage = next
	return nil
}
