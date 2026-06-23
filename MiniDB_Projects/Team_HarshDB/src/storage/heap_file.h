#pragma once
// ---------------------------------------------------------------------------
// heap_file.h - an unordered collection of tuples spread across a chain of
// pages, exactly like a PostgreSQL heap. The heap file is what turns "rows" the
// user thinks about into "bytes in slots on pages" that the buffer pool stores.
//
// Every stored tuple carries an MVCC header (xmin, xmax) ahead of the row data:
//   [ int64 xmin ][ int64 xmax ][ column data ... ]
// xmin = the transaction that created the version, xmax = the transaction that
// deleted/superseded it (0 means still live). This is the on-disk foundation for
// both 2PL visibility and the MVCC extension track.
// ---------------------------------------------------------------------------
#include "buffer_pool.h"
#include "page.h"
#include <vector>
#include <cstring>

namespace minidb {

struct StoredTuple {
    Row   row;
    TxId  xmin = INVALID_TX;
    TxId  xmax = INVALID_TX; // 0 == live
    RID   rid;
};

class HeapFile {
public:
    HeapFile(BufferPool* bp, Schema schema, int first_page_id)
        : bp_(bp), schema_(std::move(schema)), first_page_id_(first_page_id) {
        // Walk the chain to find the last page so inserts are O(1) amortised.
        last_page_id_ = first_page_id_;
        Page* p = bp_->fetch_page(last_page_id_);
        while (p->next_page() != INVALID_PAGE_ID) {
            int next = p->next_page();
            bp_->unpin(last_page_id_, false);
            last_page_id_ = next;
            p = bp_->fetch_page(last_page_id_);
        }
        bp_->unpin(last_page_id_, false);
    }

    const Schema& schema() const { return schema_; }
    int first_page_id() const { return first_page_id_; }

    // Create the very first page for a brand new table.
    static int create(BufferPool* bp) {
        int pid;
        Page* p = bp->new_page(pid);
        p->init();
        bp->unpin(pid, true);
        return pid;
    }

    RID insert(const Row& row, TxId xmin) {
        std::vector<char> bytes = serialize(xmin, INVALID_TX, row);

        Page* p = bp_->fetch_page(last_page_id_);
        int slot = p->insert(bytes.data(), (int)bytes.size());
        if (slot >= 0) {
            bp_->unpin(last_page_id_, true);
            return RID{last_page_id_, slot};
        }
        // Current page is full - allocate and link a new one.
        bp_->unpin(last_page_id_, false);
        int new_pid;
        Page* np = bp_->new_page(new_pid);
        np->init();
        slot = np->insert(bytes.data(), (int)bytes.size());
        bp_->unpin(new_pid, true);

        Page* old = bp_->fetch_page(last_page_id_);
        old->set_next_page(new_pid);
        bp_->unpin(last_page_id_, true);

        last_page_id_ = new_pid;
        return RID{new_pid, slot};
    }

    bool get(const RID& rid, StoredTuple& out) {
        Page* p = bp_->fetch_page(rid.page_id);
        const char* ptr; int len;
        bool ok = p->get(rid.slot, ptr, len);
        if (ok) { out = deserialize(ptr, len); out.rid = rid; }
        bp_->unpin(rid.page_id, false);
        return ok;
    }

    // Stamp xmax on an existing version in place (logical delete / supersede).
    void set_xmax(const RID& rid, TxId xmax) {
        Page* p = bp_->fetch_page(rid.page_id);
        const char* ptr; int len;
        if (p->get(rid.slot, ptr, len)) {
            StoredTuple t = deserialize(ptr, len);
            std::vector<char> bytes = serialize(t.xmin, xmax, t.row);
            p->overwrite(rid.slot, bytes.data(), (int)bytes.size());
            bp_->unpin(rid.page_id, true);
        } else {
            bp_->unpin(rid.page_id, false);
        }
    }

    // Physically tombstone a slot (used by recovery / non-MVCC deletes).
    void remove(const RID& rid) {
        Page* p = bp_->fetch_page(rid.page_id);
        p->remove(rid.slot);
        bp_->unpin(rid.page_id, true);
    }

    // Full sequential scan: every live-or-dead version on every page.
    std::vector<StoredTuple> scan() {
        std::vector<StoredTuple> out;
        int pid = first_page_id_;
        while (pid != INVALID_PAGE_ID) {
            Page* p = bp_->fetch_page(pid);
            int n = p->num_slots();
            int next = p->next_page();
            for (int s = 0; s < n; ++s) {
                const char* ptr; int len;
                if (p->get(s, ptr, len)) {
                    StoredTuple t = deserialize(ptr, len);
                    t.rid = RID{pid, s};
                    out.push_back(std::move(t));
                }
            }
            bp_->unpin(pid, false);
            pid = next;
        }
        return out;
    }

    int page_count() {
        int count = 0, pid = first_page_id_;
        while (pid != INVALID_PAGE_ID) {
            Page* p = bp_->fetch_page(pid);
            int next = p->next_page();
            bp_->unpin(pid, false);
            count++; pid = next;
        }
        return count;
    }

private:
    std::vector<char> serialize(TxId xmin, TxId xmax, const Row& row) {
        std::vector<char> b;
        append_u64(b, xmin);
        append_u64(b, xmax);
        for (size_t i = 0; i < schema_.size(); ++i) {
            if (schema_[i].type == Type::INT) {
                append_i64(b, as_int(row[i]));
            } else {
                const std::string& s = as_text(row[i]);
                append_i32(b, (int)s.size());
                b.insert(b.end(), s.begin(), s.end());
            }
        }
        return b;
    }

    StoredTuple deserialize(const char* ptr, int len) {
        StoredTuple t;
        int off = 0;
        t.xmin = read_u64(ptr, off);
        t.xmax = read_u64(ptr, off);
        for (size_t i = 0; i < schema_.size(); ++i) {
            if (schema_[i].type == Type::INT) {
                t.row.push_back((int64_t)read_i64(ptr, off));
            } else {
                int slen = read_i32(ptr, off);
                t.row.push_back(std::string(ptr + off, ptr + off + slen));
                off += slen;
            }
        }
        (void)len;
        return t;
    }

    static void append_u64(std::vector<char>& b, uint64_t v) {
        for (int i = 0; i < 8; ++i) b.push_back((char)((v >> (i*8)) & 0xFF));
    }
    static void append_i64(std::vector<char>& b, int64_t v) { append_u64(b, (uint64_t)v); }
    static void append_i32(std::vector<char>& b, int v) {
        for (int i = 0; i < 4; ++i) b.push_back((char)((v >> (i*8)) & 0xFF));
    }
    static uint64_t read_u64(const char* p, int& off) {
        uint64_t v = 0;
        for (int i = 0; i < 8; ++i) v |= ((uint64_t)(unsigned char)p[off+i]) << (i*8);
        off += 8; return v;
    }
    static int64_t read_i64(const char* p, int& off) { return (int64_t)read_u64(p, off); }
    static int read_i32(const char* p, int& off) {
        int v = 0;
        for (int i = 0; i < 4; ++i) v |= ((int)(unsigned char)p[off+i]) << (i*8);
        off += 4; return v;
    }

    BufferPool* bp_;
    Schema      schema_;
    int         first_page_id_;
    int         last_page_id_;
};

} // namespace minidb
