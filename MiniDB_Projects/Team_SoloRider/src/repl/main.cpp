#include <iostream>
#include <string>
#include <unordered_map>
#include "sql/parser.h"
#include "catalog/catalog.h"
#include "plan/optimizer.h"
#include "storage/heap_file.h"
#include "index/bplus_tree.h"

using namespace minidb;

int main() {
    std::cout << "========================================\n";
    std::cout << " MiniDB REPL (Educational DB Engine) \n";
    std::cout << "========================================\n";
    std::cout << "Type 'exit' or 'quit' to exit.\n\n";

    Catalog catalog("minidb_data");
    std::unordered_map<std::string, HeapFile*> heap_files;
    std::unordered_map<std::string, BPlusTree*> indexes;

    // Minimal hardcoded table for demonstration purposes
    Schema schema({Column("id", ColumnType::INT), Column("name", ColumnType::VARCHAR)});
    catalog.create_table("users", schema, 0);
    catalog.set_has_index("users", true);
    
    HeapFile hf("users.db");
    BPlusTree tree(4);
    
    // Insert dummy data
    auto t1 = serialize_tuple(Tuple({1, std::string("Alice")}), schema);
    RecordId rid1 = hf.insert_tuple(t1.data(), t1.size());
    tree.insert(1, rid1);

    auto t2 = serialize_tuple(Tuple({2, std::string("Bob")}), schema);
    RecordId rid2 = hf.insert_tuple(t2.data(), t2.size());
    tree.insert(2, rid2);

    heap_files["users"] = &hf;
    indexes["users"] = &tree;

    std::string input;
    while (true) {
        std::cout << "minidb> ";
        std::getline(std::cin, input);
        
        if (input == "exit" || input == "quit" || std::cin.eof()) break;
        if (input.empty()) continue;
        
        try {
            auto ast = parse_sql(input);
            if (!ast) {
                std::cout << "Error: Could not parse query.\n";
                continue;
            }
            
            if (ast->type != ASTNodeType::SELECT_STMT) {
                std::cout << "Note: This minimal REPL demo only executes SELECT queries.\n";
                continue;
            }
            
            Optimizer optimizer(&catalog, heap_files, indexes);
            auto plan = optimizer.optimize(ast.get());
            if (!plan) {
                std::cout << "Error: Could not generate execution plan.\n";
                continue;
            }
            
            plan->Open();
            Tuple tuple;
            int count = 0;
            while (plan->Next(tuple)) {
                std::cout << "[ ";
                for (size_t i = 0; i < tuple.values.size(); i++) {
                    std::cout << value_to_string(tuple.get_value(i));
                    if (i < tuple.values.size() - 1) std::cout << " | ";
                }
                std::cout << " ]\n";
                count++;
            }
            plan->Close();
            std::cout << "(" << count << " rows returned)\n";
            
        } catch (const std::exception& e) {
            std::cout << "Execution error: " << e.what() << "\n";
        }
    }
    
    std::remove("users.db");
    return 0;
}
