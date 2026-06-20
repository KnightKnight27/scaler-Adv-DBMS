// Package recovery implements MiniDB's write-ahead log (WAL) and crash recovery.
//
// Logging is logical and row-level: each data change appends a record carrying
// the before- and after-images of the row. This supports both runtime ROLLBACK
// and crash recovery using a redo/undo scheme:
//
//   - redo: re-apply every change made by a committed transaction (idempotent
//     because the engine's Put is an upsert and Delete is a no-op when absent);
//   - undo: reverse every change made by a transaction that never committed
//     (a "loser"), also idempotent.
//
// Trade-off: the data pages do not carry per-page LSNs, so the buffer pool's
// write-ahead rule is enforced conservatively — the whole log is fsynced before
// any dirty data page is written back. Simpler to reason about; a little extra
// fsyncing. On a clean shutdown the log is reset because all committed data is
// already durable in the heap.
package recovery

import (
	"bufio"
	"encoding/binary"
	"fmt"
	"io"
	"os"
	"sync"
)

// RecType identifies a log record kind.
type RecType uint8

const (
	RecBegin      RecType = 1
	RecInsert     RecType = 2
	RecDelete     RecType = 3
	RecCommit     RecType = 4
	RecAbort      RecType = 5
	RecCheckpoint RecType = 6
)

// Record is one WAL entry. For Insert, Before is empty and After holds the new
// row; for Delete, Before holds the old row and After is empty.
type Record struct {
	LSN    uint64
	Type   RecType
	Txn    int
	Table  string
	Before []byte
	After  []byte
}

// WAL is an append-only log file.
type WAL struct {
	mu   sync.Mutex
	f    *os.File
	path string
	lsn  uint64
}

// Open opens (creating if needed) the WAL at path.
func Open(path string) (*WAL, error) {
	f, err := os.OpenFile(path, os.O_RDWR|os.O_CREATE, 0o644)
	if err != nil {
		return nil, err
	}
	return &WAL{f: f, path: path}, nil
}

// Append writes a record and returns its LSN. The caller flushes (fsyncs) at
// commit via Flush.
func (w *WAL) Append(rec Record) (uint64, error) {
	w.mu.Lock()
	defer w.mu.Unlock()
	w.lsn++
	rec.LSN = w.lsn
	if _, err := w.f.Seek(0, io.SeekEnd); err != nil {
		return 0, err
	}
	if _, err := w.f.Write(encodeRecord(rec)); err != nil {
		return 0, err
	}
	return rec.LSN, nil
}

// Flush fsyncs the log to stable storage.
func (w *WAL) Flush() error {
	w.mu.Lock()
	defer w.mu.Unlock()
	return w.f.Sync()
}

// ReadAll returns every fully-written record in the log. A torn trailing record
// (from a crash mid-append) is ignored.
func (w *WAL) ReadAll() ([]Record, error) {
	w.mu.Lock()
	defer w.mu.Unlock()
	if _, err := w.f.Seek(0, io.SeekStart); err != nil {
		return nil, err
	}
	r := bufio.NewReader(w.f)
	var recs []Record
	for {
		rec, err := decodeRecord(r)
		if err == io.EOF || err == io.ErrUnexpectedEOF {
			break // clean end or torn tail record
		}
		if err != nil {
			return nil, err
		}
		recs = append(recs, rec)
		if rec.LSN > w.lsn {
			w.lsn = rec.LSN
		}
	}
	return recs, nil
}

// Reset truncates the log to empty (used after a clean shutdown or after recovery
// has made the data durable).
func (w *WAL) Reset() error {
	w.mu.Lock()
	defer w.mu.Unlock()
	if err := w.f.Truncate(0); err != nil {
		return err
	}
	if _, err := w.f.Seek(0, io.SeekStart); err != nil {
		return err
	}
	w.lsn = 0
	return w.f.Sync()
}

// Close closes the underlying file.
func (w *WAL) Close() error {
	w.mu.Lock()
	defer w.mu.Unlock()
	return w.f.Close()
}

// Record framing: totalLen(4) | LSN(8) | type(1) | txn(8) | tlen(2) table |
// blen(4) before | alen(4) after. totalLen covers everything after itself.
func encodeRecord(rec Record) []byte {
	body := make([]byte, 0, 32+len(rec.Table)+len(rec.Before)+len(rec.After))
	var u8 [8]byte
	binary.BigEndian.PutUint64(u8[:], rec.LSN)
	body = append(body, u8[:]...)
	body = append(body, byte(rec.Type))
	binary.BigEndian.PutUint64(u8[:], uint64(rec.Txn))
	body = append(body, u8[:]...)
	var u2 [2]byte
	binary.BigEndian.PutUint16(u2[:], uint16(len(rec.Table)))
	body = append(body, u2[:]...)
	body = append(body, rec.Table...)
	var u4 [4]byte
	binary.BigEndian.PutUint32(u4[:], uint32(len(rec.Before)))
	body = append(body, u4[:]...)
	body = append(body, rec.Before...)
	binary.BigEndian.PutUint32(u4[:], uint32(len(rec.After)))
	body = append(body, u4[:]...)
	body = append(body, rec.After...)

	out := make([]byte, 4+len(body))
	binary.BigEndian.PutUint32(out[:4], uint32(len(body)))
	copy(out[4:], body)
	return out
}

func decodeRecord(r *bufio.Reader) (Record, error) {
	var lenBuf [4]byte
	if _, err := io.ReadFull(r, lenBuf[:]); err != nil {
		return Record{}, err // io.EOF at clean end
	}
	total := int(binary.BigEndian.Uint32(lenBuf[:]))
	body := make([]byte, total)
	if _, err := io.ReadFull(r, body); err != nil {
		return Record{}, io.ErrUnexpectedEOF // torn tail
	}
	off := 0
	rec := Record{}
	rec.LSN = binary.BigEndian.Uint64(body[off : off+8])
	off += 8
	rec.Type = RecType(body[off])
	off++
	rec.Txn = int(binary.BigEndian.Uint64(body[off : off+8]))
	off += 8
	tl := int(binary.BigEndian.Uint16(body[off : off+2]))
	off += 2
	rec.Table = string(body[off : off+tl])
	off += tl
	bl := int(binary.BigEndian.Uint32(body[off : off+4]))
	off += 4
	rec.Before = append([]byte(nil), body[off:off+bl]...)
	off += bl
	al := int(binary.BigEndian.Uint32(body[off : off+4]))
	off += 4
	rec.After = append([]byte(nil), body[off:off+al]...)
	if off+al > len(body) {
		return Record{}, fmt.Errorf("wal: malformed record")
	}
	return rec, nil
}
