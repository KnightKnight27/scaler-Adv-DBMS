#include <cassert>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

template <typename Key, typename Row>
class DB {
private:
  struct Entry {
    Key key;
    Row row;

    Entry() = default;
    Entry(const Key &k, const Row &r = Row{}) : key(k), row(r) {}
  };

  struct Node {
    bool leaf;
    std::vector<Entry> keys;
    std::vector<Node *> children;

    explicit Node(bool leaf_) : leaf(leaf_) {}
    ~Node() {
      for (Node *child : children) {
        delete child;
      }
    }

    bool isFull(size_t maxKeys) const {
      return keys.size() == maxKeys;
    }
  };

  Node *root;
  size_t minDegree;
  size_t maxKeys;

  std::optional<Entry> searchRecursive(Node *node, const Key &key) const {
    size_t i = 0;
    while (i < node->keys.size() && key > node->keys[i].key) {
      ++i;
    }

    if (i < node->keys.size() && node->keys[i].key == key) {
      return node->keys[i];
    }

    if (node->leaf) {
      return std::nullopt;
    }

    return searchRecursive(node->children[i], key);
  }

  void splitChild(Node *parent, size_t index) {
    Node *fullChild = parent->children[index];
    Node *newChild = new Node(fullChild->leaf);
    size_t t = minDegree;

    for (size_t j = 0; j < t - 1; ++j) {
      newChild->keys.push_back(fullChild->keys[j + t]);
    }

    if (!fullChild->leaf) {
      for (size_t j = 0; j < t; ++j) {
        newChild->children.push_back(fullChild->children[j + t]);
      }
    }

    Entry midKey = fullChild->keys[t - 1];
    fullChild->keys.resize(t - 1);
    if (!fullChild->leaf) {
      fullChild->children.resize(t);
    }

    parent->children.insert(parent->children.begin() + index + 1, newChild);
    parent->keys.insert(parent->keys.begin() + index, midKey);
  }

  void insertNonFull(Node *node, const Entry &entry) {
    size_t i = node->keys.size();

    if (node->leaf) {
      node->keys.push_back(entry);
      while (i > 0 && node->keys[i].key < node->keys[i - 1].key) {
        std::swap(node->keys[i], node->keys[i - 1]);
        --i;
      }
      return;
    }

    while (i > 0 && entry.key < node->keys[i - 1].key) {
      --i;
    }

    if (node->children[i]->isFull(maxKeys)) {
      splitChild(node, i);
      if (entry.key > node->keys[i].key) {
        ++i;
      }
    }

    insertNonFull(node->children[i], entry);
  }

public:
  explicit DB(size_t degree) : minDegree(degree), maxKeys(2 * degree - 1) {
    root = new Node(true);
  }

  ~DB() {
    delete root;
  }

  std::optional<Entry> Search(const Key &key) const {
    return searchRecursive(root, key);
  }

  void Insert(const Key &key, const Row &row) {
    if (root->isFull(maxKeys)) {
      Node *newRoot = new Node(false);
      newRoot->children.push_back(root);
      splitChild(newRoot, 0);
      root = newRoot;
    }
    insertNonFull(root, Entry(key, row));
  }
};

int main() {
  DB<int, std::string> db(3);

  db.Insert(10, "row10");
  db.Insert(20, "row20");
  db.Insert(5, "row5");
  db.Insert(6, "row6");
  db.Insert(12, "row12");
  db.Insert(30, "row30");
  db.Insert(7, "row7");
  db.Insert(17, "row17");

  auto result = db.Search(6);
  assert(result.has_value());
  assert(result->key == 6);
  assert(result->row == "row6");

  result = db.Search(17);
  assert(result.has_value());
  assert(result->row == "row17");

  result = db.Search(100);
  assert(!result.has_value());

  std::cout << "Lab 6 B-tree implementation tests passed.\n";
  return 0;
}
