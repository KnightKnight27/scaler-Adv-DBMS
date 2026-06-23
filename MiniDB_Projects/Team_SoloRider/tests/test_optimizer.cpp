#include <catch2/catch_test_macros.hpp>
#include "plan/optimizer.h"
#include <filesystem>

using namespace minidb;

TEST_CASE("Optimizer: Select Scan Choice", "[optimizer]") {
    Catalog cat("test_db");
    Schema schema({Column("id", ColumnType::INT)});
    cat.create_table("users", schema, 0); // pk_col=0
    cat.set_has_index("users", true);
    
    std::unordered_map<std::string, HeapFile*> hfs;
    std::unordered_map<std::string, BPlusTree*> idxs;
    HeapFile hf("test_opt.db");
    BPlusTree tree(4);
    hfs["users"] = &hf;
    idxs["users"] = &tree;
    
    Optimizer opt(&cat, hfs, idxs);
    
    auto col = std::make_unique<ColumnRef>("", "id");
    auto lit = std::make_unique<Literal>(10);
    auto bin = std::make_unique<BinaryExpr>(std::move(col), "=", std::move(lit));
    
    SelectStmt sel;
    sel.type = ASTNodeType::SELECT_STMT;
    sel.from_table = "users";
    sel.where_clause = std::move(bin);
    
    auto root = opt.optimize(&sel);
    REQUIRE(root != nullptr);
    
    std::filesystem::remove("test_opt.db");
}
