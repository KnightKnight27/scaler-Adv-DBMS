#include "sql/executor.h"
#include "mvcc/mvcc_manager.h"
#include <iostream>
#include <fstream>

namespace minidb {

void ExecutorContext::SaveCatalog(const std::string &filename) {
    std::ofstream out(filename, std::ios::binary);
    if (!out) return;
    
    size_t num_tables = catalog_.size();
    out.write(reinterpret_cast<const char*>(&num_tables), sizeof(size_t));
    
    for (const auto &[name, tinfo] : catalog_) {
        size_t name_len = name.length();
        out.write(reinterpret_cast<const char*>(&name_len), sizeof(size_t));
        out.write(name.c_str(), name_len);
        
        out.write(reinterpret_cast<const char*>(&tinfo.first_page_id), sizeof(page_id_t));
        out.write(reinterpret_cast<const char*>(&tinfo.last_page_id), sizeof(page_id_t));
        
        page_id_t root_id = tinfo.index ? tinfo.index->GetRootPageId() : INVALID_PAGE_ID;
        out.write(reinterpret_cast<const char*>(&root_id), sizeof(page_id_t));
        
        size_t num_cols = tinfo.schema.columns.size();
        out.write(reinterpret_cast<const char*>(&num_cols), sizeof(size_t));
        
        for (const auto &col : tinfo.schema.columns) {
            size_t cname_len = col.name.length();
            out.write(reinterpret_cast<const char*>(&cname_len), sizeof(size_t));
            out.write(col.name.c_str(), cname_len);
            
            out.write(reinterpret_cast<const char*>(&col.type), sizeof(ColumnType));
            out.write(reinterpret_cast<const char*>(&col.size), sizeof(size_t));
        }
    }
}

void ExecutorContext::LoadCatalog(const std::string &filename) {
    std::ifstream in(filename, std::ios::binary);
    if (!in) return;
    
    size_t num_tables;
    if (!in.read(reinterpret_cast<char*>(&num_tables), sizeof(size_t))) return;
    
    catalog_.clear();
    for (size_t i = 0; i < num_tables; ++i) {
        TableInfo tinfo;
        
        size_t name_len;
        in.read(reinterpret_cast<char*>(&name_len), sizeof(size_t));
        std::string name(name_len, '\0');
        in.read(&name[0], name_len);
        tinfo.name = name;
        
        in.read(reinterpret_cast<char*>(&tinfo.first_page_id), sizeof(page_id_t));
        in.read(reinterpret_cast<char*>(&tinfo.last_page_id), sizeof(page_id_t));
        
        page_id_t root_id;
        in.read(reinterpret_cast<char*>(&root_id), sizeof(page_id_t));
        tinfo.index = new BPlusTree(buffer_pool_, root_id);
        
        size_t num_cols;
        in.read(reinterpret_cast<char*>(&num_cols), sizeof(size_t));
        
        for (size_t j = 0; j < num_cols; ++j) {
            Column col;
            size_t cname_len;
            in.read(reinterpret_cast<char*>(&cname_len), sizeof(size_t));
            std::string cname(cname_len, '\0');
            in.read(&cname[0], cname_len);
            col.name = cname;
            
            in.read(reinterpret_cast<char*>(&col.type), sizeof(ColumnType));
            in.read(reinterpret_cast<char*>(&col.size), sizeof(size_t));
            tinfo.schema.columns.push_back(col);
        }
        
        catalog_[name] = tinfo;
    }
}

SeqScan::SeqScan(ExecutorContext *ctx, const std::string &table_name, const std::string &filter_col, const std::string &filter_op, const std::string &filter_val)
    : ctx_(ctx), table_name_(table_name), filter_col_(filter_col), filter_op_(filter_op), filter_val_(filter_val), current_page_(nullptr) {}

void SeqScan::Open() {
    if (ctx_->catalog_.find(table_name_) == ctx_->catalog_.end()) return;
    current_page_id_ = ctx_->catalog_[table_name_].first_page_id;
    current_slot_id_ = 0;
    if (current_page_id_ != INVALID_PAGE_ID) {
        current_page_ = ctx_->buffer_pool_->FetchPage(current_page_id_);
    }
}

bool SeqScan::Next(Tuple *tuple) {
    if (!current_page_) return false;
    
    TableInfo& tinfo = ctx_->catalog_[table_name_];
    
    while (true) {
        if (current_page_->GetTuple(current_slot_id_, tuple)) {
            current_slot_id_++;
            
            if (ctx_->mvcc_manager_ && ctx_->txn_) {
                if (!ctx_->mvcc_manager_->ReadVisibleVersion(tuple->rid_, ctx_->txn_, tuple)) {
                    continue;
                }
            }
            
            bool match = true;
            if (!filter_col_.empty()) {
                auto values = TupleSerializer::Deserialize(*tuple, tinfo.schema);
                int col_idx = -1;
                for (size_t i = 0; i < tinfo.schema.columns.size(); ++i) {
                    if (tinfo.schema.columns[i].name == filter_col_) {
                        col_idx = i;
                        break;
                    }
                }
                
                if (col_idx != -1) {
                    const std::string& val = values[col_idx];
                    if (tinfo.schema.columns[col_idx].type == ColumnType::INT) {
                        int v1 = std::stoi(val);
                        int v2 = std::stoi(filter_val_);
                        if (filter_op_ == "=") match = (v1 == v2);
                        else if (filter_op_ == ">") match = (v1 > v2);
                        else if (filter_op_ == "<") match = (v1 < v2);
                        else if (filter_op_ == ">=") match = (v1 >= v2);
                        else if (filter_op_ == "<=") match = (v1 <= v2);
                        else if (filter_op_ == "!=") match = (v1 != v2);
                    } else {
                        if (filter_op_ == "=") match = (val == filter_val_);
                        else if (filter_op_ == "!=") match = (val != filter_val_);
                    }
                }
            }
            
            if (match) return true;
        } else {
            page_id_t next_page_id = current_page_->GetNextPageId();
            ctx_->buffer_pool_->UnpinPage(current_page_id_, false);
            current_page_id_ = next_page_id;
            
            if (current_page_id_ == INVALID_PAGE_ID) {
                current_page_ = nullptr;
                return false;
            }
            
            current_page_ = ctx_->buffer_pool_->FetchPage(current_page_id_);
            current_slot_id_ = 0;
            if (!current_page_) return false;
        }
    }
}

void SeqScan::Close() {
    if (current_page_) {
        ctx_->buffer_pool_->UnpinPage(current_page_id_, false);
        current_page_ = nullptr;
    }
}


IndexScan::IndexScan(ExecutorContext *ctx, const std::string &table_name, int32_t key)
    : ctx_(ctx), table_name_(table_name), key_(key), done_(false) {}

void IndexScan::Open() {
    done_ = false;
}

bool IndexScan::Next(Tuple *tuple) {
    if (done_) return false;
    if (ctx_->catalog_.find(table_name_) == ctx_->catalog_.end()) return false;
    
    BPlusTree *index = ctx_->catalog_[table_name_].index;
    if (!index) return false;
    
    RecordId rid;
    if (index->Search(key_, &rid)) {
        Page *p = ctx_->buffer_pool_->FetchPage(rid.page_id);
        if (p) {
            bool found = p->GetTuple(rid.slot_id, tuple);
            ctx_->buffer_pool_->UnpinPage(rid.page_id, false);
            done_ = true;
            return found;
        }
    }
    done_ = true;
    return false;
}

void IndexScan::Close() {}


NestedLoopJoin::NestedLoopJoin(ExecutorContext *ctx, std::unique_ptr<Operator> left, std::unique_ptr<Operator> right,
                               const std::string &left_table, const std::string &right_table,
                               const std::string &left_cond, const std::string &right_cond)
    : ctx_(ctx), left_(std::move(left)), right_(std::move(right)), 
      left_table_(left_table), right_table_(right_table),
      left_cond_(left_cond), right_cond_(right_cond), has_left_(false) {}

void NestedLoopJoin::Open() {
    left_->Open();
    right_->Open();
    has_left_ = left_->Next(&left_tuple_);
}

static std::string extract_col(const std::string &cond) {
    auto pos = cond.find('.');
    if (pos != std::string::npos) return cond.substr(pos + 1);
    return cond;
}

bool NestedLoopJoin::Next(Tuple *tuple) {
    while (has_left_) {
        Tuple right_tuple;
        if (right_->Next(&right_tuple)) {
            auto left_schema = ctx_->catalog_[left_table_].schema;
            auto right_schema = ctx_->catalog_[right_table_].schema;
            
            auto left_vals = TupleSerializer::Deserialize(left_tuple_, left_schema);
            auto right_vals = TupleSerializer::Deserialize(right_tuple, right_schema);
            
            std::string l_col = extract_col(left_cond_);
            std::string r_col = extract_col(right_cond_);
            
            std::string l_val, r_val;
            for (size_t i = 0; i < left_schema.columns.size(); ++i) {
                if (left_schema.columns[i].name == l_col) l_val = left_vals[i];
            }
            for (size_t i = 0; i < right_schema.columns.size(); ++i) {
                if (right_schema.columns[i].name == r_col) r_val = right_vals[i];
            }
            
            if (l_val == r_val) {
                tuple->data_ = left_tuple_.data_;
                if (right_tuple.data_.size() > 8) {
                    tuple->data_.insert(tuple->data_.end(), right_tuple.data_.begin() + 8, right_tuple.data_.end());
                }
                return true;
            }
        } else {
            right_->Close();
            right_->Open();
            has_left_ = left_->Next(&left_tuple_);
        }
    }
    return false;
}

void NestedLoopJoin::Close() {
    left_->Close();
    right_->Close();
}

} // namespace minidb
