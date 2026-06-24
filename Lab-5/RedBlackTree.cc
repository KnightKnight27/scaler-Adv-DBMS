#include "RedBlackTree.h"
#include <cassert>
#include <cstddef>
#include <iostream>
#include <vector>
#include <queue>

using namespace std;

// Uncomment to enable verbose case-dispatch logging.
// #define DEBUG

#ifdef DEBUG
	#define LOG(x) cout << x << '\n';
#else
	#define LOG(x) ;
#endif


/* Theory
 *
 * INSERT FIXUP — after BST-style insertion of a red node z, fixTree(z) dispatches
 * to one of four cases based on the state of z's parent and uncle:
 *
 *  Case 0 — Parent of z is black (or z is root)
 *           No violation; nothing to do.
 *
 *  Case 1 — Parent of z is red AND uncle of z is red
 *           Recolor parent + uncle to black, grandparent to red, then
 *           recurse on grandparent.
 *
 *  Case 2 — Parent is red, uncle is black, z is "inner" grandchild (LR/RL)
 *           Rotate around parent to transform into Case 3.
 *
 *  Case 3 — Parent is red, uncle is black, z is "outer" grandchild (LL/RR)
 *           Recolor parent to black, grandparent to red, rotate around
 *           grandparent.
 *
 * DELETE FIXUP — standard CLRS RBT delete: BST-delete the node, then if a
 * black node was removed, walk up from the replacement node x and rebalance
 * using four mirrored sibling cases inside fixDelete().
*/

RedBlackTree::RedBlackTree()
	: NIL(new Node(0))
{
	NIL->color = Color::black;
	NIL->left = nullptr;
	NIL->right = nullptr;
	NIL->parent = nullptr;
	m_Root = NIL;
}

RedBlackTree::~RedBlackTree()
{
	destroy(m_Root);
	delete NIL;
}

void RedBlackTree::destroy(Node *node)
{
	if (node == NIL || node == nullptr) {
		return;
	}
	destroy(node->left);
	destroy(node->right);
	delete node;
}

bool RedBlackTree::find(int val)
{
	return findNode(val) != NIL;
}

RedBlackTree::Node* RedBlackTree::findNode(int val)
{
	Node *node = m_Root;
	while (node != NIL) {
		if (val == node->val) {
			return node;
		} else if (val < node->val) {
			node = node->left;
		} else {
			node = node->right;
		}
	}
	return NIL;
}

void RedBlackTree::insert(int val)
{
	Node *node = new Node(val);
	node->left = NIL;
	node->right = NIL;
	node->color = Color::red;

	Node *parent = nullptr;
	Node *trav = m_Root;

	while (trav != NIL) {
		parent = trav;
		if (val <= trav->val) {
			trav = trav->left;
		} else {
			trav = trav->right;
		}
	}

	node->parent = parent;
	if (parent == nullptr) {
		m_Root = node;
	} else if (val <= parent->val) {
		parent->left = node;
	} else {
		parent->right = node;
	}

	fixTree(node);
	m_Root->color = Color::black;
}

void RedBlackTree::remove(int val)
{
	Node *z = findNode(val);
	if (z == NIL) {
		return;
	}

	Node *y = z;
	Color yOriginalColor = y->color;
	Node *x;

	if (z->left == NIL) {
		x = z->right;
		transplant(z, z->right);
	} else if (z->right == NIL) {
		x = z->left;
		transplant(z, z->left);
	} else {
		y = minimum(z->right);
		yOriginalColor = y->color;
		x = y->right;
		if (y->parent == z) {
			x->parent = y;
		} else {
			transplant(y, y->right);
			y->right = z->right;
			y->right->parent = y;
		}
		transplant(z, y);
		y->left = z->left;
		y->left->parent = y;
		y->color = z->color;
	}

	delete z;

	if (yOriginalColor == Color::black) {
		fixDelete(x);
	}
}

void RedBlackTree::fixTree(Node *node)
{
	if (node == m_Root) {
		return;
	}

	if (isCase0(node)) {
		handleCase0(node);
	} else if (isCase1(node)) {
		handleCase1(node);
	} else if (isCase3(node)) {
		handleCase3(node);
	} else if (isCase2(node)) {
		handleCase2(node);
	} else {
		LOG("Unknown case");
	}
}

bool RedBlackTree::isCase0(Node *node)
{
	return (node->parent && node->parent->color == Color::black);
}

bool RedBlackTree::isCase1(Node *node)
{
	Node *uncle = getUncle(node);
	return (node->parent && node->parent->color == Color::red
	        && uncle != NIL && uncle->color == Color::red);
}

bool RedBlackTree::isCase2(Node *node)
{
	Node *uncle = getUncle(node);
	return (node->parent && node->parent->color == Color::red
	        && uncle->color == Color::black);
}

bool RedBlackTree::isCase3(Node *node)
{
	Node *parent = node->parent;
	Node *grandparent = getGrandParent(node);
	Node *uncle = getUncle(node);

	if (!parent || parent->color != Color::red) return false;
	if (grandparent == NIL) return false;
	if (uncle->color == Color::red) return false;

	bool leftLeaning  = (parent->left  == node && grandparent->left  == parent);
	bool rightLeaning = (parent->right == node && grandparent->right == parent);
	return leftLeaning || rightLeaning;
}

void RedBlackTree::handleCase0(Node *node)
{
	LOG("case0");
	(void)node;
}

void RedBlackTree::handleCase1(Node *node)
{
	LOG("case1");
	Node *grandparent = getGrandParent(node);
	Node *uncle = getUncle(node);
	Node *parent = node->parent;

	parent->color = Color::black;
	uncle->color = Color::black;
	grandparent->color = Color::red;

	fixTree(grandparent);
}

void RedBlackTree::handleCase2(Node *node)
{
	LOG("case2");
	Node *parent = node->parent;
	Node *grandparent = getGrandParent(node);

	if (parent == grandparent->left) {
		// LR: rotate left around parent, then fix the old parent (now a child)
		rotateLeft(parent);
		fixTree(parent);
	} else {
		// RL: rotate right around parent, then fix the old parent
		rotateRight(parent);
		fixTree(parent);
	}
}

void RedBlackTree::handleCase3(Node *node)
{
	LOG("case3");
	Node *parent = node->parent;
	Node *grandparent = getGrandParent(node);

	parent->color = Color::black;
	grandparent->color = Color::red;

	if (parent == grandparent->left) {
		rotateRight(grandparent);
	} else {
		rotateLeft(grandparent);
	}
}

void RedBlackTree::rotateLeft(Node *x)
{
	Node *y = x->right;
	x->right = y->left;
	if (y->left != NIL) {
		y->left->parent = x;
	}
	y->parent = x->parent;
	if (x->parent == nullptr) {
		m_Root = y;
	} else if (x == x->parent->left) {
		x->parent->left = y;
	} else {
		x->parent->right = y;
	}
	y->left = x;
	x->parent = y;
}

void RedBlackTree::rotateRight(Node *x)
{
	Node *y = x->left;
	x->left = y->right;
	if (y->right != NIL) {
		y->right->parent = x;
	}
	y->parent = x->parent;
	if (x->parent == nullptr) {
		m_Root = y;
	} else if (x == x->parent->right) {
		x->parent->right = y;
	} else {
		x->parent->left = y;
	}
	y->right = x;
	x->parent = y;
}

void RedBlackTree::transplant(Node *u, Node *v)
{
	if (u->parent == nullptr) {
		m_Root = v;
	} else if (u == u->parent->left) {
		u->parent->left = v;
	} else {
		u->parent->right = v;
	}
	v->parent = u->parent;
}

RedBlackTree::Node* RedBlackTree::minimum(Node *node)
{
	while (node->left != NIL) {
		node = node->left;
	}
	return node;
}

void RedBlackTree::fixDelete(Node *x)
{
	while (x != m_Root && x->color == Color::black) {
		if (x == x->parent->left) {
			Node *w = x->parent->right;
			if (w->color == Color::red) {
				w->color = Color::black;
				x->parent->color = Color::red;
				rotateLeft(x->parent);
				w = x->parent->right;
			}
			if (w->left->color == Color::black && w->right->color == Color::black) {
				w->color = Color::red;
				x = x->parent;
			} else {
				if (w->right->color == Color::black) {
					w->left->color = Color::black;
					w->color = Color::red;
					rotateRight(w);
					w = x->parent->right;
				}
				w->color = x->parent->color;
				x->parent->color = Color::black;
				w->right->color = Color::black;
				rotateLeft(x->parent);
				x = m_Root;
			}
		} else {
			Node *w = x->parent->left;
			if (w->color == Color::red) {
				w->color = Color::black;
				x->parent->color = Color::red;
				rotateRight(x->parent);
				w = x->parent->left;
			}
			if (w->right->color == Color::black && w->left->color == Color::black) {
				w->color = Color::red;
				x = x->parent;
			} else {
				if (w->left->color == Color::black) {
					w->right->color = Color::black;
					w->color = Color::red;
					rotateLeft(w);
					w = x->parent->left;
				}
				w->color = x->parent->color;
				x->parent->color = Color::black;
				w->left->color = Color::black;
				rotateRight(x->parent);
				x = m_Root;
			}
		}
	}
	x->color = Color::black;
}

RedBlackTree::Node* RedBlackTree::getGrandParent(Node *node) {
	if (node->parent && node->parent->parent) {
		return node->parent->parent;
	} else {
		return NIL;
	}
}

RedBlackTree::Node* RedBlackTree::getUncle(Node *node) {
	Node *grandparent = getGrandParent(node);

	if (!node->parent || grandparent == NIL) {
		return NIL;
	}

	bool isParentLeftOfGrandparent = (grandparent->left == node->parent);

	if (isParentLeftOfGrandparent) {
		return grandparent->right;
	} else {
		return grandparent->left;
	}
}

void RedBlackTree::print()
{
	if (m_Root == NIL) {
		cout << "[]\n";
		return;
	}

	vector<string> result;
	queue<Node*> q;

	q.push(m_Root);

	while (!q.empty()) {
		Node *node = q.front();
		q.pop();

		if (node == NIL) {
			result.emplace_back("null");
		} else {
			string label = to_string(node->val);
			label += (node->color == Color::red) ? "R" : "B";
			result.emplace_back(label);

			// Push children even if NIL so structure is preserved
			q.push(node->left);
			q.push(node->right);
		}
	}

	// Remove trailing nulls (LeetCode style)
	while (!result.empty() && result.back() == "null") {
		result.pop_back();
	}

	cout << "[";

	for (size_t i = 0; i < result.size(); i++) {
		cout << result[i];

		if (i + 1 < result.size()) {
			cout << ", ";
		}
	}

	cout << "]\n";
}
