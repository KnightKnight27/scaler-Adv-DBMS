#include "index/bplus_tree.h"

#include <algorithm>

#include "storage/page.h"

namespace minidb {

BPlusTree::BPlusTree(BufferPool* bp, PageId root, std::size_t order)
    : bp_(bp), root_(root), order_(order) {}

PageId BPlusTree::create(BufferPool* bp) {
    PageId pid;
    Page* page = bp->new_page(pid);
    BPlusNode(page->data()).init(/*leaf=*/true);
    bp->unpin_page(pid, /*dirty=*/true);
    return pid;
}

bool BPlusTree::insert(BTKey key, RID rid) {
    bool inserted = false;
    Split s = insert_rec(root_, key, rid, inserted);
    if (!inserted) return false;  // duplicate key

    if (s.happened) {
        // Root split: build a new root one level up.
        PageId new_root;
        Page* page = bp_->new_page(new_root);
        BPlusNode node(page->data());
        std::vector<BTKey>  keys{s.sep_key};
        std::vector<PageId> children{root_, s.right};
        node.init(/*leaf=*/false);
        node.store_internal(keys, children);
        bp_->unpin_page(new_root, /*dirty=*/true);
        root_ = new_root;
    }
    return true;
}

BPlusTree::Split BPlusTree::insert_rec(PageId node_pid, BTKey key, RID rid, bool& inserted) {
    Page* page = bp_->fetch_page(node_pid);
    BPlusNode node(page->data());

    if (node.is_leaf()) {
        std::vector<BTKey> keys;
        std::vector<RID>   rids;
        node.load_leaf(keys, rids);

        auto pos = std::lower_bound(keys.begin(), keys.end(), key);
        std::size_t i = static_cast<std::size_t>(pos - keys.begin());
        if (pos != keys.end() && *pos == key) {     // duplicate
            bp_->unpin_page(node_pid, false);
            inserted = false;
            return {};
        }
        keys.insert(keys.begin() + i, key);
        rids.insert(rids.begin() + i, rid);
        inserted = true;

        if (keys.size() <= leaf_max()) {
            node.store_leaf(keys, rids);
            bp_->unpin_page(node_pid, true);
            return {};
        }

        // Split leaf: right half goes to a new leaf; link it into the chain.
        std::size_t mid = keys.size() / 2;
        std::vector<BTKey> rkeys(keys.begin() + mid, keys.end());
        std::vector<RID>   rrids(rids.begin() + mid, rids.end());
        keys.resize(mid);
        rids.resize(mid);

        PageId rpid;
        Page* rpage = bp_->new_page(rpid);
        BPlusNode rnode(rpage->data());
        rnode.init(/*leaf=*/true);
        rnode.set_next_leaf(node.next_leaf());
        rnode.store_leaf(rkeys, rrids);
        bp_->unpin_page(rpid, true);

        node.set_next_leaf(rpid);
        node.store_leaf(keys, rids);
        bp_->unpin_page(node_pid, true);

        return {true, rkeys.front(), rpid};
    }

    // Internal node: pick the child, release this node, then recurse.
    std::vector<BTKey>  keys;
    std::vector<PageId> children;
    node.load_internal(keys, children);
    std::size_t ci = static_cast<std::size_t>(
        std::upper_bound(keys.begin(), keys.end(), key) - keys.begin());
    PageId child = children[ci];
    bp_->unpin_page(node_pid, false);

    Split cs = insert_rec(child, key, rid, inserted);
    if (!cs.happened) return {};

    // Child split: insert its separator and right pointer into this node.
    Page* page2 = bp_->fetch_page(node_pid);
    BPlusNode node2(page2->data());
    node2.load_internal(keys, children);
    keys.insert(keys.begin() + ci, cs.sep_key);
    children.insert(children.begin() + ci + 1, cs.right);

    if (keys.size() <= internal_max()) {
        node2.store_internal(keys, children);
        bp_->unpin_page(node_pid, true);
        return {};
    }

    // Split internal: the middle key moves up (not kept in either half).
    std::size_t mid = keys.size() / 2;
    BTKey sep = keys[mid];
    std::vector<BTKey>  rkeys(keys.begin() + mid + 1, keys.end());
    std::vector<PageId> rchildren(children.begin() + mid + 1, children.end());
    keys.resize(mid);
    children.resize(mid + 1);

    PageId rpid;
    Page* rpage = bp_->new_page(rpid);
    BPlusNode rnode(rpage->data());
    rnode.init(/*leaf=*/false);
    rnode.store_internal(rkeys, rchildren);
    bp_->unpin_page(rpid, true);

    node2.store_internal(keys, children);
    bp_->unpin_page(node_pid, true);

    return {true, sep, rpid};
}

bool BPlusTree::search(BTKey key, RID& out) const {
    PageId pid = root_;
    for (;;) {
        Page* page = bp_->fetch_page(pid);
        BPlusNode node(page->data());
        if (node.is_leaf()) {
            std::vector<BTKey> keys;
            std::vector<RID>   rids;
            node.load_leaf(keys, rids);
            auto pos = std::lower_bound(keys.begin(), keys.end(), key);
            bool found = (pos != keys.end() && *pos == key);
            if (found) out = rids[static_cast<std::size_t>(pos - keys.begin())];
            bp_->unpin_page(pid, false);
            return found;
        }
        std::vector<BTKey>  keys;
        std::vector<PageId> children;
        node.load_internal(keys, children);
        std::size_t ci = static_cast<std::size_t>(
            std::upper_bound(keys.begin(), keys.end(), key) - keys.begin());
        PageId child = children[ci];
        bp_->unpin_page(pid, false);
        pid = child;
    }
}

bool BPlusTree::erase(BTKey key) {
    PageId pid = root_;
    for (;;) {
        Page* page = bp_->fetch_page(pid);
        BPlusNode node(page->data());
        if (node.is_leaf()) {
            std::vector<BTKey> keys;
            std::vector<RID>   rids;
            node.load_leaf(keys, rids);
            auto pos = std::lower_bound(keys.begin(), keys.end(), key);
            if (pos == keys.end() || *pos != key) {
                bp_->unpin_page(pid, false);
                return false;
            }
            std::size_t i = static_cast<std::size_t>(pos - keys.begin());
            keys.erase(keys.begin() + i);
            rids.erase(rids.begin() + i);
            node.store_leaf(keys, rids);
            bp_->unpin_page(pid, true);
            return true;
        }
        std::vector<BTKey>  keys;
        std::vector<PageId> children;
        node.load_internal(keys, children);
        std::size_t ci = static_cast<std::size_t>(
            std::upper_bound(keys.begin(), keys.end(), key) - keys.begin());
        PageId child = children[ci];
        bp_->unpin_page(pid, false);
        pid = child;
    }
}

std::size_t BPlusTree::height() const {
    std::size_t h = 1;
    PageId pid = root_;
    for (;;) {
        Page* page = bp_->fetch_page(pid);
        BPlusNode node(page->data());
        if (node.is_leaf()) {
            bp_->unpin_page(pid, false);
            return h;
        }
        std::vector<BTKey>  keys;
        std::vector<PageId> children;
        node.load_internal(keys, children);
        PageId child = children[0];
        bp_->unpin_page(pid, false);
        pid = child;
        ++h;
    }
}

BPlusTree::RangeIterator BPlusTree::range(BTKey lo, BTKey hi, bool lo_inclusive,
                                          bool hi_inclusive) const {
    // Descend to the leaf that should contain lo.
    PageId pid = root_;
    for (;;) {
        Page* page = bp_->fetch_page(pid);
        BPlusNode node(page->data());
        if (node.is_leaf()) {
            std::vector<BTKey> keys;
            std::vector<RID>   rids;
            node.load_leaf(keys, rids);
            auto pos = lo_inclusive ? std::lower_bound(keys.begin(), keys.end(), lo)
                                    : std::upper_bound(keys.begin(), keys.end(), lo);
            int idx = static_cast<int>(pos - keys.begin());
            bp_->unpin_page(pid, false);
            return RangeIterator(bp_, pid, idx, hi, hi_inclusive);
        }
        std::vector<BTKey>  keys;
        std::vector<PageId> children;
        node.load_internal(keys, children);
        std::size_t ci = static_cast<std::size_t>(
            std::upper_bound(keys.begin(), keys.end(), lo) - keys.begin());
        PageId child = children[ci];
        bp_->unpin_page(pid, false);
        pid = child;
    }
}

bool BPlusTree::RangeIterator::next(BTKey& key, RID& rid) {
    while (leaf_ != INVALID_PAGE_ID) {
        Page* page = bp_->fetch_page(leaf_);
        BPlusNode node(page->data());
        std::vector<BTKey> keys;
        std::vector<RID>   rids;
        node.load_leaf(keys, rids);
        if (idx_ < static_cast<int>(keys.size())) {
            BTKey k = keys[static_cast<std::size_t>(idx_)];
            if (k > hi_ || (k == hi_ && !hi_incl_)) {
                bp_->unpin_page(leaf_, false);
                leaf_ = INVALID_PAGE_ID;
                return false;
            }
            key = k;
            rid = rids[static_cast<std::size_t>(idx_)];
            ++idx_;
            bp_->unpin_page(leaf_, false);
            return true;
        }
        PageId next = node.next_leaf();
        bp_->unpin_page(leaf_, false);
        leaf_ = next;
        idx_ = 0;
    }
    return false;
}

} // namespace minidb
