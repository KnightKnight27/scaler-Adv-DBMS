#ifndef QUERY_ENGINE_H
#define QUERY_ENGINE_H

#include "common/config.h"
#include "common/rid.h"
#include "storage/buffer_pool_manager.h"
#include "storage/slotted_page.h"
#include "index/b_plus_tree.h"

#include <string>
#include <vector>
#include <unordered_map>
#include <variant>
#include <memory>
#include <sstream>
#include <iostream>

namespace minidb {

/**
 * Standard comparator evaluating ordering operations on integer values.
 */
struct IntComparator {
    bool operator()(const int& lhs, const int& rhs) const {
        return lhs < rhs;
    }
};

using Value = std::variant<int, double, std::string>;

/**
 * Structure mapping columns to typed values, managing serialization structures.
 */
struct Row {
    std::unordered_map<std::string, Value> cols;
    
    // Serializes row into a simple string for storage in SlottedPage
    std::string Serialize() const {
        std::stringstream ss;
        ss << cols.size() << " ";
        for (const auto& [k, v] : cols) {
            ss << k << " ";
            if (auto* i = std::get_if<int>(&v)) {
                ss << "I " << *i << " ";
            } else if (auto* d = std::get_if<double>(&v)) {
                ss << "D " << *d << " ";
            } else if (auto* s = std::get_if<std::string>(&v)) {
                // assume no spaces in string values for simplicity
                ss << "S " << *s << " ";
            }
        }
        return ss.str();
    }

    // Deserializes row from slotted page string
    static Row Deserialize(const std::string& data) {
        Row r;
        std::stringstream ss(data);
        size_t size;
        if (!(ss >> size)) return r;
        for (size_t i = 0; i < size; ++i) {
            std::string col_name, type;
            ss >> col_name >> type;
            if (type == "I") {
                int val;
                ss >> val;
                r.cols[col_name] = val;
            } else if (type == "D") {
                double val;
                ss >> val;
                r.cols[col_name] = val;
            } else if (type == "S") {
                std::string val;
                ss >> val;
                r.cols[col_name] = val;
            }
        }
        return r;
    }
};

/**
 * Maintains basic metadata mappings detailing active table pages and B+ Tree indexes.
 */
struct TableMetadata {
    std::string name;
    std::vector<page_id_t> pages;
    std::unordered_map<std::string, std::shared_ptr<BPlusTree<int, RID, IntComparator>>> indexes;
};

/**
 * Catalog registering active table configurations and indexing structures.
 */
class Catalog {
public:
    void AddTable(const std::string& name) {
        tables_[name] = std::make_shared<TableMetadata>();
        tables_[name]->name = name;
    }

    std::shared_ptr<TableMetadata> GetTable(const std::string& name) {
        auto it = tables_.find(name);
        if (it == tables_.end()) return nullptr;
        return it->second;
    }

    void CreateIndex(const std::string& table_name, const std::string& col_name, BufferPoolManager* bpm) {
        auto table = GetTable(table_name);
        if (!table) return;
        page_id_t root_id = INVALID_PAGE_ID;
        // Allocate a base root page for B+ Tree index
        bpm->NewPage(&root_id);
        
        // Initialize as a leaf page
        Page* page = bpm->FetchPage(root_id);
        page->WLock();
        auto* leaf = reinterpret_cast<BPlusTreeLeafPage<int, RID, IntComparator>*>(page->GetData());
        leaf->Init(INVALID_PAGE_ID, 3);
        page->WUnlock();
        bpm->UnpinPage(root_id, true);

        table->indexes[col_name] = std::make_shared<BPlusTree<int, RID, IntComparator>>(root_id, bpm, IntComparator());
    }

private:
    std::unordered_map<std::string, std::shared_ptr<TableMetadata>> tables_;
};

/**
 * PlanNode representing execution steps.
 */
class PlanNode {
public:
    virtual ~PlanNode() = default;
    virtual std::vector<Row> Execute(BufferPoolManager* bpm, std::shared_ptr<TableMetadata> table) = 0;
    virtual std::string GetPlanName() const = 0;
};

/**
 * Scan plan node scanning tables sequentially.
 */
class TableScanNode : public PlanNode {
public:
    TableScanNode(std::string filter_col, int filter_val)
        : filter_col_(filter_col), filter_val_(filter_val) {}

    std::vector<Row> Execute(BufferPoolManager* bpm, std::shared_ptr<TableMetadata> table) override {
        std::vector<Row> results;
        for (page_id_t pid : table->pages) {
            Page* page = bpm->FetchPage(pid);
            page->RLock();
            
            uint16_t slot_count = SlottedPage::GetSlotCount(page->GetData());
            for (uint16_t i = 0; i < slot_count; ++i) {
                std::string data;
                if (SlottedPage::GetTuple(page->GetData(), i, data)) {
                    Row r = Row::Deserialize(data);
                    auto it = r.cols.find(filter_col_);
                    if (it != r.cols.end()) {
                        if (auto* val = std::get_if<int>(&it->second)) {
                            if (*val == filter_val_) {
                                results.push_back(r);
                            }
                        }
                    }
                }
            }
            page->RUnlock();
            bpm->UnpinPage(pid, false);
        }
        return results;
    }

    std::string GetPlanName() const override { return "TableScan(Filter: " + filter_col_ + " = " + std::to_string(filter_val_) + ")"; }

private:
    std::string filter_col_;
    int filter_val_;
};

/**
 * Plan node scanning entries via registered B+ Tree index structures.
 */
class IndexScanNode : public PlanNode {
public:
    IndexScanNode(std::string col_name, int val, std::shared_ptr<BPlusTree<int, RID, IntComparator>> index)
        : col_name_(col_name), val_(val), index_(index) {}

    std::vector<Row> Execute(BufferPoolManager* bpm, std::shared_ptr<TableMetadata> table) override {
        std::vector<Row> results;
        std::vector<RID> rids;
        
        // Find RIDs in the B+ Tree
        index_->Find(val_, &rids);

        for (const RID& rid : rids) {
            Page* page = bpm->FetchPage(rid.GetPageId());
            page->RLock();
            std::string data;
            if (SlottedPage::GetTuple(page->GetData(), rid.GetSlotNum(), data)) {
                results.push_back(Row::Deserialize(data));
            }
            page->RUnlock();
            bpm->UnpinPage(rid.GetPageId(), false);
        }
        return results;
    }

    std::string GetPlanName() const override { return "IndexScan(Index: " + col_name_ + ", Val: " + std::to_string(val_) + ")"; }

private:
    std::string col_name_;
    int val_;
    std::shared_ptr<BPlusTree<int, RID, IntComparator>> index_;
};

/**
 * Heuristic query optimizer selecting between sequential scans and index query structures.
 */
class Optimizer {
public:
    static std::shared_ptr<PlanNode> Optimize(const std::string& col_name, int filter_val, std::shared_ptr<TableMetadata> table) {
        // Estimate selectivity:
        // For equality filter `col_name = filter_val`, if an index exists, we estimate high selectivity (Index Scan).
        auto it = table->indexes.find(col_name);
        if (it != table->indexes.end()) {
            std::cout << "[OPTIMIZER] Index found on column '" << col_name << "'. Choosing IndexScan (Est. Cost: O(log N))." << std::endl;
            return std::make_shared<IndexScanNode>(col_name, filter_val, it->second);
        } else {
            std::cout << "[OPTIMIZER] No index found on column '" << col_name << "'. Falling back to TableScan (Est. Cost: O(N))." << std::endl;
            return std::make_shared<TableScanNode>(col_name, filter_val);
        }
    }
};

} // namespace minidb

#endif // QUERY_ENGINE_H
