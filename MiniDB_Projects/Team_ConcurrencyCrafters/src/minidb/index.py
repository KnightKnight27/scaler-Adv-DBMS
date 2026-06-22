from __future__ import annotations

import json
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

from .types import RecordID


@dataclass
class BPlusTreeNode:
    is_leaf: bool
    keys: list[int] = field(default_factory=list)
    children: list["BPlusTreeNode"] = field(default_factory=list)
    values: list[Any] = field(default_factory=list)
    next_leaf: "BPlusTreeNode | None" = None


class BPlusTree:
    def __init__(self, order: int = 4):
        if order < 3:
            raise ValueError("B+ tree order must be at least 3.")
        self.order = order
        self.root = BPlusTreeNode(is_leaf=True)

    def search(self, key: int) -> Any | None:
        leaf = self._find_leaf(self.root, key)
        for index, existing_key in enumerate(leaf.keys):
            if existing_key == key:
                return leaf.values[index]
        return None

    def insert(self, key: int, value: Any) -> None:
        split = self._insert_recursive(self.root, key, value)
        if split is None:
            return
        promoted_key, left, right = split
        self.root = BPlusTreeNode(
            is_leaf=False,
            keys=[promoted_key],
            children=[left, right],
        )

    def delete(self, key: int) -> None:
        entries = [(entry_key, entry_value) for entry_key, entry_value in self.iter_items() if entry_key != key]
        self.root = BPlusTreeNode(is_leaf=True)
        for entry_key, entry_value in entries:
            self.insert(entry_key, entry_value)

    def iter_items(self) -> list[tuple[int, Any]]:
        node = self.root
        while not node.is_leaf:
            node = node.children[0]
        items: list[tuple[int, Any]] = []
        while node is not None:
            items.extend(zip(node.keys, node.values))
            node = node.next_leaf
        return items

    def _find_leaf(self, node: BPlusTreeNode, key: int) -> BPlusTreeNode:
        if node.is_leaf:
            return node
        child_index = 0
        while child_index < len(node.keys) and key >= node.keys[child_index]:
            child_index += 1
        return self._find_leaf(node.children[child_index], key)

    def _insert_recursive(
        self, node: BPlusTreeNode, key: int, value: Any
    ) -> tuple[int, BPlusTreeNode, BPlusTreeNode] | None:
        if node.is_leaf:
            return self._insert_into_leaf(node, key, value)

        child_index = 0
        while child_index < len(node.keys) and key >= node.keys[child_index]:
            child_index += 1
        split = self._insert_recursive(node.children[child_index], key, value)
        if split is None:
            return None

        promoted_key, left_child, right_child = split
        node.children[child_index] = left_child
        node.keys.insert(child_index, promoted_key)
        node.children.insert(child_index + 1, right_child)
        if len(node.keys) < self.order:
            return None

        return self._split_internal(node)

    def _insert_into_leaf(
        self, leaf: BPlusTreeNode, key: int, value: Any
    ) -> tuple[int, BPlusTreeNode, BPlusTreeNode] | None:
        for index, existing_key in enumerate(leaf.keys):
            if existing_key == key:
                leaf.values[index] = value
                return None
            if key < existing_key:
                leaf.keys.insert(index, key)
                leaf.values.insert(index, value)
                break
        else:
            leaf.keys.append(key)
            leaf.values.append(value)

        if len(leaf.keys) < self.order:
            return None
        return self._split_leaf(leaf)

    def _split_leaf(self, leaf: BPlusTreeNode) -> tuple[int, BPlusTreeNode, BPlusTreeNode]:
        midpoint = len(leaf.keys) // 2
        left = BPlusTreeNode(
            is_leaf=True,
            keys=leaf.keys[:midpoint],
            values=leaf.values[:midpoint],
        )
        right = BPlusTreeNode(
            is_leaf=True,
            keys=leaf.keys[midpoint:],
            values=leaf.values[midpoint:],
            next_leaf=leaf.next_leaf,
        )
        left.next_leaf = right
        return right.keys[0], left, right

    def _split_internal(
        self, node: BPlusTreeNode
    ) -> tuple[int, BPlusTreeNode, BPlusTreeNode]:
        midpoint = len(node.keys) // 2
        promoted_key = node.keys[midpoint]
        left = BPlusTreeNode(
            is_leaf=False,
            keys=node.keys[:midpoint],
            children=node.children[: midpoint + 1],
        )
        right = BPlusTreeNode(
            is_leaf=False,
            keys=node.keys[midpoint + 1 :],
            children=node.children[midpoint + 1 :],
        )
        return promoted_key, left, right


class PersistentBPlusTree:
    def __init__(self, path: str | Path, *, unique: bool):
        self.path = Path(path)
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self.unique = unique
        self.tree = BPlusTree(order=4)
        self._load()

    def search(self, key: int) -> list[RecordID]:
        payload = self.tree.search(key)
        if payload is None:
            return []
        return [RecordID(page_id=item[0], slot_id=item[1]) for item in payload]

    def insert(self, key: int, rid: RecordID) -> None:
        payload = self.tree.search(key) or []
        if self.unique and payload:
            raise ValueError(f"Duplicate key '{key}' for unique index '{self.path.name}'.")
        if [rid.page_id, rid.slot_id] not in payload:
            payload.append([rid.page_id, rid.slot_id])
        self.tree.insert(key, payload)
        self._save()

    def delete(self, key: int, rid: RecordID) -> None:
        payload = self.tree.search(key) or []
        payload = [entry for entry in payload if entry != [rid.page_id, rid.slot_id]]
        if payload:
            self.tree.insert(key, payload)
        else:
            self.tree.delete(key)
        self._save()

    def entries(self) -> list[tuple[int, list[RecordID]]]:
        rows: list[tuple[int, list[RecordID]]] = []
        for key, payload in self.tree.iter_items():
            rows.append(
                (
                    key,
                    [RecordID(page_id=item[0], slot_id=item[1]) for item in payload],
                )
            )
        return rows

    def rebuild(self, rows: list[tuple[int, RecordID]]) -> None:
        self.tree = BPlusTree(order=4)
        for key, rid in rows:
            self.insert(key, rid)

    def _load(self) -> None:
        if not self.path.exists():
            self._save()
            return
        content = self.path.read_text(encoding="utf-8").strip()
        if not content:
            self._save()
            return
        data = json.loads(content)
        for key, payload in sorted(data.items(), key=lambda item: int(item[0])):
            self.tree.insert(int(key), payload)

    def _save(self) -> None:
        payload = {str(key): value for key, value in self.tree.iter_items()}
        self.path.write_text(json.dumps(payload, indent=2), encoding="utf-8")

