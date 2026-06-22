// btree.cpp - btree implementation
#include "btree.h"
#include <iostream>
#include <queue>

BTree::Node::Node(bool leaf) : is_leaf(leaf) {}

// constructor
BTree::BTree(int t) : root_(new Node(true)), min_degree_(t) {}

// destructor
BTree::~BTree() {
  free_node(root_);
}

// split the child of a node
void BTree::split_child(Node *parent, int index, Node *child) {
  Node *sibling = new Node(child->is_leaf);
  int mid = min_degree_ - 1;

  // move the second half of child's keys to sibling
  for (int i = mid + 1; i < (int)child->keys.size(); ++i) {
    sibling->keys.push_back(child->keys[i]);
  }

  // move the second half of child's children to sibling (if not a leaf)
  if (!child->is_leaf) {
    for (int i = mid + 1; i < (int)child->children.size(); ++i) {
      sibling->children.push_back(child->children[i]);
    }
  }

  int up = child->keys[mid];

  // resize child to retain only the first half
  child->keys.resize(mid);
  if (!child->is_leaf) {
    child->children.resize(mid + 1);
  }

  // insert sibling pointer into parent's children at index + 1
  parent->children.insert(parent->children.begin() + index + 1, sibling);

  // insert the median key into parent's keys at index
  parent->keys.insert(parent->keys.begin() + index, up);
}

// insert a key when the node is guaranteed not to be full
void BTree::insert_nonfull(Node *node, int key) {
  if (node->is_leaf) {
    // classic insertion sort to insert key in sorted order
    int i = (int)node->keys.size() - 1;
    node->keys.push_back(0); // add a placeholder at the end
    while (i >= 0 && node->keys[i] > key) {
      node->keys[i + 1] = node->keys[i];
      --i;
    }
    node->keys[i + 1] = key;
  } else {
    // find the child that will have the new key
    int i = 0;
    while (i < (int)node->keys.size() && key > node->keys[i]) {
      ++i;
    }

    // if the child is full, split it first
    if ((int)node->children[i]->keys.size() == 2 * min_degree_ - 1) {
      split_child(node, i, node->children[i]);
      // after split, the middle key of children[i] moves up to parent,
      // and children[i] is split into children[i] and sibling.
      // determine which of the two halves should contain the key.
      if (key > node->keys[i]) {
        ++i;
      }
    }
    insert_nonfull(node->children[i], key);
  }
}

// private search helper
bool BTree::contains(const Node *node, int key) const {
  int i = 0;
  // find the first key greater than or equal to key
  while (i < (int)node->keys.size() && key > node->keys[i]) {
    ++i;
  }

  // if key is found in this node, return true
  if (i < (int)node->keys.size() && node->keys[i] == key) {
    return true;
  }

  // if this is a leaf and key was not found here, it's not in the tree
  if (node->is_leaf) {
    return false;
  }

  // otherwise, search recursively in the appropriate child
  return contains(node->children[i], key);
}

// print inorder keys of the subtree
void BTree::print_inorder(const Node *node) const {
  size_t i;
  for (i = 0; i < node->keys.size(); ++i) {
    if (!node->is_leaf) {
      print_inorder(node->children[i]);
    }
    std::cout << node->keys[i] << " ";
  }
  if (!node->is_leaf) {
    print_inorder(node->children[i]);
  }
}

// collect inorder keys of the subtree into a vector
void BTree::collect_inorder(const Node *node, std::vector<int> &out) const {
  size_t i;
  for (i = 0; i < node->keys.size(); ++i) {
    if (!node->is_leaf) {
      collect_inorder(node->children[i], out);
    }
    out.push_back(node->keys[i]);
  }
  if (!node->is_leaf) {
    collect_inorder(node->children[i], out);
  }
}

// find first key index greater than or equal to the target
int BTree::get_key_index(const Node *node, int key) const {
  int idx = 0;
  while (idx < (int)node->keys.size() && node->keys[idx] < key) {
    ++idx;
  }
  return idx;
}

// delete key from leaf
void BTree::remove_from_leaf(Node *node, int idx) {
  node->keys.erase(node->keys.begin() + idx);
}

// get predecessor key (maximum in left subtree)
int BTree::get_predecessor(const Node *node, int idx) const {
  const Node *cur = node->children[idx];
  while (!cur->is_leaf) {
    cur = cur->children.back();
  }
  return cur->keys.back();
}

// get successor key (minimum in right subtree)
int BTree::get_successor(const Node *node, int idx) const {
  const Node *cur = node->children[idx + 1];
  while (!cur->is_leaf) {
    cur = cur->children.front();
  }
  return cur->keys.front();
}

// merge left and right children around parent key idx
void BTree::merge_nodes(Node *node, int idx) {
  Node *left = node->children[idx];
  Node *right = node->children[idx + 1];

  // pull down parent key into left child
  left->keys.push_back(node->keys[idx]);

  // copy right child's keys to left child
  for (int k : right->keys) {
    left->keys.push_back(k);
  }

  // copy right child's child pointers to left child (if not a leaf)
  if (!left->is_leaf) {
    for (Node *c : right->children) {
      left->children.push_back(c);
    }
  }

  // remove key and child reference from parent
  node->keys.erase(node->keys.begin() + idx);
  node->children.erase(node->children.begin() + idx + 1);

  delete right;
}

// borrow key from left sibling
void BTree::borrow_from_prev(Node *node, int idx) {
  Node *child = node->children[idx];
  Node *sibling = node->children[idx - 1];

  // insert parent key as the first element in child keys
  child->keys.insert(child->keys.begin(), node->keys[idx - 1]);

  // move sibling's last child to become child's first child
  if (!child->is_leaf) {
    child->children.insert(child->children.begin(), sibling->children.back());
    sibling->children.pop_back();
  }

  // move sibling's last key to parent key
  node->keys[idx - 1] = sibling->keys.back();
  sibling->keys.pop_back();
}

// borrow key from right sibling
void BTree::borrow_from_next(Node *node, int idx) {
  Node *child = node->children[idx];
  Node *sibling = node->children[idx + 1];

  // append parent key to child keys
  child->keys.push_back(node->keys[idx]);

  // move sibling's first child to become child's last child
  if (!child->is_leaf) {
    child->children.push_back(sibling->children.front());
    sibling->children.erase(sibling->children.begin());
  }

  // move sibling's first key to parent key
  node->keys[idx] = sibling->keys.front();
  sibling->keys.erase(sibling->keys.begin());
}

// ensure child node at idx has at least min_degree_ keys
void BTree::fill_child(Node *node, int idx) {
  // if left sibling has extra keys, borrow from it
  if (idx != 0 && (int)node->children[idx - 1]->keys.size() >= min_degree_) {
    borrow_from_prev(node, idx);
  }
  // if right sibling has extra keys, borrow from it
  else if (idx != (int)node->keys.size() && (int)node->children[idx + 1]->keys.size() >= min_degree_) {
    borrow_from_next(node, idx);
  }
  // otherwise, merge child with a sibling
  else {
    if (idx != (int)node->keys.size()) {
      merge_nodes(node, idx);
    } else {
      merge_nodes(node, idx - 1);
    }
  }
}

// delete key from an internal node
void BTree::remove_from_non_leaf(Node *node, int idx) {
  int key = node->keys[idx];

  // case a: if left child has >= t keys, find predecessor and replace
  if ((int)node->children[idx]->keys.size() >= min_degree_) {
    int pred = get_predecessor(node, idx);
    node->keys[idx] = pred;
    remove_node(node->children[idx], pred);
  }
  // case b: if right child has >= t keys, find successor and replace
  else if ((int)node->children[idx + 1]->keys.size() >= min_degree_) {
    int succ = get_successor(node, idx);
    node->keys[idx] = succ;
    remove_node(node->children[idx + 1], succ);
  }
  // case c: if both children have t-1 keys, merge them first
  else {
    merge_nodes(node, idx);
    remove_node(node->children[idx], key);
  }
}

// recursive deletion routine
void BTree::remove_node(Node *node, int key) {
  int idx = get_key_index(node, key);

  // key is present in this node
  if (idx < (int)node->keys.size() && node->keys[idx] == key) {
    if (node->is_leaf) {
      remove_from_leaf(node, idx);
    } else {
      remove_from_non_leaf(node, idx);
    }
  }
  // key is not present in this node
  else {
    // if leaf node, key doesn't exist in tree
    if (node->is_leaf) {
      return;
    }

    bool is_last_child = (idx == (int)node->keys.size());

    // ensure the child has at least t keys before descending
    if ((int)node->children[idx]->keys.size() < min_degree_) {
      fill_child(node, idx);
    }

    // after fill_child, the child might have merged with sibling
    if (is_last_child && idx > (int)node->keys.size()) {
      remove_node(node->children[idx - 1], key);
    } else {
      remove_node(node->children[idx], key);
    }
  }
}

// post-order node deallocation (destructor helper)
void BTree::free_node(Node *node) {
  if (!node) {
    return;
  }
  if (!node->is_leaf) {
    for (Node *child : node->children) {
      free_node(child);
    }
  }
  delete node;
}

// public insert
void BTree::insert(int key) {
  // if root is full, split it and grow tree height
  if ((int)root_->keys.size() == 2 * min_degree_ - 1) {
    Node *new_root = new Node(false);
    new_root->children.push_back(root_);
    split_child(new_root, 0, root_);
    root_ = new_root;
  }
  insert_nonfull(root_, key);
}

// public remove
void BTree::remove(int key) {
  if (root_->keys.empty()) {
    return;
  }
  remove_node(root_, key);

  // if root keys became empty, shrink tree height
  if (root_->keys.empty()) {
    Node *old_root = root_;
    if (root_->is_leaf) {
      root_ = new Node(true);
    } else {
      root_ = root_->children[0];
    }
    delete old_root;
  }
}

// public search
bool BTree::search(int key) const {
  return contains(root_, key);
}

// public inorder print
void BTree::print_inorder() const {
  print_inorder(root_);
  std::cout << std::endl;
}

// public level order print
void BTree::print_levels() const {
  if (root_->keys.empty()) {
    std::cout << "(empty tree)\n";
    return;
  }

  std::queue<std::pair<const Node *, int>> q;
  q.push({root_, 0});
  int cur_level = -1;

  while (!q.empty()) {
    auto pr = q.front();
    q.pop();
    const Node *n = pr.first;
    int lvl = pr.second;

    if (lvl != cur_level) {
      if (cur_level != -1) {
        std::cout << '\n';
      }
      std::cout << "L" << lvl << ":  ";
      cur_level = lvl;
    }

    std::cout << "[";
    for (size_t i = 0; i < n->keys.size(); ++i) {
      std::cout << n->keys[i];
      if (i + 1 < n->keys.size()) {
        std::cout << ", ";
      }
    }
    std::cout << "]  ";

    if (!n->is_leaf) {
      for (const Node *c : n->children) {
        q.push({c, lvl + 1});
      }
    }
  }
  std::cout << '\n';
}

// public inorder collector
void BTree::collect_inorder(std::vector<int> &out) const {
  collect_inorder(root_, out);
}