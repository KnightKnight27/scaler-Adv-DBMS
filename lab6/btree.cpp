
#include <iostream>
#include <string>
#include <vector>

struct BTreeNode {
  std::vector<int> keys;
  std::vector<BTreeNode *> children;
  bool isLeaf;

  explicit BTreeNode(bool leaf) : isLeaf(leaf) {}

  ~BTreeNode() {
    for (auto *child : children)
      delete child;
  }

  bool isFull(int degree) const {
    return static_cast<int>(keys.size()) == 2 * degree - 1;
  }
};

class BTree {
private:
  BTreeNode *root;
  int minDegree;

  bool findKey(const BTreeNode *node, int key) const {
    int idx = 0;
    while (idx < static_cast<int>(node->keys.size()) && key > node->keys[idx])
      idx++;

    if (idx < static_cast<int>(node->keys.size()) && node->keys[idx] == key)
      return true;

    if (node->isLeaf)
      return false;

    return findKey(node->children[idx], key);
  }

  void splitFullChild(BTreeNode *parent, int idx) {
    BTreeNode *fullNode = parent->children[idx];
    BTreeNode *sibling = new BTreeNode(fullNode->isLeaf);

    sibling->keys.assign(fullNode->keys.begin() + minDegree,
                         fullNode->keys.end());

    if (!fullNode->isLeaf) {
      sibling->children.assign(fullNode->children.begin() + minDegree,
                               fullNode->children.end());
      fullNode->children.erase(fullNode->children.begin() + minDegree,
                               fullNode->children.end());
    }

    int medianKey = fullNode->keys[minDegree - 1];

    fullNode->keys.erase(fullNode->keys.begin() + (minDegree - 1),
                         fullNode->keys.end());

    parent->children.insert(parent->children.begin() + idx + 1, sibling);
    parent->keys.insert(parent->keys.begin() + idx, medianKey);
  }

  void insertIntoNonFull(BTreeNode *node, int key) {
    int pos = static_cast<int>(node->keys.size()) - 1;

    if (node->isLeaf) {
      node->keys.push_back(0);
      while (pos >= 0 && key < node->keys[pos]) {
        node->keys[pos + 1] = node->keys[pos];
        pos--;
      }
      node->keys[pos + 1] = key;
      return;
    }

    while (pos >= 0 && key < node->keys[pos])
      pos--;
    pos++;

    if (node->children[pos]->isFull(minDegree)) {
      splitFullChild(node, pos);
      if (key > node->keys[pos])
        pos++;
    }

    insertIntoNonFull(node->children[pos], key);
  }

  void inorderWalk(const BTreeNode *node, std::ostream &out,
                   bool &first) const {
    for (std::size_t i = 0; i < node->keys.size(); i++) {
      if (!node->isLeaf)
        inorderWalk(node->children[i], out, first);

      if (!first)
        out << " ";
      out << node->keys[i];
      first = false;
    }
    if (!node->isLeaf)
      inorderWalk(node->children.back(), out, first);
  }

  void printStructure(const BTreeNode *node, int level,
                      std::ostream &out) const {
    std::string indent(static_cast<std::size_t>(level) * 4, ' ');

    out << indent << "[";
    for (std::size_t i = 0; i < node->keys.size(); i++) {
      if (i > 0)
        out << " ";
      out << node->keys[i];
    }
    out << "]";
    if (node->isLeaf)
      out << " (leaf)";
    out << "\n";

    for (auto *child : node->children)
      printStructure(child, level + 1, out);
  }

  std::string checkInvariants(const BTreeNode *node, bool isRoot, int depth,
                              int &expectedLeafDepth) const {
    int numKeys = static_cast<int>(node->keys.size());

    if (numKeys > 2 * minDegree - 1)
      return "node exceeds maximum key count (2t-1)";
    if (!isRoot && numKeys < minDegree - 1)
      return "non-root node has fewer than t-1 keys";

    for (int i = 1; i < numKeys; i++) {
      if (node->keys[i] <= node->keys[i - 1])
        return "keys are not in sorted order";
    }

    if (node->isLeaf) {
      if (expectedLeafDepth < 0)
        expectedLeafDepth = depth;
      else if (depth != expectedLeafDepth)
        return "leaf nodes are at different depths";

      if (!node->children.empty())
        return "leaf node has children (shouldn't happen)";

      return "";
    }

    if (static_cast<int>(node->children.size()) != numKeys + 1)
      return "internal node has incorrect number of children";

    for (int i = 0; i <= numKeys; i++) {
      const BTreeNode *child = node->children[i];
      for (int ck : child->keys) {
        if (i < numKeys && ck >= node->keys[i])
          return "child key exceeds parent upper bound";
        if (i > 0 && ck <= node->keys[i - 1])
          return "child key below parent lower bound";
      }
      std::string err =
          checkInvariants(child, false, depth + 1, expectedLeafDepth);
      if (!err.empty())
        return err;
    }

    return "";
  }

public:
  explicit BTree(int degree) : root(nullptr), minDegree(degree) {
    if (minDegree < 2)
      minDegree = 2;
  }

  ~BTree() { delete root; }

  BTree(const BTree &) = delete;
  BTree &operator=(const BTree &) = delete;

  void insert(int key) {
    if (root == nullptr) {
      root = new BTreeNode(true);
      root->keys.push_back(key);
      return;
    }

    if (root->isFull(minDegree)) {
      BTreeNode *newRoot = new BTreeNode(false);
      newRoot->children.push_back(root);
      splitFullChild(newRoot, 0);
      root = newRoot;
    }

    insertIntoNonFull(root, key);
  }

  bool search(int key) const { return root != nullptr && findKey(root, key); }

  void printSorted(std::ostream &out) const {
    if (root == nullptr) {
      out << "<empty>";
      return;
    }
    bool first = true;
    inorderWalk(root, out, first);
  }

  void printTree(std::ostream &out) const {
    if (root == nullptr) {
      out << "<empty>\n";
      return;
    }
    printStructure(root, 0, out);
  }

  std::string verify() const {
    if (root == nullptr)
      return "";
    int leafDepth = -1;
    return checkInvariants(root, true, 0, leafDepth);
  }
};

void runSmallDemo() {
  std::cout << "\n--- Demo 1: degree t=3, inserting 10 keys ---\n";

  BTree tree(3);
  int vals[] = {10, 20, 5, 6, 12, 30, 7, 17, 25, 15};

  for (int v : vals)
    tree.insert(v);

  std::cout << "Tree structure:\n";
  tree.printTree(std::cout);

  std::cout << "Sorted keys: ";
  tree.printSorted(std::cout);
  std::cout << "\n";

  // search tests
  int queries[] = {6, 15, 100};
  for (int q : queries) {
    std::cout << "search(" << q << ") -> "
              << (tree.search(q) ? "found" : "not found") << "\n";
  }

  std::string result = tree.verify();
  std::cout << "Invariant check: " << (result.empty() ? "PASS" : result)
            << "\n";
}

void runSequentialDemo() {
  std::cout << "\n--- Demo 2: degree t=2 (2-3-4 tree), inserting 1..20 ---\n";

  BTree tree(2);
  for (int i = 1; i <= 20; i++)
    tree.insert(i);

  tree.printTree(std::cout);

  std::cout << "Sorted: ";
  tree.printSorted(std::cout);
  std::cout << "\n";

  std::string result = tree.verify();
  std::cout << "Invariant check: " << (result.empty() ? "PASS" : result)
            << "\n";
}

void runScaleDemo() {
  std::cout << "\n--- Demo 3: degree t=50, inserting 100,000 keys ---\n";

  BTree tree(50);
  for (int i = 0; i < 100000; i++)
    tree.insert(i);

  std::string result = tree.verify();
  std::cout << "Invariant check: " << (result.empty() ? "PASS" : result)
            << "\n";

  int testKeys[] = {0, 1, 99, 12345, 50000, 99999, 100000, -1};
  int found = 0;
  for (int k : testKeys) {
    if (tree.search(k))
      found++;
  }
  std::cout << "Search spot-check: " << found << "/8 found "
            << "(expected 6 — 100000 and -1 are out of range)\n";
}

int main() {
  std::cout << "=== B-Tree Implementation Demo ===\n";

  runSmallDemo();
  runSequentialDemo();
  runScaleDemo();

  return 0;
}
