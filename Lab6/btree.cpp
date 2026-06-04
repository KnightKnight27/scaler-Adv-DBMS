#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

template <typename K, typename V> class BTree {
private:
  struct Node {
    bool leaf;
    std::vector<K> keys;
    std::vector<V> values;
    std::vector<Node *> children;

    Node(bool isLeaf) : leaf(isLeaf) {}
  };

  int t;
  Node *root;
  int accessCount;

  void splitChild(Node *parent, int idx) {
    Node *left = parent->children[idx];
    Node *right = new Node(left->leaf);

    int mid = t - 1;

    for (int i = mid + 1; i < 2 * t - 1; i++) {
      right->keys.push_back(left->keys[i]);
      right->values.push_back(left->values[i]);
    }

    if (!left->leaf) {
      for (int i = mid + 1; i < 2 * t; i++) {
        right->children.push_back(left->children[i]);
      }
    }

    K midKey = left->keys[mid];
    V midVal = left->values[mid];

    left->keys.resize(mid);
    left->values.resize(mid);
    if (!left->leaf) {
      left->children.resize(mid + 1);
    }

    parent->children.insert(parent->children.begin() + idx + 1, right);
    parent->keys.insert(parent->keys.begin() + idx, midKey);
    parent->values.insert(parent->values.begin() + idx, midVal);
  }

  void insertNonFull(Node *n, K key, V val) {
    int i = static_cast<int>(n->keys.size()) - 1;

    if (n->leaf) {
      n->keys.push_back(K());
      n->values.push_back(V());
      while (i >= 0 && key < n->keys[i]) {
        n->keys[i + 1] = n->keys[i];
        n->values[i + 1] = n->values[i];
        i--;
      }
      n->keys[i + 1] = key;
      n->values[i + 1] = val;
    } else {
      while (i >= 0 && key < n->keys[i]) {
        i--;
      }
      i++;
      if (static_cast<int>(n->children[i]->keys.size()) == 2 * t - 1) {
        splitChild(n, i);
        if (key > n->keys[i]) {
          i++;
        }
      }
      insertNonFull(n->children[i], key, val);
    }
  }

  std::optional<V> search(Node *n, K key) {
    if (!n)
      return std::nullopt;
    accessCount++;
    int i = 0;
    int nkeys = static_cast<int>(n->keys.size());
    while (i < nkeys && key > n->keys[i]) {
      i++;
    }
    if (i < nkeys && key == n->keys[i]) {
      return n->values[i];
    }
    if (n->leaf) {
      return std::nullopt;
    }
    return search(n->children[i], key);
  }

  void traverse(Node *n, int level) {
    if (!n)
      return;
    std::cout << "Level " << level << ": [";
    for (size_t i = 0; i < n->keys.size(); i++) {
      std::cout << n->keys[i] << ":" << n->values[i];
      if (i < n->keys.size() - 1)
        std::cout << ", ";
    }
    std::cout << "] (leaf=" << (n->leaf ? "yes" : "no")
              << ", keys=" << n->keys.size() << ")\n";
    for (Node *c : n->children) {
      traverse(c, level + 1);
    }
  }

  void destroy(Node *n) {
    if (!n)
      return;
    for (Node *c : n->children) {
      destroy(c);
    }
    delete n;
  }

public:
  BTree(int degree) : t(degree), root(nullptr), accessCount(0) {
    if (degree < 2) {
      throw std::invalid_argument("Degree must be >= 2");
    }
  }

  ~BTree() { destroy(root); }

  void insert(K key, V val) {
    if (!root) {
      root = new Node(true);
      root->keys.push_back(key);
      root->values.push_back(val);
      return;
    }
    if (static_cast<int>(root->keys.size()) == 2 * t - 1) {
      Node *newRoot = new Node(false);
      newRoot->children.push_back(root);
      splitChild(newRoot, 0);
      root = newRoot;
    }
    insertNonFull(root, key, val);
  }

  std::optional<V> find(K key) {
    accessCount = 0;
    return search(root, key);
  }

  void printTree() {
    if (!root) {
      std::cout << "Empty tree\n";
      return;
    }
    traverse(root, 0);
  }

  int getAccessCount() const { return accessCount; }

  int getHeight() {
    if (!root)
      return 0;
    int h = 1;
    Node *c = root;
    while (!c->leaf) {
      c = c->children[0];
      h++;
    }
    return h;
  }
};

int main() {
  std::cout << "=== B-Tree Index Demo ===\n\n";

  BTree<int, std::string> tree(3);

  std::cout << "Degree: 3, Max keys per node: 5, Min keys (non-root): 2\n\n";

  std::vector<std::pair<int, std::string>> data = {
      {10, "ten"},    {20, "twenty"},   {30, "thirty"},    {40, "forty"},
      {50, "fifty"},  {60, "sixty"},    {70, "seventy"},   {80, "eighty"},
      {90, "ninety"}, {100, "hundred"}, {110, "hundred10"}};

  std::cout << "Inserting key-value pairs:\n";
  for (auto &p : data) {
    std::cout << "  Inserting " << p.first << " -> " << p.second << "\n";
    tree.insert(p.first, p.second);
  }

  std::cout << "\nTree structure after insertions:\n";
  tree.printTree();
  std::cout << "Tree height: " << tree.getHeight() << "\n\n";

  std::cout << "Search operations:\n";
  for (int k : {10, 50, 90, 110, 25, 75}) {
    auto result = tree.find(k);
    int accesses = tree.getAccessCount();
    if (result.has_value()) {
      std::cout << "  Find " << k << ": FOUND '" << result.value()
                << "' (accessed " << accesses << " nodes)\n";
    } else {
      std::cout << "  Find " << k << ": NOT FOUND (accessed " << accesses
                << " nodes)\n";
    }
  }

  std::cout << "\nStress test - inserting 200..400:\n";
  for (int i = 200; i <= 400; i += 20) {
    tree.insert(i, "val_" + std::to_string(i));
  }

  std::cout << "\nFinal tree structure:\n";
  tree.printTree();
  std::cout << "Tree height: " << tree.getHeight() << "\n";

  return 0;
}