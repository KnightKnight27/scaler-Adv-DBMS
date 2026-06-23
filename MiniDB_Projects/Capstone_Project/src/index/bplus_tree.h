#pragma once

#include "common/types.h"
#include "common/config.h"
#include <vector>
#include <optional>
#include <functional>

/**
 * @class BPlusTree
 * @brief Memory-resident B+ Tree index supporting primary key lookups over int32_t keys.
 *
 * Designed with a static ORDER configuration. Handles key insertion, point queries,
 * key removals (with simplified lazy deletion), range traversals via leaf linkages,
 * and diagnostic tree printing.
 */
class BPlusTree {
public:
    /**
     * @brief Construct a new B+ Tree instance.
     */
    BPlusTree() = default;

    /**
     * @brief Destructor. Releases all tree nodes.
     */
    ~BPlusTree();

    // Disable copying for safety
    BPlusTree(const BPlusTree&) = delete;
    BPlusTree& operator=(const BPlusTree&) = delete;

    /**
     * @brief Inserts a key-RecordID pair into the index.
     * Overwrites the old RecordID if the key already exists.
     */
    void insert(int32_t key, RecordID rid);

    /**
     * @brief Search for a specific key.
     * @return RecordID if found, or std::nullopt.
     */
    std::optional<RecordID> search(int32_t key) const;

    /**
     * @brief Removes a key from the index.
     * @return True if the key was found and deleted, false otherwise.
     */
    bool remove(int32_t key);

    /**
     * @brief Iterates over all index entries in ascending order.
     */
    void scanAll(std::function<void(int32_t, const RecordID&)> callback) const;

    /**
     * @brief Prints diagnostic representation of the tree structure.
     */
    void printTree() const;

    /**
     * @brief Returns the total number of entries in the tree.
     */
    size_t size() const { return num_entries_; }

private:
    /**
     * @struct Node
     * @brief Represents either an internal router node or a leaf data node.
     */
    struct Node {
        bool is_leaf = true;                   ///< True if this is a leaf node
        int num_keys = 0;                     ///< Current number of active keys
        int32_t keys[BTREE_ORDER];            ///< Ordered keys array

        Node* children[BTREE_ORDER + 1];      ///< Subtree child pointers (internal nodes)
        RecordID values[BTREE_ORDER];         ///< Associated RecordIDs (leaf nodes)
        Node* next_leaf = nullptr;            ///< Pointer to sibling leaf node (leaf nodes)

        Node();
    };

    void insertInto(Node* node, int32_t key, RecordID rid,
                    int32_t& promoted_key, Node*& new_right_child);

    Node* splitLeaf(Node* leaf, int32_t& promoted_key);

    Node* splitInternal(Node* node, int32_t& promoted_key);

    std::optional<RecordID> searchFrom(const Node* node, int32_t key) const;

    Node* findLeaf(int32_t key) const;

    bool removeFrom(Node* node, Node* parent, int child_idx, int32_t key);

    void freeTree(Node* node);

    Node* root_ = nullptr;            ///< Pointer to tree root
    size_t num_entries_ = 0;         ///< Number of elements tracked in the tree
};
