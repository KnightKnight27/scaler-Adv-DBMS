#include "RedBlackTree.h"
#include <cassert>
#include <cstddef>
#include <iostream>
#include <vector>
#include <queue>

#define DEBUG

#ifdef DEBUG
	#define LOG(x) std::cout << x << '\n';
#else
	#define LOG(x) ;
#endif


/* Theory
 *
 * "insert" node -> node to be inserted
 * 
 * Case 0 - Parent of insert node is black
 * Case 1 - Parent of insert node is red and insert node is right of parent and uncle of insert node (sibling of parent) is also red
 * Case 2 - Parent of insert node is red and insert node is right of parent and uncle of insert node is black
 * Case 3 - Parent of insert node is red and insert node is left of parent
 *
 * Solutions to each case:
 *
 * Case 0 - Directly insert the node since it doesn't violate any rules
 * Case 1 - Switch colors of grandparent with parent and uncle nodes. Propagate this further with the grandparent as the grandparent might now be red and violate the "no two adjacent reds" rule
 * Case 2 - Perform a "left rotation" and transform situation to case 3
 * Case 3 - Perform a "right rotation"
*/

RedBlackTree::RedBlackTree()
	: NIL(new Node(0))
{
	m_Root = NIL;
}

RedBlackTree::~RedBlackTree()
{
	// TODO: Iterate and delete all nodes
}

bool RedBlackTree::find(int val)
{
	Node *node = m_Root;

	while (node != NIL) {
		if (val == node->val) {
			return true;
		} else if (val < node->val) {
			node = node->left;
		} else {
			node = node->right;
		}
	}

	return false;
}

void RedBlackTree::insert(int val)
{
	Node *trav = m_Root;
	Node *parent = nullptr;

	bool isLeftChild = false;

	while (trav != NIL) {
		parent = trav;
		if (val <= trav->val) {
			trav = trav->left;
			isLeftChild = true;
		} else {
			trav = trav->right;
			isLeftChild = false;
		}
	}

	if (trav == m_Root) {
		m_Root = new Node(val);
		m_Root->color = Color::black;
		m_Root->left = NIL;
		m_Root->right = NIL;
	} else {
		Node *node = new Node(val);
		node->left = NIL;
		node->right = NIL;
		node->parent = parent;
		if (isLeftChild) {
			parent->left = node;
		} else {
			parent->right = node;
		}

		fixTree(node);
	}
}

void RedBlackTree::remove(int val)
{
	// TO BE IMPLEMENTED
}

void RedBlackTree::fixTree(Node *node)
{
	if (isCase0(node)) {
		handleCase0(node);
	} else if (isCase3(node)) {
		handleCase3(node);
	} else if (isCase1(node)) {
		handleCase1(node);
	} else if (isCase2(node)) {
		handleCase2(node);
	} else {
		LOG("Unknown case");
		LOG("node: " << node->val << " L " << (node->left ? node->left->val : -1) << " R " << (node->right ? node->right->val : -1) << '\n');
	}
}

bool RedBlackTree::isCase0(Node *node)
{
	return (node->parent && node->parent->color == Color::black);
}
bool RedBlackTree::isCase1(Node *node)
{
	Node *uncle = getUncle(node);

	return (node->parent && node->parent->color == Color::red && uncle && uncle->color == Color::red);
}
bool RedBlackTree::isCase2(Node *node)
{
	Node *uncle = getUncle(node);

	return (node->parent && node->parent->color == Color::red && uncle && uncle->color == Color::black);
}
bool RedBlackTree::isCase3(Node *node)
{
	Node *parent = node->parent;
	Node *grandparent = getGrandParent(node);

	bool leftLeaning = (parent && parent->left == node && grandparent && grandparent->left == parent && parent->color == Color::red);
	bool rightLeaning = (parent && parent->right == node && grandparent && grandparent->right == parent && parent->color == Color::red);

	return (leftLeaning || rightLeaning);
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

	if (!node->parent || !grandparent) {
		return NIL;
	}

	bool isParentLeftOfGrandparent = (grandparent->left == node->parent);

	if (isParentLeftOfGrandparent) {
		return grandparent->right;
	} else {
		return grandparent->left;
	}
}

void RedBlackTree::handleCase0(Node *node)
{
		LOG("case0");
		if (node->left == nullptr) {
			node->left = NIL;
		}
		if (node->right == nullptr) {
			node->right = NIL;
		}
}


void RedBlackTree::handleCase1(Node *node)
{
		LOG("case1");
		Node *grandparent = getGrandParent(node);
		Node *uncle = getUncle(node);
		Node *parent = node->parent;

		parent->color = Color::black;
		uncle->color = Color::black;
		if (grandparent == m_Root) {
			grandparent->color = Color::black;
		} else {
			grandparent->color = Color::red;
			fixTree(grandparent);
		}
}

void RedBlackTree::handleCase2(Node *node)
{

		LOG("case2");
		Node *parent = node->parent;
		Node *grandparent = getGrandParent(node);

		grandparent->left = node;
		node->parent = grandparent;
		node->left = parent;
		parent->parent = node;

		fixTree(parent);
}


void RedBlackTree::handleCase3(Node *node)
{
		LOG("case3");
		Node *parent = node->parent;
		Node *grandparent = getGrandParent(node);

		parent->left = node;
		parent->right = grandparent;
		parent->parent = grandparent->parent;
		grandparent->parent = parent;

		node->left = NIL;
		node->right = NIL;

		grandparent->left = NIL;
		grandparent->right = NIL;

		m_Root = parent;
}

void RedBlackTree::print()
{
	if (m_Root == NIL) {
		std::cout << "[]\n";
		return;
	}

	std::vector<std::string> result;
	std::queue<Node*> q;

	q.push(m_Root);

	while (!q.empty()) {
		Node *node = q.front();
		q.pop();

		if (node == NIL) {
			result.emplace_back("null");
		} else {
			result.emplace_back(std::to_string(node->val));

			// Push children even if NIL so structure is preserved
			q.push(node->left);
			q.push(node->right);
		}
	}

	// Remove trailing nulls (same style as LeetCode)
	while (!result.empty() && result.back() == "null") {
		result.pop_back();
	}

	std::cout << "[";

	for (size_t i = 0; i < result.size(); i++) {
		std::cout << result[i];

		if (i + 1 < result.size()) {
			std::cout << ", ";
		}
	}

	std::cout << "]\n";
}
