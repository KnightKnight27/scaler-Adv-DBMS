#pragma once

#include "btree.h"
#include "table.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

// ─── RID packing helpers for B+ Tree storage ─────────────────────────────────

inline int64_t PackRID(RID rid) {
    uint64_t hi = static_cast<uint64_t>(static_cast<uint32_t>(rid.page_id));
    uint64_t lo = static_cast<uint64_t>(static_cast<uint32_t>(rid.slot_num));
    return static_cast<int64_t>((hi << 32) | lo);
}
inline RID UnpackRID(int64_t v) {
    uint64_t uv = static_cast<uint64_t>(v);
    return {static_cast<page_id_t>(static_cast<uint32_t>(uv >> 32)),
            static_cast<int32_t>(static_cast<uint32_t>(uv & 0xFFFFFFFFULL))};
}

// ─── Volcano base ─────────────────────────────────────────────────────────────

class AbstractExecutor {
public:
    virtual ~AbstractExecutor() = default;
    virtual void Init() = 0;
    virtual bool Next(Tuple *out) = 0;

    // Default: call Next() repeatedly to fill out up to batch_size tuples.
    // Overridden by BatchSeqScanExecutor with a page-at-a-time implementation
    // that amortizes BPM pin/unpin overhead across the whole batch.
    virtual size_t NextBatch(std::vector<Tuple> *out, size_t batch_size);
};

// ─── ValueExecutor ───────────────────────────────────────────────────────────
// Emits a fixed list of in-memory tuples.  Used as a source for InsertExecutor.

class ValueExecutor : public AbstractExecutor {
public:
    explicit ValueExecutor(std::vector<Tuple> tuples);
    void Init() override;
    bool Next(Tuple *out) override;

private:
    std::vector<Tuple> tuples_;
    size_t             pos_{0};
};

// ─── SeqScanExecutor ─────────────────────────────────────────────────────────
// Scans all pages of a TableHeap, optionally filtering with a predicate.
// No page is held pinned between two successive Next() calls.

class SeqScanExecutor : public AbstractExecutor {
public:
    explicit SeqScanExecutor(TableHeap *heap,
                             std::function<bool(const Tuple &)> pred = {});
    void Init() override;
    bool Next(Tuple *out) override;

private:
    TableHeap *heap_;
    std::function<bool(const Tuple &)> predicate_;
    page_id_t cur_page_id_{INVALID_PAGE_ID};
    int32_t   cur_slot_{0};
};

// ─── IndexScanExecutor ───────────────────────────────────────────────────────
// Point lookup via B+ Tree; fetches exactly one tuple (or none).

class IndexScanExecutor : public AbstractExecutor {
public:
    IndexScanExecutor(TableHeap *heap, BPlusTree *index, int64_t search_key);
    void Init() override;
    bool Next(Tuple *out) override;

private:
    TableHeap *heap_;
    BPlusTree *index_;
    int64_t    search_key_;
    bool       done_{true};
};

// ─── InsertExecutor ──────────────────────────────────────────────────────────
// Pulls tuples from a child executor, inserts each into the heap and index.
// Next() inserts one tuple per call (Volcano style).

class InsertExecutor : public AbstractExecutor {
public:
    InsertExecutor(TableHeap *heap, BPlusTree *index,
                   std::unique_ptr<AbstractExecutor> child);
    void Init() override;
    bool Next(Tuple *out) override;

private:
    TableHeap *heap_;
    BPlusTree *index_;
    std::unique_ptr<AbstractExecutor> child_;
};

// ─── BatchSeqScanExecutor ────────────────────────────────────────────────────
// Vectorized scan: NextBatch() fetches as many tuples as possible from each
// page before unpinning, reducing BPM lock/unlock operations from O(N) to
// O(N/batch_size).  Next() is implemented by draining an internal buffer
// filled via NextBatch(), so it is fully compatible with the Volcano protocol.

class BatchSeqScanExecutor : public AbstractExecutor {
public:
    explicit BatchSeqScanExecutor(TableHeap *heap,
                                  std::function<bool(const Tuple &)> pred = {});
    void   Init() override;
    bool   Next(Tuple *out) override;
    size_t NextBatch(std::vector<Tuple> *out, size_t batch_size) override;

private:
    TableHeap *heap_;
    std::function<bool(const Tuple &)> predicate_;
    page_id_t cur_page_id_{INVALID_PAGE_ID};
    int32_t   cur_slot_{0};
    // Internal buffer used by Next() to drain batches one tuple at a time.
    std::vector<Tuple> buf_;
    size_t             buf_pos_{0};
};

// ─── DeleteExecutor ──────────────────────────────────────────────────────────
// Pulls tuples from a child executor and removes each from the heap (soft-delete
// via DELETED_TUPLE_ID tombstone) and from the B+ Tree index.
// Next() emits the deleted tuple so callers can verify which rows were removed.

class DeleteExecutor : public AbstractExecutor {
public:
    DeleteExecutor(TableHeap *heap, BPlusTree *index,
                   std::unique_ptr<AbstractExecutor> child);
    void Init() override;
    bool Next(Tuple *out) override;

private:
    TableHeap *heap_;
    BPlusTree *index_;
    std::unique_ptr<AbstractExecutor> child_;
};

// ─── NestedLoopJoinExecutor ──────────────────────────────────────────────────
// For each left tuple, scans the entire right side and emits matching pairs.
// Output tuple: {left.id, left.val1, right.val2}

class NestedLoopJoinExecutor : public AbstractExecutor {
public:
    NestedLoopJoinExecutor(std::unique_ptr<AbstractExecutor> left,
                           std::unique_ptr<AbstractExecutor> right,
                           std::function<bool(const Tuple &, const Tuple &)> pred);
    void Init() override;
    bool Next(Tuple *out) override;

private:
    std::unique_ptr<AbstractExecutor> left_;
    std::unique_ptr<AbstractExecutor> right_;
    std::function<bool(const Tuple &, const Tuple &)> predicate_;
    Tuple left_tuple_;
    bool  left_active_{false};
    bool  left_done_{false};
};
