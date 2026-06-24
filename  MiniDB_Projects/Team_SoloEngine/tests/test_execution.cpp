#include "storage.h"
#include "buffer_pool.h"
#include "btree.h"
#include "table.h"
#include "execution.h"
#include "optimizer.h"

#include <cassert>
#include <filesystem>
#include <iostream>
#include <vector>

static const std::string DB_FILE = "/tmp/test_exec_engine.db";
static void cleanup() { std::filesystem::remove(DB_FILE); }

// ─── helper: run an executor to completion, return tuple count ────────────────
static int drain(AbstractExecutor &exec, Tuple *last = nullptr) {
    exec.Init();
    int n = 0;
    Tuple t;
    while (exec.Next(&t)) { ++n; if (last) *last = t; }
    return n;
}

// ─── test 1: InsertExecutor populates heap + index ───────────────────────────
static void test_insert_500(BufferPoolManager &bpm,
                             TableHeap &heap, BPlusTree &idx) {
    std::vector<Tuple> src;
    src.reserve(500);
    for (int i = 0; i < 500; ++i)
        src.push_back({static_cast<int64_t>(i),
                       static_cast<int64_t>(i * 10),
                       static_cast<int64_t>(i * 100)});

    InsertExecutor ins(&heap, &idx, std::make_unique<ValueExecutor>(std::move(src)));
    int inserted = drain(ins);
    assert(inserted == 500);
    assert(bpm.AllUnpinned());
    std::cout << "[PASS] test_insert_500\n";
}

// ─── test 2: full SeqScan returns all 500 rows ───────────────────────────────
static void test_seq_scan_full(BufferPoolManager &bpm,
                                TableHeap &heap, BPlusTree &idx) {
    Optimizer opt;
    opt.RegisterTable("A", &heap, &idx);

    DummyAST ast;
    ast.type       = DummyAST::Type::SCAN;
    ast.table_name = "A";
    // No filter → optimizer chooses SeqScan

    auto exec = opt.Optimize(ast);
    int count = drain(*exec);
    assert(count == 500);
    assert(bpm.AllUnpinned());
    std::cout << "[PASS] test_seq_scan_full\n";
}

// ─── test 3: SeqScan with val1 filter ────────────────────────────────────────
static void test_seq_scan_filtered(BufferPoolManager &bpm,
                                    TableHeap &heap, BPlusTree &idx) {
    Optimizer opt;
    opt.RegisterTable("A", &heap, &idx);

    // val1 == 420 matches only tuple {42, 420, 4200}
    DummyAST ast;
    ast.type         = DummyAST::Type::SCAN;
    ast.table_name   = "A";
    ast.filter_field = FilterField::VAL1;
    ast.filter_value = 420;

    auto exec = opt.Optimize(ast);
    Tuple t;
    int count = drain(*exec, &t);
    assert(count == 1);
    assert(t.id == 42 && t.val1 == 420 && t.val2 == 4200);
    assert(bpm.AllUnpinned());
    std::cout << "[PASS] test_seq_scan_filtered\n";
}

// ─── test 4: IndexScan on primary key ────────────────────────────────────────
static void test_index_scan(BufferPoolManager &bpm,
                             TableHeap &heap, BPlusTree &idx) {
    Optimizer opt;
    opt.RegisterTable("A", &heap, &idx);

    // Filter on id=42 → optimizer must pick IndexScan
    DummyAST ast;
    ast.type         = DummyAST::Type::SCAN;
    ast.table_name   = "A";
    ast.filter_field = FilterField::ID;
    ast.filter_value = 42;

    auto exec = opt.Optimize(ast);
    assert(dynamic_cast<IndexScanExecutor *>(exec.get()) != nullptr);  // optimizer picked IndexScan

    Tuple t;
    int count = drain(*exec, &t);
    assert(count == 1);
    assert(t.id == 42 && t.val1 == 420 && t.val2 == 4200);

    // Also verify a missing key returns 0 tuples.
    DummyAST ast2;
    ast2.type         = DummyAST::Type::SCAN;
    ast2.table_name   = "A";
    ast2.filter_field = FilterField::ID;
    ast2.filter_value = 9999;
    auto exec2 = opt.Optimize(ast2);
    assert(drain(*exec2) == 0);

    assert(bpm.AllUnpinned());
    std::cout << "[PASS] test_index_scan\n";
}

// ─── test 5: NestedLoopJoin on two tables ────────────────────────────────────
// tableA: ids 0..499  (already populated above)
// tableB: ids 0..9
// join on A.id == B.id → 10 result tuples
static void test_nlj(BufferPoolManager &bpm,
                      TableHeap &heapA, BPlusTree &idxA) {
    // Build tableB
    TableHeap heapB(&bpm);
    BPlusTree idxB(&bpm);

    {
        std::vector<Tuple> src;
        for (int i = 0; i < 10; ++i)
            src.push_back({static_cast<int64_t>(i),
                           static_cast<int64_t>(i * 2),
                           static_cast<int64_t>(i * 3)});
        InsertExecutor ins(&heapB, &idxB, std::make_unique<ValueExecutor>(std::move(src)));
        assert(drain(ins) == 10);
        assert(bpm.AllUnpinned());
    }

    Optimizer opt;
    opt.RegisterTable("A", &heapA, &idxA);
    opt.RegisterTable("B", &heapB, &idxB);

    // Craft a NLJ AST: A ⋈ B on A.id == B.id
    DummyAST join_ast;
    join_ast.type  = DummyAST::Type::NESTED_LOOP_JOIN;
    join_ast.left  = std::make_unique<DummyAST>();
    join_ast.right = std::make_unique<DummyAST>();
    join_ast.left->type       = DummyAST::Type::SCAN;
    join_ast.left->table_name = "A";
    join_ast.right->type       = DummyAST::Type::SCAN;
    join_ast.right->table_name = "B";

    auto exec = opt.Optimize(join_ast);
    int count = drain(*exec);
    assert(count == 10);   // ids 0..9 in A each match the corresponding row in B
    assert(bpm.AllUnpinned());
    std::cout << "[PASS] test_nlj\n";
}

// ─── test 6: pin leak stress ─────────────────────────────────────────────────
// Interleave inserts, scans, and index lookups; verify AllUnpinned after each.
static void test_pin_stress(BufferPoolManager &bpm) {
    cleanup();
    DiskManager dm2(DB_FILE);
    BufferPoolManager bpm2(512, &dm2);

    TableHeap heap(&bpm2);
    BPlusTree idx(&bpm2);

    // Insert 200 rows
    {
        std::vector<Tuple> src;
        for (int i = 0; i < 200; ++i)
            src.push_back({static_cast<int64_t>(i), static_cast<int64_t>(i + 1), 0});
        InsertExecutor ins(&heap, &idx, std::make_unique<ValueExecutor>(std::move(src)));
        assert(drain(ins) == 200);
    }
    assert(bpm2.AllUnpinned());

    // SeqScan 10 times
    for (int r = 0; r < 10; ++r) {
        SeqScanExecutor sc(&heap);
        assert(drain(sc) == 200);
        assert(bpm2.AllUnpinned());
    }

    // Index lookups for every key
    for (int i = 0; i < 200; ++i) {
        IndexScanExecutor is_exec(&heap, &idx, static_cast<int64_t>(i));
        Tuple t;
        assert(drain(is_exec, &t) == 1 && t.id == i);
        assert(bpm2.AllUnpinned());
    }

    (void)bpm; // suppress unused warning
    std::cout << "[PASS] test_pin_stress\n";
}

// ─── test 7: DELETE via optimizer removes rows from heap and index ────────────
// Inserts 10 rows (ids 0–9), deletes id=5 through the optimizer, then verifies:
//   • SeqScan returns exactly 9 live rows.
//   • IndexScan for id=5 returns 0 rows (entry removed from B+ Tree).
//   • AllUnpinned() holds after every operation.
static void test_delete(BufferPoolManager &bpm) {
    cleanup();
    DiskManager       dm3(DB_FILE);
    BufferPoolManager bpm3(128, &dm3);
    TableHeap heap3(&bpm3);
    BPlusTree idx3(&bpm3);

    // Insert ids 0–9.
    {
        std::vector<Tuple> src;
        for (int i = 0; i < 10; ++i)
            src.push_back({static_cast<int64_t>(i),
                           static_cast<int64_t>(i * 10),
                           static_cast<int64_t>(i * 100)});
        InsertExecutor ins(&heap3, &idx3, std::make_unique<ValueExecutor>(std::move(src)));
        assert(drain(ins) == 10);
        assert(bpm3.AllUnpinned());
    }

    Optimizer opt3;
    opt3.RegisterTable("C", &heap3, &idx3);

    // DELETE FROM C WHERE id = 5 — optimizer picks IndexScan as child (n=10,
    // index_cost < seq_cost), wraps it in DeleteExecutor.
    {
        DummyAST del_ast;
        del_ast.type         = DummyAST::Type::DELETE;
        del_ast.table_name   = "C";
        del_ast.filter_field = FilterField::ID;
        del_ast.filter_value = 5;

        auto del_exec = opt3.Optimize(del_ast);
        int deleted = drain(*del_exec);
        assert(deleted == 1);
        assert(bpm3.AllUnpinned());
    }

    // Full scan must return 9 tuples (tombstone skipped automatically).
    {
        DummyAST scan_ast;
        scan_ast.type       = DummyAST::Type::SCAN;
        scan_ast.table_name = "C";

        auto scan_exec = opt3.Optimize(scan_ast);
        assert(drain(*scan_exec) == 9);
        assert(bpm3.AllUnpinned());
    }

    // Index lookup for the deleted key must return 0 tuples.
    {
        DummyAST idx_ast;
        idx_ast.type         = DummyAST::Type::SCAN;
        idx_ast.table_name   = "C";
        idx_ast.filter_field = FilterField::ID;
        idx_ast.filter_value = 5;

        auto idx_exec = opt3.Optimize(idx_ast);
        assert(drain(*idx_exec) == 0);
        assert(bpm3.AllUnpinned());
    }

    (void)bpm;
    std::cout << "[PASS] test_delete\n";
}

// ─── main ────────────────────────────────────────────────────────────────────

int main() {
    cleanup();
    DiskManager dm(DB_FILE);
    BufferPoolManager bpm(512, &dm);

    // Shared state for tests 1-5
    TableHeap heapA(&bpm);
    BPlusTree idxA(&bpm);

    std::cout << "=== Execution Engine Tests ===\n";
    test_insert_500(bpm, heapA, idxA);
    test_seq_scan_full(bpm, heapA, idxA);
    test_seq_scan_filtered(bpm, heapA, idxA);
    test_index_scan(bpm, heapA, idxA);
    test_nlj(bpm, heapA, idxA);
    test_pin_stress(bpm);
    test_delete(bpm);

    cleanup();
    std::cout << "\nAll execution engine tests passed.\n";
    return 0;
}
