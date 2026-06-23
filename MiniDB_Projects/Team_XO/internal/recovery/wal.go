// Package recovery implements write-ahead logging (WAL) and crash recovery for
// MiniDB. The engine runs a STEAL / NO-FORCE buffer policy: dirty pages of
// uncommitted transactions may reach disk (steal), and a commit does not force
// data pages out (no-force). Only the log is forced at commit. That makes the
// WAL the sole source of durability and demands both redo (for committed work
// not yet on disk) and undo (for uncommitted work that was stolen to disk),
// which is exactly what recoverAll performs.
package recovery

import (
	"bufio"
	"encoding/binary"
	"fmt"
	"io"
	"os"
	"sync"

	"minidb/internal/storage"
)

// RecordType tags a log record.
type RecordType uint8

const (
	RecBegin RecordType = iota
	RecUpdate
	RecCommit
	RecAbort
	RecCheckpoint
)

// OpKind distinguishes the data modifications carried by an update record.
type OpKind uint8

const (
	OpInsert OpKind = iota
	OpDelete
	OpUpdate
)

// Record is one logical WAL entry. For non-update records only Type and Txn are
// meaningful.
type Record struct {
	LSN    uint64
	Type   RecordType
	Txn    uint64
	Op     OpKind
	Table  string
	RID    storage.RID
	Before []byte // pre-image, used by undo
	After  []byte // post-image, used by redo
}

// WAL is an append-only log on disk. Appends are buffered; Flush forces them to
// stable storage to honour the write-ahead rule and force-log-at-commit.
type WAL struct {
	mu      sync.Mutex
	file    *os.File
	w       *bufio.Writer
	nextLSN uint64
}

// OpenWAL opens (creating if needed) the log file at path for append, deriving
// the next LSN from the existing entries.
func OpenWAL(path string) (*WAL, error) {
	count, err := countRecords(path)
	if err != nil {
		return nil, err
	}
	f, err := os.OpenFile(path, os.O_RDWR|os.O_CREATE|os.O_APPEND, 0o644)
	if err != nil {
		return nil, err
	}
	return &WAL{file: f, w: bufio.NewWriter(f), nextLSN: count + 1}, nil
}

func countRecords(path string) (uint64, error) {
	recs, err := ReadRecords(path)
	if err != nil {
		return 0, err
	}
	return uint64(len(recs)), nil
}

func (l *WAL) append(r *Record) (uint64, error) {
	l.mu.Lock()
	defer l.mu.Unlock()
	r.LSN = l.nextLSN
	l.nextLSN++
	payload := encodeRecord(r)
	var lenBuf [4]byte
	binary.LittleEndian.PutUint32(lenBuf[:], uint32(len(payload)))
	if _, err := l.w.Write(lenBuf[:]); err != nil {
		return 0, err
	}
	if _, err := l.w.Write(payload); err != nil {
		return 0, err
	}
	return r.LSN, nil
}

// LogBegin, LogUpdate, LogCommit and LogAbort append the corresponding records.
func (l *WAL) LogBegin(txn uint64) error {
	_, err := l.append(&Record{Type: RecBegin, Txn: txn})
	return err
}

func (l *WAL) LogUpdate(txn uint64, op OpKind, table string, rid storage.RID, before, after []byte) error {
	_, err := l.append(&Record{Type: RecUpdate, Txn: txn, Op: op, Table: table, RID: rid, Before: before, After: after})
	return err
}

// LogCommit writes the commit record and immediately flushes the log: a
// transaction is only acknowledged as committed once its commit record is
// durable.
func (l *WAL) LogCommit(txn uint64) error {
	if _, err := l.append(&Record{Type: RecCommit, Txn: txn}); err != nil {
		return err
	}
	return l.Flush()
}

func (l *WAL) LogAbort(txn uint64) error {
	if _, err := l.append(&Record{Type: RecAbort, Txn: txn}); err != nil {
		return err
	}
	return l.Flush()
}

// Flush forces buffered log records to durable storage.
func (l *WAL) Flush() error {
	l.mu.Lock()
	defer l.mu.Unlock()
	if err := l.w.Flush(); err != nil {
		return err
	}
	return l.file.Sync()
}

// Close flushes and closes the log file.
func (l *WAL) Close() error {
	if err := l.Flush(); err != nil {
		return err
	}
	return l.file.Close()
}

func encodeRecord(r *Record) []byte {
	var b []byte
	b = appendU64(b, r.LSN)
	b = append(b, byte(r.Type))
	b = appendU64(b, r.Txn)
	if r.Type == RecUpdate {
		b = append(b, byte(r.Op))
		b = appendBytes(b, []byte(r.Table))
		b = appendU64(b, uint64(r.RID.Page))
		b = appendU16(b, r.RID.Slot)
		b = appendBytes(b, r.Before)
		b = appendBytes(b, r.After)
	}
	return b
}

func decodeRecord(b []byte) *Record {
	r := &Record{}
	pos := 0
	r.LSN, pos = readU64(b, pos)
	r.Type = RecordType(b[pos])
	pos++
	r.Txn, pos = readU64(b, pos)
	if r.Type == RecUpdate {
		r.Op = OpKind(b[pos])
		pos++
		var tb []byte
		tb, pos = readBytes(b, pos)
		r.Table = string(tb)
		var page uint64
		page, pos = readU64(b, pos)
		r.RID.Page = storage.PageID(page)
		r.RID.Slot, pos = readU16(b, pos)
		r.Before, pos = readBytes(b, pos)
		r.After, pos = readBytes(b, pos)
	}
	return r
}

// ReadRecords reads and decodes every record in the log file at path. A missing
// file yields an empty slice (a database that has never logged anything).
func ReadRecords(path string) ([]*Record, error) {
	f, err := os.Open(path)
	if os.IsNotExist(err) {
		return nil, nil
	}
	if err != nil {
		return nil, err
	}
	defer f.Close()

	var recs []*Record
	r := bufio.NewReader(f)
	for {
		var lenBuf [4]byte
		if _, err := io.ReadFull(r, lenBuf[:]); err == io.EOF {
			break
		} else if err != nil {
			// A torn final record (partial write before a crash) is expected;
			// stop at the last complete record rather than failing recovery.
			break
		}
		n := binary.LittleEndian.Uint32(lenBuf[:])
		payload := make([]byte, n)
		if _, err := io.ReadFull(r, payload); err != nil {
			break
		}
		recs = append(recs, decodeRecord(payload))
	}
	return recs, nil
}

func appendU64(b []byte, v uint64) []byte {
	var t [8]byte
	binary.LittleEndian.PutUint64(t[:], v)
	return append(b, t[:]...)
}
func appendU16(b []byte, v uint16) []byte {
	var t [2]byte
	binary.LittleEndian.PutUint16(t[:], v)
	return append(b, t[:]...)
}
func appendBytes(b, v []byte) []byte {
	b = appendU64(b, uint64(len(v)))
	return append(b, v...)
}
func readU64(b []byte, pos int) (uint64, int) { return binary.LittleEndian.Uint64(b[pos:]), pos + 8 }
func readU16(b []byte, pos int) (uint16, int) { return binary.LittleEndian.Uint16(b[pos:]), pos + 2 }
func readBytes(b []byte, pos int) ([]byte, int) {
	n, pos := readU64(b, pos)
	if n == 0 {
		return nil, pos
	}
	out := make([]byte, n)
	copy(out, b[pos:pos+int(n)])
	return out, pos + int(n)
}

var _ = fmt.Sprintf
