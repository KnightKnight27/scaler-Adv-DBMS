#include <iostream>
#include <queue>

enum Color { RED, BLACK };

template <typename T> struct Node {
  T data;
  bool color;
  Node<T> *left;
  Node<T> *right;
  Node<T> *parent;

  Node(T val)
      : data(val), color(RED), left(nullptr), right(nullptr), parent(nullptr) {}
};

template <typename T> class RedBlackTree {
private:
  Node<T> *root;
  std::size_t nodeCount;

  void rotateLeft(Node<T> *x) {
    Node<T> *y = x->right;
    x->right = y->left;
    if (y->left)
      y->left->parent = x;
    y->parent = x->parent;
    if (!x->parent) {
      root = y;
    } else if (x == x->parent->left) {
      x->parent->left = y;
    } else {
      x->parent->right = y;
    }
    y->left = x;
    x->parent = y;
  }

  void rotateRight(Node<T> *x) {
    Node<T> *y = x->left;
    x->left = y->right;
    if (y->right)
      y->right->parent = x;
    y->parent = x->parent;
    if (!x->parent) {
      root = y;
    } else if (x == x->parent->right) {
      x->parent->right = y;
    } else {
      x->parent->left = y;
    }
    y->right = x;
    x->parent = y;
  }

  void insertFixup(Node<T> *z) {
    while (z->parent && z->parent->color == RED) {
      if (z->parent == z->parent->parent->left) {
        Node<T> *y = z->parent->parent->right;
        if (y && y->color == RED) {
          z->parent->color = BLACK;
          y->color = BLACK;
          z->parent->parent->color = RED;
          z = z->parent->parent;
        } else {
          if (z == z->parent->right) {
            z = z->parent;
            rotateLeft(z);
          }
          z->parent->color = BLACK;
          z->parent->parent->color = RED;
          rotateRight(z->parent->parent);
        }
      } else {
        Node<T> *y = z->parent->parent->left;
        if (y && y->color == RED) {
          z->parent->color = BLACK;
          y->color = BLACK;
          z->parent->parent->color = RED;
          z = z->parent->parent;
        } else {
          if (z == z->parent->left) {
            z = z->parent;
            rotateRight(z);
          }
          z->parent->color = BLACK;
          z->parent->parent->color = RED;
          rotateLeft(z->parent->parent);
        }
      }
    }
    root->color = BLACK;
  }

  void transplant(Node<T> *u, Node<T> *v) {
    if (!u->parent) {
      root = v;
    } else if (u == u->parent->left) {
      u->parent->left = v;
    } else {
      u->parent->right = v;
    }
    if (v)
      v->parent = u->parent;
  }

  Node<T> *minimum(Node<T> *node) {
    while (node->left)
      node = node->left;
    return node;
  }

  void eraseFixup(Node<T> *x) {
    while (x && x != root && x->color == BLACK) {
      if (x == x->parent->left) {
        Node<T> *w = x->parent->right;
        if (w && w->color == RED) {
          w->color = BLACK;
          x->parent->color = RED;
          rotateLeft(x->parent);
          w = x->parent->right;
        }
        if (w && (!w->left || w->left->color == BLACK) &&
            (!w->right || w->right->color == BLACK)) {
          w->color = RED;
          x = x->parent;
        } else {
          if (!w->right || w->right->color == BLACK) {
            if (w->left)
              w->left->color = BLACK;
            w->color = RED;
            rotateRight(w);
            w = x->parent->right;
          }
          w->color = x->parent->color;
          x->parent->color = BLACK;
          if (w->right)
            w->right->color = BLACK;
          rotateLeft(x->parent);
          x = root;
        }
      } else {
        Node<T> *w = x->parent->left;
        if (w && w->color == RED) {
          w->color = BLACK;
          x->parent->color = RED;
          rotateRight(x->parent);
          w = x->parent->left;
        }
        if (w && (!w->right || w->right->color == BLACK) &&
            (!w->left || w->left->color == BLACK)) {
          w->color = RED;
          x = x->parent;
        } else {
          if (!w->left || w->left->color == BLACK) {
            if (w->right)
              w->right->color = BLACK;
            w->color = RED;
            rotateLeft(w);
            w = x->parent->left;
          }
          w->color = x->parent->color;
          x->parent->color = BLACK;
          if (w->left)
            w->left->color = BLACK;
          rotateRight(x->parent);
          x = root;
        }
      }
    }
    if (x)
      x->color = BLACK;
  }

  void erase(Node<T> *z) {
    Node<T> *y = z;
    Color yOriginalColor = y->color ? BLACK : RED;
    Node<T> *x;

    if (!z->left) {
      x = z->right;
      transplant(z, z->right);
    } else if (!z->right) {
      x = z->left;
      transplant(z, z->left);
    } else {
      y = minimum(z->right);
      yOriginalColor = y->color ? BLACK : RED;
      x = y->right;
      if (y->parent == z) {
        if (x)
          x->parent = y;
      } else {
        transplant(y, y->right);
        y->right = z->right;
        if (y->right)
          y->right->parent = y;
      }
      transplant(z, y);
      y->left = z->left;
      if (y->left)
        y->left->parent = y;
      y->color = z->color;
    }
    if (yOriginalColor == BLACK && x) {
      eraseFixup(x);
    }
  }

  bool search(Node<T> *node, T key) const {
    if (!node)
      return false;
    if (key == node->data)
      return true;
    if (key < node->data)
      return search(node->left, key);
    return search(node->right, key);
  }

  void inorder(Node<T> *node) const {
    if (!node)
      return;
    inorder(node->left);
    std::cout << node->data << "(" << (node->color ? "B" : "R") << ") ";
    inorder(node->right);
  }

  void preorder(Node<T> *node) const {
    if (!node)
      return;
    std::cout << node->data << "(" << (node->color ? "B" : "R") << ") ";
    preorder(node->left);
    preorder(node->right);
  }

  void levelOrder() const {
    if (!root)
      return;
    std::queue<Node<T> *> q;
    q.push(root);
    int level = 0;
    while (!q.empty()) {
      int levelSize = q.size();
      std::cout << "Level " << level << ": ";
      for (int i = 0; i < levelSize; i++) {
        Node<T> *node = q.front();
        q.pop();
        std::cout << node->data << "(" << (node->color ? "B" : "R") << ") ";
        if (node->left)
          q.push(node->left);
        if (node->right)
          q.push(node->right);
      }
      std::cout << "\n";
      level++;
    }
  }

  void deleteAll(Node<T> *node) {
    if (!node)
      return;
    deleteAll(node->left);
    deleteAll(node->right);
    delete node;
  }

public:
  RedBlackTree() : root(nullptr), nodeCount(0) {}

  ~RedBlackTree() { deleteAll(root); }

  void insert(T key) {
    Node<T> *z = new Node<T>(key);
    Node<T> *y = nullptr;
    Node<T> *x = root;

    while (x) {
      y = x;
      if (z->data < x->data) {
        x = x->left;
      } else {
        x = x->right;
      }
    }
    z->parent = y;
    if (!y) {
      root = z;
    } else if (z->data < y->data) {
      y->left = z;
    } else {
      y->right = z;
    }

    nodeCount++;
    insertFixup(z);
  }

  void remove(T key) {
    Node<T> *z = root;
    while (z) {
      if (key == z->data)
        break;
      if (key < z->data) {
        z = z->left;
      } else {
        z = z->right;
      }
    }
    if (z) {
      erase(z);
      nodeCount--;
    }
  }

  bool contains(T key) const { return search(root, key); }

  bool empty() const { return nodeCount == 0; }

  std::size_t size() const { return nodeCount; }

  void inorderTraversal() const {
    inorder(root);
    std::cout << "\n";
  }

  void preorderTraversal() const {
    preorder(root);
    std::cout << "\n";
  }

  void levelOrderTraversal() const { levelOrder(); }

  void printTree() const {
    std::cout << "=== Inorder ===\n";
    inorderTraversal();
    std::cout << "=== Level Order ===\n";
    levelOrderTraversal();
  }
};

int main() {
  std::cout << "=== Red-Black Tree Demo ===\n\n";

  RedBlackTree<int> tree;

  std::cout << "Inserting: 7, 3, 18, 10, 22, 8, 11, 26, 2, 14\n";
  tree.insert(7);
  tree.insert(3);
  tree.insert(18);
  tree.insert(10);
  tree.insert(22);
  tree.insert(8);
  tree.insert(11);
  tree.insert(26);
  tree.insert(2);
  tree.insert(14);

  tree.printTree();

  std::cout << "\nTree size: " << tree.size() << "\n";
  std::cout << "Contains 10: " << (tree.contains(10) ? "yes" : "no") << "\n";
  std::cout << "Contains 15: " << (tree.contains(15) ? "yes" : "no") << "\n";

  std::cout << "\nRemoving 10...\n";
  tree.remove(10);
  tree.printTree();

  std::cout << "\nRemoving 3...\n";
  tree.remove(3);
  tree.printTree();

  std::cout << "\n=== String Keys Demo ===\n";
  RedBlackTree<std::string> strTree;
  strTree.insert("apple");
  strTree.insert("banana");
  strTree.insert("cherry");
  strTree.insert("date");
  strTree.inorderTraversal();

  return 0;
}