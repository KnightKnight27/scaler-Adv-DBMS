// =============================================================================
// src/index/bplus_tree.cpp
// -----------------------------------------------------------------------------
// B+ tree over the BufferPool. Keys are opaque byte strings (Key).
//
// On-disk layout per node is defined in include/index/node.h. We use a
// fanout of MAX_KEYS = 64 which keeps even the worst-case split well under
// a single 4 KB page.
//
// Search     — descend from root, choose child at each internal node.
// Insert     — find leaf, shift suffix; if the leaf is full, split it and
//              propagate the separator up. Internal nodes that overflow are
//              split recursively, so the tree supports arbitrary height; a
//              new internal root is created when the current root overflows.
//              New pages are allocated via BufferPool::allocatePage() —
//              insert returns a non-OK status only on genuine allocation /
//              buffer-pool failure, never on normal internal-node growth.
// Remove     — find leaf, shift suffix left; no merge/borrow for v1 (pages
//              may underflow, even to empty; an empty leaf stays in the
//              chain). The leaf path is unchanged by the recursive-split
//              machinery, so remove remains correct.
// Range scan — find leftmost leaf >= lo, then follow nextLeaf until > hi.
// =============================================================================
#include "index/bplus_tree.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "index/node.h"
#include "storage/buffer_pool.h"
#include "storage/page.h"

namespace minidb::index {

using storage::BufferPool;
using storage::Page;

namespace {

// Conservative fanout. 64 keys * ~38 B (leaf) ≈ 2.4 KB → safely fits in a
// 4 KB page even after a split's worst-case move.
constexpr std::uint16_t MAX_KEYS = 64;

struct PathEntry {
    PageId pageId;
    Page*  page;
    Node   node;        // view onto page->data()
};

// Pins a page through the BufferPool and returns a Node view.
Status fetchNode(BufferPool* bp, PageId pid, Page*& outPage, Node& outNode) {
    Status s = bp->fetchPage(pid, outPage);
    if (s != Status::OK) return s;
    outNode.buf = outPage->data();
    outNode.end = outPage->data() + Page::SIZE;
    return Status::OK;
}

// Initialises a freshly-allocated page as an empty node of the requested kind.
void initNode(Node& n, bool asLeaf, PageId parent) {
    n.setLeaf(asLeaf);
    n.setNumKeys(0);
    n.setParent(parent);
    n.setPrevLeaf(INVALID_PAGE_ID);
    n.setNextLeaf(INVALID_PAGE_ID);
}

// Walks to the start of entry i's *key bytes* in a leaf node. The byte
// before the returned pointer holds the keyLen of entry i.
std::uint8_t* leafEntryPtr(Node& n, std::uint16_t i) {
    std::uint8_t* p = n.buf + 16;        // OFF_DATA_BASE
    for (std::uint16_t k = 0; k < i; ++k) {
        std::uint16_t kLen;
        std::memcpy(&kLen, p, sizeof(kLen));
        p += 2u + kLen + 8u;
    }
    return p;
}

// Internal-node layout (after the 16-byte header):
//
//     [child0:4][keyLen0:2][key0][child1:4][keyLen1:2][key1][child2:4] ...
//
// With N keys there are N+1 children. child_0 is stored first; child_{i>=1}
// is stored immediately AFTER key_{i-1}. key_i is stored immediately AFTER
// child_i. This helper returns a pointer to the 4 bytes storing child_i.
std::uint8_t* childPtr(Node& n, std::uint16_t i) {
    if (i == 0) return n.buf + 16;             // OFF_DATA_BASE
    std::uint8_t* p = n.buf + 16 + 4u;         // past child0, at keyLen_0
    for (std::uint16_t k = 0; k + 1 < i; ++k) {
        std::uint16_t kLen;
        std::memcpy(&kLen, p, sizeof(kLen));
        p += 2u + kLen + 4u;                   // skip key_k and child_{k+1}
    }
    // p now points at keyLen_{i-1}; skip it and key_{i-1} to reach child_i.
    std::uint16_t kLen;
    std::memcpy(&kLen, p, sizeof(kLen));
    p += 2u + kLen;
    return p;
}

// Total bytes consumed by all leaf entries.
std::size_t leafDataUsed(const Node& n) {
    std::size_t total = 0;
    std::uint16_t nk = n.numKeys();
    const std::uint8_t* p = n.buf + 16;
    for (std::uint16_t k = 0; k < nk; ++k) {
        std::uint16_t kLen;
        std::memcpy(&kLen, p, sizeof(kLen));
        total += 2u + kLen + 8u;
        p += 2u + kLen + 8u;
    }
    return total;
}
std::size_t intDataUsed(const Node& n) {
    std::size_t total = 4u;              // child0
    std::uint16_t nk = n.numKeys();
    const std::uint8_t* p = n.buf + 16 + 4;
    for (std::uint16_t k = 0; k < nk; ++k) {
        std::uint16_t kLen;
        std::memcpy(&kLen, p, sizeof(kLen));
        total += 2u + kLen + 4u;
        p += 2u + kLen + 4u;
    }
    return total;
}

// Insert a (key, rid) entry at position `i` in a leaf page. Caller must
// ensure the page has room and that i is in [0, numKeys()].
void leafInsert(Node& n, std::uint16_t i, std::span<const std::uint8_t> key,
                RecordId rid) {
    std::uint8_t* dst = leafEntryPtr(n, i);
    std::uint16_t oldNum = n.numKeys();
    std::size_t suffixBytes = 0;
    if (i < oldNum) {
        const std::uint8_t* end = leafEntryPtr(n, oldNum);
        suffixBytes = static_cast<std::size_t>(end - dst);
    }
    if (suffixBytes > 0) {
        std::memmove(dst + 2u + key.size() + 8u, dst, suffixBytes);
    }
    std::uint16_t kLen = static_cast<std::uint16_t>(key.size());
    std::memcpy(dst, &kLen, sizeof(kLen));
    if (!key.empty()) std::memcpy(dst + 2, key.data(), key.size());
    std::uint64_t r = rid;
    std::memcpy(dst + 2 + key.size(), &r, sizeof(r));
    n.setNumKeys(static_cast<std::uint16_t>(oldNum + 1));
}

// Remove the entry at position `i` from a leaf page.
void leafErase(Node& n, std::uint16_t i) {
    std::uint16_t oldNum = n.numKeys();
    if (i >= oldNum) return;
    std::uint8_t* dst = leafEntryPtr(n, i);
    std::uint16_t kLen;
    std::memcpy(&kLen, dst, sizeof(kLen));
    std::size_t entrySize = 2u + kLen + 8u;
    std::uint8_t* next = dst + entrySize;
    const std::uint8_t* end = leafEntryPtr(n, oldNum);
    if (next < end) {
        std::memmove(dst, next, static_cast<std::size_t>(end - next));
    }
    n.setNumKeys(static_cast<std::uint16_t>(oldNum - 1));
}

// Internal node: insert separator `key` so that it becomes key_i, and
// `child` so that it becomes child_{i+1}. The existing child_i (already
// stored) stays at child slot i. Around index i the layout goes from
//
//     ... [child_i] [old_key_i] [old_child_{i+1}] ...
// to
//     ... [child_i] [new_key_i] [new_child_{i+1}] [old_key_i] [old_child_{i+1}] ...
//
// We insert the bytes [kLen:2][key][child:4] at the position immediately
// after child_i (which is where old_key_i currently lives, or the end of the
// data when i == oldNum), shifting the suffix right. This single rule covers
// prepend (i == 0), append (i == oldNum), and every middle index uniformly.
//
// Precondition: when oldNum == 0, child_0 must already have been written by
// the caller (via setChild(0, ...)) — this matches every existing call site.
void intInsert(Node& n, std::uint16_t i, PageId child,
               std::span<const std::uint8_t> key) {
    std::uint16_t oldNum = n.numKeys();
    std::uint16_t kLen = static_cast<std::uint16_t>(key.size());
    std::uint8_t* ins    = childPtr(n, i) + 4u;            // old key_i slot / end
    std::uint8_t* endPtr = n.buf + 16u + intDataUsed(n);   // end of used data
    std::size_t suffix = static_cast<std::size_t>(endPtr - ins);
    if (suffix > 0) {
        std::memmove(ins + 2u + kLen + 4u, ins, suffix);
    }
    std::memcpy(ins, &kLen, sizeof(kLen));
    if (kLen > 0) std::memcpy(ins + 2u, key.data(), kLen);
    std::memcpy(ins + 2u + kLen, &child, sizeof(child));
    n.setNumKeys(static_cast<std::uint16_t>(oldNum + 1));
}

// Binary search: returns the largest i such that node.key(i) < `key`, plus 1.
std::int32_t lowerBound(const Node& n, std::span<const std::uint8_t> key) {
    std::uint16_t lo = 0, hi = n.numKeys();
    while (lo < hi) {
        std::uint16_t mid = static_cast<std::uint16_t>((lo + hi) / 2);
        auto k = n.key(mid);
        int cmp = std::memcmp(k.data(), key.data(),
                              std::min<std::size_t>(k.size(), key.size()));
        if (cmp == 0) {
            if (k.size() < key.size()) cmp = -1;
            else if (k.size() > key.size()) cmp = 1;
        }
        if (cmp < 0) {
            lo = static_cast<std::uint16_t>(mid + 1);
        } else {
            hi = mid;
        }
    }
    return static_cast<std::int32_t>(lo);
}

// Unpins every entry on the path. Used by error paths.
void unpinPath(BufferPool* bp, std::vector<PathEntry>& path, bool dirty) {
    for (auto& e : path) (void)bp->unpinPage(e.pageId, dirty);
    path.clear();
}

// Allocates a fresh page through the BufferPool, fetches it (the BufferPool
// reads it zeroed from disk — DiskManager::allocatePage extends the file with
// a zero page, and readPage zeroes any short read), and initialises it as an
// empty node of the requested kind. The returned page is PINNED; the caller
// must unpin it via the returned PageId. On failure no page is left pinned.
// The Page* is intentionally not returned: the pin is tracked by PageId in the
// BufferPool, and the Node view holds the buffer pointer the caller needs.
Status allocateAndInit(BufferPool* bp, bool asLeaf, PageId parent,
                       PageId& outId, Node& outNode) {
    PageId pid = bp->allocatePage();
    if (pid == INVALID_PAGE_ID) return Status::IO_ERROR;
    Page* pg = nullptr;
    Status s = bp->fetchPage(pid, pg);
    if (s != Status::OK) return s;
    outId = pid;
    outNode.buf = pg->data();
    outNode.end = pg->data() + Page::SIZE;
    initNode(outNode, asLeaf, parent);
    return Status::OK;
}

// Best-effort: repoint a child node's parent pointer. The child is fetched,
// updated, and unpinned dirty. Failures are swallowed (matching the leaf-chain
// fixup style) — a stale parent pointer never corrupts search/insert/remove,
// which descend from the root using child pointers, not parent pointers.
void repointParent(BufferPool* bp, PageId childId, PageId newParent) {
    if (childId == INVALID_PAGE_ID) return;
    Page* pg = nullptr;
    if (bp->fetchPage(childId, pg) != Status::OK) return;
    Node n{};
    n.buf = pg->data();
    n.end = pg->data() + Page::SIZE;
    n.setParent(newParent);
    (void)bp->unpinPage(childId, true);
}

// Split an overflowed LEAF node. `leaf` (page id `leafId`) is rewritten in
// place to hold the LEFT half (entries[0..mid)); a freshly allocated right
// leaf (returned pinned) holds entries[mid..end). The leaf doubly-linked
// list is rewired around the new leaf. The separator — the first key of the
// right half, which also stays in the right half per textbook convention —
// is copied to outSepKey for propagation to the parent. `insKey`/`insRid` is
// the entry that triggered the split, to be inserted at position `idx` in
// the materialised array. `leafParentId` becomes the parent of the new leaf.
Status splitLeaf(BufferPool* bp, Node& leaf, PageId leafId, std::uint16_t idx,
                 std::span<const std::uint8_t> insKey, RecordId insRid,
                 PageId leafParentId,
                 std::vector<std::uint8_t>& outSepKey,
                 PageId& outRightId, Node& outRightNode) {
    struct Entry { std::vector<std::uint8_t> k; RecordId r; };
    std::vector<Entry> all;
    all.reserve(leaf.numKeys() + 1);
    for (std::uint16_t i = 0; i < leaf.numKeys(); ++i) {
        auto k = leaf.key(i);
        Entry e;
        e.k.assign(k.begin(), k.end());
        e.r = leaf.rid(i);
        all.push_back(std::move(e));
    }
    Entry ins;
    ins.k.assign(insKey.begin(), insKey.end());
    ins.r = insRid;
    all.insert(all.begin() + idx, std::move(ins));

    std::size_t mid = all.size() / 2;
    if (mid == 0) mid = 1;        // invariant: at least one key per side

    // Rewrite the left half back into the original leaf.
    std::memset(leaf.buf + 16, 0, Page::SIZE - 16);
    leaf.setNumKeys(0);
    for (std::uint16_t i = 0; i < static_cast<std::uint16_t>(mid); ++i) {
        leafInsert(leaf, i,
            std::span<const std::uint8_t>(all[i].k.data(), all[i].k.size()),
            all[i].r);
    }

    // Allocate and write the right leaf.
    PageId rightId = INVALID_PAGE_ID;
    Node   rightNode{};
    Status s = allocateAndInit(bp, /*asLeaf=*/true, leafParentId,
                               rightId, rightNode);
    if (s != Status::OK) return s;
    for (std::uint16_t i = static_cast<std::uint16_t>(mid); i < all.size(); ++i) {
        leafInsert(rightNode, static_cast<std::uint16_t>(i - mid),
            std::span<const std::uint8_t>(all[i].k.data(), all[i].k.size()),
            all[i].r);
    }

    // Fix the leaf doubly-linked list.
    PageId oldNext = leaf.nextLeaf();
    rightNode.setPrevLeaf(leafId);
    rightNode.setNextLeaf(oldNext);
    leaf.setNextLeaf(rightId);
    if (oldNext != INVALID_PAGE_ID) {
        Page* np = nullptr;
        if (bp->fetchPage(oldNext, np) == Status::OK) {
            Node nn{};
            nn.buf = np->data();
            nn.end = np->data() + Page::SIZE;
            nn.setPrevLeaf(rightId);
            (void)bp->unpinPage(oldNext, true);
        }
    }

    outSepKey    = all[mid].k;
    outRightId   = rightId;
    outRightNode = rightNode;
    return Status::OK;
}

// Split an INTERNAL node that would overflow if (pendingKey, pendingChild)
// were inserted at index `pIdx`. The node currently has K keys / K+1 children
// and is full but still valid. We materialise its keys and children, insert
// the pending (key, child) into the materialised arrays at `pIdx` (key at
// keys[pIdx], child at children[pIdx+1]), then split the combined K+1 keys /
// K+2 children: left (original page, rewritten) gets keys[0..mid),
// children[0..mid+1); right (new internal node) gets keys[mid+1..K+1),
// children[mid+1..K+2); the middle separator keys[mid] is NOT kept in either
// child — it is copied to outSepKey and must be pushed up to the grandparent.
//
// The pending entry is NEVER written into the original page, so the page
// buffer is not overrun even when the entry would not fit. `nodeParentId` is
// the parent of the node (the grandparent); the new right node's parent is
// set to it. Children that move to the right node are reparented; children
// that stay in the left node keep pointing at it (its page id is unchanged).
//
// Overflow only ever happens with many keys (each internal entry is ~10 B for
// a 4-byte key, so >~408 entries before exceeding a 4 KB page), so mid is
// comfortably >= 1 and the right half has >= 1 key in practice.
Status splitInternal(BufferPool* bp, Node& node, PageId nodeParentId,
                     std::uint16_t pIdx,
                     std::span<const std::uint8_t> pendingKey, PageId pendingChild,
                     std::vector<std::uint8_t>& outSepKey,
                     PageId& outRightId, Node& outRightNode) {
    std::uint16_t K = node.numKeys();
    std::vector<std::vector<std::uint8_t>> keys;
    std::vector<PageId> children;
    keys.reserve(static_cast<std::size_t>(K) + 1);
    children.reserve(static_cast<std::size_t>(K) + 2);
    children.push_back(node.child(0));
    for (std::uint16_t i = 0; i < K; ++i) {
        auto k = node.key(i);
        keys.emplace_back(k.begin(), k.end());
        children.push_back(node.child(static_cast<std::uint16_t>(i + 1)));
    }
    // Fold the pending entry into the materialised arrays (NOT into the page).
    keys.insert(keys.begin() + pIdx,
                std::vector<std::uint8_t>(pendingKey.begin(), pendingKey.end()));
    children.insert(children.begin() + pIdx + 1, pendingChild);

    std::uint16_t total = static_cast<std::uint16_t>(keys.size());   // K+1
    std::uint16_t mid = static_cast<std::uint16_t>(total / 2);
    if (mid == 0) mid = 1;

    // Allocate the right internal node (same parent as the overflowed node).
    PageId rightId = INVALID_PAGE_ID;
    Node   rightNode{};
    Status s = allocateAndInit(bp, /*asLeaf=*/false, nodeParentId,
                               rightId, rightNode);
    if (s != Status::OK) return s;

    // Rewrite the LEFT (original) node: keys[0..mid), children[0..mid+1).
    std::memset(node.buf + 16, 0, Page::SIZE - 16);
    node.setNumKeys(0);
    node.setChild(0, children[0]);
    for (std::uint16_t i = 0; i < mid; ++i) {
        intInsert(node, i, children[static_cast<std::size_t>(i) + 1],
                  std::span<const std::uint8_t>(keys[i].data(), keys[i].size()));
    }

    // Write the RIGHT node: keys[mid+1..total), children[mid+1..total+1).
    rightNode.setChild(0, children[static_cast<std::size_t>(mid) + 1]);
    for (std::uint16_t i = static_cast<std::uint16_t>(mid) + 1; i < total; ++i) {
        std::uint16_t pos = static_cast<std::uint16_t>(i - (mid + 1));
        intInsert(rightNode, pos, children[static_cast<std::size_t>(i) + 1],
                  std::span<const std::uint8_t>(keys[i].data(), keys[i].size()));
    }

    // Repoint the right node's children to their new parent.
    for (std::uint16_t i = static_cast<std::uint16_t>(mid) + 1; i <= total; ++i) {
        repointParent(bp, children[i], rightId);
    }

    outSepKey    = keys[mid];
    outRightId   = rightId;
    outRightNode = rightNode;
    return Status::OK;
}

} // namespace

// =============================================================================
// BPlusTree
// =============================================================================

BPlusTree::BPlusTree(BufferPool* bp, PageId rootPageId)
    : bp_(bp), root_(rootPageId) {}

BPlusTree::~BPlusTree() = default;

// Descend from `start` (the root) to the leaf that should hold `key`. The
// returned `path` is the chain of pinned pages, leaf at the back.
static Status descend(BufferPool* bp, PageId start, const Key& key,
                      std::vector<PathEntry>& path) {
    PageId cur = start;
    while (true) {
        PathEntry pe{};
        Status s = fetchNode(bp, cur, pe.page, pe.node);
        if (s != Status::OK) {
            unpinPath(bp, path, false);
            return s;
        }
        pe.pageId = cur;   // record the page id so unpinPath / split fixups
                           // can unpin and repoint the RIGHT page (the node
                           // view alone is not enough — the BufferPool keys
                           // pins by PageId).
        bool leaf = pe.node.isLeaf();
        path.push_back(pe);
        if (leaf) return Status::OK;
        std::int32_t idx = lowerBound(pe.node,
            std::span<const std::uint8_t>(
                reinterpret_cast<const std::uint8_t*>(key.data()), key.size()));
        PageId child = pe.node.child(static_cast<std::uint16_t>(idx));
        if (child == INVALID_PAGE_ID) {
            unpinPath(bp, path, false);
            return Status::IO_ERROR;
        }
        cur = child;
    }
}

// Search ---------------------------------------------------------------------

Status BPlusTree::search(const Key& key, RecordId& outRid) {
    outRid = INVALID_RID;
    if (root_ == INVALID_PAGE_ID) return Status::NOT_FOUND;

    std::vector<PathEntry> path;
    Status s = descend(bp_, root_, key, path);
    if (s != Status::OK) return s;

    Node& leaf = path.back().node;
    std::int32_t idx = lowerBound(leaf,
        std::span<const std::uint8_t>(
            reinterpret_cast<const std::uint8_t*>(key.data()), key.size()));
    Status result = Status::NOT_FOUND;
    if (idx >= 0 && idx < static_cast<std::int32_t>(leaf.numKeys())) {
        auto k = leaf.key(static_cast<std::uint16_t>(idx));
        if (k.size() == key.size() &&
            (key.empty() || std::memcmp(k.data(), key.data(), k.size()) == 0)) {
            outRid = leaf.rid(static_cast<std::uint16_t>(idx));
            result = Status::OK;
        }
    }
    unpinPath(bp_, path, false);
    return result;
}

// Insert ---------------------------------------------------------------------

Status BPlusTree::insert(const Key& key, RecordId rid) {
    if (root_ == INVALID_PAGE_ID) return Status::INVALID_ARGUMENT;
    std::span<const std::uint8_t> kBytes(
        reinterpret_cast<const std::uint8_t*>(key.data()), key.size());

    std::vector<PathEntry> path;
    Status s = descend(bp_, root_, key, path);
    if (s != Status::OK) return s;

    Node& leaf = path.back().node;
    // Duplicate check: B+ tree keys are unique.
    std::int32_t idx = lowerBound(leaf, kBytes);
    if (idx < static_cast<std::int32_t>(leaf.numKeys())) {
        auto k = leaf.key(static_cast<std::uint16_t>(idx));
        if (k.size() == key.size() &&
            (key.empty() || std::memcmp(k.data(), key.data(), k.size()) == 0)) {
            unpinPath(bp_, path, false);
            return Status::DUPLICATE_KEY;
        }
    }

    // Fast path: enough room in the leaf for one more entry.
    std::size_t need = 2u + key.size() + 8u;
    std::size_t used = leafDataUsed(leaf);
    if (used + need <= Page::SIZE - 16) {
        leafInsert(leaf, static_cast<std::uint16_t>(idx), kBytes, rid);
        unpinPath(bp_, path, true);
        return Status::OK;
    }

    // Slow path: split the leaf, then propagate the separator up through
    // internal nodes, splitting any internal node that overflows. The tree
    // may therefore grow in height by one when the root itself overflows.
    PageId leafId       = path.back().pageId;
    PageId leafParentId  = (path.size() >= 2) ? path[path.size() - 2].pageId
                                              : INVALID_PAGE_ID;

    std::vector<std::uint8_t> sepKey;
    PageId  rightId   = INVALID_PAGE_ID;
    Node    rightNode{};
    Status ss = splitLeaf(bp_, leaf, leafId, static_cast<std::uint16_t>(idx),
                          kBytes, rid, leafParentId,
                          sepKey, rightId, rightNode);
    if (ss != Status::OK) {
        // splitLeaf could not allocate the right leaf — genuine allocation
        // failure. The original leaf is still pinned (and dirty, since
        // splitLeaf rewrote its left half); unpin everything dirty.
        unpinPath(bp_, path, true);
        return ss;
    }
    // rightNode is pinned (the new right leaf); leaf (path.back()) is pinned.

    // Case 1: the root was a leaf. Build a new internal root holding the
    // old leaf (child0) and the new right leaf (child1), separated by sepKey.
    if (path.size() == 1) {
        PageId newRootId = INVALID_PAGE_ID;
        Node   newRoot{};
        Status rs = allocateAndInit(bp_, /*asLeaf=*/false, INVALID_PAGE_ID,
                                     newRootId, newRoot);
        if (rs != Status::OK) {
            (void)bp_->unpinPage(rightId, true);
            unpinPath(bp_, path, true);
            return rs;
        }
        newRoot.setChild(0, leafId);
        intInsert(newRoot, 0, rightId,
            std::span<const std::uint8_t>(sepKey.data(), sepKey.size()));
        leaf.setParent(newRootId);
        rightNode.setParent(newRootId);

        (void)bp_->unpinPage(rightId, true);
        (void)bp_->unpinPage(newRootId, true);
        root_ = newRootId;
        unpinPath(bp_, path, true);     // unpins the old leaf (dirty)
        return Status::OK;
    }

    // Case 2: the root was internal. Wire the new right leaf into its parent,
    // then walk up splitting internal nodes as long as they overflow.
    rightNode.setParent(path[path.size() - 2].pageId);
    (void)bp_->unpinPage(rightId, true);           // release the right leaf
    (void)bp_->unpinPage(leafId, true);            // leaf modified → dirty
    path.pop_back();                                // path.back() is now the parent

    PageId newChildId = rightId;
    while (true) {
        Node&    parent    = path.back().node;
        PageId   parentId  = path.back().pageId;
        std::int32_t pIdx  = lowerBound(parent,
            std::span<const std::uint8_t>(sepKey.data(), sepKey.size()));

        // Check BEFORE writing: would inserting (sepKey, newChildId) overflow
        // the parent? Each internal entry contributes (2 + keyLen + 4) bytes.
        std::size_t wouldBeUsed = intDataUsed(parent) + 2u + sepKey.size() + 4u;
        if (wouldBeUsed <= Page::SIZE - 16) {
            // Fits — commit the insert and we are done.
            intInsert(parent, static_cast<std::uint16_t>(pIdx), newChildId,
                std::span<const std::uint8_t>(sepKey.data(), sepKey.size()));
            unpinPath(bp_, path, true);
            return Status::OK;
        }

        // Would overflow: split the parent, folding the pending (sepKey,
        // newChildId) into the split WITHOUT writing it into the page, and
        // push the middle separator up to the grandparent.
        PageId grandParentId = (path.size() >= 2) ? path[path.size() - 2].pageId
                                                   : INVALID_PAGE_ID;
        std::vector<std::uint8_t> upSep;
        PageId  rightIntId   = INVALID_PAGE_ID;
        Node    rightIntNode{};
        Status sp = splitInternal(bp_, parent, grandParentId,
                                  static_cast<std::uint16_t>(pIdx),
                                  std::span<const std::uint8_t>(sepKey.data(), sepKey.size()),
                                  newChildId, upSep, rightIntId, rightIntNode);
        if (sp != Status::OK) {
            // Allocation failure on the new internal node. The parent is
            // still consistent (we never wrote the pending entry); bail dirty.
            unpinPath(bp_, path, true);
            return sp;
        }
        // rightIntNode is pinned; parent (path.back()) is the LEFT node.

        if (path.size() == 1) {
            // The parent was the root and overflowed: grow the tree by one
            // level with a fresh internal root.
            PageId newRootId = INVALID_PAGE_ID;
            Node   newRoot{};
            Status rs = allocateAndInit(bp_, /*asLeaf=*/false, INVALID_PAGE_ID,
                                         newRootId, newRoot);
            if (rs != Status::OK) {
                (void)bp_->unpinPage(rightIntId, true);
                unpinPath(bp_, path, true);
                return rs;
            }
            newRoot.setChild(0, parentId);     // left = original (now-left) node
            intInsert(newRoot, 0, rightIntId,
                std::span<const std::uint8_t>(upSep.data(), upSep.size()));
            parent.setParent(newRootId);
            rightIntNode.setParent(newRootId);

            (void)bp_->unpinPage(rightIntId, true);
            (void)bp_->unpinPage(newRootId, true);
            root_ = newRootId;
            unpinPath(bp_, path, true);          // unpins parent (left, dirty)
            return Status::OK;
        }

        // Continue propagating upSep / rightIntNode to the grandparent.
        // Unpin the split parent (left node, dirty) and the new right node.
        (void)bp_->unpinPage(parentId, true);
        (void)bp_->unpinPage(rightIntId, true);
        path.pop_back();                         // path.back() is now grandparent
        newChildId = rightIntId;
        sepKey.swap(upSep);
    }
}

// Remove ---------------------------------------------------------------------

Status BPlusTree::remove(const Key& key) {
    if (root_ == INVALID_PAGE_ID) return Status::NOT_FOUND;
    std::span<const std::uint8_t> kBytes(
        reinterpret_cast<const std::uint8_t*>(key.data()), key.size());

    std::vector<PathEntry> path;
    Status s = descend(bp_, root_, key, path);
    if (s != Status::OK) return s;

    Node& leaf = path.back().node;
    std::int32_t idx = lowerBound(leaf, kBytes);
    if (idx >= static_cast<std::int32_t>(leaf.numKeys())) {
        unpinPath(bp_, path, false);
        return Status::NOT_FOUND;
    }
    auto k = leaf.key(static_cast<std::uint16_t>(idx));
    if (k.size() != key.size() ||
        (!key.empty() && std::memcmp(k.data(), key.data(), k.size()) != 0)) {
        unpinPath(bp_, path, false);
        return Status::NOT_FOUND;
    }
    leafErase(leaf, static_cast<std::uint16_t>(idx));
    // No rebalancing in v1 — pages are allowed to underutilise space.
    unpinPath(bp_, path, true);
    return Status::OK;
}

// Range scan -----------------------------------------------------------------

Status BPlusTree::rangeScan(const Key& lo, const Key& hi,
                            std::vector<RecordId>& out) {
    out.clear();
    if (root_ == INVALID_PAGE_ID) return Status::OK;
    std::span<const std::uint8_t> loBytes(
        reinterpret_cast<const std::uint8_t*>(lo.data()), lo.size());
    std::span<const std::uint8_t> hiBytes(
        reinterpret_cast<const std::uint8_t*>(hi.data()), hi.size());

    std::vector<PathEntry> path;
    Status s = descend(bp_, root_, lo, path);
    if (s != Status::OK) return s;

    // Unpin ancestors; we only need the leaf pinned (and we re-pin each
    // next leaf as we walk).
    for (std::size_t i = 0; i + 1 < path.size(); ++i) {
        (void)bp_->unpinPage(path[i].pageId, false);
    }
    PageId leafId = path.back().pageId;
    PageId curPage = leafId;
    bool leafStillPinned = true;

    while (curPage != INVALID_PAGE_ID) {
        Page* pg = nullptr;
        s = bp_->fetchPage(curPage, pg);
        if (s != Status::OK) {
            if (leafStillPinned) (void)bp_->unpinPage(leafId, false);
            return s;
        }
        Node n{};
        n.buf = pg->data();
        n.end = pg->data() + Page::SIZE;

        std::uint16_t start = 0;
        if (curPage == leafId) {
            std::int32_t lb = lowerBound(n, loBytes);
            start = static_cast<std::uint16_t>(lb);
        }
        for (std::uint16_t i = start; i < n.numKeys(); ++i) {
            auto k = n.key(i);
            int cmp = 0;
            if (k.size() != hi.size()) {
                cmp = (k.size() < hi.size()) ? -1 : 1;
            } else if (!k.empty()) {
                cmp = std::memcmp(k.data(), hi.data(), k.size());
            }
            if (cmp > 0) {
                (void)bp_->unpinPage(curPage, false);
                if (leafStillPinned && curPage != leafId) {
                    (void)bp_->unpinPage(leafId, false);
                }
                return Status::OK;
            }
            out.push_back(n.rid(i));
        }
        PageId next = n.nextLeaf();
        (void)bp_->unpinPage(curPage, false);
        if (leafStillPinned && curPage == leafId) leafStillPinned = false;
        curPage = next;
    }
    if (leafStillPinned) (void)bp_->unpinPage(leafId, false);
    return Status::OK;
}

} // namespace minidb::index