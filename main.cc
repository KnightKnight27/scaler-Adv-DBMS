#include <iostream>
#include <vector>
#include <cassert>
#include "RedBlackTree.h"

// RBT Invariant Verifiers
class RBTVerifier {
private:
    // Helper to calculate and verify Black Height recursively
    static int verifyBlackHeight(const RedBlackTree& tree, Node* node, bool& valid) {
        if (tree.isNil(node)) {
            return 1; // Leaf count as black
        }

        // Check Rule 3: No two consecutive RED nodes
        if (node->color == RED) {
            if ((node->left != nullptr && node->left->color == RED) || 
                (node->right != nullptr && node->right->color == RED)) {
                std::cout << "[Verification Failure] Consecutive RED nodes detected at " << node->data << std::endl;
                valid = false;
            }
        }

        int leftBlackHeight = verifyBlackHeight(tree, node->left, valid);
        int rightBlackHeight = verifyBlackHeight(tree, node->right, valid);

        // Check Rule 4: Every path must have same black height
        if (leftBlackHeight != rightBlackHeight) {
            std::cout << "[Verification Failure] Black height mismatch at " << node->data 
                      << ": Left side = " << leftBlackHeight << ", Right side = " << rightBlackHeight << std::endl;
            valid = false;
        }

        return leftBlackHeight + (node->color == BLACK ? 1 : 0);
    }

public:
    static bool verifyInvariants(const RedBlackTree& tree) {
        Node* root = tree.getRoot();
        if (tree.isNil(root)) {
            return true; // Empty tree is valid
        }

        bool valid = true;

        // Invariant 1: Root is BLACK
        if (root->color != BLACK) {
            std::cout << "[Verification Failure] Root is not BLACK." << std::endl;
            valid = false;
        }

        verifyBlackHeight(tree, root, valid);
        return valid;
    }
};

int main() {
    std::cout << "===========================================" << std::endl;
    std::cout << "  Red-Black Tree (RBT) Verification Lab   " << std::endl;
    std::cout << "===========================================" << std::endl;

    RedBlackTree rbt;

    // Insert keys: 10, 15, 20, 25, 30
    std::vector<int> keys = {10, 15, 20, 25, 30};
    
    std::cout << "\nInserting elements step-by-step:" << std::endl;
    for (int key : keys) {
        std::cout << "-> Inserting: " << key << std::endl;
        rbt.insert(key);
        
        // Output RBT structure
        rbt.printTree();
        
        // Assert invariants are met
        bool isValid = RBTVerifier::verifyInvariants(rbt);
        if (isValid) {
            std::cout << "✓ RBT Invariants: Valid\n" << std::endl;
        } else {
            std::cout << "✗ RBT Invariants: Violation Detected!\n" << std::endl;
            return 1;
        }
    }

    std::cout << "-------------------------------------------" << std::endl;
    std::cout << "Final Tree Traversal" << std::endl;
    std::cout << "-------------------------------------------" << std::endl;
    std::cout << "Inorder Traversal: ";
    rbt.inorder();
    
    std::cout << "\nVisualizing Balanced Tree Structure:" << std::endl;
    rbt.printTree();
    
    std::cout << "\nVerification Completed Successfully! All RBT Invariants Maintained." << std::endl;
    std::cout << "===========================================" << std::endl;

    return 0;
}
