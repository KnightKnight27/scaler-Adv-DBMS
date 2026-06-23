// Package storage implements MiniDB's on-disk storage engine: a slotted page
// layout, a disk manager that performs raw page I/O, heap files built on top of
// pages, and a buffer pool that caches pages in memory with a Clock replacement
// policy. Tuples are stored as opaque byte slices; the catalog layer owns the
// encoding of rows into bytes.
package storage

import (
	"encoding/binary"
	"errors"
)

// PageSize is the fixed size of every page, in bytes. 4 KiB matches the page
// size of most real systems and a typical OS page, which keeps page-aligned
// reads and writes cheap.
const PageSize = 4096

// PageID identifies a page within a single heap file. Page numbering starts at
// 0 and is dense, so a file with N pages contains page ids 0..N-1.
type PageID int64

// InvalidPageID is returned by allocation helpers on failure and used as a
// sentinel "no page" value.
const InvalidPageID PageID = -1

// RID (record identifier) is the physical address of a tuple: the page it lives
// on plus its slot number within that page. RIDs are stable for the lifetime of
// a tuple and are what the B+Tree index and lock manager key on.
type RID struct {
	Page PageID
	Slot uint16
}

// Slotted page on-disk layout:
//
//	+----------------------------------------------------------+
//	| header | slot 0 | slot 1 | ... | -> free <- | tuple 1 | tuple 0 |
//	+----------------------------------------------------------+
//
// The slot directory grows forward from just after the header; tuple bodies
// grow backward from the end of the page. They meet in the middle, and the page
// is full when they would overlap. A slot stores the (offset, length) of its
// tuple; a length of 0 marks a deleted (tombstone) slot whose space is not yet
// reclaimed.
const (
	offNumSlots    = 0 // uint16: number of slot entries in the directory
	offFreeSpaceTo = 2 // uint16: offset of the lowest tuple byte written so far
	pageHeaderSize = 4
	slotEntrySize  = 4 // uint16 offset + uint16 length
)

var (
	// ErrPageFull is returned when a tuple cannot fit on a page.
	ErrPageFull = errors.New("storage: page is full")
	// ErrSlotNotFound is returned for an out-of-range or deleted slot.
	ErrSlotNotFound = errors.New("storage: slot not found")
)

// Page is an in-memory image of a single page. The raw byte backing array is
// what gets written to and read from disk verbatim.
type Page struct {
	id    PageID
	data  []byte
	dirty bool
	pins  int
}

// newPage wraps an existing byte buffer (read from disk) as a Page.
func newPage(id PageID, data []byte) *Page {
	return &Page{id: id, data: data}
}

// initEmptyPage formats a fresh page with an empty slot directory and all free
// space available.
func initEmptyPage(id PageID) *Page {
	p := &Page{id: id, data: make([]byte, PageSize)}
	p.setNumSlots(0)
	p.setFreeSpaceTo(PageSize)
	return p
}

func (p *Page) ID() PageID { return p.id }

func (p *Page) numSlots() uint16 { return binary.LittleEndian.Uint16(p.data[offNumSlots:]) }
func (p *Page) freeSpaceTo() uint16 {
	return binary.LittleEndian.Uint16(p.data[offFreeSpaceTo:])
}

func (p *Page) setNumSlots(n uint16) {
	binary.LittleEndian.PutUint16(p.data[offNumSlots:], n)
}
func (p *Page) setFreeSpaceTo(off uint16) {
	binary.LittleEndian.PutUint16(p.data[offFreeSpaceTo:], off)
}

// slotDirEnd is the first byte after the slot directory: where the next slot
// entry would be written.
func (p *Page) slotDirEnd() int {
	return pageHeaderSize + int(p.numSlots())*slotEntrySize
}

func (p *Page) readSlot(i uint16) (offset, length uint16) {
	base := pageHeaderSize + int(i)*slotEntrySize
	offset = binary.LittleEndian.Uint16(p.data[base:])
	length = binary.LittleEndian.Uint16(p.data[base+2:])
	return
}

func (p *Page) writeSlot(i uint16, offset, length uint16) {
	base := pageHeaderSize + int(i)*slotEntrySize
	binary.LittleEndian.PutUint16(p.data[base:], offset)
	binary.LittleEndian.PutUint16(p.data[base+2:], length)
}

// freeSpace returns the number of contiguous free bytes available between the
// slot directory and the tuple region.
func (p *Page) freeSpace() int {
	return int(p.freeSpaceTo()) - p.slotDirEnd()
}

// Insert places a tuple on the page and returns its slot number. It first tries
// to reuse a tombstoned slot of sufficient size, otherwise it appends a new
// slot. ErrPageFull is returned if there is not enough room.
func (p *Page) Insert(tuple []byte) (uint16, error) {
	need := len(tuple)

	// Try to reuse an existing tombstone slot without growing the directory.
	for i := uint16(0); i < p.numSlots(); i++ {
		if _, length := p.readSlot(i); length == 0 {
			if p.freeSpace() < need {
				return 0, ErrPageFull
			}
			newTo := int(p.freeSpaceTo()) - need
			copy(p.data[newTo:], tuple)
			p.writeSlot(i, uint16(newTo), uint16(need))
			p.setFreeSpaceTo(uint16(newTo))
			p.dirty = true
			return i, nil
		}
	}

	// Otherwise we need room for the tuple body plus a new slot entry.
	if p.freeSpace() < need+slotEntrySize {
		return 0, ErrPageFull
	}
	newTo := int(p.freeSpaceTo()) - need
	copy(p.data[newTo:], tuple)
	slot := p.numSlots()
	p.writeSlot(slot, uint16(newTo), uint16(need))
	p.setNumSlots(slot + 1)
	p.setFreeSpaceTo(uint16(newTo))
	p.dirty = true
	return slot, nil
}

// Get returns a copy of the tuple stored in slot i.
func (p *Page) Get(slot uint16) ([]byte, error) {
	if slot >= p.numSlots() {
		return nil, ErrSlotNotFound
	}
	offset, length := p.readSlot(slot)
	if length == 0 {
		return nil, ErrSlotNotFound
	}
	out := make([]byte, length)
	copy(out, p.data[offset:offset+length])
	return out, nil
}

// Delete tombstones slot i. The freed bytes are not compacted; this keeps RIDs
// of other tuples stable, which the index relies on. Space is reclaimed only by
// reusing the tombstone for a future tuple that fits.
func (p *Page) Delete(slot uint16) error {
	if slot >= p.numSlots() {
		return ErrSlotNotFound
	}
	if _, length := p.readSlot(slot); length == 0 {
		return ErrSlotNotFound
	}
	p.writeSlot(slot, 0, 0)
	p.dirty = true
	return nil
}

// Update overwrites slot i in place when the new tuple is the same size or
// smaller, otherwise it reports that the caller must delete and re-insert
// (producing a new RID). In-place updates avoid churning RIDs for the common
// case of fixed-width column edits.
func (p *Page) Update(slot uint16, tuple []byte) (inPlace bool, err error) {
	if slot >= p.numSlots() {
		return false, ErrSlotNotFound
	}
	offset, length := p.readSlot(slot)
	if length == 0 {
		return false, ErrSlotNotFound
	}
	if len(tuple) <= int(length) {
		copy(p.data[offset:], tuple)
		p.writeSlot(slot, offset, uint16(len(tuple)))
		p.dirty = true
		return true, nil
	}
	return false, nil
}

// NumSlots exposes the slot count for iteration by heap file scans.
func (p *Page) NumSlots() uint16 { return p.numSlots() }
