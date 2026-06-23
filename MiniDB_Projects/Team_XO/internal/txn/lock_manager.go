package txn

import (
	"sync"
	"time"

	"minidb/internal/storage"
)

// deadlockTimeout backstops the wait-for-graph detector. Eager cycle detection
// catches most deadlocks the instant a transaction would block, but a cycle can
// also complete among transactions that are already waiting (where no one re-runs
// detection). A bounded wait guarantees liveness: a transaction that waits too
// long aborts itself as a victim and can retry.
const deadlockTimeout = 200 * time.Millisecond

// LockMode is the strength of a lock request.
type LockMode int

const (
	// Shared locks coexist with other shared locks; used for reads.
	Shared LockMode = iota
	// Exclusive locks conflict with every other lock; used for writes.
	Exclusive
)

// lockRequest is a queued, not-yet-granted acquisition. ready is signalled once
// the lock is granted (nil) or the wait ends in a deadlock abort.
type lockRequest struct {
	txn   TxnID
	mode  LockMode
	ready chan error
}

// lockState tracks the holders and FIFO waiters of a single RID's lock.
type lockState struct {
	holders map[TxnID]LockMode
	waiters []*lockRequest
}

// LockManager implements record-level locking for strict 2PL. It detects
// deadlocks eagerly: before a transaction blocks, it adds the corresponding
// edges to a wait-for graph and aborts itself if that would create a cycle. The
// requesting (youngest-to-block) transaction is the victim, which is simple and
// guarantees progress.
type LockManager struct {
	mu       sync.Mutex
	locks    map[storage.RID]*lockState
	held     map[TxnID]map[storage.RID]bool // for releasing all locks at end of txn
	waitsFor map[TxnID]map[TxnID]bool       // edge t -> u means t waits for u
}

// NewLockManager creates an empty lock manager.
func NewLockManager() *LockManager {
	return &LockManager{
		locks:    make(map[storage.RID]*lockState),
		held:     make(map[TxnID]map[storage.RID]bool),
		waitsFor: make(map[TxnID]map[TxnID]bool),
	}
}

// Acquire blocks until txn holds rid in at least the requested mode, or returns
// ErrDeadlock if waiting would deadlock. Strict 2PL means the caller never
// releases individual locks; they are all dropped at commit/abort.
func (lm *LockManager) Acquire(txn TxnID, rid storage.RID, mode LockMode) error {
	lm.mu.Lock()
	ls := lm.locks[rid]
	if ls == nil {
		ls = &lockState{holders: make(map[TxnID]LockMode)}
		lm.locks[rid] = ls
	}

	if lm.canGrant(ls, txn, mode) {
		lm.grant(ls, txn, rid, mode)
		lm.mu.Unlock()
		return nil
	}

	lm.addWaitEdges(txn, ls, mode)
	if lm.hasCycle(txn) {
		lm.clearWaits(txn)
		lm.mu.Unlock()
		return ErrDeadlock
	}

	req := &lockRequest{txn: txn, mode: mode, ready: make(chan error, 1)}
	ls.waiters = append(ls.waiters, req)
	lm.mu.Unlock()

	select {
	case err := <-req.ready:
		return err
	case <-time.After(deadlockTimeout):
		lm.mu.Lock()
		if lm.removeWaiter(ls, req) {
			// Still queued: nobody granted it, so abort as a deadlock victim.
			lm.clearWaits(txn)
			lm.mu.Unlock()
			return ErrDeadlock
		}
		// A concurrent release granted the lock just as we timed out.
		lm.mu.Unlock()
		return <-req.ready
	}
}

// removeWaiter removes req from ls.waiters if present, returning whether it was
// found. Caller holds lm.mu.
func (lm *LockManager) removeWaiter(ls *lockState, req *lockRequest) bool {
	for i, w := range ls.waiters {
		if w == req {
			ls.waiters = append(ls.waiters[:i], ls.waiters[i+1:]...)
			return true
		}
	}
	return false
}

// canGrant reports whether mode is compatible with the current holders, ignoring
// locks already held by txn itself (which permits lock upgrades when txn is the
// sole holder). Caller holds lm.mu.
func (lm *LockManager) canGrant(ls *lockState, txn TxnID, mode LockMode) bool {
	for h, hmode := range ls.holders {
		if h == txn {
			continue
		}
		if mode == Exclusive || hmode == Exclusive {
			return false
		}
	}
	return true
}

func (lm *LockManager) grant(ls *lockState, txn TxnID, rid storage.RID, mode LockMode) {
	cur, ok := ls.holders[txn]
	if !ok || mode > cur {
		ls.holders[txn] = mode // record the strongest mode held
	}
	if lm.held[txn] == nil {
		lm.held[txn] = make(map[storage.RID]bool)
	}
	lm.held[txn][rid] = true
	lm.clearWaits(txn)
}

// addWaitEdges records that txn is waiting for every conflicting current holder.
func (lm *LockManager) addWaitEdges(txn TxnID, ls *lockState, mode LockMode) {
	if lm.waitsFor[txn] == nil {
		lm.waitsFor[txn] = make(map[TxnID]bool)
	}
	for h, hmode := range ls.holders {
		if h == txn {
			continue
		}
		if mode == Exclusive || hmode == Exclusive {
			lm.waitsFor[txn][h] = true
		}
	}
}

func (lm *LockManager) clearWaits(txn TxnID) { delete(lm.waitsFor, txn) }

// hasCycle runs a depth-first search from start over the wait-for graph and
// reports whether start can reach itself, i.e. a deadlock cycle exists.
func (lm *LockManager) hasCycle(start TxnID) bool {
	visited := make(map[TxnID]bool)
	var dfs func(n TxnID) bool
	dfs = func(n TxnID) bool {
		for next := range lm.waitsFor[n] {
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

// ReleaseAll drops every lock held by txn (called once at commit or abort) and
// wakes any waiters that can now proceed.
func (lm *LockManager) ReleaseAll(txn TxnID) {
	lm.mu.Lock()
	defer lm.mu.Unlock()

	rids := lm.held[txn]
	delete(lm.held, txn)
	delete(lm.waitsFor, txn)
	for other := range lm.waitsFor {
		delete(lm.waitsFor[other], txn)
	}

	for rid := range rids {
		ls := lm.locks[rid]
		if ls == nil {
			continue
		}
		delete(ls.holders, txn)
		lm.wakeWaiters(ls, rid)
		if len(ls.holders) == 0 && len(ls.waiters) == 0 {
			delete(lm.locks, rid)
		}
	}
}

// wakeWaiters grants the lock to as many front waiters as are compatible,
// stopping at the first incompatible request to preserve FIFO fairness and
// prevent writer starvation. Caller holds lm.mu.
func (lm *LockManager) wakeWaiters(ls *lockState, rid storage.RID) {
	for len(ls.waiters) > 0 {
		req := ls.waiters[0]
		if !lm.canGrant(ls, req.txn, req.mode) {
			break
		}
		ls.waiters = ls.waiters[1:]
		lm.grant(ls, req.txn, rid, req.mode)
		req.ready <- nil
	}
}
