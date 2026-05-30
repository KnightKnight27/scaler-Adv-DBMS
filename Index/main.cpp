#include <iostream>
#include <climits>
#include <vector>
#include <algorithm>

template <typename Key, typename Row> class DB {
private:
  struct Entry {
    Key key;
    Row row;
    Entry() : key(Key()), row(Row()) {}
    Entry(Key k, Row r) : key(k), row(r) {}
  };
  
  struct BTree {
    std::vector<Entry> keys;
    std::vector<BTree *> children;
    bool isLeaf;
    
    BTree() : isLeaf(true) {}
  };

  BTree *root;
  size_t minDegree; // CHILDREN IN A HALF NODE
  size_t maxDegree; // CHILDREN IN A FULL NODE
  size_t minKeys;
  size_t maxKeys;

  bool isFull(BTree *node) {
    return node->keys.size() == maxKeys;
  }

  bool isLeaf(BTree *node) {
    return node->isLeaf;
  }

  void splitChild(BTree *parent, size_t index) {
    BTree *fullChild = parent->children[index];
    BTree *newChild = new BTree();
    
    size_t mid = minKeys;
    
    // Copy the second half of keys to newChild
    newChild->keys.assign(fullChild->keys.begin() + mid + 1, fullChild->keys.end());
    
    // Copy the second half of children if not a leaf
    if (!fullChild->isLeaf) {
      newChild->children.assign(fullChild->children.begin() + mid + 1, fullChild->children.end());
    }
    
    newChild->isLeaf = fullChild->isLeaf;
    
    // Reduce keys in fullChild
    fullChild->keys.erase(fullChild->keys.begin() + mid, fullChild->keys.end());
    if (!fullChild->isLeaf) {
      fullChild->children.erase(fullChild->children.begin() + mid + 1, fullChild->children.end());
    }
    
    // Insert middle key to parent
    parent->keys.insert(parent->keys.begin() + index, fullChild->keys[mid]);
    parent->children.insert(parent->children.begin() + index + 1, newChild);
  }

  void insertNonFull(BTree *node, Key key, Row row) {
    int i = node->keys.size() - 1;
    
    if (node->isLeaf) {
      // Find position and insert
      node->keys.resize(node->keys.size() + 1);
      while (i >= 0 && key < node->keys[i].key) {
        node->keys[i + 1] = node->keys[i];
        i--;
      }
      node->keys[i + 1] = Entry(key, row);
    } else {
      // Find child to insert into
      while (i >= 0 && key < node->keys[i].key) {
        i--;
      }
      i++;
      
      BTree *child = node->children[i];
      if (isFull(child)) {
        splitChild(node, i);
        if (key > node->keys[i].key) {
          i++;
        }
        child = node->children[i];
      }
      insertNonFull(child, key, row);
    }
  }

public:
  DB(size_t degree) {
    minDegree = degree;
    maxDegree = 2 * degree;
    minKeys = degree - 1;
    maxKeys = 2 * degree - 1;
    root = new BTree();
  }

  Entry Search(Key key) {
    return SearchRecursive(key, root);
  }

  Entry SearchRecursive(Key key, BTree *node) {
    // BASE CONDITION
    if (node == nullptr)
      return Entry();

    int i = 0;
    // Find the first key greater than or equal to search key
    while (i < (int)node->keys.size() && key > node->keys[i].key) {
      i++;
    }

    // Check if key is found
    if (i < (int)node->keys.size() && key == node->keys[i].key) {
      return node->keys[i];
    }

    // If leaf node, key is not present
    if (node->isLeaf) {
      return Entry();
    }

    // Recursively search in appropriate child
    return SearchRecursive(key, node->children[i]);
  }

  void Insert(Key key, Row row) {
    BTree *rootPtr = root;

    if (isFull(rootPtr)) {
      // Root is full, create new root
      BTree *newRoot = new BTree();
      newRoot->isLeaf = false;
      newRoot->children.push_back(rootPtr);
      splitChild(newRoot, 0);
      root = newRoot;
      insertNonFull(newRoot, key, row);
    } else {
      insertNonFull(rootPtr, key, row);
    }
  }
};

int main() {
  DB<int, std::string> database(3);
  
  // Test insertions
  database.Insert(10, "Row10");
  database.Insert(20, "Row20");
  database.Insert(5, "Row5");
  database.Insert(25, "Row25");
  
  // Test search
  auto result = database.Search(20);
  if (!result.key == 0) {
    std::cout << "Found key: " << result.key << " with row: " << result.row << std::endl;
  } else {
    std::cout << "Key not found" << std::endl;
  }
  
  return 0;
}