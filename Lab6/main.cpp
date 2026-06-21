#include "b_tree.hpp"

#include <cstdlib>
#include <iostream>
#include <map>
#include <random>
#include <string>
#include <vector>

namespace {

template <typename K, typename V>
void assert_ok(const adbms::b_tree<K, V>& tree, const std::string& step) {
    std::string err = tree.verify();
    if (!err.empty()) {
        std::cerr << "INVARIANT FAIL after " << step << ": " << err << '\n';
        std::exit(1);
    }
}

void heading(const std::string& text) {
    std::cout << "\n=== " << text << " ===\n";
}

} // namespace

int main() {
    using Tree = adbms::b_tree<int, std::string>;

    heading("1) Insert, print, and traversal");
    Tree t(3);

    const std::vector<std::pair<int, std::string>> items = {
        {41, "Inception"}, {17, "Whiplash"}, {76, "Parasite"},
        {9, "Memento"}, {23, "Dune"}, {58, "Oppenheimer"},
        {88, "Joker"}, {3, "La La Land"}, {12, "Spirited Away"},
        {30, "Goodfellas"}
    };

    for (const auto& [k, v] : items) {
        t.insert(k, v);
        assert_ok(t, "insert(" + std::to_string(k) + ")");
    }

    std::cout << "size = " << t.size() << '\n';
    t.print();

    std::cout << "in-order: ";
    t.in_order([](int k, const std::string& v) {
        std::cout << k << "=" << v << "  ";
    });
    std::cout << '\n';

    heading("2) Search and overwrite");
    for (int k : {17, 23, 99}) {
        std::cout << "contains(" << k << ") = " << (t.contains(k) ? "yes" : "no") << '\n';
    }

    std::cout << "at(58) = " << t.at(58) << '\n';
    t.insert(58, "Oppenheimer (IMAX)");
    assert_ok(t, "overwrite(58)");
    std::cout << "after overwrite at(58) = " << t.at(58) << '\n';

    try {
        (void)t.at(7);
    } catch (const std::out_of_range& e) {
        std::cout << "at(7) threw as expected: " << e.what() << '\n';
    }

    heading("3) Erase across multiple cases");
    for (int k : {3, 17, 41, 88, 23}) {
        bool ok = t.erase(k);
        assert_ok(t, "erase(" + std::to_string(k) + ")");
        std::cout << "erase(" << k << ") -> " << (ok ? "ok" : "miss")
                  << ", size = " << t.size() << '\n';
    }
    t.print();

    heading("4) Sequential inserts into a t=2 tree");
    adbms::b_tree<int, int> ordered(2);
    for (int i = 1; i <= 16; ++i) {
        ordered.insert(i, i * i);
        assert_ok(ordered, "seq insert " + std::to_string(i));
    }

    ordered.print();
    std::cout << "in-order: ";
    ordered.in_order([](int k, int) { std::cout << k << ' '; });
    std::cout << '\n';

    heading("5) Random stress test against std::map");
    adbms::b_tree<int, int> stress(4);
    std::map<int, int> oracle;

    std::mt19937 rng(0xA11CE);
    std::uniform_int_distribution<int> key_dist(0, 299);

    for (int step = 0; step < 5000; ++step) {
        int k = key_dist(rng);

        if (rng() & 1) {
            stress.insert(k, step);
            oracle[k] = step;
        } else {
            stress.erase(k);
            oracle.erase(k);
        }

        if (step % 250 == 0) {
            assert_ok(stress, "stress step " + std::to_string(step));
        }

        if (stress.size() != oracle.size()) {
            std::cerr << "size mismatch at step " << step << '\n';
            return 1;
        }

        if (stress.contains(k) != (oracle.count(k) > 0)) {
            std::cerr << "contains mismatch at step " << step << '\n';
            return 1;
        }
    }

    assert_ok(stress, "stress final");

    std::vector<int> got;
    stress.in_order([&](int k, int) { got.push_back(k); });

    std::vector<int> want;
    for (const auto& [k, v] : oracle) {
        (void)v;
        want.push_back(k);
    }

    if (got != want) {
        std::cerr << "in-order traversal mismatch\n";
        return 1;
    }

    std::cout << "stress test passed with " << stress.size() << " live keys.\n";
    std::cout << "\nAll B-Tree checks passed.\n";
    return 0;
}