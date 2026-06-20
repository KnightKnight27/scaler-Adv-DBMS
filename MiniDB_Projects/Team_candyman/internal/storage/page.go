// Package storage implements MiniDB's page-based storage: a fixed-size slotted
// page, a disk manager, an LRU buffer pool, a heap file, and the StorageEngine
// interface that both the heap and LSM engines satisfy.
//
// Trade-off: a classic slotted page keeps variable-length records simple. The
// slot directory grows forward from the header; record bytes grow backward from
// the end of the page. Deletes set a tombstone bit rather than compacting, which
// keeps RIDs stable (important: the B+Tree maps primary keys to RIDs).
package storage

import (
	"encoding/binary"
	"errors"
)

// PageSize is fixed at 4 KiB.
const PageSize = 4096

// PageID identifies a page within a file.
type PageID uint32

const InvalidPageID PageID = 0xFFFFFFFF

// Page header layout (bytes):
//
//	0:8   pageLSN   (uint64) last log record that modified this page (for WAL)
//	8:12  nextPage  (uint32) next page in the heap chain (InvalidPageID if none)
//	12:14 slotCount (uint16) number of slots in the directory
//	14:16 freePtr   (uint16) offset of the start of free space (records grow down)
//
// Each slot is 4 bytes: offset(uint16), length(uint16). A length with the high
// bit set marks a tombstone (deleted record).
const (
	hdrLSN       = 0
	hdrNextPage  = 8
	hdrSlotCount = 12
	hdrFreePtr   = 14
	headerSize   = 16
	slotSize     = 4
	tombstoneBit = 1 << 15
)

var (
	// ErrPageFull is returned when a record does not fit on a page.
	ErrPageFull = errors.New("storage: page full")
	// ErrBadSlot is returned for an out-of-range or tombstoned slot.
	ErrBadSlot = errors.New("storage: invalid slot")
)

// Page is an in-memory image of a 4 KiB page.
type Page struct {
	data [PageSize]byte
}

// NewPage returns a zeroed page initialized as an empty heap page.
func NewPage() *Page {
	p := &Page{}
	p.SetNextPage(InvalidPageID)
	binary.BigEndian.PutUint16(p.data[hdrFreePtr:], PageSize)
	return p
}

// Bytes returns the raw page image (used by the disk manager).
func (p *Page) Bytes() []byte { return p.data[:] }

// LSN / NextPage accessors are used by the buffer pool, heap file and WAL.
func (p *Page) LSN() uint64     { return binary.BigEndian.Uint64(p.data[hdrLSN:]) }
func (p *Page) SetLSN(v uint64) { binary.BigEndian.PutUint64(p.data[hdrLSN:], v) }

func (p *Page) NextPage() PageID {
	return PageID(binary.BigEndian.Uint32(p.data[hdrNextPage:]))
}
func (p *Page) SetNextPage(id PageID) {
	binary.BigEndian.PutUint32(p.data[hdrNextPage:], uint32(id))
}

func (p *Page) slotCount() int { return int(binary.BigEndian.Uint16(p.data[hdrSlotCount:])) }
func (p *Page) setSlotCount(n int) {
	binary.BigEndian.PutUint16(p.data[hdrSlotCount:], uint16(n))
}
func (p *Page) freePtr() int { return int(binary.BigEndian.Uint16(p.data[hdrFreePtr:])) }
func (p *Page) setFreePtr(n int) {
	binary.BigEndian.PutUint16(p.data[hdrFreePtr:], uint16(n))
}

func (p *Page) slotOffsetLen(i int) (off, length int, tomb bool) {
	base := headerSize + i*slotSize
	off = int(binary.BigEndian.Uint16(p.data[base:]))
	raw := binary.BigEndian.Uint16(p.data[base+2:])
	tomb = raw&tombstoneBit != 0
	length = int(raw &^ tombstoneBit)
	return
}
func (p *Page) setSlot(i, off, length int, tomb bool) {
	base := headerSize + i*slotSize
	binary.BigEndian.PutUint16(p.data[base:], uint16(off))
	raw := uint16(length)
	if tomb {
		raw |= tombstoneBit
	}
	binary.BigEndian.PutUint16(p.data[base+2:], raw)
}

// SlotCount returns the number of slots (including tombstones).
func (p *Page) SlotCount() int { return p.slotCount() }

// FreeSpace returns the bytes available for a new record including its slot.
func (p *Page) FreeSpace() int {
	used := headerSize + p.slotCount()*slotSize
	return p.freePtr() - used
}

// Insert adds a record and returns its slot index, or ErrPageFull.
func (p *Page) Insert(rec []byte) (int, error) {
	if len(rec)+slotSize > p.FreeSpace() {
		return 0, ErrPageFull
	}
	newFree := p.freePtr() - len(rec)
	copy(p.data[newFree:], rec)
	p.setFreePtr(newFree)
	slot := p.slotCount()
	p.setSlot(slot, newFree, len(rec), false)
	p.setSlotCount(slot + 1)
	return slot, nil
}

// Read returns the record bytes at a slot.
func (p *Page) Read(slot int) ([]byte, error) {
	if slot < 0 || slot >= p.slotCount() {
		return nil, ErrBadSlot
	}
	off, length, tomb := p.slotOffsetLen(slot)
	if tomb {
		return nil, ErrBadSlot
	}
	out := make([]byte, length)
	copy(out, p.data[off:off+length])
	return out, nil
}

// Delete marks a slot as a tombstone. The bytes remain but become invisible.
func (p *Page) Delete(slot int) error {
	if slot < 0 || slot >= p.slotCount() {
		return ErrBadSlot
	}
	off, length, tomb := p.slotOffsetLen(slot)
	if tomb {
		return ErrBadSlot
	}
	p.setSlot(slot, off, length, true)
	return nil
}

// IsTombstone reports whether a slot has been deleted.
func (p *Page) IsTombstone(slot int) bool {
	if slot < 0 || slot >= p.slotCount() {
		return true
	}
	_, _, tomb := p.slotOffsetLen(slot)
	return tomb
}
