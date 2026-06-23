#pragma once

#include "storage/buffer.h"
#include "storage/page.h"
#include <vector>

#pragma pack(push, 1)
struct BTreeHeader {
    uint8_t is_leaf;
    uint16_t num_keys;
    PageId_t next_page_id; // Only used for LeafNode linking
};

constexpr int BTREE_MAX_KEYS = 200;

struct BLeafNode {
    BTreeHeader header;
    int32_t keys[BTREE_MAX_KEYS];
    RID values[BTREE_MAX_KEYS];
};

struct BInternalNode {
    BTreeHeader header;
    int32_t keys[BTREE_MAX_KEYS];
    PageId_t children[BTREE_MAX_KEYS + 1];
};
#pragma pack(pop)

class BPlusTree {
public:
    BPlusTree(PageId_t root_page_id, BufferPoolManager* bpm);

    bool Search(int32_t key, RID& result);
    bool Insert(int32_t key, const RID& value);
    bool Delete(int32_t key);

    PageId_t GetRootPageId() const { return root_page_id_; }
    void PrintTree();

private:
    PageId_t FindLeafPage(int32_t key, std::vector<PageId_t>& path);
    void InsertIntoLeaf(Page* leaf_page, int32_t key, const RID& value);
    void SplitLeaf(PageId_t leaf_page_id, std::vector<PageId_t>& path);
    void InsertIntoParent(PageId_t child_id, int32_t key, PageId_t new_child_id, std::vector<PageId_t>& path);
    void SplitInternal(PageId_t internal_page_id, std::vector<PageId_t>& path);
    void PrintNode(PageId_t page_id, int level);

    PageId_t root_page_id_;
    BufferPoolManager* bpm_;
};
