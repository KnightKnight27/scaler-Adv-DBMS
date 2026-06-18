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
}
