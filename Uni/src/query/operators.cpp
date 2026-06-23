#include "query/operators.h"
#include <iostream>
#include <sstream>
#include <algorithm>

// Helper to split a string by delimiter
static std::vector<std::string> SplitString(const std::string& s, char delim) {
    std::vector<std::string> result;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, delim)) {
        result.push_back(item);
    }
    return result;
}

// -------------------------------------------------------------
// SeqScanExecutor
// -------------------------------------------------------------
SeqScanExecutor::SeqScanExecutor(const std::string& table_name,
                                 const std::vector<std::string>& schema,
                                 PageId_t first_page_id,
                                 BufferPoolManager* bpm,
                                 VisibilityChecker_t visibility_checker)
    : table_name_(table_name), schema_(schema), first_page_id_(first_page_id), bpm_(bpm), visibility_checker_(visibility_checker) {}

void SeqScanExecutor::Init() {
    Close(); // Unpins any page currently pinned
    curr_page_id_ = first_page_id_;
    curr_slot_id_ = 0;
    curr_page_ = nullptr;
}

bool SeqScanExecutor::Next(Tuple* tuple) {
    while (curr_page_id_ != INVALID_PAGE_ID) {
        if (curr_page_ == nullptr) {
            curr_page_ = bpm_->FetchPage(curr_page_id_);
            if (curr_page_ == nullptr) {
                return false;
            }
        }

        PageHeader* header = curr_page_->GetHeader();
        Slot* slots = curr_page_->GetSlots();

        while (curr_slot_id_ < header->slot_count) {
            uint16_t slot_idx = curr_slot_id_++;
            Slot& slot = slots[slot_idx];

            if (slot.offset > 0 && slot.length > 0) { // Valid non-deleted slot
                const char* record_ptr = curr_page_->data + slot.offset;
                const MVCCHeader* mvcc_hdr = reinterpret_cast<const MVCCHeader*>(record_ptr);

                // Apply MVCC visibility checker if provided
                if (visibility_checker_ && !visibility_checker_(mvcc_hdr->xmin, mvcc_hdr->xmax)) {
                    continue; // Skip invisible version
                }

                // Populate tuple metadata
                tuple->xmin = mvcc_hdr->xmin;
                tuple->xmax = mvcc_hdr->xmax;
                tuple->prev_rid = RID{mvcc_hdr->prev_page_id, mvcc_hdr->prev_slot_id};
                tuple->rid = RID{curr_page_id_, slot_idx};

                // Extract payload values
                std::string payload(record_ptr + sizeof(MVCCHeader), slot.length - sizeof(MVCCHeader));
                tuple->values = SplitString(payload, ',');
                return true;
            }
        }

        // Move to the next page in the heap chain
        PageId_t next_page_id = header->next_page_id;
        bpm_->UnpinPage(curr_page_id_, false);
        curr_page_ = nullptr;
        curr_page_id_ = next_page_id;
        curr_slot_id_ = 0;
    }
    return false;
}

void SeqScanExecutor::Close() {
    if (curr_page_ != nullptr) {
        bpm_->UnpinPage(curr_page_id_, false);
        curr_page_ = nullptr;
    }
    curr_page_id_ = INVALID_PAGE_ID;
    curr_slot_id_ = 0;
}

// -------------------------------------------------------------
// IndexScanExecutor
// -------------------------------------------------------------
IndexScanExecutor::IndexScanExecutor(const std::string& table_name,
                                     const std::vector<std::string>& schema,
                                     const std::vector<RID>& rids,
                                     BufferPoolManager* bpm,
                                     VisibilityChecker_t visibility_checker)
    : table_name_(table_name), schema_(schema), rids_(rids), bpm_(bpm), visibility_checker_(visibility_checker) {}

void IndexScanExecutor::Init() {
    curr_idx_ = 0;
}

bool IndexScanExecutor::Next(Tuple* tuple) {
    while (curr_idx_ < rids_.size()) {
        RID rid = rids_[curr_idx_++];
        if (!rid.IsValid()) continue;

        Page* page = bpm_->FetchPage(rid.page_id);
        if (!page) continue;

        Slot* slots = page->GetSlots();
        PageHeader* header = page->GetHeader();

        if (rid.slot_id < header->slot_count) {
            Slot& slot = slots[rid.slot_id];
            if (slot.offset > 0 && slot.length > 0) {
                const char* record_ptr = page->data + slot.offset;
                const MVCCHeader* mvcc_hdr = reinterpret_cast<const MVCCHeader*>(record_ptr);

                if (visibility_checker_ && !visibility_checker_(mvcc_hdr->xmin, mvcc_hdr->xmax)) {
                    bpm_->UnpinPage(rid.page_id, false);
                    continue; // Invisible version
                }

                // Populate tuple
                tuple->xmin = mvcc_hdr->xmin;
                tuple->xmax = mvcc_hdr->xmax;
                tuple->prev_rid = RID{mvcc_hdr->prev_page_id, mvcc_hdr->prev_slot_id};
                tuple->rid = rid;

                std::string payload(record_ptr + sizeof(MVCCHeader), slot.length - sizeof(MVCCHeader));
                tuple->values = SplitString(payload, ',');

                bpm_->UnpinPage(rid.page_id, false);
                return true;
            }
        }
        bpm_->UnpinPage(rid.page_id, false);
    }
    return false;
}

void IndexScanExecutor::Close() {
    curr_idx_ = rids_.size();
}

// -------------------------------------------------------------
// FilterExecutor
// -------------------------------------------------------------
FilterExecutor::FilterExecutor(std::unique_ptr<Operator> child, const SQLWhereCondition& condition)
    : child_(std::move(child)), condition_(condition) {
    // Resolve column index
    const auto& child_schema = child_->GetSchema();
    for (size_t i = 0; i < child_schema.size(); ++i) {
        // Strip table names from column name if format table.col is compared
        std::string schema_col = child_schema[i];
        size_t dot_pos = schema_col.find('.');
        if (dot_pos != std::string::npos) {
            schema_col = schema_col.substr(dot_pos + 1);
        }

        std::string cond_col = condition_.column;
        dot_pos = cond_col.find('.');
        if (dot_pos != std::string::npos) {
            cond_col = cond_col.substr(dot_pos + 1);
        }

        if (schema_col == cond_col) {
            col_idx_ = static_cast<int>(i);
            break;
        }
    }
}

void FilterExecutor::Init() {
    child_->Init();
}

bool FilterExecutor::Evaluate(const Tuple& tuple) {
    if (!condition_.has_condition || col_idx_ == -1) return true;
    if (static_cast<size_t>(col_idx_) >= tuple.values.size()) return false;

    const std::string& val = tuple.values[col_idx_];
    const std::string& target = condition_.value;

    // Check if numeric comparison is applicable
    bool is_numeric = !val.empty() && std::all_of(val.begin(), val.end(), [](char c) { return std::isdigit(c) || c == '-'; }) &&
                     !target.empty() && std::all_of(target.begin(), target.end(), [](char c) { return std::isdigit(c) || c == '-'; });

    if (is_numeric) {
        int val_i = std::stoi(val);
        int target_i = std::stoi(target);
        if (condition_.op == "=") return val_i == target_i;
        if (condition_.op == ">") return val_i > target_i;
        if (condition_.op == "<") return val_i < target_i;
        if (condition_.op == "!=") return val_i != target_i;
    } else {
        if (condition_.op == "=") return val == target;
        if (condition_.op == "!=") return val != target;
    }
    return false;
}

bool FilterExecutor::Next(Tuple* tuple) {
    while (child_->Next(tuple)) {
        if (Evaluate(*tuple)) {
            return true;
        }
    }
    return false;
}

void FilterExecutor::Close() {
    child_->Close();
}

// -------------------------------------------------------------
// ProjectExecutor
// -------------------------------------------------------------
ProjectExecutor::ProjectExecutor(std::unique_ptr<Operator> child, const std::vector<std::string>& projection_fields)
    : child_(std::move(child)), projection_fields_(projection_fields) {
    
    const auto& child_schema = child_->GetSchema();

    // If projection is SELECT *, retain the full child schema
    if (projection_fields_.size() == 1 && projection_fields_[0] == "*") {
        projected_schema_ = child_schema;
        for (size_t i = 0; i < child_schema.size(); ++i) {
            col_indices_.push_back(static_cast<int>(i));
        }
    } else {
        for (const auto& field : projection_fields_) {
            std::string target_field = field;
            size_t dot = target_field.find('.');
            if (dot != std::string::npos) {
                target_field = target_field.substr(dot + 1);
            }

            int found_idx = -1;
            for (size_t i = 0; i < child_schema.size(); ++i) {
                std::string schema_col = child_schema[i];
                size_t dot_s = schema_col.find('.');
                if (dot_s != std::string::npos) {
                    schema_col = schema_col.substr(dot_s + 1);
                }

                if (schema_col == target_field) {
                    found_idx = static_cast<int>(i);
                    break;
                }
            }
            if (found_idx != -1) {
                projected_schema_.push_back(field);
                col_indices_.push_back(found_idx);
            }
        }
    }
}

void ProjectExecutor::Init() {
    child_->Init();
}

bool ProjectExecutor::Next(Tuple* tuple) {
    Tuple child_tuple;
    if (child_->Next(&child_tuple)) {
        tuple->rid = child_tuple.rid;
        tuple->xmin = child_tuple.xmin;
        tuple->xmax = child_tuple.xmax;
        tuple->prev_rid = child_tuple.prev_rid;
        tuple->values.clear();

        for (int idx : col_indices_) {
            tuple->values.push_back(child_tuple.values[idx]);
        }
        return true;
    }
    return false;
}

void ProjectExecutor::Close() {
    child_->Close();
}

// -------------------------------------------------------------
// NestedLoopJoinExecutor
// -------------------------------------------------------------
NestedLoopJoinExecutor::NestedLoopJoinExecutor(std::unique_ptr<Operator> outer,
                                               std::unique_ptr<Operator> inner,
                                               const std::string& outer_join_col,
                                               const std::string& inner_join_col)
    : outer_(std::move(outer)), inner_(std::move(inner)), outer_join_col_(outer_join_col), inner_join_col_(inner_join_col) {
    
    // Build combined schema
    for (const auto& col : outer_->GetSchema()) {
        schema_.push_back(col);
    }
    for (const auto& col : inner_->GetSchema()) {
        schema_.push_back(col);
    }

    // Resolve outer join col index
    const auto& outer_schema = outer_->GetSchema();
    for (size_t i = 0; i < outer_schema.size(); ++i) {
        std::string s_col = outer_schema[i];
        if (s_col == outer_join_col_) {
            outer_col_idx_ = static_cast<int>(i);
            break;
        }
        // Fallback: stripped matching
        size_t dot = s_col.find('.');
        std::string stripped_s = (dot != std::string::npos) ? s_col.substr(dot + 1) : s_col;
        dot = outer_join_col_.find('.');
        std::string stripped_j = (dot != std::string::npos) ? outer_join_col_.substr(dot + 1) : outer_join_col_;
        if (stripped_s == stripped_j) {
            outer_col_idx_ = static_cast<int>(i);
            break;
        }
    }

    // Resolve inner join col index
    const auto& inner_schema = inner_->GetSchema();
    for (size_t i = 0; i < inner_schema.size(); ++i) {
        std::string s_col = inner_schema[i];
        if (s_col == inner_join_col_) {
            inner_col_idx_ = static_cast<int>(i);
            break;
        }
        size_t dot = s_col.find('.');
        std::string stripped_s = (dot != std::string::npos) ? s_col.substr(dot + 1) : s_col;
        dot = inner_join_col_.find('.');
        std::string stripped_j = (dot != std::string::npos) ? inner_join_col_.substr(dot + 1) : inner_join_col_;
        if (stripped_s == stripped_j) {
            inner_col_idx_ = static_cast<int>(i);
            break;
        }
    }
}

void NestedLoopJoinExecutor::Init() {
    outer_->Init();
    inner_->Init();
    has_outer_tuple_ = false;
}

bool NestedLoopJoinExecutor::Next(Tuple* tuple) {
    Tuple inner_tuple;
    while (true) {
        if (!has_outer_tuple_) {
            if (!outer_->Next(&outer_tuple_)) {
                return false; // Outer depleted
            }
            has_outer_tuple_ = true;
            inner_->Init(); // Rewind inner
        }

        while (inner_->Next(&inner_tuple)) {
            if (outer_col_idx_ != -1 && inner_col_idx_ != -1) {
                if (outer_tuple_.values[outer_col_idx_] == inner_tuple.values[inner_col_idx_]) {
                    // Match found! Concatenate values
                    tuple->values.clear();
                    for (const auto& v : outer_tuple_.values) {
                        tuple->values.push_back(v);
                    }
                    for (const auto& v : inner_tuple.values) {
                        tuple->values.push_back(v);
                    }
                    tuple->rid = outer_tuple_.rid;
                    return true;
                }
            }
        }
        // Inner depleted for current outer, advance outer in next loop iteration
        has_outer_tuple_ = false;
    }
}

void NestedLoopJoinExecutor::Close() {
    outer_->Close();
    inner_->Close();
    has_outer_tuple_ = false;
}
