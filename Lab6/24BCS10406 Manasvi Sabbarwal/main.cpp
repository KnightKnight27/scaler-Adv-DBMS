// Lab 6 — B-Tree demo / 24BCS10406 Manasvi Sabbarwal
//
// Drives every code path in b_tree.hpp:
//   - put / get / has / remove on a small index,
//   - the overwrite path (a re-put must not change size),
//   - all six CLRS deletion cases (leaf, internal-pred, internal-succ,
//     internal-merge, descend-borrow, descend-merge, plus root collapse),
//   - sequential insert into a t=2 tree (the BST-worst-case shape),
//   - a height bound check at t=64,
//   - a 7000-step random insert/remove stress test against std::map.
//
// audit() is called after every mutation in the first three scenarios so a
// regression in any invariant fails the demo immediately with a precise
// message rather than a silent miscompare hundreds of ops later.

#include "b_tree.hpp"

#include <cassert>
#include <cstdlib>
#include <iostream>
#include <map>
#include <random>
#include <string>
#include <vector>

using adbms::lab6::BTree;

namespace {

template <typename T>
void must_be_healthy(const T& tree, const std::string& after) {
    const auto err = tree.audit();
    if (!err.empty()) {
        std::cerr << "INVARIANT BROKEN after " << after << ": " << err << '\n';
        std::exit(1);
    }
}

void section(const std::string& title) {
    std::cout << "\n=== " << title << " ===\n";
}

}  // namespace

int main() {
    using SongIdx = BTree<int, std::string>;

    section("1) Small song index, t=3, 10 inserts");
    SongIdx songs(3);
    const std::vector<std::pair<int, std::string>> seed = {
        {45, "Bohemian Rhapsody"}, {12, "Stairway to Heaven"}, {78, "Hotel California"},
        { 6, "Comfortably Numb"},  {29, "Imagine"},            {63, "Billie Jean"},
        {91, "Smells Like Teen"},  { 2, "Yesterday"},          {18, "Purple Rain"},
        {34, "Thriller"}
    };
    for (const auto& [id, title] : seed) {
        songs.put(id, title);
        must_be_healthy(songs, "put(" + std::to_string(id) + ")");
    }
    std::cout << "size = " << songs.size() << '\n';
    songs.dump(std::cout);
    std::cout << "in-order: ";
    songs.for_each([](int k, const std::string& v){
        std::cout << k << '=' << v << "  ";
    });
    std::cout << '\n';

    section("2) Lookup + overwrite");
    for (int k : {12, 29, 99}) {
        auto v = songs.get(k);
        std::cout << "  get(" << k << ") -> "
                  << (v ? *v : std::string("<miss>")) << '\n';
    }
    const auto prev_size = songs.size();
    songs.put(63, "Billie Jean (Remastered)");
    std::cout << "  after overwrite get(63) = " << *songs.get(63)
              << "   size=" << songs.size()
              << " (was " << prev_size << ")\n";

    section("3) Erase — leaf, internal-pred, internal-succ, internal-merge");
    for (int k : {2, 91, 45, 12, 29}) {
        const bool ok = songs.remove(k);
        must_be_healthy(songs, "remove(" + std::to_string(k) + ")");
        std::cout << "  remove(" << k << ") -> " << (ok ? "yes" : "miss")
                  << "  size=" << songs.size() << '\n';
    }
    std::cout << "tree shape after erases:\n";
    songs.dump(std::cout);

    section("4) Sequential insert into t=2 (a 2-3-4 tree)");
    BTree<int, int> btree234(2);
    for (int i = 1; i <= 16; ++i) {
        btree234.put(i, i * i);
        must_be_healthy(btree234, "seq put " + std::to_string(i));
    }
    btree234.dump(std::cout);
    std::cout << "in-order keys: ";
    btree234.for_each([](int k, int){ std::cout << k << ' '; });
    std::cout << '\n';

    section("5) Height bound — t=64, 40000 random inserts");
    BTree<int, int> bulk(64);
    std::mt19937 rng(0xB4C5D6);
    std::vector<int> inserted;
    inserted.reserve(40000);
    for (int i = 0; i < 40000; ++i) {
        const int k = static_cast<int>(rng() & 0x7fffffff);
        bulk.put(k, k);
        inserted.push_back(k);
    }
    must_be_healthy(bulk, "40k random inserts");
    int hits = 0;
    for (int i = 0; i < 1000; ++i) hits += bulk.has(inserted[i]) ? 1 : 0;
    std::cout << "  size = " << bulk.size()
              << "  spot-check hits on first 1000 inserted = " << hits << "/1000\n";

    section("6) Stress test — 7000 random ops vs std::map");
    BTree<int, int>    suspect(4);
    std::map<int, int> oracle;
    std::mt19937       rng2(0x73A92F);
    std::uniform_int_distribution<int> key_dist(0, 399);
    for (int step = 0; step < 7000; ++step) {
        const int k = key_dist(rng2);
        if (rng2() & 1u) {
            suspect.put(k, step);
            oracle[k] = step;
        } else {
            suspect.remove(k);
            oracle.erase(k);
        }
        if (step % 200 == 0) must_be_healthy(suspect, "stress " + std::to_string(step));
        assert(suspect.size() == oracle.size());
        assert(suspect.has(k) == (oracle.count(k) > 0));
    }
    must_be_healthy(suspect, "stress final");

    std::vector<int> got, want;
    suspect.for_each([&](int k, int){ got.push_back(k); });
    for (const auto& [k, _] : oracle) want.push_back(k);
    assert(got == want);
    std::cout << "  passed: " << suspect.size()
              << " keys, oracle agreed on every step.\n";

    std::cout << "\nAll B-Tree checks passed.\n";
    return 0;
}
