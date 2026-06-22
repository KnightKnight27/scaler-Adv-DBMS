#ifndef RED_BLACK_TREE_HPP
#define RED_BLACK_TREE_HPP

#include <vector>

// --------------------------------------------------------
//  RedBlackBST — Balanced BST with red/black colouring
//  Student: Nandani Kumari (24bcs10317)
// --------------------------------------------------------

class RedBlackBST {
public:
	enum NodeColor {
		CLR_BLACK = 0,
		CLR_RED   = 1
	};

	struct BSTNode {
		int data;
		BSTNode *left;
		BSTNode *right;
		BSTNode *parent;
		NodeColor clr;

		BSTNode(int d)
			: data(d), left(nullptr), right(nullptr), parent(nullptr), clr(NodeColor::CLR_RED)
		{}
	};

	RedBlackBST();
	~RedBlackBST();

	bool lookup(int data);
	void add(int data);
	void erase(int data);

	void displayBFS();

	BSTNode *NIL_LEAF;

private:
	BSTNode *treeRoot_;

	// Post-insert rebalancing
	void repairInsert(BSTNode *node);

	bool testScenario0(BSTNode *node);
	bool testScenario1(BSTNode *node);
	bool testScenario2(BSTNode *node);
	bool testScenario3(BSTNode *node);

	void applyScenario0(BSTNode *node);
	void applyScenario1(BSTNode *node);
	void applyScenario2(BSTNode *node);
	void applyScenario3(BSTNode *node);

	// Structural rotations
	void spinLeft(BSTNode *node);
	void spinRight(BSTNode *node);

	// Deletion support
	BSTNode* searchNode(int data);
	BSTNode* leftmostDescendant(BSTNode *node);
	void swapSubtree(BSTNode *old_root, BSTNode *new_root);
	void repairDelete(BSTNode *node);

	// Family accessors
	BSTNode* ancestor(BSTNode *node);
	BSTNode* sibling_of_parent(BSTNode *node);

	void freeTree(BSTNode *node);
};

#endif
