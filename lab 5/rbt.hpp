#ifndef RBT_HPP
#define RBT_HPP

#include <iostream>
#include <vector>
#include <string>
#include <utility>

enum Color { RED, BLACK };

struct Node {
    int key;
    Color color;
    Node* left;
    Node* right;
    Node* parent;

    Node(int k, Color c = RED)
        : key(k), color(c), left(nullptr), right(nullptr), parent(nullptr) {}
};

class RedBlackTree {
public:
    RedBlackTree();
    ~RedBlackTree();

    void insert(int key);
    Node* search(int key);
    void printTree();

    // Diagnostics & Verification
    int getHeight();
    int getBlackHeight();
    bool isRBBalanced();
    bool isAVLBalanced();
    std::vector<std::pair<int, std::pair<int, int>>> getNodeHeightsAndBalances();

private:
    Node* root;
    Node* NIL; // Sentinel node representing leaf nodes

    void deleteTree(Node* node);
    void leftRotate(Node* x);
    void rightRotate(Node* y);
    void insertFixUp(Node* z);
    
    // Printing helper
    void printTreeHelper(Node* node, const std::string& indent, bool last);

    // Diagnostics & verification helpers
    int calculateHeight(Node* node);
    int calculateBlackHeight(Node* node);
    bool checkRBProperties(Node* node, int currentBlackHeight, int& expectedBlackHeight);
    void collectHeightsAndBalances(Node* node, std::vector<std::pair<int, std::pair<int, int>>>& list);
};

#endif // RBT_HPP
