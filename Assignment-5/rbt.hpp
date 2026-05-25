#ifndef RBT_HPP
#define RBT_HPP

#include <iostream>
#include <string>
#include <vector>
#include <utility>

enum Color { RED, BLACK };

struct RBTNode {
    int key;
    Color color;
    RBTNode *left, *right, *parent;

    RBTNode(int k, Color c = RED)
        : key(k), color(c), left(nullptr), right(nullptr), parent(nullptr) {}
};

class RedBlackTree {
public:
    RedBlackTree();
    ~RedBlackTree();

    void insert(int key);
    RBTNode* search(int key);
    void printTree();

    int  getHeight();
    int  getBlackHeight();
    bool isRBBalanced();
    bool isAVLBalanced();

    std::vector<std::pair<int, std::pair<int,int>>> getBalanceInfo();

private:
    RBTNode* root;
    RBTNode* NIL;   // sentinel — all leaf pointers point here

    void leftRotate(RBTNode* x);
    void rightRotate(RBTNode* y);
    void fixup(RBTNode* z);

    void clear(RBTNode* node);
    void printRec(RBTNode* node, const std::string& indent, bool last);
    int  height(RBTNode* node);
    int  blackHeight(RBTNode* node);
    bool checkRB(RBTNode* node, int bh, int& target);
    void collectInfo(RBTNode* node, std::vector<std::pair<int,std::pair<int,int>>>& out);
};

#endif
