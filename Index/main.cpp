#include <iostream>

// [  [] 10|ROW_OF_10 [] 20|ROW_OF_20  [] 40|ROW_OF_40 []]
//    /
#include <climits>
#include <vector>
template <typename Key, typename Row> class DB {
private:
  struct Entry {
    Key key;
    Row row;
    Entry() {} // default constructor
    Entry(Key k) : key(k) {}
    Entry(Key k, Row r) : key(k), row(r) {}
  };
  struct BTree {
    bool isLeaf;
    std::vector<Entry> keys;
    std::vector<BTree *> children;
    BTree(bool leaf) { isLeaf = leaf; }
  };

  BTree *root;
  size_t minDegree; // CHILDREN IN A HALF NODE
  size_t maxDegree; // CHILDREN IN A FULL NODE
  size_t minKeys;
  size_t maxKeys;

  bool isFull(BTree *node) {
    return node->keys.size() == maxKeys;
  }

public:
  DB(size_t degree) {
    minDegree = degree;
    maxDegree = 2 * degree;
    minKeys = degree - 1;
    maxKeys = maxDegree - 1;
    root = new BTree(true);
  }

  Entry Search(Key key) {
    return SearchRecursive(key, root);
  }

  Entry SearchRecursive(Key key, BTree *node) {
    if (node == nullptr)
      return Entry(Key(-1));

    size_t i = 0;
    while (i < node->keys.size() && key > node->keys[i].key)
      i++;

    if (i < node->keys.size() && node->keys[i].key == key)
      return node->keys[i];

    if (node->isLeaf)
      return Entry(Key(-1));

    return SearchRecursive(key, node->children[i]);
  }

  void Insert(Key key, Row row) {
    if (root == nullptr) {
      root = new BTree(true);
      root->keys.push_back(Entry(key, row));
      return;
    }

    if (isFull(root)) {
      BTree *newRoot = new BTree(false);
      newRoot->children.push_back(root);
      splitChild(newRoot, 0, root);
      
      int i = 0;
      if (newRoot->keys[0].key < key)
        i++;
      insertNonFull(newRoot->children[i], key, row);
      root = newRoot;
    } else {
      insertNonFull(root, key, row);
    }
  }

private:
  void insertNonFull(BTree *node, Key key, Row row) {
    int i = (int)node->keys.size() - 1;

    if (node->isLeaf) {
      node->keys.push_back(Entry()); // dummy space
      while (i >= 0 && node->keys[i].key > key) {
        node->keys[i + 1] = node->keys[i];
        i--;
      }
      node->keys[i + 1] = Entry(key, row);
    } else {
      while (i >= 0 && node->keys[i].key > key)
        i--;
      i++;
      if (isFull(node->children[i])) {
        splitChild(node, i, node->children[i]);
        if (node->keys[i].key < key)
          i++;
      }
      insertNonFull(node->children[i], key, row);
    }
  }

  void splitChild(BTree *parent, int i, BTree *y) {
    BTree *z = new BTree(y->isLeaf);
    
    for (size_t j = 0; j < minKeys; j++)
      z->keys.push_back(y->keys[j + minDegree]);

    if (!y->isLeaf) {
      for (size_t j = 0; j < minDegree; j++)
        z->children.push_back(y->children[j + minDegree]);
    }

    Entry midKey = y->keys[minKeys];
    
    y->keys.resize(minKeys);
    if (!y->isLeaf)
      y->children.resize(minDegree);

    parent->children.insert(parent->children.begin() + i + 1, z);
    parent->keys.insert(parent->keys.begin() + i, midKey);
  }
};

int main() {}