#include "RedBlackTree.h"
#include <iostream>

using namespace std;

int main()
{
	RBTree tree;

	// Insert a set of keys to exercise rebalancing
	int keys[] = {12, 25, 36, 18, 28, 7, 3, 9, 45, 33};

	cout << "Inserting keys: ";
	for (int k : keys) {
		cout << k << ' ';
		tree.insert(k);
	}
	cout << "\n\nTree state after insertions (BFS level-order, R=red B=black):\n";
	tree.printLevelOrder();

	// Lookup tests
	cout << "\nsearch(18) -> " << (tree.search(18) ? "found" : "not found") << '\n';
	cout << "search(28) -> " << (tree.search(28) ? "found" : "not found") << '\n';
	cout << "search(50) -> " << (tree.search(50) ? "found" : "not found") << '\n';

	// Deletion tests
	cout << "\nDeleting keys: 25, 7, 36 ...\n";
	tree.remove(25);
	tree.remove(7);
	tree.remove(36);

	cout << "Tree state after deletions:\n";
	tree.printLevelOrder();

	cout << "\nsearch(25) -> " << (tree.search(25) ? "found" : "not found") << '\n';
	cout << "search(18) -> " << (tree.search(18) ? "found" : "not found") << '\n';

	return 0;
}
