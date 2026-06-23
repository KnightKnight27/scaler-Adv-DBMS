#pragma once

#include "common/config.h"
#include "common/types.h"
#include "storage/disk_manager.h"
#include "storage/buffer_pool.h"

#include <string>
#include <vector>
#include <optional>

// ============================================================
// BPlusTree — On-disk B+Tree index (int keys → RID)
//
// Each node is one page. Page 0 = metadata (root_page_id).
// Internal nodes: keys + child page_ids
// Leaf nodes: keys + RIDs, linked via next_leaf for range scans.
//
// Evolved from Lab06's B-Tree, key differences:
//   - Data (RIDs) stored ONLY in leaves
//   - Leaf nodes linked for range scans
//   - Nodes live on disk pages, not in-memory pointers
// ============================================================

// ---- Node page layout constants ----
static constexpr int BTREE_HEADER_SIZE = 12;
// Header: [type(1B)][num_keys(2B)][next_leaf(4B)][reserved(5B)]

static constexpr uint8_t NODE_INTERNAL = 0;
static constexpr uint8_t NODE_LEAF = 1;

// Max keys per node type (computed from PAGE_SIZE = 4096)
// Internal: n keys(4B each) + (n+1) children(4B each) fit in 4084 bytes → n=510
// Leaf: n keys(4B each) + n RIDs(6B each) fit in 4084 bytes → n=408
static constexpr int INTERNAL_MAX_KEYS = 510;
static constexpr int LEAF_MAX_KEYS = 408;

// Data offsets within a node page
static constexpr int KEYS_OFFSET = BTREE_HEADER_SIZE;  // 12
static constexpr int INTERNAL_CHILDREN_OFFSET = KEYS_OFFSET + INTERNAL_MAX_KEYS * 4;  // 2052
static constexpr int LEAF_RIDS_OFFSET = KEYS_OFFSET + LEAF_MAX_KEYS * 4;  // 1644

class BPlusTree {
public:
    // Opens or creates an index file. Page 0 = metadata page.
    explicit BPlusTree(const std::string& index_file);
    ~BPlusTree();

    BPlusTree(const BPlusTree&) = delete;
    BPlusTree& operator=(const BPlusTree&) = delete;

    // Core operations
    void Insert(int key, const RID& rid);
    std::optional<RID> Search(int key);
    bool Delete(int key);
    std::vector<RID> RangeScan(int low_key, int high_key);

    // Stats
    int GetRootPageId() const { return root_page_id_; }
    bool IsEmpty() const;

private:
    DiskManager* disk_mgr_;
    BufferPool* pool_;
    int root_page_id_;

    // ---- Metadata page (page 0) ----
    void WriteMetadata();
    void ReadMetadata();

    // ---- Node creation ----
    int CreateNode(uint8_t node_type);

    // ---- Node field accessors (read/write raw page data) ----
    static uint8_t GetNodeType(Page* p);
    static void SetNodeType(Page* p, uint8_t t);
    static uint16_t GetNumKeys(Page* p);
    static void SetNumKeys(Page* p, uint16_t n);
    static int GetNextLeaf(Page* p);
    static void SetNextLeaf(Page* p, int next);

    // Keys (both node types)
    static int GetKey(Page* p, int idx);
    static void SetKey(Page* p, int idx, int key);

    // Children (internal nodes)
    static int GetChild(Page* p, int idx);
    static void SetChild(Page* p, int idx, int child_page_id);

    // RIDs (leaf nodes)
    static RID GetLeafRID(Page* p, int idx);
    static void SetLeafRID(Page* p, int idx, const RID& rid);

    // ---- Traversal ----
    // Find leaf page containing key. Returns page_id. Fills path with ancestors.
    int FindLeaf(int key, std::vector<int>& path);

    // ---- Insert helpers ----
    void InsertIntoLeaf(Page* leaf, int key, const RID& rid);
    void SplitLeaf(int leaf_page_id, int key, const RID& rid, std::vector<int>& path);
    void InsertIntoParent(int left_page_id, int key, int right_page_id, std::vector<int>& path);
    void SplitInternal(int node_page_id, int key, int right_child, std::vector<int>& path);
};
