package txn

import (
	"sync"
	"testing"
	"time"
)

func TestSharedLocksAreCompatible(t *testing.T) {
	lm := NewLockManager()
	if err := lm.Acquire(1, "r", Shared); err != nil {
		t.Fatal(err)
	}
	// a second shared lock on the same resource must be granted immediately
	done := make(chan error, 1)
	go func() { done <- lm.Acquire(2, "r", Shared) }()
	select {
	case err := <-done:
		if err != nil {
			t.Fatalf("second shared lock: %v", err)
		}
	case <-time.After(time.Second):
		t.Fatal("shared lock blocked unexpectedly")
	}
}

func TestExclusiveBlocksUntilRelease(t *testing.T) {
	lm := NewLockManager()
	lm.Acquire(1, "r", Exclusive)

	granted := make(chan struct{})
	go func() {
		lm.Acquire(2, "r", Exclusive)
		close(granted)
	}()

	// txn2 must not get the lock while txn1 holds it
	select {
	case <-granted:
		t.Fatal("exclusive lock granted while held by another txn")
	case <-time.After(100 * time.Millisecond):
	}

	lm.Release(1)
	select {
	case <-granted:
	case <-time.After(time.Second):
		t.Fatal("exclusive lock not granted after release")
	}
}

func TestDeadlockDetected(t *testing.T) {
	lm := NewLockManager()
	// txn1 holds A, txn2 holds B
	lm.Acquire(1, "A", Exclusive)
	lm.Acquire(2, "B", Exclusive)

	type res struct {
		id  int
		err error
	}
	out := make(chan res, 2)
	go func() { out <- res{1, lm.Acquire(1, "B", Exclusive)} }() // 1 waits for 2
	go func() { out <- res{2, lm.Acquire(2, "A", Exclusive)} }() // 2 waits for 1 -> cycle

	first := <-out
	if first.err != ErrDeadlock {
		// the first to *return* should be the deadlock victim; the other is still
		// blocked. Release the victim so the survivor proceeds.
		t.Fatalf("expected first returned to be deadlock, got %v", first.err)
	}
	lm.Release(first.id) // victim aborts, releasing its held lock

	second := <-out
	if second.err != nil {
		t.Fatalf("survivor should acquire after victim aborts, got %v", second.err)
	}
}

func TestTransactionDeadlockAutoAborts(t *testing.T) {
	lm := NewLockManager()
	mgr := NewManager(lm)
	t1 := mgr.Begin()
	t2 := mgr.Begin()

	if err := t1.Lock("A", Exclusive); err != nil {
		t.Fatal(err)
	}
	if err := t2.Lock("B", Exclusive); err != nil {
		t.Fatal(err)
	}

	var wg sync.WaitGroup
	wg.Add(2)
	errs := make([]error, 2)
	go func() { defer wg.Done(); errs[0] = t1.Lock("B", Exclusive) }()
	go func() { defer wg.Done(); errs[1] = t2.Lock("A", Exclusive) }()
	wg.Wait()

	// exactly one transaction should have been aborted by deadlock detection
	deadlocks := 0
	for _, e := range errs {
		if e == ErrDeadlock {
			deadlocks++
		}
	}
	if deadlocks != 1 {
		t.Fatalf("expected exactly 1 deadlock victim, got %d (errs=%v)", deadlocks, errs)
	}
	if (t1.State() == Aborted) == (t2.State() == Aborted) {
		t.Fatalf("exactly one txn should be aborted: t1=%v t2=%v", t1.State(), t2.State())
	}
}
