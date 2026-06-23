"""Tests for the lock manager + transactions (build step 11, core feature #5)."""

import threading
import time

import pytest

from minidb.lock_manager import DeadlockError, LockManager, LockMode
from minidb.transaction import TransactionManager, TxnState

S, X = LockMode.SHARED, LockMode.EXCLUSIVE


# --- lock compatibility ------------------------------------------------------


def test_shared_locks_are_compatible():
    lm = LockManager()
    lm.acquire(1, "A", S)
    lm.acquire(2, "A", S)  # must not block
    assert set(lm.holders("A")) == {1, 2}


def test_exclusive_excludes_others():
    lm = LockManager()
    lm.acquire(1, "A", X)
    got = threading.Event()

    def t2():
        lm.acquire(2, "A", X)
        got.set()

    th = threading.Thread(target=t2, daemon=True)
    th.start()
    assert not got.wait(0.2)       # blocked while txn 1 holds X
    lm.release(1, "A")
    assert got.wait(1.0)           # unblocked once released
    th.join(1.0)


def test_lock_upgrade_when_sole_holder():
    lm = LockManager()
    lm.acquire(1, "A", S)
    lm.acquire(1, "A", X)          # upgrade S -> X
    assert lm.holders("A") == {1: X}


def test_reacquire_is_noop():
    lm = LockManager()
    lm.acquire(1, "A", X)
    lm.acquire(1, "A", S)          # already hold stronger X -> no-op
    assert lm.holders("A") == {1: X}


# --- deadlock detection ------------------------------------------------------


def test_deadlock_is_detected_and_one_victim_aborts():
    """T1: lock A then B. T2: lock B then A. Exactly one is the victim."""
    lm = LockManager()
    lm.acquire(1, "A", X)
    lm.acquire(2, "B", X)

    t1_has_a = threading.Event()
    t2_has_b = threading.Event()
    t1_has_a.set()
    t2_has_b.set()
    outcomes: dict[int, str] = {}

    def t1():
        t2_has_b.wait(1.0)
        try:
            lm.acquire(1, "B", X)
            outcomes[1] = "ok"
            lm.release_all(1)
        except DeadlockError:
            outcomes[1] = "deadlock"
            lm.release_all(1)

    def t2():
        t1_has_a.wait(1.0)
        try:
            lm.acquire(2, "A", X)
            outcomes[2] = "ok"
            lm.release_all(2)
        except DeadlockError:
            outcomes[2] = "deadlock"
            lm.release_all(2)

    threads = [threading.Thread(target=t1, daemon=True),
               threading.Thread(target=t2, daemon=True)]
    for t in threads:
        t.start()
    for t in threads:
        t.join(3.0)
        assert not t.is_alive(), "thread hung -> deadlock not resolved"

    # exactly one victim, the other made progress
    assert sorted(outcomes.values()) == ["deadlock", "ok"]


def test_no_false_deadlock_on_simple_wait():
    """A plain wait (no cycle) must NOT be reported as a deadlock."""
    lm = LockManager()
    lm.acquire(1, "A", X)
    result = {}

    def t2():
        try:
            lm.acquire(2, "A", X)   # waits, no cycle
            result["got"] = True
        except DeadlockError:
            result["got"] = "deadlock"

    th = threading.Thread(target=t2, daemon=True)
    th.start()
    time.sleep(0.15)
    lm.release_all(1)               # let txn 2 proceed
    th.join(1.0)
    assert result.get("got") is True


# --- transactions / strict 2PL ----------------------------------------------


def test_strict_2pl_holds_locks_until_commit():
    tm = TransactionManager()
    t = tm.begin()
    t.lock_exclusive("users")
    assert "users" in t.locks
    assert t.state is TxnState.ACTIVE
    tm.commit(t)
    assert t.state is TxnState.COMMITTED
    assert t.locks == set()
    assert tm.lock_manager.holders("users") == {}


def test_abort_releases_locks():
    tm = TransactionManager()
    t = tm.begin()
    t.lock_exclusive("users")
    tm.abort(t)
    assert t.state is TxnState.ABORTED
    assert tm.lock_manager.holders("users") == {}


def test_cannot_use_finished_transaction():
    tm = TransactionManager()
    t = tm.begin()
    tm.commit(t)
    with pytest.raises(RuntimeError):
        t.lock_shared("x")


def test_two_transactions_get_unique_ids():
    tm = TransactionManager()
    a, b = tm.begin(), tm.begin()
    assert a.txn_id != b.txn_id
    assert set(tm.active) == {a.txn_id, b.txn_id}
