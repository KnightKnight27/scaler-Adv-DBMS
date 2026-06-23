#ifndef RBT_HPP
#define RBT_HPP

#include <iostream>
#include <vector>
#include <string>
#include <utility>

// Enum representing the two states of a node in a Red-Black Tree
enum NodeColor { COLOR_RED, COLOR_BLACK };

// Structure representing a single node in the Red-Black Tree
struct RBTNode {
    int key;
    NodeColor color;
    RBTNode* left;
    RBTNode* right;
    RBTNode* parent;

    RBTNode(int val, NodeColor col = COLOR_RED)
        : key(val), color(col), left(nullptr), right(nullptr), parent(nullptr) {}
};

// Red-Black Tree Class Interface
class RedBlackTree {
public:
    RedBlackTree();
    ~RedBlackTree();

    // Core B-tree preparation operations
    void insert(int key);
    RBTNode* search(int key);
    void printTree();

    // Verification & Structural Auditing
    int getHeight();
    int getBlackHeight();
    bool isRBBalanced();
    bool isAVLBalanced();
    std::vector<std::pair<int, std::pair<int, int>>> getNodeHeightsAndBalances();

private:
    RBTNode* root;
    RBTNode* NIL; // Sentinel node representing null leaves

    // Internal balancing and rotation mechanics
    void leftRotate(RBTNode* x);
    void rightRotate(RBTNode* y);
    void fixupInsertion(RBTNode* z);
    
    // Recursive helpers
    void clearTree(RBTNode* node);
    void printTreeRecursive(RBTNode* node, const std::string& indent, bool isLast);
    int calculateHeight(RBTNode* node);
    int calculateBlackHeight(RBTNode* node);
    bool checkRBPropertiesRecursive(RBTNode* node, int currentBlackHeight, int& targetBlackHeight);
    void traverseHeightsAndBalances(RBTNode* node, std::vector<std::pair<int, std::pair<int, int>>>& list);
};

#endif // RBT_HPP
