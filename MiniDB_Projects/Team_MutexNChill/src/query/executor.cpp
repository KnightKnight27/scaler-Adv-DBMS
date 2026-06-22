#include "executor.h"
#include <cstring>
#include <stdexcept>

Executor::Executor(Catalog& catalog) : cat(catalog) {}

// ---- Column value helpers ----

// Return the integer value of 'col' from 'row'.
// col can be "id", "age", "extra", or "dept_id" (alias for extra).
int Executor::getIntCol(const std::string& col, const Row& row) {
    if (col == "id")               return row.id;
    if (col == "age")              return row.age;
    if (col == "extra")            return row.extra;
    if (col == "dept_id")          return row.extra; // alias
    if (col == "course_id")        return row.extra;
    return 0;
}

// Get a column as a string (for display and comparison).
static std::string getStrCol(const std::string& col, const Row& row) {
    if (col == "name")             return std::string(row.name);
    if (col == "id")               return std::to_string(row.id);
    if (col == "age")              return std::to_string(row.age);
    if (col == "extra")            return std::to_string(row.extra);
    if (col == "dept_id")          return std::to_string(row.extra);
    return "";
}

// ---- Expression evaluation ----

bool Executor::evalExpr(Expr* expr, const Row& row, const std::string& table_prefix) {
    if (!expr) return true;

    if (auto* cmp = dynamic_cast<CompareExpr*>(expr)) {
        // Get the left value
        int lval = 0;
        std::string lstr;
        bool left_is_str = false;
        if (auto* col = dynamic_cast<ColumnExpr*>(cmp->left)) {
            // Resolve column reference: strip table prefix if present
            std::string c = col->column;
            lval = getIntCol(c, row);
            lstr = getStrCol(c, row);
            if (c == "name") left_is_str = true;
        } else if (auto* num = dynamic_cast<NumberExpr*>(cmp->left)) {
            lval = num->value;
        }

        // Get the right value
        int rval = 0;
        std::string rstr;
        if (auto* num = dynamic_cast<NumberExpr*>(cmp->right)) {
            rval = num->value;
        } else if (auto* col = dynamic_cast<ColumnExpr*>(cmp->right)) {
            rval = getIntCol(col->column, row);
            rstr = getStrCol(col->column, row);
        }

        // Compare
        if (left_is_str) {
            if (cmp->op == "=")  return lstr == rstr;
            if (cmp->op == "!=") return lstr != rstr;
            return false;
        }
        if (cmp->op == "=")  return lval == rval;
        if (cmp->op == "!=") return lval != rval;
        if (cmp->op == ">")  return lval >  rval;
        if (cmp->op == "<")  return lval <  rval;
        if (cmp->op == ">=") return lval >= rval;
        if (cmp->op == "<=") return lval <= rval;
    }

    if (auto* logic = dynamic_cast<LogicExpr*>(expr)) {
        bool l = evalExpr(logic->left,  row, table_prefix);
        bool r = evalExpr(logic->right, row, table_prefix);
        if (logic->op == "AND") return l && r;
        if (logic->op == "OR")  return l || r;
    }

    return true;
}

// ---- Project helpers ----

ResultRow Executor::project(const std::vector<std::string>& cols,
                             const Row& row,
                             const std::string& table_prefix) {
    ResultRow rr;
    if (cols.size() == 1 && cols[0] == "*") {
        rr.col_names = {"id", "name", "age", "extra"};
        rr.values    = {std::to_string(row.id), std::string(row.name),
                        std::to_string(row.age), std::to_string(row.extra)};
        return rr;
    }
    for (auto& c : cols) {
        // Strip "table." prefix if present
        std::string bare = c;
        auto dot = c.find('.');
        if (dot != std::string::npos) bare = c.substr(dot + 1);
        rr.col_names.push_back(bare);
        rr.values.push_back(getStrCol(bare, row));
    }
    return rr;
}

ResultRow Executor::projectJoin(const std::vector<std::string>& cols,
                                 const Row& outer, const std::string& outer_tbl,
                                 const Row& inner, const std::string& inner_tbl) {
    ResultRow rr;
    for (auto& c : cols) {
        std::string tbl, bare;
        auto dot = c.find('.');
        if (dot != std::string::npos) {
            tbl  = c.substr(0, dot);
            bare = c.substr(dot + 1);
        } else {
            bare = c;
        }
        rr.col_names.push_back(bare);
        // Decide which row to read from
        const Row& src = (tbl == inner_tbl) ? inner : outer;
        rr.values.push_back(getStrCol(bare, src));
    }
    return rr;
}

// ---- Main execute functions ----

std::vector<ResultRow> Executor::executeSelect(SelectStmt* stmt) {
    TableInfo* t = cat.getTable(stmt->table);
    if (!t) throw std::runtime_error("Table not found: " + stmt->table);

    std::vector<ResultRow> result;

    if (stmt->join_table.empty()) {
        // Simple SELECT (no join) — rows come from one table.
        std::vector<Row> rows = t->heap->scanAll();
        for (auto& row : rows) {
            if (evalExpr(stmt->where, row, stmt->table)) {
                result.push_back(project(stmt->columns, row, stmt->table));
            }
        }
    } else {
        // JOIN: nested loop over outer (stmt->table) x inner (join_table).
        TableInfo* inner_t = cat.getTable(stmt->join_table);
        if (!inner_t) throw std::runtime_error("Table not found: " + stmt->join_table);

        // Parse the ON condition columns (format: "table.col")
        auto parseCol = [](const std::string& tc, std::string& tbl, std::string& col) {
            auto d = tc.find('.');
            if (d != std::string::npos) { tbl = tc.substr(0,d); col = tc.substr(d+1); }
            else { tbl = ""; col = tc; }
        };
        std::string lt, lc, rt, rc;
        parseCol(stmt->join_left_col,  lt, lc);
        parseCol(stmt->join_right_col, rt, rc);

        std::vector<Row> outer_rows = t->heap->scanAll();
        std::vector<Row> inner_rows = inner_t->heap->scanAll();

        for (auto& o : outer_rows) {
            for (auto& i : inner_rows) {
                // Check the ON condition (equality join)
                int oval = getIntCol(lc, o);
                int ival = getIntCol(rc, i);
                if (oval == ival) {
                    // Apply optional WHERE filter on the combined row
                    bool keep = true;
                    if (stmt->where) {
                        // Evaluate WHERE against outer row (simplified: WHERE on outer table columns)
                        keep = evalExpr(stmt->where, o, stmt->table);
                    }
                    if (keep) {
                        result.push_back(projectJoin(stmt->columns, o, stmt->table, i, stmt->join_table));
                    }
                }
            }
        }
    }

    return result;
}

Row Executor::executeInsert(InsertStmt* stmt) {
    TableInfo* t = cat.getTable(stmt->table);
    if (!t) throw std::runtime_error("Table not found: " + stmt->table);

    Row row;
    row.id       = stmt->id;
    row.age      = stmt->age;
    row.extra    = stmt->extra;
    row.is_valid = true;
    strncpy(row.name, stmt->name.c_str(), 31);
    row.name[31] = '\0';

    RID rid = t->heap->insertRow(row);
    t->index->insert(row.id, rid);
    cat.recordInsert(stmt->table, row);
    return row;
}

int Executor::executeDelete(DeleteStmt* stmt) {
    TableInfo* t = cat.getTable(stmt->table);
    if (!t) throw std::runtime_error("Table not found: " + stmt->table);

    // Scan for rows matching WHERE and delete them.
    int count = 0;
    int num_pages = t->heap->pageCount();
    for (int pid = 0; pid < num_pages; pid++) {
        // We need to read through all slots on the page.
        // Easiest: use scanAll then cross-reference with RIDs.
        // Simpler approach: scan and mark directly.
    }

    // Simpler: scanAll gives rows, we re-find by id.
    std::vector<Row> rows = t->heap->scanAll();
    for (auto& row : rows) {
        if (evalExpr(stmt->where, row, stmt->table)) {
            // Find this row's RID via the index and delete.
            RID rid;
            if (t->index->search(row.id, rid)) {
                t->heap->deleteRow(rid);
                t->index->remove(row.id);
                cat.recordDelete(stmt->table);
                count++;
            }
        }
    }
    return count;
}
