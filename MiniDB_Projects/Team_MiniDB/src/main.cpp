#include <cstdio>
#include <iostream>
#include <string>

#include "common/exception.h"
#include "parser/ast.h"
#include "parser/lexer.h"
#include "parser/parser.h"
#include "storage/buffer_pool.h"
#include "storage/disk_manager.h"
#include "storage/heap_file.h"

using namespace minidb;

// M1 demo: exercises the storage stack end to end and reports buffer-pool
// behaviour. A deliberately small pool forces evictions so the clock-sweep
// policy and dirty write-back are actually used.
static int storage_selftest(const std::string& path) {
    std::remove(path.c_str());  // start from a clean file

    DiskManager disk(path);
    BufferPool  pool(/*frames=*/8, &disk);  // small on purpose, so the data spills past the pool

    PageId first = HeapFile::create(&pool);
    HeapFile heap(&pool, first);

    const int N = 2000;  // spans more pages than the pool has frames -> forces eviction
    for (int i = 0; i < N; ++i)
        heap.insert("row-" + std::to_string(i) + "-payload-data");
    pool.flush_all();

    int count = 0;
    RID rid;
    std::string val;
    for (auto it = heap.begin(); it.next(rid, val); ) ++count;

    std::cout << "inserted=" << N << " scanned=" << count << "\n"
              << "db pages=" << disk.num_pages() << "\n"
              << "buffer pool: hits=" << pool.hits()
              << " misses=" << pool.misses()
              << " evictions=" << pool.evictions() << "\n";
    return count == N ? 0 : 1;
}

// M2 demo: tokenize + parse a SQL statement and print a short summary of the AST,
// showing the lexer/parser pipeline working.
static int parse_demo(const std::string& sql) {
    Lexer lex(sql);
    Parser parser(lex.tokenize());
    StmtPtr stmt = parser.parse();
    switch (stmt->kind()) {
        case StmtKind::CreateTable: {
            auto* s = static_cast<CreateTableStmt*>(stmt.get());
            std::cout << "CREATE TABLE " << s->table << " with " << s->columns.size()
                      << " columns, primary key column index " << s->primary_key << "\n";
            break;
        }
        case StmtKind::Insert: {
            auto* s = static_cast<InsertStmt*>(stmt.get());
            std::cout << "INSERT INTO " << s->table << " (" << s->rows.size() << " row(s))\n";
            break;
        }
        case StmtKind::Delete: {
            auto* s = static_cast<DeleteStmt*>(stmt.get());
            std::cout << "DELETE FROM " << s->table
                      << (s->where ? " with WHERE" : " (all rows)") << "\n";
            break;
        }
        case StmtKind::Select: {
            auto* s = static_cast<SelectStmt*>(stmt.get());
            std::cout << "SELECT from " << s->from_table;
            if (!s->join_table.empty()) std::cout << " JOIN " << s->join_table;
            std::cout << " | projected=" << s->columns.size()
                      << " aggregates=" << s->aggregates.size()
                      << " where=" << (s->where ? "yes" : "no")
                      << " group_by=" << s->group_by.size() << "\n";
            break;
        }
    }
    return 0;
}

int main(int argc, char** argv) {
    std::string cmd = argc > 1 ? argv[1] : "";
    try {
        if (cmd == "selftest") {
            std::string path = argc > 2 ? argv[2] : "minidb_selftest.db";
            return storage_selftest(path);
        }
        if (cmd == "parse") {
            if (argc < 3) { std::cerr << "usage: minidb parse \"<sql>\"\n"; return 2; }
            return parse_demo(argv[2]);
        }
        std::cout << "MiniDB\n"
                  << "usage:\n"
                  << "  minidb selftest [dbfile]   run the storage-layer self test\n"
                  << "  minidb parse \"<sql>\"        tokenize and parse one SQL statement\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 2;
    }
}
