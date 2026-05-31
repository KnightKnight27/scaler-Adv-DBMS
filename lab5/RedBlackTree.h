#ifndef REDBLACKTREE_H
#define REDBLACKTREE_H

#include <iostream>
#include <vector>
class RedBlackTree {
public:
	enum Color {
		BLACK = 0,
		RED = 1
	};

	struct Node {
		int val;
		Node *left;
		Node *right;
		Node *parent;
		Color color;

		Node(const int valA)
			: val(valA), left(nullptr), right(nullptr), parent(nullptr), color(RED)
		{}

        ~Node()
        {
            delete left;
            delete right;
        }

	};

	void print();

	RedBlackTree();
    ~RedBlackTree();

	bool find(int val);
	void insert(int val);

private:

	Node* m_Root;

	void fixTree(Node *node);

	Node* getGrandParent(Node *node);
	Node* getUncle(Node *node);

    bool isLRConfiguration(Node *node);
    bool isRLConfiguration(Node *node);
    bool isLLConfiguration(Node *node);
    bool isRRConfiguration(Node *node);

    void leftRotate(Node *node);
    void rightRotate(Node *node);
};

#endif
