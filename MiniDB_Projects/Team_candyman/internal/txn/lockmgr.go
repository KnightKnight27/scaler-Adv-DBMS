// Package txn provides MiniDB's concurrency control: a lock manager implementing
// two-phase locking with shared/exclusive modes and wait-for-graph deadlock
// detection, plus transaction bookkeeping.
//
// Trade-off: locks are taken on string resource names. The SQL layer locks at
// table granularity (simple and correct: per table, either many readers or one
// writer), while the lock manager itself is generic enough to lock finer-grained
// resources (e.g. "users:42") in tests/demos. Strict 2PL — all locks are held
// until commit/abort — gives serializable isolation.
package txn

import (
	"errors"
	"sync"
)

// Mode is a lock mode.
type Mode int

const (
	Shared Mode = iota
	Exclusive
)

func (m Mode) String() string {
	if m == Exclusive {
		return "X"
	}
	return "S"
}

// ErrDeadlock is returned by Acquire when granting the lock would close a cycle
// in the wait-for graph; the caller must abort the transaction.
var ErrDeadlock = errors.New("txn: deadlock detected")

type lockEntry struct {
	holders map[int]Mode // txn id -> mode currently held
	cond    *sync.Cond
}

// LockManager coordinates locks across transactions.
type LockManager struct {
	mu      sync.Mutex
	entries map[string]*lockEntry
	waitFor map[int]map[int]bool // txn -> txns it is currently waiting on
}

// NewLockManager creates an empty lock manager.
func NewLockManager() *LockManager {
	return &LockManager{
		entries: map[string]*lockEntry{},
		waitFor: map[int]map[int]bool{},
	}
}

func (lm *LockManager) entry(res string) *lockEntry {
	e, ok := lm.entries[res]
	if !ok {
		e = &lockEntry{holders: map[int]Mode{}, cond: sync.NewCond(&lm.mu)}
		lm.entries[res] = e
	}
	return e
}

// Acquire blocks until transaction txn holds res in the requested mode, or
// returns ErrDeadlock if waiting would deadlock.
func (lm *LockManager) Acquire(txn int, res string, mode Mode) error {
	lm.mu.Lock()
	defer lm.mu.Unlock()
	e := lm.entry(res)
	for {
		if lm.canGrant(e, txn, mode) {
			cur, held := e.holders[txn]
			if !held || mode == Exclusive { // grant or upgrade
				if !held || mode > cur {
					e.holders[txn] = mode
				}
			}
			delete(lm.waitFor, txn)
			return nil
		}
		// record that txn waits for every conflicting holder
		lm.setWaitFor(txn, e)
		if lm.hasCycle(txn) {
			delete(lm.waitFor, txn)
			return ErrDeadlock
		}
		e.cond.Wait()
	}
}

// canGrant reports whether txn may immediately hold res in mode.
func (lm *LockManager) canGrant(e *lockEntry, txn int, mode Mode) bool {
	for h, hm := range e.holders {
		if h == txn {
			continue
		}
		// any other holder conflicts with an exclusive request; a shared request
		// conflicts only with an exclusive holder.
		if mode == Exclusive || hm == Exclusive {
			return false
		}
	}
	return true
}

func (lm *LockManager) setWaitFor(txn int, e *lockEntry) {
	set := map[int]bool{}
	for h := range e.holders {
		if h != txn {
			set[h] = true
		}
	}
	lm.waitFor[txn] = set
}

// hasCycle runs DFS over the wait-for graph from start looking for a cycle.
func (lm *LockManager) hasCycle(start int) bool {
	visited := map[int]bool{}
	var dfs func(n int) bool
	dfs = func(n int) bool {
		for next := range lm.waitFor[n] {
			if next == start {
				return true
			}
			if !visited[next] {
				visited[next] = true
				if dfs(next) {
					return true
				}
			}
		}
		return false
	}
	return dfs(start)
}

// Release drops every lock held by txn and wakes waiters.
func (lm *LockManager) Release(txn int) {
	lm.mu.Lock()
	defer lm.mu.Unlock()
	for _, e := range lm.entries {
		if _, ok := e.holders[txn]; ok {
			delete(e.holders, txn)
			e.cond.Broadcast()
		}
	}
	delete(lm.waitFor, txn)
}
