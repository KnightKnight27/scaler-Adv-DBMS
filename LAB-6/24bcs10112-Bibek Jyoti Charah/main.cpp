// Lab 6 - B-Tree demo / 24bcs10112 Bibek Jyoti Charah
//
// Exercises insert, search, overwrite, delete and a 5000-op randomized
// cross-check against std::map. check() runs after structural changes.

#include "b_tree.hpp"

#include <cstdlib>
#include <iostream>
#include <map>
#include <random>
#include <string>
#include <vector>

namespace {

template <typename K, typename V>
void mustHold(const BTree<K, V> &tree, const char *where) {
    if (!tree.check()) {
        std::cerr << "invariant broken after " << where << '\n';
        std::exit(1);
    }
}

void heading(const char *text) { std::cout << "\n=== " << text << " ===\n"; }

}  // namespace

int main() {
    heading("insert, print, traverse");
    BTree<int, std::string> t(3);
    const std::vector<std::pair<int, std::string>> films = {
        {41, "Inception"},   {17, "Whiplash"}, {76, "Parasite"},
        {9,  "Memento"},     {23, "Dune"},     {58, "Oppenheimer"},
        {88, "Joker"},       {3,  "La La Land"},{12, "Spirited Away"},
        {30, "Goodfellas"},
    };
    for (const auto &[k, v] : films) {
        t.put(k, v);
        mustHold(t, "put");
    }

    std::cout << "size = " << t.size() << '\n';
    t.print();
    std::cout << "in-order: ";
    t.for_each([](int k, const std::string &v) { std::cout << k << ':' << v << "  "; });
    std::cout << '\n';

    heading("search and overwrite");
    for (int k : {17, 23, 99})
        std::cout << "contains(" << k << ") = " << (t.contains(k) ? "yes" : "no") << '\n';
    t.put(58, "Oppenheimer (IMAX)");
    mustHold(t, "overwrite");
    std::cout << "get(58) = " << *t.get(58) << '\n';

    heading("erase");
    for (int k : {3, 17, 41, 88, 23}) {
        bool ok = t.erase(k);
        mustHold(t, "erase");
        std::cout << "erase(" << k << ") -> " << (ok ? "ok" : "miss")
                  << ", size = " << t.size() << '\n';
    }
    t.print();

    heading("randomized cross-check vs std::map");
    BTree<int, int> bt(4);
    std::map<int, int> ref;
    std::mt19937 rng(0xA11CE);
    std::uniform_int_distribution<int> pick(0, 299);

    for (int step = 0; step < 5000; ++step) {
        int k = pick(rng);
        if (rng() & 1) { bt.put(k, step); ref[k] = step; }
        else           { bt.erase(k);     ref.erase(k); }

        if (step % 250 == 0) mustHold(bt, "stress");
        if (bt.size() != ref.size() || bt.contains(k) != (ref.count(k) > 0)) {
            std::cerr << "mismatch at step " << step << '\n';
            return 1;
        }
    }

    std::vector<int> got, want;
    bt.for_each([&](int k, int) { got.push_back(k); });
    for (const auto &[k, v] : ref) { (void)v; want.push_back(k); }
    if (got != want) {
        std::cerr << "in-order traversal mismatch\n";
        return 1;
    }

    std::cout << "stress test passed with " << bt.size() << " live keys\n";
    std::cout << "\nAll B-Tree checks passed.\n";
    return 0;
}
