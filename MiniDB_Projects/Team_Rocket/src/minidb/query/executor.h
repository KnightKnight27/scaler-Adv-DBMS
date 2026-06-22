#pragma once

#include <string>
#include <vector>

#include "exec_context.h"
#include "operators.h"
#include "optimizer.h"
#include "parser.h"

namespace minidb {

struct Result {
    bool ok = true;
    std::string message;
    std::string plan;
    std::vector<std::string> columns;
    std::vector<std::vector<std::string>> rows;
};

class Executor {
public:
    static Result run_select(ExecContext* ctx, const Statement& st) {
        Plan plan = Optimizer::build(ctx, st);
        Result res;
        res.plan = plan.explain;
        Operator* root = plan.root.get();
        for (const Column& c : root->out_schema) res.columns.push_back(c.name);
        root->open();
        Row r;
        while (root->next(r)) {
            std::vector<std::string> out;
            for (const Value& v : r.tuple) out.push_back(v.to_string());
            res.rows.push_back(std::move(out));
        }
        root->close();
        res.message = std::to_string(res.rows.size()) + " row(s)";
        return res;
    }
};

}  // namespace minidb
