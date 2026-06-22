#include "execution.h"

#include <stdexcept>

// ─── AbstractExecutor::NextBatch (default) ────────────────────────────────────
// Falls back to calling Next() repeatedly.  Subclasses that can do better
// (e.g. BatchSeqScanExecutor) override this.

size_t AbstractExecutor::NextBatch(std::vector<Tuple> *out, size_t batch_size) {
    out->clear();
    Tuple t;
    while (out->size() < batch_size && Next(&t))
        out->push_back(t);
    return out->size();
}

// ─── ValueExecutor ───────────────────────────────────────────────────────────

ValueExecutor::ValueExecutor(std::vector<Tuple> tuples)
    : tuples_(std::move(tuples)) {}

void ValueExecutor::Init() { pos_ = 0; }

bool ValueExecutor::Next(Tuple *out) {
    if (pos_ >= tuples_.size()) return false;
    if (out) *out = tuples_[pos_];
    ++pos_;
    return true;
}

// ─── SeqScanExecutor ─────────────────────────────────────────────────────────
// Pin discipline: each Next() call fetches the current page, reads one or more
// tuples (skipping tombstones and those that fail the predicate), and unpins
// before returning.  Between calls, zero pages are pinned.

SeqScanExecutor::SeqScanExecutor(TableHeap *heap,
                                 std::function<bool(const Tuple &)> pred)
    : heap_(heap), predicate_(std::move(pred)) {}

void SeqScanExecutor::Init() {
    cur_page_id_ = heap_->GetFirstPageId();
    cur_slot_    = 0;
}

bool SeqScanExecutor::Next(Tuple *out) {
    while (cur_page_id_ != INVALID_PAGE_ID) {
        Page *page = heap_->GetBPM()->FetchPage(cur_page_id_);
        if (!page) return false;

        auto     *hp   = reinterpret_cast<HeapPage *>(page->data);
        int32_t   n    = hp->num_tuples;
        page_id_t next = hp->next_page_id;

        // Scan remaining slots in this page, skipping soft-deleted tombstones.
        while (cur_slot_ < n) {
            Tuple t = hp->tuples[cur_slot_++];

            if (t.id != DELETED_TUPLE_ID && (!predicate_ || predicate_(t))) {
                // Unpin before returning; advance to next page if this was the last slot.
                heap_->GetBPM()->UnpinPage(cur_page_id_, false);
                if (cur_slot_ >= n) {
                    cur_page_id_ = next;
                    cur_slot_    = 0;
                }
                if (out) *out = t;
                return true;
            }
        }

        // Page exhausted without a match — move on.
        heap_->GetBPM()->UnpinPage(cur_page_id_, false);
        cur_page_id_ = next;
        cur_slot_    = 0;
    }
    return false;
}

// ─── IndexScanExecutor ───────────────────────────────────────────────────────

IndexScanExecutor::IndexScanExecutor(TableHeap *heap, BPlusTree *index,
                                     int64_t search_key)
    : heap_(heap), index_(index), search_key_(search_key) {}

void IndexScanExecutor::Init() { done_ = false; }

bool IndexScanExecutor::Next(Tuple *out) {
    if (done_) return false;
    done_ = true;

    auto rid_packed = index_->Search(search_key_);
    if (!rid_packed.has_value()) return false;

    RID rid = UnpackRID(rid_packed.value());
    return heap_->GetTuple(rid, out);
}

// ─── InsertExecutor ──────────────────────────────────────────────────────────

InsertExecutor::InsertExecutor(TableHeap *heap, BPlusTree *index,
                               std::unique_ptr<AbstractExecutor> child)
    : heap_(heap), index_(index), child_(std::move(child)) {}

void InsertExecutor::Init() { child_->Init(); }

bool InsertExecutor::Next(Tuple *out) {
    Tuple t;
    if (!child_->Next(&t)) return false;

    RID rid = heap_->InsertTuple(t);
    index_->Insert(t.id, PackRID(rid));

    if (out) *out = t;
    return true;
}

// ─── DeleteExecutor ──────────────────────────────────────────────────────────
// Each Next() call pulls one tuple from the child, looks up its RID via the
// index, soft-deletes the heap slot, and removes the key from the B+ Tree.
// The deleted tuple is emitted so callers can verify what was removed.

DeleteExecutor::DeleteExecutor(TableHeap *heap, BPlusTree *index,
                               std::unique_ptr<AbstractExecutor> child)
    : heap_(heap), index_(index), child_(std::move(child)) {}

void DeleteExecutor::Init() { child_->Init(); }

bool DeleteExecutor::Next(Tuple *out) {
    Tuple t;
    if (!child_->Next(&t)) return false;

    // Locate the physical slot through the index, then erase both records.
    auto rid_packed = index_->Search(t.id);
    if (rid_packed.has_value()) {
        heap_->DeleteTuple(UnpackRID(rid_packed.value()));
        index_->Delete(t.id);
    }

    if (out) *out = t;
    return true;
}

// ─── NestedLoopJoinExecutor ──────────────────────────────────────────────────
// For each left tuple, re-initialize the right side and scan for matches.
// Since SeqScan pins/unpins within Next(), calling right_->Init() while the
// right scan is between calls is always safe (no pages are held pinned).

NestedLoopJoinExecutor::NestedLoopJoinExecutor(
    std::unique_ptr<AbstractExecutor> left,
    std::unique_ptr<AbstractExecutor> right,
    std::function<bool(const Tuple &, const Tuple &)> pred)
    : left_(std::move(left)), right_(std::move(right)),
      predicate_(std::move(pred)) {}

void NestedLoopJoinExecutor::Init() {
    left_->Init();
    left_active_ = false;
    left_done_   = false;
}

bool NestedLoopJoinExecutor::Next(Tuple *out) {
    while (true) {
        // Advance the left side when we need a new left tuple.
        if (!left_active_) {
            if (left_done_) return false;
            if (!left_->Next(&left_tuple_)) {
                left_done_ = true;
                return false;
            }
            right_->Init();   // reset right scan for this left tuple
            left_active_ = true;
        }

        Tuple right_tuple;
        if (right_->Next(&right_tuple)) {
            if (predicate_(left_tuple_, right_tuple)) {
                if (out) {
                    out->id   = left_tuple_.id;
                    out->val1 = left_tuple_.val1;
                    out->val2 = right_tuple.val2;
                }
                return true;
            }
        } else {
            // Right side exhausted for this left tuple.
            left_active_ = false;
        }
    }
}

// ─── BatchSeqScanExecutor ────────────────────────────────────────────────────
// Key optimization: NextBatch() pins each page ONCE per batch rather than once
// per tuple.  With 170 tuples per 4 KB page and batch_size=100, a full scan of
// 100 000 tuples requires ~1 200 FetchPage calls instead of 100 000.

BatchSeqScanExecutor::BatchSeqScanExecutor(
    TableHeap *heap, std::function<bool(const Tuple &)> pred)
    : heap_(heap), predicate_(std::move(pred)) {}

void BatchSeqScanExecutor::Init() {
    cur_page_id_ = heap_->GetFirstPageId();
    cur_slot_    = 0;
    buf_.clear();
    buf_pos_     = 0;
}

size_t BatchSeqScanExecutor::NextBatch(std::vector<Tuple> *out, size_t batch_size) {
    out->clear();

    while (out->size() < batch_size && cur_page_id_ != INVALID_PAGE_ID) {
        Page *page = heap_->GetBPM()->FetchPage(cur_page_id_);
        if (!page) break;

        auto     *hp   = reinterpret_cast<HeapPage *>(page->data);
        int32_t   n    = hp->num_tuples;
        page_id_t next = hp->next_page_id;

        // Drain as many tuples from this page as the batch allows;
        // skip soft-deleted tombstones silently.
        while (cur_slot_ < n && out->size() < batch_size) {
            Tuple t = hp->tuples[cur_slot_++];
            if (t.id != DELETED_TUPLE_ID && (!predicate_ || predicate_(t)))
                out->push_back(t);
        }

        heap_->GetBPM()->UnpinPage(cur_page_id_, false);

        // Advance to the next page only when the current one is fully consumed.
        if (cur_slot_ >= n) {
            cur_page_id_ = next;
            cur_slot_    = 0;
        }
    }

    return out->size();
}

bool BatchSeqScanExecutor::Next(Tuple *out) {
    // Drain the internal buffer before fetching the next batch.
    if (buf_pos_ < buf_.size()) {
        if (out) *out = buf_[buf_pos_];
        ++buf_pos_;
        return true;
    }
    buf_pos_ = 0;
    buf_.clear();
    if (NextBatch(&buf_, 256) == 0) return false;
    if (out) *out = buf_[0];
    buf_pos_ = 1;
    return true;
}
