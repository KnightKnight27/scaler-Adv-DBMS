#include "RedBlackTree.h"
#include <cassert>
#include <cstddef>
#include <iostream>
#include <vector>
#include <queue>

using namespace std;

// Toggle verbose logging for debugging fixup dispatch.
// #define VERBOSE_LOG

#ifdef VERBOSE_LOG
	#define TRACE(msg) cout << msg << '\n';
#else
	#define TRACE(msg) ;
#endif


/*
 * ================================================================
 *  RBTree — Red-Black Tree Implementation
 *  Author: Ayush Kumar Patra (24bcs10474)
 * ================================================================
 *
 * INSERTION REBALANCING
 *   After a standard BST insert of a RED node `z`, we call
 *   rebalanceAfterInsert(z). The dispatcher checks four scenarios:
 *
 *   Scenario 0 — z's parent is BLACK (or z is the root).
 *                Nothing is broken; return immediately.
 *
 *   Scenario 1 — Both parent and uncle of z are RED.
 *                Flip parent + uncle to BLACK, grandparent to RED,
 *                then recurse upward on the grandparent.
 *
 *   Scenario 2 — Parent RED, uncle BLACK, z is the "inner" grandchild
 *                (left-right or right-left configuration).
 *                Rotate around parent to straighten into Scenario 3.
 *
 *   Scenario 3 — Parent RED, uncle BLACK, z is the "outer" grandchild
 *                (left-left or right-right configuration).
 *                Recolour parent BLACK, grandparent RED, rotate around
 *                grandparent. The subtree black-height is restored.
 *
 * DELETION REBALANCING
 *   Follows the standard CLRS approach: BST-delete the target, and
 *   if a BLACK node was excised, invoke rebalanceAfterDelete(x) on the
 *   replacement node x to resolve the double-black deficit.
 */

// -------------------------------------------------------
//  Constructor — allocate a shared SENTINEL leaf node
// -------------------------------------------------------
RBTree::RBTree()
	: SENTINEL(new TreeNode(0))
{
	SENTINEL->colour = Colour::BLACK;
	SENTINEL->left   = nullptr;
	SENTINEL->right  = nullptr;
	SENTINEL->parent = nullptr;
	root_ = SENTINEL;
}

// -------------------------------------------------------
//  Destructor — post-order traversal to free all nodes
// -------------------------------------------------------
RBTree::~RBTree()
{
	deallocate(root_);
	delete SENTINEL;
}

void RBTree::deallocate(TreeNode *nd)
{
	if (nd == SENTINEL || nd == nullptr) {
		return;
	}
	deallocate(nd->left);
	deallocate(nd->right);
	delete nd;
}

// -------------------------------------------------------
//  Public API: search — O(log n) key lookup
// -------------------------------------------------------
bool RBTree::search(int key)
{
	return locateNode(key) != SENTINEL;
}

RBTree::TreeNode* RBTree::locateNode(int key)
{
	TreeNode *cur = root_;
	while (cur != SENTINEL) {
		if (key == cur->key) {
			return cur;
		} else if (key < cur->key) {
			cur = cur->left;
		} else {
			cur = cur->right;
		}
	}
	return SENTINEL;
}

// -------------------------------------------------------
//  Public API: insert — BST insert + rebalance
// -------------------------------------------------------
void RBTree::insert(int key)
{
	TreeNode *fresh = new TreeNode(key);
	fresh->left   = SENTINEL;
	fresh->right  = SENTINEL;
	fresh->colour = Colour::RED;

	TreeNode *trail  = nullptr;
	TreeNode *cursor = root_;

	// Standard BST walk to find insertion point
	while (cursor != SENTINEL) {
		trail = cursor;
		if (key <= cursor->key) {
			cursor = cursor->left;
		} else {
			cursor = cursor->right;
		}
	}

	fresh->parent = trail;
	if (trail == nullptr) {
		root_ = fresh;                // Tree was empty
	} else if (key <= trail->key) {
		trail->left = fresh;
	} else {
		trail->right = fresh;
	}

	rebalanceAfterInsert(fresh);
	root_->colour = Colour::BLACK;    // Root must always be BLACK
}

// -------------------------------------------------------
//  Public API: remove — BST delete + rebalance
// -------------------------------------------------------
void RBTree::remove(int key)
{
	TreeNode *z = locateNode(key);
	if (z == SENTINEL) {
		return;                       // Key not present; silently ignore
	}

	TreeNode *successor = z;
	Colour savedColour  = successor->colour;
	TreeNode *fixupNode;

	if (z->left == SENTINEL) {
		fixupNode = z->right;
		replaceSubtree(z, z->right);
	} else if (z->right == SENTINEL) {
		fixupNode = z->left;
		replaceSubtree(z, z->left);
	} else {
		// Two children: find in-order successor (minimum of right subtree)
		successor   = subtreeMin(z->right);
		savedColour = successor->colour;
		fixupNode   = successor->right;
		if (successor->parent == z) {
			fixupNode->parent = successor;
		} else {
			replaceSubtree(successor, successor->right);
			successor->right = z->right;
			successor->right->parent = successor;
		}
		replaceSubtree(z, successor);
		successor->left = z->left;
		successor->left->parent = successor;
		successor->colour = z->colour;
	}

	delete z;

	// If the removed/moved node was BLACK, repair the deficit
	if (savedColour == Colour::BLACK) {
		rebalanceAfterDelete(fixupNode);
	}
}

// -------------------------------------------------------
//  Insertion Fixup — dispatcher
// -------------------------------------------------------
void RBTree::rebalanceAfterInsert(TreeNode *nd)
{
	if (nd == root_) {
		return;
	}

	if (checkCase0(nd)) {
		resolveCase0(nd);
	} else if (checkCase1(nd)) {
		resolveCase1(nd);
	} else if (checkCase3(nd)) {
		resolveCase3(nd);
	} else if (checkCase2(nd)) {
		resolveCase2(nd);
	} else {
		TRACE("Unrecognised fixup state");
	}
}

// ---- Case predicates ----

bool RBTree::checkCase0(TreeNode *nd)
{
	return (nd->parent && nd->parent->colour == Colour::BLACK);
}

bool RBTree::checkCase1(TreeNode *nd)
{
	TreeNode *unc = fetchUncle(nd);
	return (nd->parent && nd->parent->colour == Colour::RED
	        && unc != SENTINEL && unc->colour == Colour::RED);
}

bool RBTree::checkCase2(TreeNode *nd)
{
	TreeNode *unc = fetchUncle(nd);
	return (nd->parent && nd->parent->colour == Colour::RED
	        && unc->colour == Colour::BLACK);
}

bool RBTree::checkCase3(TreeNode *nd)
{
	TreeNode *par = nd->parent;
	TreeNode *gp  = fetchGrandparent(nd);
	TreeNode *unc = fetchUncle(nd);

	if (!par || par->colour != Colour::RED) return false;
	if (gp == SENTINEL) return false;
	if (unc->colour == Colour::RED) return false;

	bool outerLeft  = (par->left  == nd && gp->left  == par);
	bool outerRight = (par->right == nd && gp->right == par);
	return outerLeft || outerRight;
}

// ---- Case handlers ----

void RBTree::resolveCase0(TreeNode *nd)
{
	TRACE("scenario-0: parent is BLACK — no action");
	(void)nd;
}

void RBTree::resolveCase1(TreeNode *nd)
{
	TRACE("scenario-1: recolour parent, uncle, grandparent");
	TreeNode *gp  = fetchGrandparent(nd);
	TreeNode *unc = fetchUncle(nd);
	TreeNode *par = nd->parent;

	par->colour = Colour::BLACK;
	unc->colour = Colour::BLACK;
	gp->colour  = Colour::RED;

	rebalanceAfterInsert(gp);
}

void RBTree::resolveCase2(TreeNode *nd)
{
	TRACE("scenario-2: rotate to align into scenario-3");
	TreeNode *par = nd->parent;
	TreeNode *gp  = fetchGrandparent(nd);

	if (par == gp->left) {
		leftRotate(par);
		rebalanceAfterInsert(par);
	} else {
		rightRotate(par);
		rebalanceAfterInsert(par);
	}
}

void RBTree::resolveCase3(TreeNode *nd)
{
	TRACE("scenario-3: recolour + rotate grandparent");
	TreeNode *par = nd->parent;
	TreeNode *gp  = fetchGrandparent(nd);

	par->colour = Colour::BLACK;
	gp->colour  = Colour::RED;

	if (par == gp->left) {
		rightRotate(gp);
	} else {
		leftRotate(gp);
	}
}

// -------------------------------------------------------
//  Left rotation — standard BST left rotate
// -------------------------------------------------------
void RBTree::leftRotate(TreeNode *x)
{
	TreeNode *y = x->right;
	x->right = y->left;
	if (y->left != SENTINEL) {
		y->left->parent = x;
	}
	y->parent = x->parent;
	if (x->parent == nullptr) {
		root_ = y;
	} else if (x == x->parent->left) {
		x->parent->left = y;
	} else {
		x->parent->right = y;
	}
	y->left = x;
	x->parent = y;
}

// -------------------------------------------------------
//  Right rotation — standard BST right rotate
// -------------------------------------------------------
void RBTree::rightRotate(TreeNode *x)
{
	TreeNode *y = x->left;
	x->left = y->right;
	if (y->right != SENTINEL) {
		y->right->parent = x;
	}
	y->parent = x->parent;
	if (x->parent == nullptr) {
		root_ = y;
	} else if (x == x->parent->right) {
		x->parent->right = y;
	} else {
		x->parent->left = y;
	}
	y->right = x;
	x->parent = y;
}

// -------------------------------------------------------
//  Subtree transplant — replaces subtree rooted at target
//  with subtree rooted at replacement
// -------------------------------------------------------
void RBTree::replaceSubtree(TreeNode *target, TreeNode *replacement)
{
	if (target->parent == nullptr) {
		root_ = replacement;
	} else if (target == target->parent->left) {
		target->parent->left = replacement;
	} else {
		target->parent->right = replacement;
	}
	replacement->parent = target->parent;
}

// -------------------------------------------------------
//  subtreeMin — find leftmost node in a subtree
// -------------------------------------------------------
RBTree::TreeNode* RBTree::subtreeMin(TreeNode *nd)
{
	while (nd->left != SENTINEL) {
		nd = nd->left;
	}
	return nd;
}

// -------------------------------------------------------
//  Deletion Fixup — resolve double-black via sibling cases
// -------------------------------------------------------
void RBTree::rebalanceAfterDelete(TreeNode *x)
{
	while (x != root_ && x->colour == Colour::BLACK) {
		if (x == x->parent->left) {
			TreeNode *sib = x->parent->right;
			// Case A: sibling is RED
			if (sib->colour == Colour::RED) {
				sib->colour = Colour::BLACK;
				x->parent->colour = Colour::RED;
				leftRotate(x->parent);
				sib = x->parent->right;
			}
			// Case B: sibling's children are both BLACK
			if (sib->left->colour == Colour::BLACK && sib->right->colour == Colour::BLACK) {
				sib->colour = Colour::RED;
				x = x->parent;
			} else {
				// Case C: sibling's far child is BLACK
				if (sib->right->colour == Colour::BLACK) {
					sib->left->colour = Colour::BLACK;
					sib->colour = Colour::RED;
					rightRotate(sib);
					sib = x->parent->right;
				}
				// Case D: sibling's far child is RED — terminal fix
				sib->colour = x->parent->colour;
				x->parent->colour = Colour::BLACK;
				sib->right->colour = Colour::BLACK;
				leftRotate(x->parent);
				x = root_;
			}
		} else {
			// Mirror: x is right child
			TreeNode *sib = x->parent->left;
			if (sib->colour == Colour::RED) {
				sib->colour = Colour::BLACK;
				x->parent->colour = Colour::RED;
				rightRotate(x->parent);
				sib = x->parent->left;
			}
			if (sib->right->colour == Colour::BLACK && sib->left->colour == Colour::BLACK) {
				sib->colour = Colour::RED;
				x = x->parent;
			} else {
				if (sib->left->colour == Colour::BLACK) {
					sib->right->colour = Colour::BLACK;
					sib->colour = Colour::RED;
					leftRotate(sib);
					sib = x->parent->left;
				}
				sib->colour = x->parent->colour;
				x->parent->colour = Colour::BLACK;
				sib->left->colour = Colour::BLACK;
				rightRotate(x->parent);
				x = root_;
			}
		}
	}
	x->colour = Colour::BLACK;
}

// -------------------------------------------------------
//  Ancestor helpers
// -------------------------------------------------------

RBTree::TreeNode* RBTree::fetchGrandparent(TreeNode *nd) {
	if (nd->parent && nd->parent->parent) {
		return nd->parent->parent;
	} else {
		return SENTINEL;
	}
}

RBTree::TreeNode* RBTree::fetchUncle(TreeNode *nd) {
	TreeNode *gp = fetchGrandparent(nd);

	if (!nd->parent || gp == SENTINEL) {
		return SENTINEL;
	}

	bool parentIsLeftChild = (gp->left == nd->parent);

	if (parentIsLeftChild) {
		return gp->right;
	} else {
		return gp->left;
	}
}

// -------------------------------------------------------
//  BFS Level-order print (LeetCode-style output)
// -------------------------------------------------------
void RBTree::printLevelOrder()
{
	if (root_ == SENTINEL) {
		cout << "[]\n";
		return;
	}

	vector<string> output;
	queue<TreeNode*> bfsQueue;

	bfsQueue.push(root_);

	while (!bfsQueue.empty()) {
		TreeNode *cur = bfsQueue.front();
		bfsQueue.pop();

		if (cur == SENTINEL) {
			output.emplace_back("null");
		} else {
			string tag = to_string(cur->key);
			tag += (cur->colour == Colour::RED) ? "R" : "B";
			output.emplace_back(tag);

			bfsQueue.push(cur->left);
			bfsQueue.push(cur->right);
		}
	}

	// Strip trailing "null" entries for cleaner output
	while (!output.empty() && output.back() == "null") {
		output.pop_back();
	}

	cout << "[";

	for (size_t idx = 0; idx < output.size(); idx++) {
		cout << output[idx];

		if (idx + 1 < output.size()) {
			cout << ", ";
		}
	}

	cout << "]\n";
}
