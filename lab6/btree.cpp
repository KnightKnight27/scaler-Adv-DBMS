/*
 * Lab 6 - B-Tree Implementation
 * Student Name: Rohan Ranjan
 * Roll Number: 24BCS10428
 *
 * Description:
 * A complete and highly optimized C++17 implementation of a B-Tree structure.
 * Supports efficient searching, top-down insertion with node splitting, inorder 
 * sorting verification, and structured level-order display.
 */

#include <algorithm>
#include <iostream>
#include <memory>
#include <queue>
#include <stdexcept>
#include <sstream>
#include <string>
#include <vector>

class BTree
{
public:
    // Constructor to initialize B-Tree with a given minimum degree
    explicit BTree(int minimum_degree)
        : min_degree(minimum_degree), root_node(std::make_unique<BTreeNode>(true))
    {
        if (min_degree < 2)
        {
            throw std::invalid_argument("Minimum degree must be at least 2");
        }
    }

    // Insert a unique key into the B-Tree
    void insert(int key)
    {
        if (contains(key))
        {
            return;
        }

        // If the root is full, split it and increase the tree height
        if (is_node_full(root_node.get()))
        {
            auto new_root = std::make_unique<BTreeNode>(false);
            new_root->children.push_back(std::move(root_node));
            split_full_child(new_root.get(), 0);
            root_node = std::move(new_root);
        }

        insert_into_non_full_node(root_node.get(), key);
    }

    // Search for a key in the B-Tree
    bool contains(int key) const
    {
        return find_key_recursive(root_node.get(), key);
    }

    // Retrieve all keys in sorted order (inorder traversal)
    std::vector<int> inorder() const
    {
        std::vector<int> sorted_keys;
        traverse_inorder(root_node.get(), sorted_keys);
        return sorted_keys;
    }

    // Output all sorted keys
    void printInorder() const
    {
        std::cout << "Sorted keys: ";
        const std::vector<int> keys = inorder();

        for (int key : keys)
        {
            std::cout << key << ' ';
        }

        std::cout << '\n';
    }

    // Output the layout of the tree level by level
    void printLevels() const
    {
        std::queue<const BTreeNode*> node_queue;
        node_queue.push(root_node.get());

        int current_level = 0;
        std::cout << "Level order layout:\n";

        while (!node_queue.empty())
        {
            const int nodes_at_level = static_cast<int>(node_queue.size());
            std::cout << "Level " << current_level << ": ";

            for (int i = 0; i < nodes_at_level; ++i)
            {
                const BTreeNode* current = node_queue.front();
                node_queue.pop();

                std::cout << serialize_node(current) << ' ';

                for (const auto& child : current->children)
                {
                    node_queue.push(child.get());
                }
            }

            std::cout << '\n';
            ++current_level;
        }
    }

    // Validate the B-Tree invariants (sorting, key count, identical leaf depths)
    bool validate() const
    {
        const std::vector<int> keys = inorder();
        if (!std::is_sorted(keys.begin(), keys.end()))
        {
            return false;
        }

        int leaf_depth = -1;
        return verify_node_constraints(root_node.get(), true, 0, leaf_depth, nullptr, nullptr);
    }

private:
    // Internal node structure representable in B-Tree
    struct BTreeNode
    {
        bool is_leaf;
        std::vector<int> keys;
        std::vector<std::unique_ptr<BTreeNode>> children;

        explicit BTreeNode(bool leaf)
            : is_leaf(leaf)
        {
        }
    };

    int min_degree;
    std::unique_ptr<BTreeNode> root_node;

    // Check if the node is full (holds max allowed keys)
    bool is_node_full(const BTreeNode* node) const
    {
        return static_cast<int>(node->keys.size()) == (2 * min_degree - 1);
    }

    // Recursively look up a key starting from a specific node
    bool find_key_recursive(const BTreeNode* node, int key) const
    {
        auto it = std::lower_bound(node->keys.begin(), node->keys.end(), key);

        if (it != node->keys.end() && *it == key)
        {
            return true;
        }

        if (node->is_leaf)
        {
            return false;
        }

        const int child_idx = static_cast<int>(std::distance(node->keys.begin(), it));
        return find_key_recursive(node->children[child_idx].get(), key);
    }

    // Split a full child node of a given parent node
    void split_full_child(BTreeNode* parent, int child_idx)
    {
        BTreeNode* child = parent->children[child_idx].get();
        auto sibling = std::make_unique<BTreeNode>(child->is_leaf);

        const int median_key = child->keys[min_degree - 1];

        // Move upper keys to sibling
        sibling->keys.reserve(min_degree - 1);
        std::move(child->keys.begin() + min_degree, child->keys.end(), std::back_inserter(sibling->keys));
        child->keys.resize(min_degree - 1);

        // Move upper child pointers to sibling if not a leaf
        if (!child->is_leaf)
        {
            sibling->children.reserve(min_degree);
            std::move(child->children.begin() + min_degree, child->children.end(), std::back_inserter(sibling->children));
            child->children.resize(min_degree);
        }

        // Insert key and child pointer into the parent node
        parent->keys.insert(parent->keys.begin() + child_idx, median_key);
        parent->children.insert(parent->children.begin() + child_idx + 1, std::move(sibling));
    }

    // Insert a key when the target node is verified not to be full
    void insert_into_non_full_node(BTreeNode* node, int key)
    {
        if (node->is_leaf)
        {
            auto pos = std::lower_bound(node->keys.begin(), node->keys.end(), key);
            node->keys.insert(pos, key);
            return;
        }

        int child_idx = static_cast<int>(
            std::upper_bound(node->keys.begin(), node->keys.end(), key) - node->keys.begin()
        );

        if (is_node_full(node->children[child_idx].get()))
        {
            split_full_child(node, child_idx);

            if (key > node->keys[child_idx])
            {
                ++child_idx;
            }
        }

        insert_into_non_full_node(node->children[child_idx].get(), key);
    }

    // Recursively collect keys in inorder sequence
    void traverse_inorder(const BTreeNode* node, std::vector<int>& sorted_keys) const
    {
        for (std::size_t i = 0; i < node->keys.size(); ++i)
        {
            if (!node->is_leaf)
            {
                traverse_inorder(node->children[i].get(), sorted_keys);
            }
            sorted_keys.push_back(node->keys[i]);
        }

        if (!node->is_leaf)
        {
            traverse_inorder(node->children.back().get(), sorted_keys);
        }
    }

    // Validate tree nodes for B-Tree invariants recursively
    bool verify_node_constraints(
        const BTreeNode* node,
        bool is_root_node,
        int current_depth,
        int& expected_leaf_depth,
        const int* min_bound,
        const int* max_bound
    ) const
    {
        if (!std::is_sorted(node->keys.begin(), node->keys.end()))
        {
            return false;
        }

        const int num_keys = static_cast<int>(node->keys.size());

        // Check key count constraints
        if (!is_root_node && (num_keys < min_degree - 1 || num_keys > 2 * min_degree - 1))
        {
            return false;
        }

        if (is_root_node && num_keys > 2 * min_degree - 1)
        {
            return false;
        }

        // Validate values are strictly within bounds
        for (int k : node->keys)
        {
            if ((min_bound != nullptr && k <= *min_bound) || (max_bound != nullptr && k >= *max_bound))
            {
                return false;
            }
        }

        // Check identical leaf depth property
        if (node->is_leaf)
        {
            if (expected_leaf_depth == -1)
            {
                expected_leaf_depth = current_depth;
            }
            return expected_leaf_depth == current_depth;
        }

        // Branching factor check
        if (static_cast<int>(node->children.size()) != num_keys + 1)
        {
            return false;
        }

        // Recursively validate child nodes
        for (int i = 0; i <= num_keys; ++i)
        {
            const int* child_min = min_bound;
            const int* child_max = max_bound;

            if (i > 0)
            {
                child_min = &node->keys[i - 1];
            }

            if (i < num_keys)
            {
                child_max = &node->keys[i];
            }

            if (!verify_node_constraints(node->children[i].get(), false, current_depth + 1, expected_leaf_depth, child_min, child_max))
            {
                return false;
            }
        }

        return true;
    }

    // Help serialize a node representation as a readable string
    static std::string serialize_node(const BTreeNode* node)
    {
        std::ostringstream oss;
        oss << '[';

        for (std::size_t i = 0; i < node->keys.size(); ++i)
        {
            if (i > 0)
            {
                oss << '|';
            }
            oss << node->keys[i];
        }

        oss << ']';
        return oss.str();
    }
};

int main()
{
    BTree tree(3);
    const std::vector<int> values{
        42, 18, 7, 29, 63, 54, 11, 3, 91, 75, 33, 48,
        57, 60, 2, 5, 8, 14, 21, 24, 27, 36, 39, 69
    };

    for (int value : values)
    {
        tree.insert(value);
        std::cout << "Inserted " << value
                  << " | valid: " << (tree.validate() ? "yes" : "no")
                  << '\n';
    }

    std::cout << '\n';
    tree.printInorder();
    tree.printLevels();

    std::cout << "\nSearch 54: " << (tree.contains(54) ? "found" : "not found") << '\n';
    std::cout << "Search 100: " << (tree.contains(100) ? "found" : "not found") << '\n';
    std::cout << "Final validation: " << (tree.validate() ? "passed" : "failed") << '\n';

    return 0;
}
