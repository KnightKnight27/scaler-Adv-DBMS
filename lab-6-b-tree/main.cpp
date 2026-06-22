// main.cpp - Interactive driver for BTree
#include "btree.h"
#include <iostream>

using std::cin;
using std::cout;

#ifndef TESTING
int main() {
  int t = 0;
  cout << "Enter minimum degree (t >= 2): ";
  if (!(cin >> t)) {
    return 1;
  }
  if (t < 2) {
    cout << "Invalid: t must be at least 2.\n";
    return 1;
  }

  BTree tree(t);

  int choice = 0;
  int key = 0;
  while (true) {
    cout << "\n=== B-Tree Menu ===\n"
         << "1) Insert\n"
         << "2) Delete\n"
         << "3) Search\n"
         << "4) Show inorder\n"
         << "5) Show levels\n"
         << "6) Exit\n"
         << "Choose: ";

    if (!(cin >> choice)) {
      break;
    }

    if (choice == 1) {
      cout << "Key to insert: ";
      if (cin >> key) {
        tree.insert(key);
      }
    } else if (choice == 2) {
      cout << "Key to delete: ";
      if (cin >> key) {
        tree.remove(key);
      }
    } else if (choice == 3) {
      cout << "Key to search: ";
      if (cin >> key) {
        cout << (tree.search(key) ? "Found\n" : "Not found\n");
      }
    } else if (choice == 4) {
      cout << "Inorder: ";
      tree.print_inorder();
    } else if (choice == 5) {
      cout << "Levels:\n";
      tree.print_levels();
    } else if (choice == 6) {
      cout << "Bye.\n";
      break;
    } else {
      cout << "Invalid option. Please try again.\n";
    }
  }

  return 0;
}
#endif