#include "RedBlackTree.h"
#include <iostream>
#include <vector>
#include <queue>
#include <string>

using namespace std;

// Constructor
RedBlackBST::RedBlackBST() {
	NIL_LEAF = new BSTNode(0);
	NIL_LEAF->clr = CLR_BLACK;
	NIL_LEAF->left = nullptr;
	NIL_LEAF->right = nullptr;
	NIL_LEAF->parent = nullptr;
	treeRoot_ = NIL_LEAF;
}

// Destructor
RedBlackBST::~RedBlackBST() {
	freeTree(treeRoot_);
	delete NIL_LEAF;
}

// Post-order free helper
void RedBlackBST::freeTree(BSTNode *node) {
	if (node == nullptr || node == NIL_LEAF) {
		return;
	}
	freeTree(node->left);
	freeTree(node->right);
	delete node;
}

// Public search function
bool RedBlackBST::lookup(int data) {
	return searchNode(data) != NIL_LEAF;
}

// Internal node finder
RedBlackBST::BSTNode* RedBlackBST::searchNode(int data) {
	BSTNode *curr = treeRoot_;
	while (curr != NIL_LEAF) {
		if (data == curr->data) {
			return curr;
		}
		if (data < curr->data) {
			curr = curr->left;
		} else {
			curr = curr->right;
		}
	}
	return NIL_LEAF;
}

// Public insert wrapper
void RedBlackBST::add(int data) {
	BSTNode *n = new BSTNode(data);
	n->left = NIL_LEAF;
	n->right = NIL_LEAF;
	n->clr = CLR_RED;

	BSTNode *prev = nullptr;
	BSTNode *curr = treeRoot_;

	// Binary search tree insert walk
	while (curr != NIL_LEAF) {
		prev = curr;
		if (data <= curr->data) {
			curr = curr->left;
		} else {
			curr = curr->right;
		}
	}

	n->parent = prev;
	if (prev == nullptr) {
		treeRoot_ = n;
	} else if (data <= prev->data) {
		prev->left = n;
	} else {
		prev->right = n;
	}

	repairInsert(n);
	treeRoot_->clr = CLR_BLACK;
}

// Public remove wrapper
void RedBlackBST::erase(int data) {
	BSTNode *z = searchNode(data);
	if (z == NIL_LEAF) {
		return;
	}

	BSTNode *y = z;
	NodeColor origColor = y->clr;
	BSTNode *x;

	if (z->left == NIL_LEAF) {
		x = z->right;
		swapSubtree(z, z->right);
	} else if (z->right == NIL_LEAF) {
		x = z->left;
		swapSubtree(z, z->left);
	} else {
		y = leftmostDescendant(z->right);
		origColor = y->clr;
		x = y->right;
		if (y->parent == z) {
			x->parent = y;
		} else {
			swapSubtree(y, y->right);
			y->right = z->right;
			y->right->parent = y;
		}
		swapSubtree(z, y);
		y->left = z->left;
		y->left->parent = y;
		y->clr = z->clr;
	}

	delete z;

	if (origColor == CLR_BLACK) {
		repairDelete(x);
	}
}

// Insert rebalancer
void RedBlackBST::repairInsert(BSTNode *node) {
	if (node == treeRoot_) {
		return;
	}

	if (testScenario0(node)) {
		applyScenario0(node);
	} else if (testScenario1(node)) {
		applyScenario1(node);
	} else if (testScenario3(node)) {
		applyScenario3(node);
	} else if (testScenario2(node)) {
		applyScenario2(node);
	}
}

// Case predicates
bool RedBlackBST::testScenario0(BSTNode *node) {
	return (node->parent && node->parent->clr == CLR_BLACK);
}

bool RedBlackBST::testScenario1(BSTNode *node) {
	BSTNode *u = sibling_of_parent(node);
	return (node->parent && node->parent->clr == CLR_RED
			&& u != NIL_LEAF && u->clr == CLR_RED);
}

bool RedBlackBST::testScenario2(BSTNode *node) {
	BSTNode *u = sibling_of_parent(node);
	return (node->parent && node->parent->clr == CLR_RED
			&& u->clr == CLR_BLACK);
}

bool RedBlackBST::testScenario3(BSTNode *node) {
	BSTNode *p = node->parent;
	BSTNode *g = ancestor(node);
	BSTNode *u = sibling_of_parent(node);

	if (!p || p->clr != CLR_RED) return false;
	if (g == NIL_LEAF) return false;
	if (u->clr == CLR_RED) return false;

	bool isLeftLeft = (p->left == node && g->left == p);
	bool isRightRight = (p->right == node && g->right == p);
	return isLeftLeft || isRightRight;
}

// Case operations
void RedBlackBST::applyScenario0(BSTNode *node) {
	(void)node; // No changes required when parent is black
}

void RedBlackBST::applyScenario1(BSTNode *node) {
	BSTNode *g = ancestor(node);
	BSTNode *u = sibling_of_parent(node);
	BSTNode *p = node->parent;

	p->clr = CLR_BLACK;
	u->clr = CLR_BLACK;
	g->clr = CLR_RED;

	repairInsert(g);
}

void RedBlackBST::applyScenario2(BSTNode *node) {
	BSTNode *p = node->parent;
	BSTNode *g = ancestor(node);

	if (p == g->left) {
		spinLeft(p);
		repairInsert(p);
	} else {
		spinRight(p);
		repairInsert(p);
	}
}

void RedBlackBST::applyScenario3(BSTNode *node) {
	BSTNode *p = node->parent;
	BSTNode *g = ancestor(node);

	p->clr = CLR_BLACK;
	g->clr = CLR_RED;

	if (p == g->left) {
		spinRight(g);
	} else {
		spinLeft(g);
	}
}

// Rotations
void RedBlackBST::spinLeft(BSTNode *x) {
	BSTNode *y = x->right;
	x->right = y->left;
	if (y->left != NIL_LEAF) {
		y->left->parent = x;
	}
	y->parent = x->parent;
	if (x->parent == nullptr) {
		treeRoot_ = y;
	} else if (x == x->parent->left) {
		x->parent->left = y;
	} else {
		x->parent->right = y;
	}
	y->left = x;
	x->parent = y;
}

void RedBlackBST::spinRight(BSTNode *x) {
	BSTNode *y = x->left;
	x->left = y->right;
	if (y->right != NIL_LEAF) {
		y->right->parent = x;
	}
	y->parent = x->parent;
	if (x->parent == nullptr) {
		treeRoot_ = y;
	} else if (x == x->parent->right) {
		x->parent->right = y;
	} else {
		x->parent->left = y;
	}
	y->right = x;
	x->parent = y;
}

// Deletion helpers
void RedBlackBST::swapSubtree(BSTNode *old_root, BSTNode *new_root) {
	if (old_root->parent == nullptr) {
		treeRoot_ = new_root;
	} else if (old_root == old_root->parent->left) {
		old_root->parent->left = new_root;
	} else {
		old_root->parent->right = new_root;
	}
	new_root->parent = old_root->parent;
}

RedBlackBST::BSTNode* RedBlackBST::leftmostDescendant(BSTNode *node) {
	while (node->left != NIL_LEAF) {
		node = node->left;
	}
	return node;
}

// Delete rebalancer
void RedBlackBST::repairDelete(BSTNode *x) {
	while (x != treeRoot_ && x->clr == CLR_BLACK) {
		if (x == x->parent->left) {
			BSTNode *s = x->parent->right;
			// Case A: Sibling is red
			if (s->clr == CLR_RED) {
				s->clr = CLR_BLACK;
				x->parent->clr = CLR_RED;
				spinLeft(x->parent);
				s = x->parent->right;
			}
			// Case B: Sibling's children are black
			if (s->left->clr == CLR_BLACK && s->right->clr == CLR_BLACK) {
				s->clr = CLR_RED;
				x = x->parent;
			} else {
				// Case C: Sibling's right child is black
				if (s->right->clr == CLR_BLACK) {
					s->left->clr = CLR_BLACK;
					s->clr = CLR_RED;
					spinRight(s);
					s = x->parent->right;
				}
				// Case D: Sibling's right child is red
				s->clr = x->parent->clr;
				x->parent->clr = CLR_BLACK;
				s->right->clr = CLR_BLACK;
				spinLeft(x->parent);
				x = treeRoot_;
			}
		} else {
			BSTNode *s = x->parent->left;
			// Case A: Sibling is red
			if (s->clr == CLR_RED) {
				s->clr = CLR_BLACK;
				x->parent->clr = CLR_RED;
				spinRight(x->parent);
				s = x->parent->left;
			}
			// Case B: Sibling's children are black
			if (s->right->clr == CLR_BLACK && s->left->clr == CLR_BLACK) {
				s->clr = CLR_RED;
				x = x->parent;
			} else {
				// Case C: Sibling's left child is black
				if (s->left->clr == CLR_BLACK) {
					s->right->clr = CLR_BLACK;
					s->clr = CLR_RED;
					spinLeft(s);
					s = x->parent->left;
				}
				// Case D: Sibling's left child is red
				s->clr = x->parent->clr;
				x->parent->clr = CLR_BLACK;
				s->left->clr = CLR_BLACK;
				spinRight(x->parent);
				x = treeRoot_;
			}
		}
	}
	x->clr = CLR_BLACK;
}

// Ancestry helpers
RedBlackBST::BSTNode* RedBlackBST::ancestor(BSTNode *node) {
	if (node->parent && node->parent->parent) {
		return node->parent->parent;
	}
	return NIL_LEAF;
}

RedBlackBST::BSTNode* RedBlackBST::sibling_of_parent(BSTNode *node) {
	BSTNode *g = ancestor(node);
	if (!node->parent || g == NIL_LEAF) {
		return NIL_LEAF;
	}
	if (g->left == node->parent) {
		return g->right;
	}
	return g->left;
}

// Level-order BFS output
void RedBlackBST::displayBFS() {
	if (treeRoot_ == NIL_LEAF) {
		cout << "[]\n";
		return;
	}

	vector<string> items;
	queue<BSTNode*> q;
	q.push(treeRoot_);

	while (!q.empty()) {
		BSTNode *curr = q.front();
		q.pop();

		if (curr == NIL_LEAF) {
			items.push_back("null");
		} else {
			string representation = to_string(curr->data);
			representation += (curr->clr == CLR_RED) ? "R" : "B";
			items.push_back(representation);

			q.push(curr->left);
			q.push(curr->right);
		}
	}

	// Remove trailing nulls
	while (!items.empty() && items.back() == "null") {
		items.pop_back();
	}

	cout << "[";
	for (size_t i = 0; i < items.size(); ++i) {
		cout << items[i];
		if (i + 1 < items.size()) {
			cout << ", ";
		}
	}
	cout << "]\n";
}
