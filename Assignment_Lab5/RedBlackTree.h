#ifndef REDBLACKTREE_H
#define REDBLACKTREE_H

#include <vector>

// Red Black Tree header
// Cases for insertion:
// case 0 - parent is black, no problem
// case 1 - parent red, uncle red -> recolor
// case 2 - parent red, uncle black, zig zag shape -> rotate to make it case 3
// case 3 - parent red, uncle black, straight line -> rotate + recolor

class RedBlackTree {
public:

	enum Color {
		BLACK = 0,
		RED = 1
	};

	struct Node {
		int key;
		Color color;
		Node *left;
		Node *right;
		Node *parent;

		Node(int k)
			: key(k), color(RED), left(nullptr), right(nullptr), parent(nullptr)
		{}
	};

	Node *NIL; // sentinel null node

	RedBlackTree();
	~RedBlackTree();

	void insert(int key);
	bool search(int key);
	void remove(int key);

	void printLevelOrder();
	void printInOrder();

private:
	Node *root;

	void fixInsert(Node *z);

	bool isCase0(Node *z);
	bool isCase1(Node *z);
	bool isCase2(Node *z);
	bool isCase3(Node *z);

	void handleCase0(Node *z);
	void handleCase1(Node *z);
	void handleCase2(Node *z);
	void handleCase3(Node *z);

	void rotateLeft(Node *x);
	void rotateRight(Node *x);

	Node *getGrandparent(Node *z);
	Node *getUncle(Node *z);

	void deleteTree(Node *node);
	void inOrderHelper(Node *node);
};

#endif
