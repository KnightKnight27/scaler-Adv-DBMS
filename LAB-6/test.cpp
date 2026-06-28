// Lab 6 - B-Tree tests (directed + 200-key randomized)
// Bhavya Jain (23BCS10088) <Bhavya.23bcs10088@sst.scaler.com>
// Exits non-zero on failure; "All tests passed" on success.
#include <iostream>
#include <vector>
#include <algorithm>
#include <cassert>
#include "btree.h"
#include <random>
#include <set>

int main()
{
    BalancedTree tree(3);

    std::vector<int> ins = {10, 20, 5, 6, 12, 30, 7, 17};
    for (int v : ins)
        tree.insert(v);

    for (int v : ins)
        if (!tree.search(v))
        {
            std::cerr << "FAIL: inserted " << v << " not found\n";
            return 2;
        }
    if (tree.search(999))
    {
        std::cerr << "FAIL: found non-inserted key\n";
        return 3;
    }

    // remove some keys and verify
    tree.remove(6);
    if (tree.search(6))
    {
        std::cerr << "FAIL: key 6 should be removed\n";
        return 4;
    }

    tree.remove(13); // remove absent key should be safe

    tree.remove(7);
    if (tree.search(7))
    {
        std::cerr << "FAIL: key 7 should be removed\n";
        return 5;
    }

    // More thorough: random inserts and deletes, validate inorder is sorted
    BalancedTree rndTree(4);
    std::mt19937_64 rng(123456);
    std::uniform_int_distribution<int> dist(1, 1000);

    std::set<int> ref;
    for (int i = 0; i < 200; ++i)
    {
        int v;
        // ensure unique inserts (match std::set behavior)
        do
        {
            v = dist(rng);
        } while (!ref.insert(v).second);
        rndTree.insert(v);
    }

    // delete about half randomly
    std::vector<int> current(ref.begin(), ref.end());
    for (size_t i = 0; i < current.size(); i += 2)
    {
        rndTree.remove(current[i]);
        ref.erase(current[i]);
    }

    std::vector<int> out;
    rndTree.collect_inorder(out);

    std::vector<int> expected(ref.begin(), ref.end());
    if (out != expected)
    {
        std::cerr << "FAIL: inorder mismatch\n";
        std::cerr << "expected size=" << expected.size() << " out size=" << out.size() << "\n";
        size_t m = std::min(out.size(), expected.size());
        for (size_t i = 0; i < m; ++i)
            if (out[i] != expected[i])
            {
                std::cerr << "first diff at " << i << ": out=" << out[i] << " exp=" << expected[i] << "\n";
                break;
            }
        std::cerr << "expected first 20:\n";
        for (size_t i = 0; i < expected.size() && i < 20; ++i)
            std::cerr << expected[i] << " ";
        std::cerr << "\nactual first 20:\n";
        for (size_t i = 0; i < out.size() && i < 20; ++i)
            std::cerr << out[i] << " ";
        std::cerr << "\n";
        return 6;
    }

    std::cout << "All tests passed\n";
    return 0;
}
