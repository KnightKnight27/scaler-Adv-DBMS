// Lab 6 — B-Tree demo runner
// 24BCS10123  Kushal Talati
//
// Walks kt::BTreeIndex<K, V> through:
//   (a) a bookstore-style ISBN -> title index (small enough to print),
//   (b) lookups + overwrite + missing-key,
//   (c) removes covering leaf, internal-with-predecessor, merge, root-collapse,
//   (d) a 16-entry t=2 sequential insert ("worst case for a plain BST"),
//   (e) a t=64 / 60 000-key height-bound spot-check,
//   (f) a 6 000-op randomised stress test against std::map<int,int> as oracle.
//
// check() runs after every mutation; any invariant violation aborts the run.

#include "btree_index.hpp"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <map>
#include <random>
#include <string>
#include <vector>

namespace {

template <typename K, typename V>
void must_be_clean(const kt::BTreeIndex<K, V>& t, const std::string& after) {
    auto problem = t.check();
    if (problem) {
        std::cerr << "INVARIANT BROKEN after " << after << ": " << *problem << "\n";
        std::exit(1);
    }
}

void section(const std::string& s) {
    std::cout << "\n>>> " << s << "\n";
}

}  // namespace

int main() {
    // ============================================================
    section("(a) Bookstore index — ISBN-suffix -> book title, t=3");
    // ============================================================
    kt::BTreeIndex<int, std::string> shop(/*min_branch=*/3);

    // Twelve books — small enough to print the whole tree shape.
    const std::vector<std::pair<int, std::string>> stock = {
        {8101, "Database System Concepts"},
        {8205, "Designing Data-Intensive Applications"},
        {8410, "The C++ Programming Language"},
        {8508, "Operating Systems — Three Easy Pieces"},
        {8612, "Computer Networking — A Top-Down Approach"},
        {8714, "Effective Modern C++"},
        {8816, "Algorithms (Sedgewick)"},
        {8918, "Introduction to Algorithms (CLRS)"},
        {9020, "Linear Algebra Done Right"},
        {9122, "Pattern Recognition and Machine Learning"},
        {9224, "Computer Systems — A Programmer's Perspective"},
        {9326, "Refactoring (Fowler)"},
    };
    for (auto& [isbn, title] : stock) {
        shop.put(isbn, title);
        must_be_clean(shop, "put(" + std::to_string(isbn) + ")");
    }
    std::cout << "length = " << shop.length() << "  branching t = " << shop.branching() << "\n";
    shop.dump(std::cout);

    std::cout << "scan: ";
    shop.scan([](int isbn, const std::string& title){
        std::cout << isbn << "=\"" << title.substr(0, 14) << (title.size() > 14 ? "...\"  " : "\"  ");
    });
    std::cout << "\n";

    // ============================================================
    section("(b) Lookups + overwrite + missing key");
    // ============================================================
    for (int isbn : {8410, 8714, 9999}) {
        auto v = shop.get(isbn);
        std::cout << "  get(" << isbn << ") = " << (v ? *v : std::string("<absent>")) << "\n";
    }
    shop.put(8410, "The C++ Programming Language (4th ed.)");
    must_be_clean(shop, "overwrite");
    std::cout << "  after overwrite — length=" << shop.length()
              << "  fetch(8410)=\"" << shop.fetch(8410) << "\"\n";
    try { (void)shop.fetch(1); }
    catch (const std::out_of_range& e) {
        std::cout << "  fetch(1) threw as expected: " << e.what() << "\n";
    }

    // ============================================================
    section("(c) Removes — leaf / internal-with-predecessor / merge / root-collapse");
    // ============================================================
    for (int isbn : {8101, 8918, 9326, 8410, 8508, 9020}) {
        bool ok = shop.take(isbn);
        must_be_clean(shop, "take(" + std::to_string(isbn) + ")");
        std::cout << "  take(" << isbn << ") -> " << (ok ? "ok" : "miss")
                  << "  length=" << shop.length() << "\n";
    }
    std::cout << "after removes:\n";
    shop.dump(std::cout);

    // ============================================================
    section("(d) t=2 (2-3-4 tree) sequential insert 1..16 — worst case for plain BST");
    // ============================================================
    kt::BTreeIndex<int, int> tiny(/*min_branch=*/2);
    for (int i = 1; i <= 16; ++i) {
        tiny.put(i, i * i);
        must_be_clean(tiny, "tiny put " + std::to_string(i));
    }
    tiny.dump(std::cout);
    std::cout << "  scan: ";
    tiny.scan([](int k, int){ std::cout << k << ' '; });
    std::cout << "\n";

    // ============================================================
    section("(e) Height bound — t=64, 60 000 random inserts");
    // ============================================================
    // With t=64 the height is at most ~log_64(60000) ≈ 3.
    kt::BTreeIndex<int, int> wide(/*min_branch=*/64);
    std::mt19937 rng(static_cast<std::uint32_t>(0xB011FACE));
    std::vector<int> keys; keys.reserve(60000);
    for (int i = 0; i < 60000; ++i) {
        int k = static_cast<int>(rng() & 0x7FFFFFFFu);
        wide.put(k, k);
        keys.push_back(k);
    }
    must_be_clean(wide, "60k random inserts");
    int hits = 0;
    for (int i = 0; i < 1000; ++i) hits += wide.has(keys[static_cast<std::size_t>(i)]) ? 1 : 0;
    std::cout << "  length = " << wide.length()
              << "   spot-check hits on first 1000 inserted keys = " << hits << "/1000\n";

    // ============================================================
    section("(f) Randomised stress — 6 000 ops, oracle = std::map<int,int>");
    // ============================================================
    kt::BTreeIndex<int, int> stress(/*min_branch=*/4);
    std::map<int, int>        oracle;
    std::mt19937 rng2(static_cast<std::uint32_t>(0xD1CE1A5E));
    std::uniform_int_distribution<int> ks(0, 399);

    for (int step = 0; step < 6000; ++step) {
        int k = ks(rng2);
        if (rng2() & 1u) {
            stress.put(k, step);
            oracle[k] = step;
        } else {
            stress.take(k);
            oracle.erase(k);
        }
        if (step % 300 == 0) must_be_clean(stress, "stress " + std::to_string(step));
        assert(stress.length() == oracle.size());
        assert(stress.has(k) == (oracle.count(k) > 0));
    }
    must_be_clean(stress, "stress final");

    // Sorted-walk output must match std::map's iteration order.
    std::vector<int> got;
    stress.scan([&](int k, int){ got.push_back(k); });
    std::vector<int> want; want.reserve(oracle.size());
    for (auto& [k, _] : oracle) want.push_back(k);
    assert(got == want);

    std::cout << "  passed: " << stress.length()
              << " keys live, invariants healthy, oracle agreed on every step.\n";

    std::cout << "\nAll BTreeIndex checks passed.\n";
    return 0;
}
