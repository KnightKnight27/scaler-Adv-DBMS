#include <iostream>
using namespace std;

enum Color { RED, BLACK };

template <typename T> class RedBlackTree {
  struct Node {
    T data;
    Color color;
    Node *left, *right, *parent;

    Node(T val)
        : data(val), color(RED), left(nullptr), right(nullptr),
          parent(nullptr) {}
  };

  Node *root = nullptr;

  void rotateLeft(Node *x) {
    Node *y = x->right;
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

  void rotateRight(Node *x) {
    Node *y = x->left;
    x->left = y->right;

    if (y->right)
      y->right->parent = x;

    y->parent = x->parent;

    if (!x->parent)
      root = y;
    else if (x == x->parent->right)
      x->parent->right = y;
    else
      x->parent->left = y;

    y->right = x;
    x->parent = y;
  }

  void balance(Node *node) {
    while (node != root && node->parent->color == RED) {
      if (node->parent == node->parent->parent->left) {
        Node *uncle = node->parent->parent->right;

        if (uncle && uncle->color == RED) {
          node->parent->color = BLACK;
          uncle->color = BLACK;
          node->parent->parent->color = RED;
          node = node->parent->parent;
        } else {
          if (node == node->parent->right) {
            node = node->parent;
            rotateLeft(node);
          }
          node->parent->color = BLACK;
          node->parent->parent->color = RED;
          rotateRight(node->parent->parent);
        }
      } else {
        Node *uncle = node->parent->parent->left;

        if (uncle && uncle->color == RED) {
          node->parent->color = BLACK;
          uncle->color = BLACK;
          node->parent->parent->color = RED;
          node = node->parent->parent;
        } else {
          if (node == node->parent->left) {
            node = node->parent;
            rotateRight(node);
          }
          node->parent->color = BLACK;
          node->parent->parent->color = RED;
          rotateLeft(node->parent->parent);
        }
      }
    }
    root->color = BLACK;
  }

  void inorder(Node *node) {
    if (!node)
      return;
    inorder(node->left);
    cout << node->data << (node->color == RED ? "(R) " : "(B) ");
    inorder(node->right);
  }

public:
  void insert(T val) {
    Node *node = new Node(val);
    Node *parent = nullptr;
    Node *curr = root;

    while (curr) {
      parent = curr;
      if (node->data < curr->data)
        curr = curr->left;
      else
        curr = curr->right;
    }

    node->parent = parent;

    if (!parent)
      root = node;
    else if (node->data < parent->data)
      parent->left = node;
    else
      parent->right = node;

    balance(node);
  }

  void display() {
    inorder(root);
    cout << endl;
  }
};

int main() {
  RedBlackTree<int> rbTree;
  rbTree.insert(10);
  rbTree.insert(20);
  rbTree.insert(30);
  rbTree.insert(15);
  rbTree.insert(5);
  rbTree.insert(1);
  rbTree.display();
  return 0;
}