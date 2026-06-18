// Lab 5 - Red-Black Tree demo / smoke test
// Rama Krishnan (24BCS10087) <rama.24bcs10087@sst.scaler.com>

#include "RedBlackTree.h"

#include <algorithm>
#include <cassert>
#include <iostream>
#include <random>
#include <vector>

static void expect(bool cond, const char* msg) {
    if (!cond) {
        std::cerr << "FAIL: " << msg << "\n";
        std::exit(1);
    }
}

static void demoInsertSequence() {
    std::cout << "== insert sequence ==\n";
    RedBlackTree t;
    for (int v : {41, 38, 31, 12, 19, 8, 22, 5, 27, 17}) {
        t.insert(v);
        expect(t.validate(), "RB invariants after insert");
    }
    t.print();

    auto sorted = t.inorder();
    std::cout << "inorder:";
    for (int v : sorted) std::cout << ' ' << v;
    std::cout << "\n";
    expect(std::is_sorted(sorted.begin(), sorted.end()), "inorder must be sorted");
    expect(t.size() == 10, "size after 10 inserts");
}

static void demoEraseSequence() {
    std::cout << "\n== erase sequence ==\n";
    RedBlackTree t;
    for (int v : {10, 20, 30, 40, 50, 25, 35, 45, 5, 15}) t.insert(v);
    expect(t.size() == 10, "10 nodes inserted");

    for (int v : {25, 10, 50, 5, 35}) {
        expect(t.erase(v), "erase existing key");
        expect(t.validate(), "RB invariants after erase");
    }
    expect(!t.erase(999), "erase missing key returns false");

    auto sorted = t.inorder();
    std::cout << "remaining:";
    for (int v : sorted) std::cout << ' ' << v;
    std::cout << "\n";
    expect(sorted == std::vector<int>({15, 20, 30, 40, 45}), "expected remaining set");
    expect(t.size() == 5, "size after 5 erases");
}

static void stressTest() {
    std::cout << "\n== randomized stress (10k ops) ==\n";
    RedBlackTree t;
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> dist(0, 999);
    std::vector<int> mirror;

    for (int i = 0; i < 10000; ++i) {
        int op = rng() % 3;
        int v  = dist(rng);
        if (op == 0) {
            t.insert(v);
            mirror.push_back(v);
        } else if (op == 1) {
            bool inMirror = std::find(mirror.begin(), mirror.end(), v) != mirror.end();
            expect(t.find(v) == inMirror, "find vs mirror");
        } else {
            auto it = std::find(mirror.begin(), mirror.end(), v);
            bool present = it != mirror.end();
            bool erased  = t.erase(v);
            expect(erased == present, "erase return value vs mirror");
            if (present) mirror.erase(it);
        }
        if ((i & 0xff) == 0) expect(t.validate(), "RB invariants under stress");
    }
    std::sort(mirror.begin(), mirror.end());
    expect(t.inorder() == mirror, "tree matches mirror after stress");
    std::cout << "ok (final size " << t.size() << ")\n";
}

int main() {
    demoInsertSequence();
    demoEraseSequence();
    stressTest();
    std::cout << "\nAll RB-Tree tests passed.\n";
    return 0;
}
