#pragma once

#include "common/config.h"
#include "common/types.h"
#include "execution/tuple.h"
#include "execution/catalog.h"
#include "storage/buffer_pool_manager.h"
#include "concurrency/lock_manager.h"
#include "recovery/log_manager.h"
#include "index/b_plus_tree.h"
#include "optimizer/optimizer.h"
#include <memory>
#include <iostream>

namespace minidb {

class AbstractExecutor {
public:
    virtual ~AbstractExecutor() = default;
    virtual void Init() = 0;
    virtual bool Next(Tuple &tuple, RID &rid) = 0;
    virtual const Schema &GetOutputSchema() const = 0;
};

// --- SEQ SCAN EXECUTOR ---
class SeqScanExecutor : public AbstractExecutor {
public:
    SeqScanExecutor(Transaction *txn, TableMetadata *meta, WhereOp op, const std::string &col, const Value &val,
                    BufferPoolManager *bpm, LockManager *lock_mgr)
        : txn_(txn), meta_(meta), op_(op), filter_col_(col), filter_val_(val), bpm_(bpm), lock_mgr_(lock_mgr) {}

    void Init() override {
        curr_page_id_ = meta_->first_page_id;
        curr_slot_id_ = 0;
        curr_page_ = nullptr;
    }

    bool Next(Tuple &tuple, RID &rid) override {
        while (curr_page_id_ != INVALID_PAGE_ID) {
            if (curr_page_ == nullptr) {
                curr_page_ = bpm_->FetchPage(curr_page_id_);
                if (curr_page_ == nullptr) return false;
            }

            SlottedPage slotted(curr_page_);
            uint16_t slot_count = slotted.GetSlotCount();

            while (curr_slot_id_ < slot_count) {
                RID candidate_rid{curr_page_id_, curr_slot_id_};
                
                // Strict 2PL: acquire Shared lock before reading tuple
                if (lock_mgr_) {
                    if (!lock_mgr_->AcquireShared(txn_, candidate_rid)) {
                        throw std::runtime_error("Shared lock acquisition failed for RID " + candidate_rid.ToString());
                    }
                }

                uint16_t size;
                char buf[PAGE_SIZE];
                if (slotted.GetTuple(curr_slot_id_, buf, size)) {
                    std::string serialized(buf, size);
                    Tuple t = Tuple::Deserialize(serialized, meta_->schema);
                    
                    if (EvaluateFilter(t)) {
                        tuple = t;
                        rid = candidate_rid;
                        curr_slot_id_++;
                        return true;
                    }
                }
                curr_slot_id_++;
            }

            // Move to next page (Heap files link pages sequentially)
            // For simple heap files in our MiniDB, let's assume we can increment page_id_t if allocated.
            // Or we can check if the next page exists by seeking. In our DiskManager, we allocate pages sequentially:
            // Page 0, Page 1...
            // Let's assume table pages are contiguous starting from first_page_id to the last page.
            // Let's get next page by checking if FetchPage returns valid, or checking page boundaries.
            // Follow the page chain to avoid scanning index pages
            page_id_t next_page_id = slotted.GetNextPageId();
            bpm_->UnpinPage(curr_page_id_, false);
            curr_page_ = nullptr;
            
            curr_page_id_ = next_page_id;
            curr_slot_id_ = 0;
        }
        return false;
    }

    const Schema &GetOutputSchema() const override { return meta_->schema; }

private:
    bool EvaluateFilter(const Tuple &t) {
        if (op_ == WhereOp::NONE) return true;
        int idx = meta_->schema.GetColIdx(filter_col_);
        if (idx == -1) return true;
        Value col_val = t.GetValues()[idx];
        if (op_ == WhereOp::EQUALS) return col_val == filter_val_;
        if (op_ == WhereOp::GREATER_THAN) return col_val > filter_val_;
        if (op_ == WhereOp::LESS_THAN) return col_val < filter_val_;
        return false;
    }

    Transaction *txn_;
    TableMetadata *meta_;
    WhereOp op_;
    std::string filter_col_;
    Value filter_val_;
    BufferPoolManager *bpm_;
    LockManager *lock_mgr_;

    page_id_t curr_page_id_{INVALID_PAGE_ID};
    slot_id_t curr_slot_id_{0};
    Page *curr_page_{nullptr};
};

// --- INDEX SCAN EXECUTOR ---
class IndexScanExecutor : public AbstractExecutor {
public:
    IndexScanExecutor(Transaction *txn, TableMetadata *meta, const std::string &col, const Value &val,
                      BufferPoolManager *bpm, LockManager *lock_mgr)
        : txn_(txn), meta_(meta), index_col_(col), scan_val_(val), bpm_(bpm), lock_mgr_(lock_mgr) {}

    void Init() override {
        executed_ = false;
    }

    bool Next(Tuple &tuple, RID &rid) override {
        if (executed_) return false;
        executed_ = true;

        BPlusTree tree(meta_->root_page_id, bpm_);
        RID target_rid;
        if (tree.Search(scan_val_.GetInt(), target_rid)) {
            // Strict 2PL: acquire Shared lock before reading tuple
            if (lock_mgr_) {
                if (!lock_mgr_->AcquireShared(txn_, target_rid)) {
                    throw std::runtime_error("Shared lock acquisition failed for RID " + target_rid.ToString());
                }
            }

            Page *page = bpm_->FetchPage(target_rid.page_id);
            if (page == nullptr) return false;

            SlottedPage slotted(page);
            uint16_t size;
            char buf[PAGE_SIZE];
            if (slotted.GetTuple(target_rid.slot_id, buf, size)) {
                std::string serialized(buf, size);
                tuple = Tuple::Deserialize(serialized, meta_->schema);
                rid = target_rid;
                bpm_->UnpinPage(target_rid.page_id, false);
                return true;
            }
            bpm_->UnpinPage(target_rid.page_id, false);
        }

        return false;
    }

    const Schema &GetOutputSchema() const override { return meta_->schema; }

private:
    Transaction *txn_;
    TableMetadata *meta_;
    std::string index_col_;
    Value scan_val_;
    BufferPoolManager *bpm_;
    LockManager *lock_mgr_;
    bool executed_{false};
};

// --- NESTED LOOP JOIN EXECUTOR ---
class NestedLoopJoinExecutor : public AbstractExecutor {
public:
    NestedLoopJoinExecutor(std::unique_ptr<AbstractExecutor> left_child,
                           std::unique_ptr<AbstractExecutor> right_child,
                           const std::string &left_col, const std::string &right_col)
        : left_child_(std::move(left_child)), right_child_(std::move(right_child)),
          left_col_(left_col), right_col_(right_col) {
        // Build combined output schema
        std::vector<Column> cols;
        for (const auto &col : left_child_->GetOutputSchema().GetColumns()) {
            cols.push_back(col);
        }
        for (const auto &col : right_child_->GetOutputSchema().GetColumns()) {
            cols.push_back(col);
        }
        output_schema_ = Schema(cols);
    }

    void Init() override {
        left_child_->Init();
        right_child_->Init();
        has_more_left_ = left_child_->Next(left_tuple_, left_rid_);
    }

    bool Next(Tuple &tuple, RID &rid) override {
        while (has_more_left_) {
            Tuple right_tuple;
            RID right_rid;
            while (right_child_->Next(right_tuple, right_rid)) {
                // Check join condition
                int left_idx = left_child_->GetOutputSchema().GetColIdx(left_col_);
                int right_idx = right_child_->GetOutputSchema().GetColIdx(right_col_);
                
                Value left_val = left_tuple_.GetValue(left_child_->GetOutputSchema(), left_idx);
                Value right_val = right_tuple.GetValue(right_child_->GetOutputSchema(), right_idx);

                if (left_val == right_val) {
                    // Combine tuples
                    std::vector<Value> vals;
                    for (const auto &val : left_tuple_.GetValues()) vals.push_back(val);
                    for (const auto &val : right_tuple.GetValues()) vals.push_back(val);
                    
                    tuple = Tuple(vals);
                    rid = left_rid_; // use left RID as join output RID representation
                    return true;
                }
            }

            // Advance outer table, reset inner table
            right_child_->Init();
            has_more_left_ = left_child_->Next(left_tuple_, left_rid_);
        }
        return false;
    }

    const Schema &GetOutputSchema() const override { return output_schema_; }

private:
    std::unique_ptr<AbstractExecutor> left_child_;
    std::unique_ptr<AbstractExecutor> right_child_;
    std::string left_col_;
    std::string right_col_;
    Schema output_schema_;

    Tuple left_tuple_;
    RID left_rid_;
    bool has_more_left_{false};
};

} // namespace minidb
