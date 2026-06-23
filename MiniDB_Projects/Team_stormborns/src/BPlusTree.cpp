#include "BPlusTree.h"

#include <stack>

#include "BPlusInternalPage.h"
#include "BPlusLeafPage.h"
#include "BPlusNode.h"

// ── Constructors ────────────────────────────────────────────────────────

BPlusTree::BPlusTree(BufferPool* pool, int rootPageId)
    : pool_(pool), rootPageId_(rootPageId)
{
}

BPlusTree::BPlusTree(BufferPool* pool)
    : pool_(pool)
{
    // Allocate page 0 for the root
    rootPageId_ = pool_->getDiskManager()->allocatePage();

    // Fetch it, format it as an empty leaf, and unpin it
    Page* rootPage = pool_->fetchPage(rootPageId_);
    BPlusLeafPage initialRoot(rootPage);
    initialRoot.setLeaf(true);
    initialRoot.setNumKeys(0);
    pool_->unpinPage(rootPageId_, true);
}

// ── Search ──────────────────────────────────────────────────────────────

int BPlusTree::search(int32_t searchKey) {
    int currentId = rootPageId_;

    // Walk down internal nodes
    while (true) {
        Page* page = pool_->fetchPage(currentId);
        BPlusNode node(page);

        if (node.isLeaf()) {
            // We've reached a leaf – search it
            BPlusLeafPage leaf(page);
            int numKeys = leaf.getNumKeys();

            for (int i = 0; i < numKeys; i++) {
                if (leaf.getKeyAt(i) == searchKey) {
                    int recordId = leaf.getRecordIdAt(i);
                    pool_->unpinPage(currentId, false);
                    return recordId;
                }
            }

            // Key not found
            pool_->unpinPage(currentId, false);
            return -1;
        }

        // Internal node – route to the correct child
        BPlusInternalPage internal(page);
        int childId = internal.searchChild(searchKey);
        pool_->unpinPage(currentId, false);
        currentId = childId;
    }
}

// ── Insert ──────────────────────────────────────────────────────────────

void BPlusTree::insert(int32_t key, int32_t recordId) {

    // ── Step 1: Traverse and Track ──────────────────────────────────
    std::stack<int> parentPath;
    int currentId = rootPageId_;

    while (true) {
        Page* page = pool_->fetchPage(currentId);
        BPlusNode node(page);

        if (node.isLeaf()) {
            break;  // currentId page is still pinned; we'll use it below
        }

        // Internal node – push onto parent path and descend
        parentPath.push(currentId);
        BPlusInternalPage internal(page);
        int childId = internal.searchChild(key);
        pool_->unpinPage(currentId, false);
        currentId = childId;
    }

    // ── Step 2: Simple Insert ───────────────────────────────────────
    Page* leafPage = pool_->fetchPage(currentId);
    // fetchPage increments pin count again, so unpin the extra pin from
    // the traversal loop. We'll keep the pin from this fetch.
    pool_->unpinPage(currentId, false);

    BPlusLeafPage leaf(leafPage);

    if (leaf.getNumKeys() < BPlusLeafPage::MAX_PAIRS) {
        leaf.insertPair(key, recordId);
        pool_->unpinPage(currentId, true);
        return;
    }

    // ── Step 3: Leaf Split ──────────────────────────────────────────
    int newPageId = pool_->getDiskManager()->allocatePage();
    Page* blankPage = pool_->fetchPage(newPageId);
    BPlusLeafPage newLeaf = leaf.split(blankPage);

    // Insert the new pair into the correct half
    if (key < newLeaf.getKeyAt(0)) {
        leaf.insertPair(key, recordId);
    } else {
        newLeaf.insertPair(key, recordId);
    }

    int32_t pushUpKey = newLeaf.getKeyAt(0);
    int rightNodeId = newPageId;

    pool_->unpinPage(currentId, true);
    pool_->unpinPage(newPageId, true);

    // ── Step 4: Propagate Up (The Stack Loop) ───────────────────────
    while (!parentPath.empty()) {
        int parentId = parentPath.top();
        parentPath.pop();

        Page* parentPage = pool_->fetchPage(parentId);
        BPlusInternalPage internal(parentPage);

        if (internal.getNumKeys() < BPlusInternalPage::MAX_KEYS) {
            // There is room – insert and we're done
            internal.insertEntry(pushUpKey, rightNodeId);
            pool_->unpinPage(parentId, true);
            return;
        }

        // Internal node is full – must split
        int splitIndex = internal.getNumKeys() / 2;
        // The key at splitIndex will be pushed up AFTER the split
        int32_t middleKey = internal.getKeyAt(splitIndex);

        int newInternalPageId = pool_->getDiskManager()->allocatePage();
        Page* newInternalBlankPage = pool_->fetchPage(newInternalPageId);
        BPlusInternalPage newInternal = internal.split(newInternalBlankPage);

        // After split: internal has keys [0..splitIndex-1],
        // newInternal has keys [splitIndex+1..end],
        // and middleKey was removed from both.
        // Now insert pushUpKey into the correct side.
        if (pushUpKey < middleKey) {
            internal.insertEntry(pushUpKey, rightNodeId);
        } else {
            newInternal.insertEntry(pushUpKey, rightNodeId);
        }

        // The middle key becomes the new push-up key
        pushUpKey = middleKey;
        rightNodeId = newInternalPageId;

        pool_->unpinPage(parentId, true);
        pool_->unpinPage(newInternalPageId, true);
    }

    // ── Step 5: Root Split ──────────────────────────────────────────
    // Stack is empty and we still have a pushUpKey → create new root
    int newRootId = pool_->getDiskManager()->allocatePage();
    Page* newRootPage = pool_->fetchPage(newRootId);
    BPlusInternalPage newRoot(newRootPage);
    newRoot.setLeaf(false);
    newRoot.setChildIdAt(0, this->rootPageId_);
    newRoot.setKeyAt(0, pushUpKey);
    newRoot.setChildIdAt(1, rightNodeId);
    newRoot.setNumKeys(1);

    this->rootPageId_ = newRootId;
    pool_->unpinPage(newRootId, true);
}
