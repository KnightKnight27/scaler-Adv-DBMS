#include "RedBlackTree.h"
#include <iostream>
#include <queue>
#include <vector>
#include <string>

// uncomment to disable debug output
#define DEBUG

#ifdef DEBUG
	#define LOG(x) std::cout << x << '\n'
#else
	#define LOG(x) ;
#endif


RedBlackTree::RedBlackTree()
	: NIL(new Node(0))
{
	NIL->color = BLACK;
	root = NIL;
}

RedBlackTree::~RedBlackTree()
{
	deleteTree(root);
	delete NIL;
}

void RedBlackTree::deleteTree(Node *node)
{
	if (node == NIL || node == nullptr) return;
	deleteTree(node->left);
	deleteTree(node->right);
	delete node;
}


// search for a value, returns true if found
bool RedBlackTree::search(int key)
{
	Node *curr = root;

	while (curr != NIL) {
		if (key == curr->key) {
			return true;
		} else if (key < curr->key) {
			curr = curr->left;
		} else {
			curr = curr->right;
		}
	}

	return false;
}


void RedBlackTree::insert(int key)
{
	// normal BST insert first
	Node *curr = root;
	Node *parent = nullptr;
	bool wentLeft = false;

	while (curr != NIL) {
		parent = curr;
		if (key <= curr->key) {
			curr = curr->left;
			wentLeft = true;
		} else {
			curr = curr->right;
			wentLeft = false;
		}
	}

	// if tree is empty
	if (curr == root) {
		root = new Node(key);
		root->color = BLACK;
		root->left = NIL;
		root->right = NIL;
		return;
	}

	// create new node and attach
	Node *z = new Node(key);
	z->left = NIL;
	z->right = NIL;
	z->parent = parent;

	if (wentLeft) {
		parent->left = z;
	} else {
		parent->right = z;
	}

	// fix any violations
	fixInsert(z);
}


void RedBlackTree::fixInsert(Node *z)
{
	if (isCase0(z)) {
		handleCase0(z);
	} else if (isCase3(z)) {
		handleCase3(z);
	} else if (isCase1(z)) {
		handleCase1(z);
	} else if (isCase2(z)) {
		handleCase2(z);
	} else {
		LOG("unknown case for node " << z->key);
	}
}


// case 0 - parent is black, nothing to fix
bool RedBlackTree::isCase0(Node *z)
{
	return (z->parent != nullptr && z->parent->color == BLACK);
}

// case 1 - parent red and uncle red
bool RedBlackTree::isCase1(Node *z)
{
	Node *u = getUncle(z);
	return (z->parent && z->parent->color == RED && u && u->color == RED);
}

// case 2 - parent red, uncle black, zig zag
bool RedBlackTree::isCase2(Node *z)
{
	Node *u = getUncle(z);
	Node *p = z->parent;
	Node *gp = getGrandparent(z);

	if (!p || p->color != RED) return false;
	if (!u || u->color != BLACK) return false;

	bool lr = (gp && gp->left == p && p->right == z);
	bool rl = (gp && gp->right == p && p->left == z);

	return (lr || rl);
}

// case 3 - parent red, uncle black, straight line
bool RedBlackTree::isCase3(Node *z)
{
	Node *p = z->parent;
	Node *gp = getGrandparent(z);

	if (!p || p->color != RED) return false;

	bool ll = (gp && gp->left == p && p->left == z);
	bool rr = (gp && gp->right == p && p->right == z);

	return (ll || rr);
}


void RedBlackTree::handleCase0(Node *z)
{
	LOG("case0");
	if (z->left == nullptr) z->left = NIL;
	if (z->right == nullptr) z->right = NIL;
}

void RedBlackTree::handleCase1(Node *z)
{
	LOG("case1");
	Node *p = z->parent;
	Node *u = getUncle(z);
	Node *gp = getGrandparent(z);

	p->color = BLACK;
	u->color = BLACK;

	if (gp == root) {
		gp->color = BLACK;
	} else {
		gp->color = RED;
		fixInsert(gp);
	}
}

// zig zag -> rotate to make it straight line then call fixInsert again
void RedBlackTree::handleCase2(Node *z)
{
	LOG("case2");
	Node *p = z->parent;
	Node *gp = getGrandparent(z);

	if (gp && gp->left == p) {
		rotateLeft(p);
		fixInsert(p);
	} else {
		rotateRight(p);
		fixInsert(p);
	}
}

void RedBlackTree::handleCase3(Node *z)
{
	LOG("case3");
	Node *p = z->parent;
	Node *gp = getGrandparent(z);

	// swap colors
	p->color = BLACK;
	gp->color = RED;

	if (p->left == z) {
		rotateRight(gp);
	} else {
		rotateLeft(gp);
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
		root = y;
	} else if (x == x->parent->left) {
		x->parent->left = y;
	} else {
		x->parent->right = y;
	}

	y->left = x;
	x->parent = y;
}

void RedBlackTree::rotateRight(Node *y)
{
	Node *x = y->left;
	y->left = x->right;

	if (x->right != NIL) {
		x->right->parent = y;
	}

	x->parent = y->parent;

	if (y->parent == nullptr) {
		root = x;
	} else if (y == y->parent->left) {
		y->parent->left = x;
	} else {
		y->parent->right = x;
	}

	x->right = y;
	y->parent = x;
}


RedBlackTree::Node* RedBlackTree::getGrandparent(Node *z)
{
	if (z->parent && z->parent->parent) {
		return z->parent->parent;
	}
	return NIL;
}

RedBlackTree::Node* RedBlackTree::getUncle(Node *z)
{
	Node *gp = getGrandparent(z);
	if (gp == NIL || !z->parent) return NIL;

	if (gp->left == z->parent) {
		return gp->right;
	} else {
		return gp->left;
	}
}


void RedBlackTree::remove(int key)
{
	// TODO: implement delete
	(void)key;
}


// level order print (bfs)
void RedBlackTree::printLevelOrder()
{
	if (root == NIL) {
		std::cout << "[]\n";
		return;
	}

	std::vector<std::string> result;
	std::queue<Node*> q;
	q.push(root);

	while (!q.empty()) {
		Node *node = q.front();
		q.pop();

		if (node == NIL) {
			result.push_back("null");
		} else {
			std::string label = std::to_string(node->key);
			label += (node->color == RED) ? "(R)" : "(B)";
			result.push_back(label);
			q.push(node->left);
			q.push(node->right);
		}
	}

	// remove trailing nulls
	while (!result.empty() && result.back() == "null") {
		result.pop_back();
	}

	std::cout << "[";
	for (int i = 0; i < (int)result.size(); i++) {
		std::cout << result[i];
		if (i + 1 < (int)result.size()) std::cout << ", ";
	}
	std::cout << "]\n";
}

void RedBlackTree::printInOrder()
{
	inOrderHelper(root);
	std::cout << '\n';
}

void RedBlackTree::inOrderHelper(Node *node)
{
	if (node == NIL) return;
	inOrderHelper(node->left);
	std::cout << node->key << (node->color == RED ? "(R)" : "(B)") << " ";
	inOrderHelper(node->right);
}
