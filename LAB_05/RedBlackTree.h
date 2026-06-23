#ifndef RB_TREE_H
#define RB_TREE_H

#include <vector>

// -------------------------------------------------------
//  Red-Black Tree — self-balancing BST with colour bits
//  Author: Ayush Kumar Patra (24bcs10474)
// -------------------------------------------------------

class RBTree {
public:
  enum Colour { BLACK = 0, RED = 1 };

  struct TreeNode {
    int key;
    TreeNode *left;
    TreeNode *right;
    TreeNode *parent;
    Colour colour;

    TreeNode(int k)
        : key(k), left(nullptr), right(nullptr), parent(nullptr),
          colour(Colour::RED) {}
  };

  RBTree();
  ~RBTree();

  bool search(int key);
  void insert(int key);
  void remove(int key);

  void printLevelOrder();

  TreeNode *SENTINEL;

private:
  TreeNode *root_;

  // Insertion rebalancing
  void rebalanceAfterInsert(TreeNode *nd);

  bool checkCase0(TreeNode *nd);
  bool checkCase1(TreeNode *nd);
  bool checkCase2(TreeNode *nd);
  bool checkCase3(TreeNode *nd);

  void resolveCase0(TreeNode *nd);
  void resolveCase1(TreeNode *nd);
  void resolveCase2(TreeNode *nd);
  void resolveCase3(TreeNode *nd);

  // Tree rotations
  void leftRotate(TreeNode *nd);
  void rightRotate(TreeNode *nd);

  // Deletion utilities
  TreeNode *locateNode(int key);
  TreeNode *subtreeMin(TreeNode *nd);
  void replaceSubtree(TreeNode *target, TreeNode *replacement);
  void rebalanceAfterDelete(TreeNode *nd);

  // Ancestor helpers
  TreeNode *fetchGrandparent(TreeNode *nd);
  TreeNode *fetchUncle(TreeNode *nd);

  void deallocate(TreeNode *nd);
};

#endif