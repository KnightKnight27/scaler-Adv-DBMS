#ifndef RBT_HPP
#define RBT_HPP

#include <string>

enum class Color {
    Red,
    Black
};

struct RBNode {
    int key;
    Color color;
    RBNode* left;
    RBNode* right;
    RBNode* parent;

    explicit RBNode(int value);
};

class RedBlackTree {
public:
    RedBlackTree();
    ~RedBlackTree();

    void insert(int key);
    const RBNode* search(int key) const;
    void inorder() const;
    void printStructure() const;

private:
    RBNode* root;
    RBNode* nil;

    void rotateLeft(RBNode* node);
    void rotateRight(RBNode* node);
    void repairAfterInsert(RBNode* node);
    void inorderFrom(const RBNode* node) const;
    void printFrom(const RBNode* node, const std::string& prefix, bool isLast) const;
    void destroy(RBNode* node);
};

#endif