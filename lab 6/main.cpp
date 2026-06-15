#include "b_tree.hpp"
#include <cstdlib>
#include <iostream>
#include <map>
#include <random>
#include <string>
#include <vector>

namespace {

// check tree invariants after every mutation
template <typename K, typename V>
void check(const adbms::b_tree<K, V>& tree, const std::string& ctx) {
    std::string err = tree.verify();
    if (!err.empty()) {
        std::cerr << "[FAIL] " << ctx << " => " << err << '\n';
        std::exit(1);
    }
}

void section(const std::string& title) {
    std::cout << "\n--- " << title << " ---\n";
}

} // namespace

int main() {
    // --- Part 1: Basic insert and structure check ---
    section("1) Basic insert + print + in-order");

    adbms::b_tree<int, std::string> bt(3);

    // some country-capital pairs as test data
    std::vector<std::pair<int, std::string>> data = {
        {55, "Brazil"},   {20, "Egypt"},    {80, "Germany"},
        {5,  "Angola"},   {33, "Finland"},  {67, "Mexico"},
        {91, "Thailand"}, {14, "Cuba"},     {48, "Hungary"},
        {72, "Nigeria"}
    };

    for (auto& [k, v] : data) {
        bt.insert(k, v);
        check(bt, "insert(" + std::to_string(k) + ")");
    }

    std::cout << "total keys: " << bt.size() << "\n";
    bt.print();

    std::cout << "in-order: ";
    bt.in_order([](int k, const std::string& v) {
        std::cout << k << ":" << v << "  ";
    });
    std::cout << "\n";

    // --- Part 2: Lookup, update, missing key ---
    section("2) Lookup and update");

    for (int k : {14, 55, 100}) {
        std::cout << "contains(" << k << ") -> "
                  << (bt.contains(k) ? "true" : "false") << "\n";
    }

    std::cout << "at(67) = " << bt.at(67) << "\n";

    // overwrite an existing key
    bt.insert(67, "Mexico (updated)");
    check(bt, "overwrite(67)");
    std::cout << "after update, at(67) = " << bt.at(67) << "\n";

    // at() on missing key should throw
    try {
        (void)bt.at(999);
    } catch (const std::out_of_range& ex) {
        std::cout << "at(999) correctly threw: " << ex.what() << "\n";
    }

    // --- Part 3: Deletion ---
    section("3) Erase various keys");

    std::vector<int> to_remove = {5, 20, 55, 91, 33};
    for (int k : to_remove) {
        bool removed = bt.erase(k);
        check(bt, "erase(" + std::to_string(k) + ")");
        std::cout << "erase(" << k << ") -> "
                  << (removed ? "removed" : "not found")
                  << "  size=" << bt.size() << "\n";
    }

    // try erasing a key that was never inserted
    bool miss = bt.erase(42);
    std::cout << "erase(42) -> " << (miss ? "removed" : "not found") << "\n";

    bt.print();

    // --- Part 4: Minimal degree tree (t=2) with sequential keys ---
    section("4) t=2 tree, sequential inserts 1..20");

    adbms::b_tree<int, int> seq(2);
    for (int i = 1; i <= 20; ++i) {
        seq.insert(i, i * 10);
        check(seq, "seq insert " + std::to_string(i));
    }
    seq.print();

    std::cout << "in-order: ";
    seq.in_order([](int k, int v) { std::cout << k << "(" << v << ") "; });
    std::cout << "\n";

    // delete every other key and verify
    for (int i = 1; i <= 20; i += 2) {
        seq.erase(i);
        check(seq, "seq erase " + std::to_string(i));
    }
    std::cout << "after removing odd keys, size=" << seq.size() << "\n";
    seq.print();

    // --- Part 5: Stress test vs std::map ---
    section("5) Random stress test (t=5, 6000 ops)");

    adbms::b_tree<int, int> stree(5);
    std::map<int, int> ref;

    std::mt19937 rng(0xBEEF42);
    std::uniform_int_distribution<int> kdist(0, 499);

    for (int op = 0; op < 6000; ++op) {
        int k = kdist(rng);

        if (rng() % 3 != 0) {   // 2/3 inserts, 1/3 erases
            stree.insert(k, op);
            ref[k] = op;
        } else {
            stree.erase(k);
            ref.erase(k);
        }

        // periodic structural check
        if (op % 300 == 0)
            check(stree, "stress op " + std::to_string(op));

        if (stree.size() != ref.size()) {
            std::cerr << "size mismatch at op " << op << "\n";
            return 1;
        }
        if (stree.contains(k) != (ref.count(k) > 0)) {
            std::cerr << "contains mismatch at op " << op << "\n";
            return 1;
        }
    }

    check(stree, "stress final");

    // compare full in-order output against the oracle map
    std::vector<int> got, want;
    stree.in_order([&](int k, int) { got.push_back(k); });
    for (auto& [k, _] : ref) want.push_back(k);

    if (got != want) {
        std::cerr << "traversal mismatch after stress test\n";
        return 1;
    }

    std::cout << "stress passed, " << stree.size() << " live keys.\n";
    std::cout << "\nAll tests passed.\n";
    return 0;
}
