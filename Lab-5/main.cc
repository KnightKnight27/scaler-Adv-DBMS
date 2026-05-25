#include "RedBlackTree.h"
#include <iostream>

using namespace std;

int main()
{
	RedBlackTree tree;

	int values[] = {10, 20, 30, 15, 25, 5, 1, 8, 40, 35};

	cout << "Inserting: ";
	for (int v : values) {
		cout << v << ' ';
		tree.insert(v);
	}
	cout << "\n\nTree after inserts (level-order, suffix R = red, B = black):\n";
	tree.print();

	cout << "\nfind(15) -> " << (tree.find(15) ? "found" : "not found") << '\n';
	cout << "find(25) -> " << (tree.find(25) ? "found" : "not found") << '\n';
	cout << "find(99) -> " << (tree.find(99) ? "found" : "not found") << '\n';

	cout << "\nRemoving 20, 5, 30 ...\n";
	tree.remove(20);
	tree.remove(5);
	tree.remove(30);

	cout << "Tree after removals:\n";
	tree.print();

	cout << "\nfind(20) -> " << (tree.find(20) ? "found" : "not found") << '\n';
	cout << "find(15) -> " << (tree.find(15) ? "found" : "not found") << '\n';

	return 0;
}
