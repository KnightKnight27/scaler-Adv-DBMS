#include "RedBlackTree.h"
#include <iostream>

using namespace std;

int main() {
	RedBlackBST bst;

	// Populate the tree with dynamic values
	int dataValues[] = {14, 22, 38, 11, 29, 6, 2, 8, 44, 32};

	cout << "Adding items to the tree: ";
	for (int val : dataValues) {
		cout << val << " ";
		bst.add(val);
	}
	cout << "\n\nRed-Black Tree BFS representation (R=Red, B=Black):\n";
	bst.displayBFS();

	// Search operations
	cout << "\nlookup(11) -> " << (bst.lookup(11) ? "Found" : "Missing") << "\n";
	cout << "lookup(29) -> " << (bst.lookup(29) ? "Found" : "Missing") << "\n";
	cout << "lookup(99) -> " << (bst.lookup(99) ? "Found" : "Missing") << "\n";

	// Removal operations
	cout << "\nErasing items: 22, 6, 38 ...\n";
	bst.erase(22);
	bst.erase(6);
	bst.erase(38);

	cout << "Red-Black Tree BFS representation after erasure:\n";
	bst.displayBFS();

	cout << "\nlookup(22) -> " << (bst.lookup(22) ? "Found" : "Missing") << "\n";
	cout << "lookup(14) -> " << (bst.lookup(14) ? "Found" : "Missing") << "\n";

	return 0;
}
