package txn

import "sync"

// State is a transaction's lifecycle state.
type State int

const (
	Active State = iota
	Committed
	Aborted
)

// Transaction is a unit of work holding locks under strict 2PL.
type Transaction struct {
	ID    int
	lm    *LockManager
	state State
}

// Lock acquires res in mode for this transaction. On deadlock it aborts the
// transaction (releasing its locks so the other party can proceed) and returns
// ErrDeadlock.
func (t *Transaction) Lock(res string, mode Mode) error {
	err := t.lm.Acquire(t.ID, res, mode)
	if err == ErrDeadlock {
		t.Abort()
	}
	return err
}

// Commit releases all locks and marks the transaction committed.
func (t *Transaction) Commit() {
	if t.state == Active {
		t.state = Committed
		t.lm.Release(t.ID)
	}
}

// Abort releases all locks and marks the transaction aborted.
func (t *Transaction) Abort() {
	if t.state == Active {
		t.state = Aborted
		t.lm.Release(t.ID)
	}
}

// State returns the current lifecycle state.
func (t *Transaction) State() State { return t.state }

// Manager hands out transactions with monotonically increasing ids.
type Manager struct {
	mu   sync.Mutex
	next int
	lm   *LockManager
}

// NewManager creates a transaction manager over a lock manager.
func NewManager(lm *LockManager) *Manager {
	return &Manager{next: 1, lm: lm}
}

// Begin starts a new transaction.
func (m *Manager) Begin() *Transaction {
	m.mu.Lock()
	id := m.next
	m.next++
	m.mu.Unlock()
	return &Transaction{ID: id, lm: m.lm, state: Active}
}

// LockManager exposes the underlying lock manager (for diagnostics/demos).
func (m *Manager) LockManager() *LockManager { return m.lm }
