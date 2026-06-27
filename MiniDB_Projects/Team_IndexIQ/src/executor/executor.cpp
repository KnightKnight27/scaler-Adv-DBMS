#include "executor/executor.h"
#include <stdexcept>
#include <cstring>
#include <iostream>

Executor::Executor(Catalog&                                     catalog,
                   std::unordered_map<std::string, HeapFile*>&  heap_files,
                   std::unordered_map<std::string, BTree*>&     indexes,
                   BufferPool&                                   pool)
    : catalog_(catalog), heap_files_(heap_files), indexes_(indexes), pool_(pool) {}

std::string Executor::eval_value(Expression* expr, const Row& row,
                                  const TableSchema& schema) const {
    if (auto* lit = dynamic_cast<Literal*>(expr)) return lit->value;
    if (auto* col = dynamic_cast<ColumnRef*>(expr)) {
        int idx = schema.col_index(col->col);
        if (idx < 0) throw std::runtime_error("Unknown column: " + col->col);
        return row[idx];
    }
    throw std::runtime_error("Cannot evaluate expression as value");
}

bool Executor::eval_filter(Expression* expr, const Row& row,
                            const TableSchema& schema) const {
    if (!expr) return true;

    auto* bin = dynamic_cast<BinaryExpr*>(expr);
    if (!bin) return true;

    if (bin->op == "AND") return eval_filter(bin->left, row, schema)
                              && eval_filter(bin->right, row, schema);
    if (bin->op == "OR")  return eval_filter(bin->left, row, schema)
                              || eval_filter(bin->right, row, schema);

    std::string lv = eval_value(bin->left,  row, schema);
    std::string rv = eval_value(bin->right, row, schema);

    bool numeric = true;
    int  li = 0, ri = 0;
    try { li = std::stoi(lv); ri = std::stoi(rv); }
    catch (...) { numeric = false; }

    if (bin->op == "=")  return numeric ? li == ri : lv == rv;
    if (bin->op == "!=") return numeric ? li != ri : lv != rv;
    if (bin->op == ">")  return numeric ? li >  ri : lv >  rv;
    if (bin->op == "<")  return numeric ? li <  ri : lv <  rv;
    if (bin->op == ">=") return numeric ? li >= ri : lv >= rv;
    if (bin->op == "<=") return numeric ? li <= ri : lv <= rv;
    return false;
}

Row Executor::project(const Row& row, const std::vector<std::string>& cols,
                       const TableSchema& schema) const {
    if (cols.size() == 1 && cols[0] == "*") return row;
    Row out;
    for (auto& c : cols) {
        int idx = schema.col_index(c);
        if (idx < 0) throw std::runtime_error("Unknown column: " + c);
        out.push_back(row[idx]);
    }
    return out;
}

std::vector<Row> Executor::full_scan(const std::string& table) {
    auto& schema   = catalog_.get_schema(table);
    auto* file     = heap_files_.at(table);
    int   spp      = schema.slots_per_page();
    int   rec_size = schema.record_size();
    uint32_t pages = file->total_pages();

    std::vector<Row> result;
    for (uint32_t p = 0; p < pages; p++) {
        Page& page = pool_.get_page(*file, p);
        for (int s = 0; s < spp; s++) {
            int slot_off = s * (1 + rec_size);
            if (page.data[slot_off] == 0) continue;
            Row row = decode_record(page.data + slot_off + 1, schema);
            result.push_back(row);
        }
    }
    return result;
}

std::vector<Row> Executor::execute_select(const QueryPlan& plan, const SelectStmt& stmt) {
    const auto& schema = catalog_.get_schema(plan.table);
    std::vector<Row> result;

    if (plan.scan == ScanType::INDEX_SCAN) {
        auto* idx    = indexes_.at(plan.table);
        auto  row_id = idx->search(plan.pk_val);
        if (row_id) {
            auto* file     = heap_files_.at(plan.table);
            int   spp      = schema.slots_per_page();
            int   rec_size = schema.record_size();
            uint32_t page_id   = static_cast<uint32_t>(*row_id / spp);
            int      slot_idx  = *row_id % spp;
            Page& page = pool_.get_page(*file, page_id);
            int   off  = slot_idx * (1 + rec_size);
            if (page.data[off]) {
                Row row = decode_record(page.data + off + 1, schema);
                if (eval_filter(plan.filter, row, schema))
                    result.push_back(project(row, stmt.cols, schema));
            }
        }
        return result;
    }

    auto rows = full_scan(plan.table);
    for (auto& row : rows) {
        if (eval_filter(plan.filter, row, schema))
            result.push_back(project(row, stmt.cols, schema));
    }

    if (plan.has_join) {
        if (!catalog_.has_table(plan.join_table))
            throw std::runtime_error("Table not found: " + plan.join_table);
        const auto& jschema = catalog_.get_schema(plan.join_table);
        auto jrows = full_scan(plan.join_table);

        auto col_name = [](const std::string& tc) {
            auto dot = tc.find('.');
            return dot == std::string::npos ? tc : tc.substr(dot + 1);
        };

        std::string lc = col_name(plan.join_left_col);
        std::string rc = col_name(plan.join_right_col);
        int li = schema.col_index(lc);
        int ri = jschema.col_index(rc);

        bool left_is_main = plan.join_left_col.substr(0, plan.join_left_col.find('.')) == plan.table;
        if (!left_is_main) std::swap(li, ri);

        std::vector<Row> joined;
        for (auto& lr : result) {
            for (auto& jr : jrows) {
                if (lr[li] == jr[ri]) {
                    Row merged = lr;
                    merged.insert(merged.end(), jr.begin(), jr.end());
                    joined.push_back(merged);
                }
            }
        }
        return joined;
    }

    return result;
}

int Executor::execute_insert(const std::string& table, const Row& values) {
    const auto& schema = catalog_.get_schema(table);
    if ((int)values.size() != (int)schema.columns.size())
        throw std::runtime_error("INSERT: wrong number of values");

    auto* file     = heap_files_.at(table);
    auto* idx      = indexes_.at(table);
    int   spp      = schema.slots_per_page();
    int   rec_size = schema.record_size();

    int pk_val = std::stoi(values[0]);
    if (idx->search(pk_val)) {
        std::cerr << "Duplicate primary key: " << pk_val << "\n";
        return -1;
    }

    uint32_t pages = file->total_pages();
    for (uint32_t p = 0; p < pages; p++) {
        Page& page = pool_.get_page(*file, p);
        for (int s = 0; s < spp; s++) {
            int off = s * (1 + rec_size);
            if (page.data[off] == 0) {
                page.data[off] = 1;
                encode_record(values, schema, page.data + off + 1);
                pool_.mark_dirty(*file, p);
                int row_id = static_cast<int>(p) * spp + s;
                idx->insert(pk_val, row_id);
                return row_id;
            }
        }
    }

    uint32_t new_page_id = file->alloc_page();
    Page& page = pool_.get_page(*file, new_page_id);
    page.data[0] = 1;
    encode_record(values, schema, page.data + 1);
    pool_.mark_dirty(*file, new_page_id);
    int row_id = static_cast<int>(new_page_id) * spp;
    idx->insert(pk_val, row_id);
    return row_id;
}

std::vector<int> Executor::matching_pks(const std::string& table, Expression* where) {
    const auto& schema = catalog_.get_schema(table);
    auto* file     = heap_files_.at(table);
    int   spp      = schema.slots_per_page();
    int   rec_size = schema.record_size();
    std::vector<int> pks;
    uint32_t pages = file->total_pages();
    for (uint32_t p = 0; p < pages; p++) {
        Page& page = pool_.get_page(*file, p);
        for (int s = 0; s < spp; s++) {
            int off = s * (1 + rec_size);
            if (page.data[off] == 0) continue;
            Row row = decode_record(page.data + off + 1, schema);
            if (eval_filter(where, row, schema))
                pks.push_back(std::stoi(row[0]));
        }
    }
    return pks;
}

int Executor::execute_delete(const std::string& table, Expression* where) {
    const auto& schema = catalog_.get_schema(table);
    auto* file     = heap_files_.at(table);
    auto* idx      = indexes_.at(table);
    int   spp      = schema.slots_per_page();
    int   rec_size = schema.record_size();

    int deleted = 0;
    uint32_t pages = file->total_pages();
    for (uint32_t p = 0; p < pages; p++) {
        Page& page = pool_.get_page(*file, p);
        for (int s = 0; s < spp; s++) {
            int off = s * (1 + rec_size);
            if (page.data[off] == 0) continue;
            Row row = decode_record(page.data + off + 1, schema);
            if (eval_filter(where, row, schema)) {
                page.data[off] = 0;
                pool_.mark_dirty(*file, p);
                int pk_val = std::stoi(row[0]);
                idx->remove(pk_val);
                deleted++;
            }
        }
    }
    return deleted;
}

int Executor::recover_insert(const std::string& table, const Row& values) {
    auto* idx  = indexes_.at(table);
    int pk_val = std::stoi(values[0]);
    if (idx->search(pk_val)) return -1;
    return execute_insert(table, values);
}

void Executor::recover_delete(const std::string& table, int pk_val) {
    auto* idx  = indexes_.at(table);
    if (!idx->search(pk_val)) return;
    auto* lit    = new Literal(std::to_string(pk_val));
    auto* col    = new ColumnRef(catalog_.get_schema(table).columns[0].name);
    auto* filter = new BinaryExpr("=", col, lit);
    execute_delete(table, filter);
    delete filter;
}

void Executor::flush(const std::string& table) {
    pool_.flush_all(*heap_files_.at(table));
}
