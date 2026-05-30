#include "btree.hpp"
#include <iostream>
#include <limits>

void resetInput() {
    std::cin.clear();
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
}

void sampleDemo() {
    BTree tree(3);
    int values[] = {12, 25, 35, 48, 52, 67, 75, 81, 93, 7, 17, 29};

    std::cout << "\nRunning Demo...\n";
    for (int num : values) {
        tree.insert(num);
    }

    std::cout << "Traversal: ";
    tree.traverse();
    std::cout << "\n\nTree Structure:\n";
    tree.printTree();

    int tests[] = {25, 93, 100};
    for (int val : tests) {
        int pos = -1;
        if (tree.search(val, pos)) {
            std::cout << val << " found in tree.\n";
        } else {
            std::cout << val << " not present.\n";
        }
    }
}

int main() {
    std::cout << "B-Tree Program - Lab 6\n";
    std::cout << "Student: Akarsh Garg | Roll No: 24BCS10181 | Batch: B\n";

    int degree;
    std::cout << "Enter minimum degree (>=2): ";

    while (!(std::cin >> degree) || degree < 2) {
        std::cout << "Invalid input. Enter again: ";
        resetInput();
    }

    BTree tree(degree);

    int choice;
    do {
        std::cout << "\n1.Insert\n2.Search\n3.Traversal\n4.Display Tree\n5.Run Demo\n6.Exit\n";
        std::cout << "Choice: ";
        std::cin >> choice;

        switch (choice) {
            case 1: {
                int value;
                std::cout << "Enter value: ";
                std::cin >> value;
                tree.insert(value);
                break;
            }

            case 2: {
                int value, pos = -1;
                std::cout << "Enter key to search: ";
                std::cin >> value;

                if (tree.search(value, pos)) {
                    std::cout << "Element found!\n";
                } else {
                    std::cout << "Element not found.\n";
                }
                break;
            }

            case 3:
                tree.traverse();
                std::cout << "\n";
                break;

            case 4:
                tree.printTree();
                break;

            case 5:
                sampleDemo();
                break;

            case 6:
                std::cout << "Program Closed.\n";
                break;

            default:
                std::cout << "Invalid Choice\n";
        }

    } while (choice != 6);

    return 0;
}