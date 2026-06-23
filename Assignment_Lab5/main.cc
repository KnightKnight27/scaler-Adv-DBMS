#include "RedBlackTree.h"
#include <iostream>

int main()
{
	RedBlackTree rbt;

	// insert some values
	int keys[] = {10, 20, 30, 15, 25, 5, 1, 7};
	int n = sizeof(keys) / sizeof(keys[0]);

	std::cout << "Inserting: ";
	for (int i = 0; i < n; i++) {
		std::cout << keys[i] << " ";
		rbt.insert(keys[i]);
	}
	std::cout << "\n\n";

	// print tree level order
	std::cout << "Level order:\n";
	rbt.printLevelOrder();
	std::cout << "\n";

	// print inorder (should be sorted)
	std::cout << "Inorder (sorted):\n";
	rbt.printInOrder();
	std::cout << "\n";

	// test search
	std::cout << "Search:\n";
	int toSearch[] = {10, 25, 99, 1};
	for (int i = 0; i < 4; i++) {
		std::cout << "  search(" << toSearch[i] << ") = "
				  << (rbt.search(toSearch[i]) ? "found" : "not found") << "\n";
	}

	return 0;
}
