"""demo_btree.py — B+ tree: search, ordered range scan, splits, delete."""

import _demo
from _demo import banner, step, show

from minidb.btree import BPlusTree
from minidb.heap import RID


def main() -> None:
    banner("B+ TREE INDEX: balanced, ordered, O(log n)")
    t = BPlusTree(order=4)  # small fanout so splits happen quickly

    step("Insert 0..19; node splits keep the tree balanced")
    for k in range(20):
        t.insert(k, RID(k, 0))
    show("keys", len(t))
    show("tree height (levels)", t.height())

    step("Point search via the index")
    show("search(13)", t.search(13))
    show("search(99)", t.search(99))

    step("Ordered range scan 5..10 (walks the linked leaf chain)")
    show("range(5,10)", [k for k, _ in t.range(5, 10)])

    step("Delete keys; borrow/merge keep it a valid B+ tree")
    for k in [0, 1, 2, 3, 4, 5]:
        t.delete(k)
    show("remaining keys", [k for k, _ in t.items()])
    show("height after deletes", t.height())

    print("\nTakeaway: all values live in leaves linked in sorted order, so the same")
    print("structure serves both point lookups and range scans efficiently.")


if __name__ == "__main__":
    main()
