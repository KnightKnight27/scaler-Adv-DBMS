#ifndef EXECUTORS_H
#define EXECUTORS_H

#include "execution/abstract_executor.h"
#include "storage/buffer_pool_manager.h"
#include "storage/slotted_page.h"
#include "index/b_plus_tree.h"
#include "concurrency/lock_manager.h"

#include <vector>
#include <string>
#include <memory>
#include <unordered_map>
#include <map>
#include <algorithm>
#include <iostream>
#include <fstream>

namespace minidb {

// Helper to convert Value variant to double for calculations
inline double GetAsDouble(const Value& val) {
    if (auto* i = std::get_if<int>(&val)) return static_cast<double>(*i);
    if (auto* d = std::get_if<double>(&val)) return *d;
    if (auto* s = std::get_if<std::string>(&val)) {
        try { return std::stod(*s); } catch (...) {}
    }
    return 0.0;
}

// Helper to compare two Values (handles mixed numeric types)
inline bool ValueLessThan(const Value& lhs, const Value& rhs) {
    if (std::holds_alternative<std::string>(lhs) || std::holds_alternative<std::string>(rhs)) {
        std::string s1 = std::holds_alternative<std::string>(lhs) ? std::get<std::string>(lhs) : std::to_string(GetAsDouble(lhs));
        std::string s2 = std::holds_alternative<std::string>(rhs) ? std::get<std::string>(rhs) : std::to_string(GetAsDouble(rhs));
        return s1 < s2;
    }
    return GetAsDouble(lhs) < GetAsDouble(rhs);
}

// Helper to add two Values (keeps ints as int, otherwise double)
inline Value ValueAdd(const Value& lhs, const Value& rhs) {
    if (std::holds_alternative<double>(lhs) || std::holds_alternative<double>(rhs)) {
        return GetAsDouble(lhs) + GetAsDouble(rhs);
    }
    int i1 = std::holds_alternative<int>(lhs) ? std::get<int>(lhs) : static_cast<int>(std::get<double>(lhs));
    int i2 = std::holds_alternative<int>(rhs) ? std::get<int>(rhs) : static_cast<int>(std::get<double>(rhs));
    return i1 + i2;
}

struct ValueHasher {
    std::size_t operator()(const Value& val) const {
        if (auto* i = std::get_if<int>(&val)) {
            return std::hash<double>{}(static_cast<double>(*i));
        }
        if (auto* d = std::get_if<double>(&val)) {
            return std::hash<double>{}(*d);
        }
        if (auto* s = std::get_if<std::string>(&val)) {
            return std::hash<std::string>{}(*s);
        }
        return 0;
    }
};

struct ValueEqual {
    bool operator()(const Value& lhs, const Value& rhs) const {
        if (std::holds_alternative<std::string>(lhs) && std::holds_alternative<std::string>(rhs)) {
            return std::get<std::string>(lhs) == std::get<std::string>(rhs);
        }
        if (std::holds_alternative<std::string>(lhs) || std::holds_alternative<std::string>(rhs)) {
            return false;
        }
        return GetAsDouble(lhs) == GetAsDouble(rhs);
    }
};

// Helper to merge two tuples
inline Tuple CombineTuples(const Tuple* left, const Tuple* right) {
    Row new_row;
    for (const auto& [k, v] : left->GetRow().cols) {
        new_row.cols[k] = v;
    }
    for (const auto& [k, v] : right->GetRow().cols) {
        new_row.cols[k] = v;
    }
    return Tuple(new_row);
}

// ─────────────────────────────────────────────────────────────────────────────
// 1. Sequential Scan Executor
// ─────────────────────────────────────────────────────────────────────────────
class SeqScanExecutor : public AbstractExecutor {
public:
    SeqScanExecutor(BufferPoolManager* bpm, std::shared_ptr<TableMetadata> table, Schema schema,
                    Transaction* txn = nullptr, LockManager* lock_mgr = nullptr)
        : bpm_(bpm), table_(table), schema_(std::move(schema)), txn_(txn), lock_mgr_(lock_mgr) {}

    void Init() override {
        page_idx_ = 0;
        slot_idx_ = 0;
    }

    bool Next(Tuple* tuple, RID* rid) override {
        while (page_idx_ < table_->pages.size()) {
            page_id_t pid = table_->pages[page_idx_];
            Page* page = bpm_->FetchPage(pid);
            if (!page) {
                page_idx_++;
                slot_idx_ = 0;
                continue;
            }
            page->RLock();
            uint16_t slot_count = SlottedPage::GetSlotCount(page->GetData());
            while (slot_idx_ < slot_count) {
                std::string data;
                if (SlottedPage::GetTuple(page->GetData(), slot_idx_, data)) {
                    Row r = Row::Deserialize(data);
                    *tuple = Tuple(r);
                    *rid = RID(pid, slot_idx_);
                    if (lock_mgr_ && txn_) {
                        if (!lock_mgr_->LockShared(txn_, *rid)) {
                            page->RUnlock();
                            bpm_->UnpinPage(pid, false);
                            throw std::runtime_error("Transaction Aborted due to lock acquisition failure");
                        }
                    }
                    slot_idx_++;
                    page->RUnlock();
                    bpm_->UnpinPage(pid, false);
                    return true;
                }
                slot_idx_++;
            }
            page->RUnlock();
            bpm_->UnpinPage(pid, false);
            page_idx_++;
            slot_idx_ = 0;
        }
        return false;
    }

    void Close() override {}

    const Schema* GetOutputSchema() const override { return &schema_; }

private:
    BufferPoolManager* bpm_;
    std::shared_ptr<TableMetadata> table_;
    Schema schema_;
    size_t page_idx_{0};
    uint16_t slot_idx_{0};
    Transaction* txn_;
    LockManager* lock_mgr_;
};

// ─────────────────────────────────────────────────────────────────────────────
// 2. Index Scan Executor
// ─────────────────────────────────────────────────────────────────────────────
class IndexScanExecutor : public AbstractExecutor {
public:
    IndexScanExecutor(BufferPoolManager* bpm, std::shared_ptr<TableMetadata> table,
                      std::shared_ptr<BPlusTree<int, RID, IntComparator>> index,
                      int search_key, Schema schema,
                      Transaction* txn = nullptr, LockManager* lock_mgr = nullptr)
        : bpm_(bpm), table_(table), index_(index), search_key_(search_key), schema_(std::move(schema)),
          txn_(txn), lock_mgr_(lock_mgr) {}

    void Init() override {
        rids_.clear();
        index_->Find(search_key_, &rids_);
        rid_idx_ = 0;
    }

    bool Next(Tuple* tuple, RID* rid) override {
        while (rid_idx_ < rids_.size()) {
            RID curr_rid = rids_[rid_idx_];
            Page* page = bpm_->FetchPage(curr_rid.GetPageId());
            if (!page) {
                rid_idx_++;
                continue;
            }
            page->RLock();
            std::string data;
            if (SlottedPage::GetTuple(page->GetData(), curr_rid.GetSlotNum(), data)) {
                *tuple = Tuple(Row::Deserialize(data));
                *rid = curr_rid;
                if (lock_mgr_ && txn_) {
                    if (!lock_mgr_->LockShared(txn_, *rid)) {
                        page->RUnlock();
                        bpm_->UnpinPage(curr_rid.GetPageId(), false);
                        throw std::runtime_error("Transaction Aborted due to lock acquisition failure");
                    }
                }
                rid_idx_++;
                page->RUnlock();
                bpm_->UnpinPage(curr_rid.GetPageId(), false);
                return true;
            }
            page->RUnlock();
            bpm_->UnpinPage(curr_rid.GetPageId(), false);
            rid_idx_++;
        }
        return false;
    }

    void Close() override {}

    const Schema* GetOutputSchema() const override { return &schema_; }

private:
    BufferPoolManager* bpm_;
    std::shared_ptr<TableMetadata> table_;
    std::shared_ptr<BPlusTree<int, RID, IntComparator>> index_;
    int search_key_;
    Schema schema_;
    std::vector<RID> rids_;
    size_t rid_idx_{0};
    Transaction* txn_;
    LockManager* lock_mgr_;
};

// ─────────────────────────────────────────────────────────────────────────────
// 3. Filter Executor
// ─────────────────────────────────────────────────────────────────────────────
class FilterExecutor : public AbstractExecutor {
public:
    FilterExecutor(std::unique_ptr<AbstractExecutor> child, Expression predicate)
        : child_(std::move(child)), predicate_(std::move(predicate)) {}

    void Init() override {
        child_->Init();
    }

    bool Next(Tuple* tuple, RID* rid) override {
        while (child_->Next(tuple, rid)) {
            Value res = predicate_.Evaluate(tuple);
            bool is_true = false;
            if (auto* i = std::get_if<int>(&res)) {
                is_true = (*i != 0);
            } else if (auto* d = std::get_if<double>(&res)) {
                is_true = (*d != 0.0);
            }
            if (is_true) {
                return true;
            }
        }
        return false;
    }

    void Close() override {
        child_->Close();
    }

    const Schema* GetOutputSchema() const override { return child_->GetOutputSchema(); }

private:
    std::unique_ptr<AbstractExecutor> child_;
    Expression predicate_;
};

// ─────────────────────────────────────────────────────────────────────────────
// 4. Nested-Loop Join Executor
// ─────────────────────────────────────────────────────────────────────────────
class NestedLoopJoinExecutor : public AbstractExecutor {
public:
    NestedLoopJoinExecutor(std::unique_ptr<AbstractExecutor> left_child,
                           std::unique_ptr<AbstractExecutor> right_child,
                           Expression join_predicate,
                           Schema output_schema)
        : left_child_(std::move(left_child)), right_child_(std::move(right_child)),
          join_predicate_(std::move(join_predicate)), output_schema_(std::move(output_schema)) {}

    void Init() override {
        left_child_->Init();
        right_child_->Init();
        has_outer_tuple_ = false;
    }

    bool Next(Tuple* tuple, RID* rid) override {
        while (true) {
            if (!has_outer_tuple_) {
                if (!left_child_->Next(&outer_tuple_, &outer_rid_)) {
                    return false;
                }
                has_outer_tuple_ = true;
                right_child_->Init();
            }

            Tuple inner_tuple;
            RID inner_rid;
            if (right_child_->Next(&inner_tuple, &inner_rid)) {
                Tuple combined = CombineTuples(&outer_tuple_, &inner_tuple);
                Value res = join_predicate_.Evaluate(&combined);
                bool matches = false;
                if (auto* i = std::get_if<int>(&res)) {
                    matches = (*i != 0);
                } else if (auto* d = std::get_if<double>(&res)) {
                    matches = (*d != 0.0);
                }
                if (matches) {
                    *tuple = combined;
                    *rid = outer_rid_;
                    return true;
                }
            } else {
                has_outer_tuple_ = false;
            }
        }
    }

    void Close() override {
        left_child_->Close();
        right_child_->Close();
    }

    const Schema* GetOutputSchema() const override { return &output_schema_; }

private:
    std::unique_ptr<AbstractExecutor> left_child_;
    std::unique_ptr<AbstractExecutor> right_child_;
    Expression join_predicate_;
    Schema output_schema_;
    Tuple outer_tuple_;
    RID outer_rid_;
    bool has_outer_tuple_{false};
};

// ─────────────────────────────────────────────────────────────────────────────
// 5. In-Memory Hash Join Executor
// ─────────────────────────────────────────────────────────────────────────────
class HashJoinExecutor : public AbstractExecutor {
public:
    HashJoinExecutor(std::unique_ptr<AbstractExecutor> left_child,
                     std::unique_ptr<AbstractExecutor> right_child,
                     Expression left_join_key,
                     Expression right_join_key,
                     Schema output_schema)
        : left_child_(std::move(left_child)), right_child_(std::move(right_child)),
          left_join_key_(std::move(left_join_key)), right_join_key_(std::move(right_join_key)),
          output_schema_(std::move(output_schema)) {}

    void Init() override {
        left_child_->Init();
        right_child_->Init();
        hash_table_.clear();

        // Build Phase: Materialize inner (right) relation into hash map
        Tuple inner_tuple;
        RID inner_rid;
        while (right_child_->Next(&inner_tuple, &inner_rid)) {
            Value key = right_join_key_.Evaluate(&inner_tuple);
            hash_table_[key].push_back(inner_tuple);
        }

        probe_bucket_idx_ = 0;
        has_active_probe_ = false;
    }

    bool Next(Tuple* tuple, RID* rid) override {
        while (true) {
            if (!has_active_probe_) {
                RID outer_rid;
                if (!left_child_->Next(&active_outer_tuple_, &outer_rid)) {
                    return false;
                }
                Value key = left_join_key_.Evaluate(&active_outer_tuple_);
                auto it = hash_table_.find(key);
                if (it != hash_table_.end()) {
                    active_matching_bucket_ = it->second;
                    probe_bucket_idx_ = 0;
                    has_active_probe_ = true;
                    active_outer_rid_ = outer_rid;
                }
            } else {
                if (probe_bucket_idx_ < active_matching_bucket_.size()) {
                    Tuple combined = CombineTuples(&active_outer_tuple_, &active_matching_bucket_[probe_bucket_idx_]);
                    *tuple = combined;
                    *rid = active_outer_rid_;
                    probe_bucket_idx_++;
                    return true;
                } else {
                    has_active_probe_ = false;
                }
            }
        }
    }

    void Close() override {
        left_child_->Close();
        right_child_->Close();
    }

    const Schema* GetOutputSchema() const override { return &output_schema_; }

private:
    std::unique_ptr<AbstractExecutor> left_child_;
    std::unique_ptr<AbstractExecutor> right_child_;
    Expression left_join_key_;
    Expression right_join_key_;
    Schema output_schema_;

    std::unordered_map<Value, std::vector<Tuple>, ValueHasher, ValueEqual> hash_table_;
    Tuple active_outer_tuple_;
    RID active_outer_rid_;
    std::vector<Tuple> active_matching_bucket_;
    size_t probe_bucket_idx_{0};
    bool has_active_probe_{false};
};

// ─────────────────────────────────────────────────────────────────────────────
// 5B. Grace Hash Join Executor
// ─────────────────────────────────────────────────────────────────────────────
class GraceHashJoinExecutor : public AbstractExecutor {
public:
    GraceHashJoinExecutor(std::unique_ptr<AbstractExecutor> left_child,
                          std::unique_ptr<AbstractExecutor> right_child,
                          Expression left_join_key,
                          Expression right_join_key,
                          Schema output_schema,
                          size_t num_partitions = 4)
        : left_child_(std::move(left_child)), right_child_(std::move(right_child)),
          left_join_key_(std::move(left_join_key)), right_join_key_(std::move(right_join_key)),
          output_schema_(std::move(output_schema)), num_partitions_(num_partitions) {}

    ~GraceHashJoinExecutor() override {
        GraceHashJoinExecutor::Close();
    }

    void Init() override {
        left_child_->Init();
        right_child_->Init();
        ClosePartitionStreams();
        CleanupFiles();

        // 1. Partition Phase
        // Open partition output streams
        std::vector<std::unique_ptr<std::ofstream>> left_outs(num_partitions_);
        std::vector<std::unique_ptr<std::ofstream>> right_outs(num_partitions_);
        for (size_t i = 0; i < num_partitions_; ++i) {
            left_outs[i] = std::make_unique<std::ofstream>(GetPartitionFileName(true, i), std::ios::binary);
            right_outs[i] = std::make_unique<std::ofstream>(GetPartitionFileName(false, i), std::ios::binary);
        }

        // Partition Right child (inner)
        Tuple inner_tuple;
        RID inner_rid;
        while (right_child_->Next(&inner_tuple, &inner_rid)) {
            Value key = right_join_key_.Evaluate(&inner_tuple);
            size_t p = ValueHasher{}(key) % num_partitions_;
            SerializeTuple(*right_outs[p], inner_tuple, inner_rid);
        }

        // Partition Left child (outer)
        Tuple outer_tuple;
        RID outer_rid;
        while (left_child_->Next(&outer_tuple, &outer_rid)) {
            Value key = left_join_key_.Evaluate(&outer_tuple);
            size_t p = ValueHasher{}(key) % num_partitions_;
            SerializeTuple(*left_outs[p], outer_tuple, outer_rid);
        }

        // Close all partition output streams
        for (size_t i = 0; i < num_partitions_; ++i) {
            left_outs[i]->close();
            right_outs[i]->close();
        }

        // 2. Setup Probe Phase
        current_partition_idx_ = 0;
        hash_table_loaded_ = false;
        has_active_probe_ = false;
    }

    bool Next(Tuple* tuple, RID* rid) override {
        while (true) {
            if (!hash_table_loaded_) {
                if (current_partition_idx_ >= num_partitions_) {
                    return false;
                }

                // Load inner relation partition into memory hash table
                partition_hash_table_.clear();
                std::ifstream right_in(GetPartitionFileName(false, current_partition_idx_), std::ios::binary);
                if (right_in.is_open()) {
                    Tuple t;
                    RID r;
                    while (DeserializeTuple(right_in, &t, &r)) {
                        Value key = right_join_key_.Evaluate(&t);
                        partition_hash_table_[key].push_back(t);
                    }
                    right_in.close();
                }

                // Open left partition for streaming
                if (current_left_partition_file_.is_open()) {
                    current_left_partition_file_.close();
                }
                current_left_partition_file_.open(GetPartitionFileName(true, current_partition_idx_), std::ios::binary);
                hash_table_loaded_ = true;
                has_active_probe_ = false;
            }

            if (has_active_probe_) {
                if (probe_bucket_idx_ < active_matching_bucket_.size()) {
                    *tuple = CombineTuples(&active_outer_tuple_, &active_matching_bucket_[probe_bucket_idx_]);
                    *rid = active_outer_rid_;
                    probe_bucket_idx_++;
                    return true;
                } else {
                    has_active_probe_ = false;
                }
            }

            // Read next outer tuple from active left partition
            if (current_left_partition_file_.is_open()) {
                if (DeserializeTuple(current_left_partition_file_, &active_outer_tuple_, &active_outer_rid_)) {
                    Value key = left_join_key_.Evaluate(&active_outer_tuple_);
                    auto it = partition_hash_table_.find(key);
                    if (it != partition_hash_table_.end()) {
                        active_matching_bucket_ = it->second;
                        probe_bucket_idx_ = 0;
                        has_active_probe_ = true;
                    }
                } else {
                    // Left partition exhausted, advance to next partition index
                    current_left_partition_file_.close();
                    partition_hash_table_.clear();
                    current_partition_idx_++;
                    hash_table_loaded_ = false;
                }
            } else {
                current_partition_idx_++;
                hash_table_loaded_ = false;
            }
        }
    }

    void Close() override {
        left_child_->Close();
        right_child_->Close();
        ClosePartitionStreams();
        CleanupFiles();
    }

    const Schema* GetOutputSchema() const override { return &output_schema_; }

private:
    std::string GetPartitionFileName(bool is_left, size_t part_idx) {
        return "temp_ghj_" + std::string(is_left ? "L_" : "R_") + std::to_string(part_idx) + ".dat";
    }

    void SerializeTuple(std::ostream& os, const Tuple& tuple, const RID& rid) {
        std::string s = tuple.GetRow().Serialize();
        uint32_t len = s.length();
        os.write(reinterpret_cast<const char*>(&len), sizeof(len));
        os.write(s.data(), len);
        page_id_t pid = rid.GetPageId();
        uint32_t slot = rid.GetSlotNum();
        os.write(reinterpret_cast<const char*>(&pid), sizeof(pid));
        os.write(reinterpret_cast<const char*>(&slot), sizeof(slot));
    }

    bool DeserializeTuple(std::istream& is, Tuple* tuple, RID* rid) {
        uint32_t len = 0;
        if (!is.read(reinterpret_cast<char*>(&len), sizeof(len))) {
            return false;
        }
        std::string s(len, '\0');
        is.read(&s[0], len);
        page_id_t pid;
        uint32_t slot;
        is.read(reinterpret_cast<char*>(&pid), sizeof(pid));
        is.read(reinterpret_cast<char*>(&slot), sizeof(slot));
        *tuple = Tuple(Row::Deserialize(s));
        *rid = RID(pid, slot);
        return true;
    }

    void ClosePartitionStreams() {
        if (current_left_partition_file_.is_open()) {
            current_left_partition_file_.close();
        }
    }

    void CleanupFiles() {
        for (size_t i = 0; i < num_partitions_; ++i) {
            std::string lf = GetPartitionFileName(true, i);
            std::string rf = GetPartitionFileName(false, i);
            std::remove(lf.c_str());
            std::remove(rf.c_str());
        }
    }

    std::unique_ptr<AbstractExecutor> left_child_;
    std::unique_ptr<AbstractExecutor> right_child_;
    Expression left_join_key_;
    Expression right_join_key_;
    Schema output_schema_;

    size_t num_partitions_;
    size_t current_partition_idx_{0};
    bool hash_table_loaded_{false};

    std::ifstream current_left_partition_file_;
    std::unordered_map<Value, std::vector<Tuple>, ValueHasher, ValueEqual> partition_hash_table_;

    Tuple active_outer_tuple_;
    RID active_outer_rid_;
    std::vector<Tuple> active_matching_bucket_;
    size_t probe_bucket_idx_{0};
    bool has_active_probe_{false};
};

// ─────────────────────────────────────────────────────────────────────────────
// 6. Aggregation Executor
// ─────────────────────────────────────────────────────────────────────────────
enum class AggregationType { MIN, MAX, SUM, COUNT, AVG };

class AggregationExecutor : public AbstractExecutor {
public:
    AggregationExecutor(std::unique_ptr<AbstractExecutor> child,
                        std::vector<std::string> group_by_cols,
                        std::vector<std::string> agg_cols,
                        std::vector<AggregationType> agg_types,
                        Schema output_schema)
        : child_(std::move(child)), group_by_cols_(std::move(group_by_cols)),
          agg_cols_(std::move(agg_cols)), agg_types_(std::move(agg_types)),
          output_schema_(std::move(output_schema)) {}

    void Init() override {
        child_->Init();
        agg_map_.clear();

        Tuple child_tuple;
        RID child_rid;
        while (child_->Next(&child_tuple, &child_rid)) {
            // Construct Group By Key
            std::vector<Value> group_key;
            for (const auto& col : group_by_cols_) {
                group_key.push_back(child_tuple.GetValue(col));
            }

            // Fetch running aggregation info or initialize
            auto& entries = agg_map_[group_key];
            if (entries.empty()) {
                entries.resize(agg_cols_.size(), {Value(0), 0});
                for (size_t i = 0; i < agg_cols_.size(); ++i) {
                    Value col_val = child_tuple.GetValue(agg_cols_[i]);
                    if (agg_types_[i] == AggregationType::COUNT) {
                        entries[i] = {Value(1), 1};
                    } else {
                        entries[i] = {col_val, 1};
                    }
                }
            } else {
                for (size_t i = 0; i < agg_cols_.size(); ++i) {
                    Value col_val = child_tuple.GetValue(agg_cols_[i]);
                    entries[i].second += 1;
                    if (agg_types_[i] == AggregationType::COUNT) {
                        entries[i].first = ValueAdd(entries[i].first, Value(1));
                    } else if (agg_types_[i] == AggregationType::SUM || agg_types_[i] == AggregationType::AVG) {
                        entries[i].first = ValueAdd(entries[i].first, col_val);
                    } else if (agg_types_[i] == AggregationType::MIN) {
                        if (ValueLessThan(col_val, entries[i].first)) {
                            entries[i].first = col_val;
                        }
                    } else if (agg_types_[i] == AggregationType::MAX) {
                        if (ValueLessThan(entries[i].first, col_val)) {
                            entries[i].first = col_val;
                        }
                    }
                }
            }
        }

        iter_ = agg_map_.begin();
    }

    bool Next(Tuple* tuple, RID* rid) override {
        if (iter_ == agg_map_.end()) {
            return false;
        }

        Row new_row;
        // Group by values
        const auto& group_key = iter_->first;
        for (size_t i = 0; i < group_by_cols_.size(); ++i) {
            new_row.cols[group_by_cols_[i]] = group_key[i];
        }

        // Aggregate values
        const auto& entries = iter_->second;
        for (size_t i = 0; i < agg_cols_.size(); ++i) {
            std::string out_col_name = output_schema_.GetColumns()[group_by_cols_.size() + i].GetName();
            if (agg_types_[i] == AggregationType::AVG) {
                double sum = GetAsDouble(entries[i].first);
                double count = entries[i].second;
                new_row.cols[out_col_name] = count > 0 ? (sum / count) : 0.0;
            } else {
                new_row.cols[out_col_name] = entries[i].first;
            }
        }

        *tuple = Tuple(new_row);
        *rid = RID(INVALID_PAGE_ID, 0); // aggregation has no distinct storage RID
        iter_++;
        return true;
    }

    void Close() override {
        child_->Close();
    }

    const Schema* GetOutputSchema() const override { return &output_schema_; }

private:
    std::unique_ptr<AbstractExecutor> child_;
    std::vector<std::string> group_by_cols_;
    std::vector<std::string> agg_cols_;
    std::vector<AggregationType> agg_types_;
    Schema output_schema_;

    std::map<std::vector<Value>, std::vector<std::pair<Value, int>>> agg_map_;
    std::map<std::vector<Value>, std::vector<std::pair<Value, int>>>::iterator iter_;
};

} // namespace minidb

#endif // EXECUTORS_H
