// Name: Vanditabyaa Dwivedi
// Roll Number: 24BCS10505

#include <iostream>
#include <vector>

using namespace std;

enum Color { RED, BLACK };

struct Node {
  int data;
  Color color;

  Node* left;
  Node* right;
  Node* parent;

  Node(int val)
      : data(val), color(RED), left(nullptr), right(nullptr), parent(nullptr) {}
};

class RBTree {
 private:
  Node* root;

 private:
  void rotateLeft(Node* x) {
    Node* y = x->right;

    x->right = y->left;

    if (y->left)
      y->left->parent = x;

    y->parent = x->parent;

    if (!x->parent)
      root = y;

    else if (x == x->parent->left)
      x->parent->left = y;

    else
      x->parent->right = y;

    y->left = x;
    x->parent = y;
  }

  void rotateRight(Node* x) {
    Node* y = x->left;

    x->left = y->right;

    if (y->right)
      y->right->parent = x;

    y->parent = x->parent;

    if (!x->parent)
      root = y;

    else if (x == x->parent->left)
      x->parent->left = y;

    else
      x->parent->right = y;

    y->right = x;
    x->parent = y;
  }

  void fixInsert(Node* node) {
    while (node != root && node->parent && node->parent->color == RED) {
      Node* parent = node->parent;
      Node* grandParent = parent->parent;

      // Parent is left child
      if (parent == grandParent->left) {
        Node* uncle = grandParent->right;

        // Case 1: Uncle is RED
        if (uncle && uncle->color == RED) {
          parent->color = BLACK;
          uncle->color = BLACK;
          grandParent->color = RED;

          node = grandParent;
        } else {
          // Case 2: Triangle
          if (node == parent->right) {
            rotateLeft(parent);

            node = parent;
            parent = node->parent;
          }

          // Case 3: Line
          rotateRight(grandParent);

          parent->color = BLACK;
          grandParent->color = RED;
        }
      } else {
        Node* uncle = grandParent->left;

        // Mirror Case 1
        if (uncle && uncle->color == RED) {
          parent->color = BLACK;
          uncle->color = BLACK;
          grandParent->color = RED;

          node = grandParent;
        } else {
          // Mirror Case 2
          if (node == parent->left) {
            rotateRight(parent);

            node = parent;
            parent = node->parent;
          }

          // Mirror Case 3
          rotateLeft(grandParent);

          parent->color = BLACK;
          grandParent->color = RED;
        }
      }
    }

    root->color = BLACK;
  }

 public:
  RBTree() : root(nullptr) {}

  void insert(int val) {
    Node* newNode = new Node(val);

    Node* current = root;
    Node* parent = nullptr;

    while (current) {
      parent = current;

      if (val < current->data)
        current = current->left;

      else if (val > current->data)
        current = current->right;

      // Duplicate value
      else {
        delete newNode;
        return;
      }
    }

    newNode->parent = parent;

    if (!parent)
      root = newNode;

    else if (val < parent->data)
      parent->left = newNode;

    else
      parent->right = newNode;

    fixInsert(newNode);
  }

  bool search(int val) {
    Node* current = root;

    while (current) {
      if (val == current->data)
        return true;

      if (val < current->data)
        current = current->left;

      else
        current = current->right;
    }

    return false;
  }
};

int main() {
  RBTree tree;

  vector<int> values = {10, 20, 30, 15, 5, 1, 50, 60, 70, 65, 55};

  for (int val : values) {
    cout << "Inserting " << val << endl;

    tree.insert(val);
  }

  cout << endl;

  cout << "Search 15: " << (tree.search(15) ? "Found" : "Not Found") << endl;

  cout << "Search 65: " << (tree.search(65) ? "Found" : "Not Found") << endl;

  cout << "Search 100: " << (tree.search(100) ? "Found" : "Not Found") << endl;

  return 0;
}