// The engine: the glue that ties the layers together.
//   parse  ->  (SELECT) optimize into a plan, run operators
//          ->  (INSERT/DELETE) update the heap file and index directly
//
// execute() returns a structured Result so tests can assert on it; the REPL
// (main.cpp) prints those results as a table.
//
// The two seams marked "Phase 2" are where transactions and write-ahead
// logging will plug in - they wrap the same insert/delete paths.
#pragma once

#include <string>
#include <vector>

#include "catalog.h"
#include "optimizer.h"
#include "parser.h"
#include "types.h"

namespace minidb {

struct Result {
    enum Kind { Message, Rows, Explain } kind;
    std::string text;                  // Message / Explain
    std::vector<std::string> headers;  // Rows
    std::vector<Row> rows;             // Rows
};

class Engine {
public:
    explicit Engine(const std::string& dataDir = "team_cash_data");
    Result execute(const std::string& sql);
    Result explain(const std::string& sql);
    Catalog& catalog() { return catalog_; }
    void close() { catalog_.flush(); }

private:
    Catalog catalog_;
    Optimizer optimizer_;

    Result doCreate(const CreateStmt& s);
    Result doInsert(const InsertStmt& s);
    Result doSelect(const SelectStmt& s);
    Result doDelete(const DeleteStmt& s);
};

}  // namespace minidb
