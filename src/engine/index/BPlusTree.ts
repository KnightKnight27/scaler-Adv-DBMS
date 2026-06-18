import { IndexEntry } from '../types';

export class BPlusNode {
  isLeaf: boolean;
  keys: number[];
  children: (BPlusNode | IndexEntry)[];
  next: BPlusNode | null = null;

  constructor(isLeaf: boolean) {
    this.isLeaf = isLeaf;
    this.keys = [];
    this.children = [];
  }
}

export class BPlusTree {
  root: BPlusNode;
  degree: number;

  constructor(degree: number = 3) {
    this.root = new BPlusNode(true);
    this.degree = degree;
  }

  search(key: number): IndexEntry | null {
    let curr = this.root;
    while (!curr.isLeaf) {
      let found = false;
      for (let i = 0; i < curr.keys.length; i++) {
        if (key < curr.keys[i]) {
          curr = curr.children[i] as BPlusNode;
          found = true;
          break;
        }
      }
      if (!found) {
        curr = curr.children[curr.children.length - 1] as BPlusNode;
      }
    }

    for (let i = 0; i < curr.keys.length; i++) {
      if (curr.keys[i] === key) {
        return curr.children[i] as IndexEntry;
      }
    }
    return null;
  }

  insert(key: number, entry: IndexEntry) {
    const root = this.root;
    if (root.keys.length >= this.degree) {
      const newRoot = new BPlusNode(false);
      newRoot.children.push(this.root);
      this.splitChild(newRoot, 0, this.root);
      this.root = newRoot;
    }
    this.insertNonFull(this.root, key, entry);
  }

  private insertNonFull(node: BPlusNode, key: number, entry: IndexEntry) {
    if (node.isLeaf) {
      let idx = node.keys.findIndex(k => k > key);
      if (idx === -1) {
        node.keys.push(key);
        node.children.push(entry);
      } else {
        node.keys.splice(idx, 0, key);
        node.children.splice(idx, 0, entry);
      }
    } else {
      let idx = node.keys.findIndex(k => key < k);
      if (idx === -1) idx = node.keys.length;
      let child = node.children[idx] as BPlusNode;
      if (child.keys.length >= this.degree) {
        this.splitChild(node, idx, child);
        if (key > node.keys[idx]) {
          child = node.children[idx + 1] as BPlusNode;
        }
      }
      this.insertNonFull(child, key, entry);
    }
  }

  private splitChild(parent: BPlusNode, idx: number, child: BPlusNode) {
    const sibling = new BPlusNode(child.isLeaf);
    const mid = Math.floor(child.keys.length / 2);
    const pushUpKey = child.keys[mid];

    if (child.isLeaf) {
      sibling.keys = child.keys.slice(mid);
      sibling.children = child.children.slice(mid);
      child.keys = child.keys.slice(0, mid);
      child.children = child.children.slice(0, mid);

      sibling.next = child.next;
      child.next = sibling;
    } else {
      sibling.keys = child.keys.slice(mid + 1);
      sibling.children = child.children.slice(mid + 1);
      child.keys = child.keys.slice(0, mid);
      child.children = child.children.slice(0, mid + 1);
    }

    parent.keys.splice(idx, 0, pushUpKey);
    parent.children.splice(idx + 1, 0, sibling);
  }
}
