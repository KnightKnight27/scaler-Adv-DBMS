#pragma once

#include "common/config.h"
#include "common/types.h"
#include "parser/parser.h"
#include "execution/catalog.h"
#include <string>
#include <memory>
#include <unordered_map>

namespace minidb {

enum class PlanType {
    SEQ_SCAN,
    INDEX_SCAN,
    NESTED_LOOP_JOIN
};

struct PhysicalPlan {
    PlanType type;
    std::string table_name;
    
    // Scan fields
    std::string scan_col;
    WhereOp scan_op{WhereOp::NONE};
    Value scan_val;

    // Join fields
    std::unique_ptr<PhysicalPlan> left_plan;
    std::unique_ptr<PhysicalPlan> right_plan;
    std::string join_col_left;
    std::string join_col_right;

    std::string ToString() const {
        if (type == PlanType::SEQ_SCAN) {
            std::string s = "SeqScan(" + table_name + ")";
            if (scan_op != WhereOp::NONE) {
                s += " WHERE " + scan_col + " " + (scan_op == WhereOp::EQUALS ? "=" : (scan_op == WhereOp::GREATER_THAN ? ">" : "<")) + " " + scan_val.ToString();
            }
            return s;
        } else if (type == PlanType::INDEX_SCAN) {
            return "IndexScan(" + table_name + " on " + scan_col + " = " + scan_val.ToString() + ")";
        } else {
            return "NestedLoopJoin(" + left_plan->ToString() + " JOIN " + right_plan->ToString() + " ON " + join_col_left + " = " + join_col_right + ")";
        }
    }
};

class Optimizer {
public:
    explicit Optimizer(Catalog *catalog);
    ~Optimizer() = default;

    // Set statistics for cost calculation
    void SetTableStats(const std::string &table, int num_tuples, int num_pages);

    // Generate optimal physical plan
    std::unique_ptr<PhysicalPlan> Optimize(const SQLStatement &stmt);

private:
    std::unique_ptr<PhysicalPlan> MakeScanPlan(const std::string &table, const std::string &where_col, WhereOp where_op, const Value &where_val);

    Catalog *catalog_;
    
    struct TableStats {
        int num_tuples{0};
        int num_pages{0};
    };
    std::unordered_map<std::string, TableStats> stats_;
};

} // namespace minidb
