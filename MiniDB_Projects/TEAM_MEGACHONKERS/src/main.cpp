#include <iostream>
#include <vector>
#include <memory>
#include <string>
#include <sstream>
#include <algorithm>
#include <cctype>

#include "catalog/catalog.h"
#include "concurrency/lock_manager.h"
#include "concurrency/transaction_manager.h"
#include "execution/executor_context.h"
#include "execution/values_executor.h"
#include "execution/insert_executor.h"
#include "execution/seq_scan_executor.h"

using namespace minidb;

// =========================================================================
// SQL QUERY PARSER SUBSYSTEM
// Converts raw SQL strings into actionable Abstract Syntax Tree (AST) nodes.
// =========================================================================
enum class StatementType {
    CREATE_TABLE,
    INSERT,
    SELECT,
    EXIT,
    INVALID
};

struct SQLStatement {
    StatementType type{StatementType::INVALID};
    std::string table_name;
    
    // Used for INSERT (e.g., ["1", "Alice"])
    std::vector<std::string> values; 
    
    // Used for CREATE TABLE (e.g., [{"id", "INT"}, {"name", "VARCHAR"}])
    std::vector<std::pair<std::string, std::string>> columns; 
    
    std::string error_message;
};

class SQLParser {
private:
    static std::string ToUpper(std::string str) {
        std::transform(str.begin(), str.end(), str.begin(), ::toupper);
        return str;
    }

    // A basic lexer that splits SQL into tokens, ignoring structural characters
    static std::vector<std::string> Tokenize(const std::string& input) {
        std::vector<std::string> tokens;
        std::string current;
        for (char c : input) {
            // Treat spaces, commas, parentheses, and semicolons as delimiters
            if (std::isspace(c) || c == ';' || c == '(' || c == ')' || c == ',') {
                if (!current.empty()) {
                    tokens.push_back(current);
                    current.clear();
                }
            } else {
                current += c;
            }
        }
        if (!current.empty()) tokens.push_back(current);
        return tokens;
    }

public:
    static SQLStatement Parse(const std::string& query) {
        SQLStatement stmt;
        auto tokens = Tokenize(query);
        
        if (tokens.empty()) return stmt;

        std::string keyword = ToUpper(tokens[0]);

        if (keyword == ".EXIT" || keyword == "EXIT" || keyword == "QUIT") {
            stmt.type = StatementType::EXIT;
            return stmt;
        }

        if (keyword == "CREATE") {
            if (tokens.size() >= 3 && ToUpper(tokens[1]) == "TABLE") {
                stmt.type = StatementType::CREATE_TABLE;
                stmt.table_name = tokens[2];
                
                // Dynamically extract pairs of column names and types
                if ((tokens.size() - 3) % 2 != 0) {
                    stmt.type = StatementType::INVALID;
                    stmt.error_message = "Syntax Error: Column definitions must be in pairs (name type).";
                } else if (tokens.size() == 3) {
                    stmt.type = StatementType::INVALID;
                    stmt.error_message = "Syntax Error: Table must have at least one column.";
                } else {
                    for (size_t i = 3; i < tokens.size(); i += 2) {
                        stmt.columns.push_back({tokens[i], ToUpper(tokens[i+1])});
                    }
                }
            } else {
                stmt.error_message = "Syntax Error: Expected CREATE TABLE <table_name> (<col1> <type1>, ...);";
            }
        } 
        else if (keyword == "INSERT") {
            // Syntax: INSERT INTO table_name VALUES (val1, val2, ...);
            if (tokens.size() >= 5 && ToUpper(tokens[1]) == "INTO" && ToUpper(tokens[3]) == "VALUES") {
                stmt.type = StatementType::INSERT;
                stmt.table_name = tokens[2];
                // Extract any number of dynamic values
                for (size_t i = 4; i < tokens.size(); ++i) {
                    stmt.values.push_back(tokens[i]);
                }
            } else {
                stmt.error_message = "Syntax Error: Expected INSERT INTO <table_name> VALUES (<v1>, <v2>...);";
            }
        } 
        else if (keyword == "SELECT") {
            // Syntax: SELECT * FROM table_name;
            if (tokens.size() >= 4 && tokens[1] == "*" && ToUpper(tokens[2]) == "FROM") {
                stmt.type = StatementType::SELECT;
                stmt.table_name = tokens[3];
            } else {
                stmt.error_message = "Syntax Error: Expected SELECT * FROM <table_name>;";
            }
        } 
        else {
            stmt.error_message = "Syntax Error: Unrecognized command '" + tokens[0] + "'.";
        }

        return stmt;
    }
};

// =========================================================================
// MAIN ENGINE REPL (Read-Eval-Print Loop)
// =========================================================================
int main() {
    std::cout << "========================================\n";
    std::cout << "       MiniDB (LSM-Tree Engine)         \n";
    std::cout << "========================================\n";
    std::cout << "Type 'EXIT;' to quit.\n";
    std::cout << "Supported SQL:\n";
    std::cout << "  -> CREATE TABLE <name> (<col> <type>, ...);\n";
    std::cout << "  -> INSERT INTO <name> VALUES (<v1>, <v2>...);\n";
    std::cout << "  -> SELECT * FROM <name>;\n";
    std::cout << "----------------------------------------\n";

    // Boot up the global database managers
    Catalog catalog;
    LockManager lock_manager;
    TransactionManager txn_manager(&lock_manager);

    std::string input;
    while (true) {
        std::cout << "minidb> ";
        if (!std::getline(std::cin, input)) break; 

        if (input.empty()) continue;

        // 1. Parser / Lexer Phase
        SQLStatement stmt = SQLParser::Parse(input);

        // 2. Evaluation / Execution Phase
        if (stmt.type == StatementType::EXIT) {
            break;
        } 
        else if (stmt.type == StatementType::INVALID) {
            std::cout << "Error: " << stmt.error_message << "\n";
        } 
        else if (stmt.type == StatementType::CREATE_TABLE) {
            std::vector<Column> schema_columns;
            bool valid_schema = true;

            // Dynamically assign Types based on the user's parsed string
            for (const auto& col : stmt.columns) {
                if (col.second == "INT" || col.second == "INTEGER") {
                    schema_columns.emplace_back(col.first, TypeId::INTEGER, 4);
                } else if (col.second == "VARCHAR") {
                    schema_columns.emplace_back(col.first, TypeId::VARCHAR, 255);
                } else {
                    std::cout << "Error: Unsupported data type '" << col.second 
                              << "' for column '" << col.first << "'. Use INT or VARCHAR.\n";
                    valid_schema = false;
                    break;
                }
            }

            if (!valid_schema) continue;

            Schema schema(schema_columns);
            if (catalog.CreateTable(stmt.table_name, schema)) {
                std::cout << "Success: Table '" << stmt.table_name << "' created with " 
                          << schema_columns.size() << " columns.\n";
            }
        }
        else if (stmt.type == StatementType::INSERT) {
            TableMetadata* table = catalog.GetTable(stmt.table_name);
            if (!table) {
                std::cout << "Error: Table '" << stmt.table_name << "' does not exist.\n";
                continue;
            }

            // Validation: Prevent the user from inserting 2 values into a 3-column table
            if (stmt.values.size() != table->schema->GetColumnCount()) {
                std::cout << "Error: Table '" << stmt.table_name << "' expects " 
                          << table->schema->GetColumnCount() << " values, but " 
                          << stmt.values.size() << " were provided.\n";
                continue;
            }

            Transaction* txn = txn_manager.Begin();
            ExecutorContext context(&catalog, txn->GetTransactionId());

            std::vector<Row> raw_data = { Row{stmt.values} };
            auto values_node = std::make_unique<ValuesExecutor>(&context, raw_data);
            InsertExecutor insert_node(&context, std::move(values_node), table->oid);
            
            insert_node.Init();
            Row dummy;
            if (insert_node.Next(&dummy)) {
                std::cout << "Success: 1 row inserted into '" << stmt.table_name << "'.\n";
            }
            
            txn_manager.Commit(txn);
        }
        else if (stmt.type == StatementType::SELECT) {
            TableMetadata* table = catalog.GetTable(stmt.table_name);
            if (!table) {
                std::cout << "Error: Table '" << stmt.table_name << "' does not exist.\n";
                continue;
            }

            Transaction* txn = txn_manager.Begin();
            ExecutorContext context(&catalog, txn->GetTransactionId());

            SeqScanExecutor scan_node(&context, table->oid);
            scan_node.Init();

            Row output_row;
            int count = 0;
            
            // Dynamically print headers based on the table's schema
            std::string separator = "";
            for (const auto& col : table->schema->GetColumns()) {
                std::cout << col.name_ << "\t| ";
                separator += "--------";
            }
            std::cout << "\n" << separator << "\n";
            
            // Dynamically print row values
            while (scan_node.Next(&output_row)) {
                for (const auto& val : output_row.columns) {
                    std::cout << val << "\t| ";
                }
                std::cout << "\n";
                count++;
            }
            std::cout << "(" << count << " rows)\n";
            
            txn_manager.Commit(txn);
        }
    }

    std::cout << "Shutting down MiniDB cleanly. Bye!\n";
    return 0;
}