// Package txn provides MiniDB's concurrency control. It offers two
// interchangeable strategies selected at startup:
//
//   - Strict two-phase locking (2PL) via the LockManager, which provides
//     serializable isolation. This is the required core mechanism.
//   - Multi-Version Concurrency Control (MVCC) with snapshot isolation, the
//     project's chosen extension (Track B). MVCC reads never block writers and
//     vice versa, which the benchmarks contrast against 2PL under contention.
//
// The Manager owns transaction identifiers, lifecycle and (for MVCC) snapshots.
// The storage strategies live in the engine layer and call into this package to
// acquire locks or test version visibility.
package txn

import (
	"errors"
	"sync"
)

// TxnID uniquely identifies a transaction. IDs are assigned monotonically and
// also serve as MVCC version stamps.
type TxnID uint64

// Mode selects the concurrency-control strategy for the whole database.
type Mode int

const (
	Mode2PL Mode = iota
	ModeMVCC
)

func (m Mode) String() string {
	if m == ModeMVCC {
		return "MVCC"
	}
	return "2PL"
}

// State is a transaction's lifecycle state.
type State int

const (
	Active State = iota
	Committed
	Aborted
)

// Snapshot captures the set of transactions visible to an MVCC transaction at
// the moment it began. A version created by transaction cx is visible if cx is
// this transaction, or cx had already committed before the snapshot was taken:
// that is, cx < Xmax and cx is not in the Active set, and cx ultimately
// committed.
type Snapshot struct {
	Xmax   TxnID          // ids >= Xmax started after us and are invisible
	Active map[TxnID]bool // transactions in flight when our snapshot was taken
}

// Transaction is a handle threaded through every statement in a transaction.
type Transaction struct {
	ID       TxnID
	Mode     Mode
	snapshot *Snapshot
	undo     []func() // in-memory rollback actions applied on Abort, in reverse
}

// AddUndo registers a compensating action run if the transaction aborts. Stores
// use this to reverse in-memory effects (heap and index mutations) without
// touching the WAL, which is reserved for crash recovery.
func (t *Transaction) AddUndo(fn func()) { t.undo = append(t.undo, fn) }

// Snapshot returns the transaction's MVCC snapshot (nil under 2PL).
func (t *Transaction) Snapshot() *Snapshot { return t.snapshot }

// ErrDeadlock is returned by lock acquisition when granting a lock would close a
// cycle in the wait-for graph. The caller must abort the transaction.
var ErrDeadlock = errors.New("txn: transaction aborted to break deadlock")

// ErrWriteConflict is returned under MVCC when two concurrent transactions try
// to update the same row (first committer wins).
var ErrWriteConflict = errors.New("txn: write-write conflict, transaction aborted")

// Manager assigns transaction ids, tracks their states, and owns the lock
// manager used in 2PL mode.
type Manager struct {
	mu     sync.Mutex
	mode   Mode
	nextID TxnID
	states map[TxnID]State
	locks  *LockManager
}

// NewManager creates a transaction manager in the given mode.
func NewManager(mode Mode) *Manager {
	return &Manager{
		mode:   mode,
		nextID: 1,
		states: make(map[TxnID]State),
		locks:  NewLockManager(),
	}
}

// Mode reports the active concurrency strategy.
func (m *Manager) Mode() Mode { return m.mode }

// Advance bumps the id generator so that new transactions get ids greater than
// any seen during crash recovery, preventing reuse of recovered transaction ids.
func (m *Manager) Advance(min TxnID) {
	m.mu.Lock()
	defer m.mu.Unlock()
	if min+1 > m.nextID {
		m.nextID = min + 1
	}
}

// Locks exposes the lock manager for the 2PL store.
func (m *Manager) Locks() *LockManager { return m.locks }

// Begin starts a new transaction. Under MVCC it captures a snapshot of the
// currently active transactions.
func (m *Manager) Begin() *Transaction {
	m.mu.Lock()
	defer m.mu.Unlock()
	id := m.nextID
	m.nextID++
	m.states[id] = Active
	t := &Transaction{ID: id, Mode: m.mode}
	if m.mode == ModeMVCC {
		active := make(map[TxnID]bool)
		for tid, st := range m.states {
			if tid != id && st == Active {
				active[tid] = true
			}
		}
		t.snapshot = &Snapshot{Xmax: id, Active: active}
	}
	return t
}

// Commit marks the transaction committed and releases its locks. In-memory undo
// actions are discarded.
func (m *Manager) Commit(t *Transaction) {
	m.mu.Lock()
	m.states[t.ID] = Committed
	m.mu.Unlock()
	if m.mode == Mode2PL {
		m.locks.ReleaseAll(t.ID)
	}
	t.undo = nil
}

// Abort rolls back the transaction's in-memory effects (in reverse order),
// marks it aborted and releases its locks.
func (m *Manager) Abort(t *Transaction) {
	for i := len(t.undo) - 1; i >= 0; i-- {
		t.undo[i]()
	}
	t.undo = nil
	m.mu.Lock()
	m.states[t.ID] = Aborted
	m.mu.Unlock()
	if m.mode == Mode2PL {
		m.locks.ReleaseAll(t.ID)
	}
}

// Status returns the recorded state of a transaction id. Unknown ids are
// treated as aborted, which is the safe default for MVCC visibility checks.
func (m *Manager) Status(id TxnID) State {
	m.mu.Lock()
	defer m.mu.Unlock()
	st, ok := m.states[id]
	if !ok {
		return Aborted
	}
	return st
}

// Visible reports whether a version created by creator and deleted by deleter
// (0 if still live) is visible to transaction t under its snapshot. This is the
// heart of snapshot isolation.
func (m *Manager) Visible(t *Transaction, creator, deleter TxnID) bool {
	if !m.versionCommittedVisible(t, creator) {
		return false // creating transaction's effect is not visible to us
	}
	if deleter == 0 {
		return true // live version
	}
	// The version is hidden only if its deletion is also visible to us.
	return !m.versionCommittedVisible(t, deleter)
}

// versionCommittedVisible reports whether the effect of transaction id is
// visible to t: either it is t itself, or it committed before t's snapshot.
func (m *Manager) versionCommittedVisible(t *Transaction, id TxnID) bool {
	if id == 0 {
		return false
	}
	if id == t.ID {
		return true
	}
	s := t.snapshot
	if id >= s.Xmax || s.Active[id] {
		return false // started at/after our snapshot, or was in flight then
	}
	return m.Status(id) == Committed
}
