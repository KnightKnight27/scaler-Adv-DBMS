#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "catalog/catalog.h"
#include "common/config.h"
#include "common/exception.h"
#include "engine/rowstore_engine.h"
#include "execution/executor.h"
#include "parser/ast.h"
#include "parser/lexer.h"
#include "parser/parser.h"
#include "storage/buffer_pool.h"
#include "storage/disk_manager.h"
#include "storage/heap_file.h"

using namespace minidb;

// Bundles the full engine stack for a database file: disk -> buffer pool ->
// catalog -> row-store engine -> executor. Member order is the init order.
struct Database {
    DiskManager    disk;
    BufferPool     pool;
    Catalog        cat;
    RowStoreEngine engine;
    Executor       exec;
    explicit Database(const std::string& path)
        : disk(path),
          pool(DEFAULT_BUFFER_POOL_FRAMES, &disk),
          cat(&pool, path + ".cat"),
          engine(&cat, &pool, &disk),
          exec(&cat, &engine) {}
    void flush() { engine.flush(); }
};

// M1 demo: storage stack + buffer-pool stats.
static int storage_selftest(const std::string& path) {
    std::remove(path.c_str());
    DiskManager disk(path);
    BufferPool  pool(8, &disk);
    PageId first = HeapFile::create(&pool);
    HeapFile heap(&pool, first);
    const int N = 2000;
    for (int i = 0; i < N; ++i) heap.insert("row-" + std::to_string(i) + "-payload-data");
    pool.flush_all();
    int count = 0; RID rid; std::string val;
    for (auto it = heap.begin(); it.next(rid, val); ) ++count;
    std::cout << "inserted=" << N << " scanned=" << count << "\n"
              << "db pages=" << disk.num_pages() << "\n"
              << "buffer pool: hits=" << pool.hits() << " misses=" << pool.misses()
              << " evictions=" << pool.evictions() << "\n";
    return count == N ? 0 : 1;
}

// M2 demo: tokenize + parse a statement, print an AST summary.
static int parse_demo(const std::string& sql) {
    Lexer lex(sql);
    Parser parser(lex.tokenize());
    StmtPtr stmt = parser.parse();
    switch (stmt->kind()) {
        case StmtKind::CreateTable: std::cout << "parsed: CREATE TABLE\n"; break;
        case StmtKind::Insert:      std::cout << "parsed: INSERT\n"; break;
        case StmtKind::Delete:      std::cout << "parsed: DELETE\n"; break;
        case StmtKind::Select:      std::cout << "parsed: SELECT\n"; break;
    }
    return 0;
}

static std::string read_file(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw DBException("cannot open SQL file: " + path);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

static int run_file(const std::string& db, const std::string& file) {
    Database d(db);
    d.exec.execute_script(read_file(file));
    d.flush();
    return 0;
}

static int exec_sql(const std::string& db, const std::string& sql) {
    Database d(db);
    d.exec.execute_script(sql);
    d.flush();
    return 0;
}

static int repl(const std::string& db) {
    Database d(db);
    std::cout << "MiniDB REPL on " << db << " (one statement per line; Ctrl-D to quit)\n";
    std::string line;
    while (std::cout << "minidb> " && std::getline(std::cin, line)) {
        if (line.empty()) continue;
        try { d.exec.execute_script(line); d.flush(); }
        catch (const std::exception& e) { std::cerr << "error: " << e.what() << "\n"; }
    }
    return 0;
}

int main(int argc, char** argv) {
    std::string cmd = argc > 1 ? argv[1] : "";
    try {
        if (cmd == "selftest") return storage_selftest(argc > 2 ? argv[2] : "minidb_selftest.db");
        if (cmd == "parse") {
            if (argc < 3) { std::cerr << "usage: minidb parse \"<sql>\"\n"; return 2; }
            return parse_demo(argv[2]);
        }
        if (cmd == "run") {
            if (argc < 4) { std::cerr << "usage: minidb run <db> <file.sql>\n"; return 2; }
            return run_file(argv[2], argv[3]);
        }
        if (cmd == "exec") {
            if (argc < 4) { std::cerr << "usage: minidb exec <db> \"<sql>\"\n"; return 2; }
            return exec_sql(argv[2], argv[3]);
        }
        if (cmd == "repl") {
            if (argc < 3) { std::cerr << "usage: minidb repl <db>\n"; return 2; }
            return repl(argv[2]);
        }
        std::cout << "MiniDB\n"
                  << "usage:\n"
                  << "  minidb run <db> <file.sql>   execute a SQL script\n"
                  << "  minidb exec <db> \"<sql>\"      execute SQL passed on the command line\n"
                  << "  minidb repl <db>             interactive SQL shell\n"
                  << "  minidb parse \"<sql>\"          parse one statement (AST summary)\n"
                  << "  minidb selftest [dbfile]     storage-layer self test\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 2;
    }
}
