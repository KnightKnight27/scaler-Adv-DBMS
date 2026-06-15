#include <iostream>
using namespace std;

enum Color { RB_RED, RB_BLACK };

struct RBNode {
  int key;
  Color col;
  RBNode *left, *right, *parent;

  RBNode(int k)
      : key(k), col(RB_RED), left(nullptr), right(nullptr), parent(nullptr) {}
};

class RedBlackTree {
private:
  RBNode *root;

  void leftRotate(RBNode *node) {
    RBNode *rChild = node->right;
    node->right = rChild->left;

    if (rChild->left != nullptr)
      rChild->left->parent = node;

    rChild->parent = node->parent;

    if (node->parent == nullptr)
      root = rChild;
    else if (node == node->parent->left)
      node->parent->left = rChild;
    else
      node->parent->right = rChild;

    rChild->left = node;
    node->parent = rChild;
  }

  void rightRotate(RBNode *node) {
    RBNode *lChild = node->left;
    node->left = lChild->right;

    if (lChild->right != nullptr)
      lChild->right->parent = node;

    lChild->parent = node->parent;

    if (node->parent == nullptr)
      root = lChild;
    else if (node == node->parent->left)
      node->parent->left = lChild;
    else
      node->parent->right = lChild;

    lChild->right = node;
    node->parent = lChild;
  }

  void balanceAfterInsert(RBNode *curr) {
    while (curr->parent != nullptr && curr->parent->col == RB_RED) {
      RBNode *par = curr->parent;
      RBNode *gpar = par->parent;

      if (par == gpar->left) {
        RBNode *uncle = gpar->right;

        if (uncle != nullptr && uncle->col == RB_RED) {
          par->col = RB_BLACK;
          uncle->col = RB_BLACK;
          gpar->col = RB_RED;
          curr = gpar;
        } else {
          if (curr == par->right) {
            leftRotate(par);
            curr = par;
            par = curr->parent;
          }
          rightRotate(gpar);
          par->col = RB_BLACK;
          gpar->col = RB_RED;
        }
      } else {
        RBNode *uncle = gpar->left;

        if (uncle != nullptr && uncle->col == RB_RED) {
          par->col = RB_BLACK;
          uncle->col = RB_BLACK;
          gpar->col = RB_RED;
          curr = gpar;
        } else {
          if (curr == par->left) {
            rightRotate(par);
            curr = par;
            par = curr->parent;
          }
          leftRotate(gpar);
          par->col = RB_BLACK;
          gpar->col = RB_RED;
        }
      }
    }

    root->col = RB_BLACK;
  }

  void inorderHelper(RBNode *node) {
    if (node == nullptr)
      return;

    inorderHelper(node->left);
    cout << node->key << (node->col == RB_RED ? "(R)" : "(B)") << " ";
    inorderHelper(node->right);
  }

  int computeBlackHeight(RBNode *node) {
    if (node == nullptr)
      return 1;

    int leftBH = computeBlackHeight(node->left);
    int rightBH = computeBlackHeight(node->right);

    if (leftBH == -1 || rightBH == -1 || leftBH != rightBH)
      return -1;

    return leftBH + (node->col == RB_BLACK ? 1 : 0);
  }

public:
  RedBlackTree() : root(nullptr) {}

  void insert(int key) {
    RBNode *newNode = new RBNode(key);

    RBNode *curr = root;
    RBNode *par = nullptr;

    while (curr != nullptr) {
      par = curr;
      if (key < curr->key)
        curr = curr->left;
      else
        curr = curr->right;
    }

    newNode->parent = par;

    if (par == nullptr)
      root = newNode;
    else if (key < par->key)
      par->left = newNode;
    else
      par->right = newNode;

    balanceAfterInsert(newNode);
  }

  bool search(int key) {
    RBNode *curr = root;
    while (curr != nullptr) {
      if (key == curr->key)
        return true;
      else if (key < curr->key)
        curr = curr->left;
      else
        curr = curr->right;
    }
    return false;
  }

  void printInorder() {
    inorderHelper(root);
    cout << endl;
  }

  bool verifyBlackHeight() { return computeBlackHeight(root) != -1; }
};

int main() {
  RedBlackTree tree;

  int values[] = {10, 20, 30, 15, 5};
  int n = sizeof(values) / sizeof(values[0]);

  cout << "Inserting keys: ";
  for (int i = 0; i < n; i++) {
    cout << values[i];
    if (i < n - 1)
      cout << ", ";
    tree.insert(values[i]);
  }
  cout << endl;

  cout << "\nIn-order traversal: ";
  tree.printInorder();

  cout << "Black-height valid: " << (tree.verifyBlackHeight() ? "Yes" : "No")
       << endl;

  cout << "\nSearch 15: " << (tree.search(15) ? "Found" : "Not Found") << endl;
  cout << "Search 99: " << (tree.search(99) ? "Found" : "Not Found") << endl;

  return 0;
}
