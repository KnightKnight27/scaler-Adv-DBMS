"""Tests for the B+Tree index, including randomized fuzzing vs a reference."""

import os
import random
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "src"))

import pytest

from minidb.index.bplustree import BPlusTree, DuplicateKeyError
from minidb.storage.rid import RID


def _rid(n):
    return RID(n // 100, n % 100)


def test_insert_search_small_order():
    t = BPlusTree(order=4, unique=True)
    for k in [5, 3, 8, 1, 4, 7, 9, 2, 6, 10]:
        t.insert(k, _rid(k))
    for k in range(1, 11):
        assert t.search(k) == [_rid(k)]
    assert t.search(99) == []
    assert len(t) == 10
    assert t.height() >= 2  # splits happened with order 4


def test_unique_rejects_duplicate():
    t = BPlusTree(order=4, unique=True)
    t.insert(10, _rid(10))
    with pytest.raises(DuplicateKeyError):
        t.insert(10, _rid(11))


def test_non_unique_multiple_rids():
    t = BPlusTree(order=4, unique=False)
    t.insert("a", RID(1, 1))
    t.insert("a", RID(2, 2))
    t.insert("b", RID(3, 3))
    assert set(t.search("a")) == {RID(1, 1), RID(2, 2)}
    assert t.delete("a", RID(1, 1)) is True
    assert t.search("a") == [RID(2, 2)]


def test_range_scan():
    t = BPlusTree(order=4, unique=True)
    for k in range(1, 21):
        t.insert(k, _rid(k))
    got = [k for k, _ in t.range(5, 10)]
    assert got == [5, 6, 7, 8, 9, 10]
    got_excl = [k for k, _ in t.range(5, 10, include_low=False, include_high=False)]
    assert got_excl == [6, 7, 8, 9]
    assert [k for k, _ in t.range(high=3)] == [1, 2, 3]
    assert [k for k, _ in t.range(low=18)] == [18, 19, 20]


def test_delete_with_rebalance():
    t = BPlusTree(order=4, unique=True)
    for k in range(1, 31):
        t.insert(k, _rid(k))
    for k in range(1, 25):
        assert t.delete(k) is True
    assert len(t) == 6
    for k in range(25, 31):
        assert t.search(k) == [_rid(k)]
    # ordered scan still intact after many merges/borrows
    assert [k for k, _ in t.items()] == [25, 26, 27, 28, 29, 30]


def test_fuzz_against_reference():
    rng = random.Random(1234)
    for order in (3, 4, 8):
        t = BPlusTree(order=order, unique=True)
        ref = {}
        for _ in range(2000):
            k = rng.randint(0, 200)
            if k in ref or rng.random() < 0.45:
                # delete (if present) else skip
                if k in ref:
                    assert t.delete(k) is True
                    del ref[k]
            else:
                r = _rid(k)
                t.insert(k, r)
                ref[k] = r
            # spot check invariants occasionally
            if rng.random() < 0.02:
                assert sorted(ref) == [kk for kk, _ in t.items()]
                assert len(t) == len(ref)
        # final full comparison
        assert sorted(ref) == [k for k, _ in t.items()]
        for k, r in ref.items():
            assert t.search(k) == [r]
