// btree.h - btree declaration
#ifndef BTREE_H
#define BTREE_H

#include <vector>

class BTree {
public:
  struct Node {
    bool is_leaf;
    std::vector<int> keys;
    std::vector<Node *> children;

    explicit Node(bool leaf);
  };

private:
  Node *root_;
  int min_degree_; // minimum degree 't' (defines bounds on number of keys/children)

  // internal helper methods
  void split_child(Node *parent, int index, Node *child);
  void insert_nonfull(Node *node, int key);
  bool contains(const Node *node, int key) const;
  void print_inorder(const Node *node) const;
  void collect_inorder(const Node *node, std::vector<int> &out) const;
  int get_key_index(const Node *node, int key) const;
  void remove_from_leaf(Node *node, int idx);
  int get_predecessor(const Node *node, int idx) const;
  int get_successor(const Node *node, int idx) const;
  void merge_nodes(Node *node, int idx);
  void borrow_from_prev(Node *node, int idx);
  void borrow_from_next(Node *node, int idx);
  void fill_child(Node *node, int idx);
  void remove_from_non_leaf(Node *node, int idx);
  void remove_node(Node *node, int key);
  void free_node(Node *node);

public:
  explicit BTree(int t);
  ~BTree();

  // disable copying to prevent double-free issues of internal node pointers
  BTree(const BTree &) = delete;
  BTree &operator=(const BTree &) = delete;

  void insert(int key);
  void remove(int key);
  bool search(int key) const;

  void print_inorder() const;
  void print_levels() const;

  // test helper: collect inorder keys into vector
  void collect_inorder(std::vector<int> &out) const;
};

#endif // BTREE_H