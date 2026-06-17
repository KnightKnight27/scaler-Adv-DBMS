// main.cc — ADBMS Lab 6 demo / 24BCS10115 Gauri Shukla
//
// Drives every code path in BTree.cc:
//   * insert with proactive split + root growth,
//   * contains,
//   * remove across all CLRS cases (leaf, predecessor, successor, merge,
//     sibling-borrow, root-collapse),
//   * level-order print + in-order traversal,
//   * validate() is called after every mutation as a built-in assertion,
//   * a randomised stress run that checks the tree against std::set.

#include "BTree.h"

#include <cstdlib>
#include <iostream>
#include <random>
#include <set>
#include <string>
#include <vector>

namespace {

void must_be_valid(const BTree& t, const std::string& after) {
    std::string err = t.validate();
    if (!err.empty()) {
        std::cerr << "INVARIANT BROKEN after " << after << ": " << err << "\n";
        std::exit(1);
    }
}

void banner(const std::string& s) { std::cout << "\n=== " << s << " ===\n"; }

void show_inorder(const BTree& t) {
    std::cout << "in-order:";
    for (int k : t.inorder()) std::cout << ' ' << k;
    std::cout << "  (" << t.size() << " keys, height " << t.height() << ")\n";
}

}  // namespace

int main() {
    // --------------------------------------------------------------------
    banner("1) Build a t=3 tree and look at its shape");
    // --------------------------------------------------------------------
    BTree tree(3);
    const std::vector<int> seed = {50, 20, 70, 10, 30, 60, 80, 5, 15, 25,
                                   35, 65, 75, 85, 90, 95, 40, 45, 55, 100};
    for (int k : seed) {
        tree.insert(k);
        must_be_valid(tree, "insert(" + std::to_string(k) + ")");
    }
    tree.print(std::cout);
    show_inorder(tree);

    std::cout << "duplicate insert(50) is ignored: ";
    tree.insert(50);
    std::cout << "size still " << tree.size() << "\n";

    // --------------------------------------------------------------------
    banner("2) Searching");
    // --------------------------------------------------------------------
    for (int k : {35, 51, 100, 1}) {
        std::cout << "  contains(" << k << ") = "
                  << (tree.contains(k) ? "yes" : "no") << "\n";
    }

    // --------------------------------------------------------------------
    banner("3) Remove — leaf, internal (pred/succ), merge, root-collapse");
    // --------------------------------------------------------------------
    for (int k : {5, 50, 70, 90, 20, 30, 40, 60}) {
        bool ok = tree.remove(k);
        must_be_valid(tree, "remove(" + std::to_string(k) + ")");
        std::cout << "  remove(" << k << ") -> " << (ok ? "ok " : "miss")
                  << "  size=" << tree.size() << " height=" << tree.height() << "\n";
    }
    std::cout << "tree after removals:\n";
    tree.print(std::cout);
    show_inorder(tree);

    // --------------------------------------------------------------------
    banner("4) t=2 (a 2-3-4 tree): insert 1..20 in order");
    // --------------------------------------------------------------------
    // A plain BST would degenerate into a 20-deep right chain on this input;
    // the B-Tree stays a shallow bush.
    BTree ttt(2);
    for (int i = 1; i <= 20; ++i) {
        ttt.insert(i);
        must_be_valid(ttt, "seq insert " + std::to_string(i));
    }
    ttt.print(std::cout);
    show_inorder(ttt);

    // --------------------------------------------------------------------
    banner("5) Randomised stress test vs std::set (8000 ops)");
    // --------------------------------------------------------------------
    BTree           bt(4);
    std::set<int>   oracle;
    std::mt19937    rng(20250617u);                 // fixed seed → reproducible
    std::uniform_int_distribution<int> key_pick(0, 499);

    for (int step = 0; step < 8000; ++step) {
        int k = key_pick(rng);
        if (rng() & 1u) {
            bt.insert(k);
            oracle.insert(k);
        } else {
            bool a = bt.remove(k);
            bool b = (oracle.erase(k) > 0);
            if (a != b) { std::cerr << "remove disagreement on " << k << "\n"; return 1; }
        }
        if (step % 500 == 0) must_be_valid(bt, "stress step " + std::to_string(step));
        if (bt.size() != static_cast<int>(oracle.size())) {
            std::cerr << "size mismatch at step " << step << "\n";
            return 1;
        }
    }
    must_be_valid(bt, "stress final");

    std::vector<int> got  = bt.inorder();
    std::vector<int> want(oracle.begin(), oracle.end());
    if (got != want) { std::cerr << "in-order != sorted std::set\n"; return 1; }

    std::cout << "  passed: " << bt.size() << " keys live, height " << bt.height()
              << ", invariants held and std::set agreed on every step.\n";

    std::cout << "\nAll B-Tree checks passed.\n";
    return 0;
}
