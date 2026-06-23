"""Tests for the B+ tree index (build step 6).

We test with a deliberately tiny order to force frequent splits, borrows, and
merges, and cross-check against a plain dict ("model-based" testing).
"""

import random

import pytest

from minidb.btree import BPlusTree, DuplicateKeyError
from minidb.heap import RID


def rid(n):
    return RID(n, 0)


def collect_keys(t):
    return [k for k, _ in t.items()]


def assert_sorted_chain(t):
    keys = collect_keys(t)
    assert keys == sorted(keys), f"leaf chain not sorted: {keys}"


def test_empty_tree():
    t = BPlusTree(order=4)
    assert t.search(5) is None
    assert len(t) == 0
    assert list(t.items()) == []


def test_insert_search_small():
    t = BPlusTree(order=4)
    for k in [10, 20, 5, 15, 25]:
        t.insert(k, rid(k))
    assert t.search(15) == rid(15)
    assert t.search(99) is None
    assert collect_keys(t) == [5, 10, 15, 20, 25]
    assert len(t) == 5


def test_splits_grow_height():
    t = BPlusTree(order=4)
    for k in range(100):
        t.insert(k, rid(k))
    assert t.height() > 1  # tree grew past a single leaf
    assert collect_keys(t) == list(range(100))
    for k in range(100):
        assert t.search(k) == rid(k)


def test_duplicate_key_raises_when_unique():
    t = BPlusTree(order=4, unique=True)
    t.insert(1, rid(1))
    with pytest.raises(DuplicateKeyError):
        t.insert(1, rid(2))


def test_non_unique_overwrites():
    t = BPlusTree(order=4, unique=False)
    t.insert(1, rid(1))
    t.insert(1, rid(99))
    assert t.search(1) == rid(99)


def test_range_scan():
    t = BPlusTree(order=4)
    for k in range(0, 100, 5):  # 0,5,10,...,95
        t.insert(k, rid(k))
    got = [k for k, _ in t.range(20, 40)]
    assert got == [20, 25, 30, 35, 40]
    # unbounded low
    assert [k for k, _ in t.range(high=10)] == [0, 5, 10]
    # unbounded high
    assert [k for k, _ in t.range(low=85)] == [85, 90, 95]


def test_delete_simple():
    t = BPlusTree(order=4)
    for k in [10, 20, 30, 40, 50]:
        t.insert(k, rid(k))
    assert t.delete(30) is True
    assert t.search(30) is None
    assert collect_keys(t) == [10, 20, 40, 50]
    assert t.delete(999) is False  # absent


def test_delete_triggers_merges_and_borrows():
    t = BPlusTree(order=3)  # tiny -> lots of structural churn
    for k in range(20):
        t.insert(k, rid(k))
    assert_sorted_chain(t)
    # delete every other key
    for k in range(0, 20, 2):
        assert t.delete(k) is True
        assert_sorted_chain(t)
    remaining = collect_keys(t)
    assert remaining == list(range(1, 20, 2))
    for k in remaining:
        assert t.search(k) == rid(k)


def test_delete_all_shrinks_to_empty_leaf():
    t = BPlusTree(order=3)
    keys = list(range(15))
    for k in keys:
        t.insert(k, rid(k))
    for k in keys:
        t.delete(k)
    assert len(t) == 0
    assert t.root.leaf
    assert collect_keys(t) == []


@pytest.mark.parametrize("seed", [1, 2, 3, 7, 42])
def test_model_based_random_ops(seed):
    """Random insert/delete vs a dict oracle; tree must always agree."""
    rng = random.Random(seed)
    t = BPlusTree(order=4)
    model: dict[int, RID] = {}
    for _ in range(500):
        k = rng.randrange(0, 50)
        if rng.random() < 0.6:
            if k not in model:
                t.insert(k, rid(k))
                model[k] = rid(k)
        else:
            expected = k in model
            assert t.delete(k) == expected
            model.pop(k, None)
        # spot-check a few keys
        for probe in (k, rng.randrange(0, 50)):
            assert t.search(probe) == model.get(probe)
    # final full agreement, in sorted order
    assert collect_keys(t) == sorted(model.keys())
    assert len(t) == len(model)


def test_string_keys():
    t = BPlusTree(order=4)
    for w in ["banana", "apple", "cherry", "date", "fig"]:
        t.insert(w, rid(len(w)))
    assert collect_keys(t) == ["apple", "banana", "cherry", "date", "fig"]
    assert t.search("cherry") == rid(6)
