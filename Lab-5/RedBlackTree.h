#ifndef REDBLACKTREE_H
#define REDBLACKTREE_H

#include <vector>

class RedBlackTree {
public:
	enum Color {
		black = 0,
		red = 1
	};

	struct Node {
		int val;
		Node *left;
		Node *right;
		Node *parent;
		Color color;

		Node(int valA)
			: val(valA), left(nullptr), right(nullptr), parent(nullptr), color(Color::red)
		{}
	};

	RedBlackTree();
	~RedBlackTree();

	bool find(int val);
	void insert(int val);
	void remove(int val);

	void print();

	Node *NIL;

private:
	Node *m_Root;

	// Insert fixup
	void fixTree(Node *node);

	bool isCase0(Node *node);
	bool isCase1(Node *node);
	bool isCase2(Node *node);
	bool isCase3(Node *node);

	void handleCase0(Node *node);
	void handleCase1(Node *node);
	void handleCase2(Node *node);
	void handleCase3(Node *node);

	// Rotations
	void rotateLeft(Node *x);
	void rotateRight(Node *x);

	// Delete helpers
	Node* findNode(int val);
	Node* minimum(Node *node);
	void transplant(Node *u, Node *v);
	void fixDelete(Node *x);

	// Utility
	Node* getGrandParent(Node *node);
	Node* getUncle(Node *node);

	void destroy(Node *node);
};

#endif
