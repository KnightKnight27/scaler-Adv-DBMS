// Compact B-tree implementation with rewritten identifiers and comments
#include "btree.h"
#include <iostream>

using namespace std;

// Interactive driver; excluded when building tests
#ifndef TESTING
int main()
{
    int t;
    cout << "Enter minimum degree (t >= 2): ";
    if (!(cin >> t))
        return 1;
    if (t < 2)
    {
        cout << "Invalid: t must be at least 2.\n";
        return 1;
    }

    BalancedTree tree(t);

    int choice = 0, key = 0;
    while (true)
    {
        cout << "\nB-Tree Menu\n1) Insert\n2) Delete\n3) Search\n4) Show inorder\n5) Show levels\n6) Exit\nChoose: ";
        if (!(cin >> choice))
            break;

        if (choice == 1)
        {
            cout << "Key to insert: ";
            cin >> key;
            tree.insert(key);
        }
        else if (choice == 2)
        {
            cout << "Key to delete: ";
            cin >> key;
            tree.remove(key);
        }
        else if (choice == 3)
        {
            cout << "Key to search: ";
            cin >> key;
            cout << (tree.search(key) ? "Found\n" : "Not found\n");
        }
        else if (choice == 4)
        {
            cout << "Inorder: ";
            tree.print_inorder();
        }
        else if (choice == 5)
        {
            cout << "Levels:\n";
            tree.print_levels();
        }
        else if (choice == 6)
        {
            cout << "Bye.\n";
            break;
        }
        else
            cout << "Invalid option.\n";
    }
    return 0;
}

#endif
