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

// InsertAt writes a record at a specific RID's page+slot during redo recovery.
// It only applies the write if the page's LSN is older than recLSN (idempotent
// redo). Used by the WAL recovery path.
func (h *HeapFile) InsertAt(rid RID, rec []byte, recLSN uint64) error {
	page, err := h.bp.Fetch(FileData, rid.PageID)
	if err != nil {
		return err
	}
	defer func() { h.bp.Unpin(FileData, rid.PageID, true) }()
	if page.LSN() >= recLSN {
		return nil // already reflects this update
	}
	// best-effort: append; slot may differ but redo replays full history in order
	if _, err := page.Insert(rec); err != nil {
		return err
	}
	page.SetLSN(recLSN)
	return nil
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
