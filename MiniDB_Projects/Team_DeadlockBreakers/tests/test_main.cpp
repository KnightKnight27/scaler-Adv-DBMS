#include "test_support.h"
#include <iostream>

int main() {
    try {
        TestStorageAndIndex();
        TestTransactionsAndDeadlock();
        TestRecovery();
        std::cout << "All MiniDB correctness tests passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "TEST FAILURE: " << error.what() << "\n";
        return 1;
    }
}
