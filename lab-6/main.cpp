#include "b_tree.hpp"

#include <cassert>
#include <cstdlib>
#include <iostream>
#include <map>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace demo {

template <typename K, typename V>
void validate_tree(const adbms::b_tree<K, V>& tree,
                   const std::string& operation) {
    const auto problem = tree.verify();

    if (!problem.empty()) {
        std::cerr
            << "INVARIANT FAIL after "
            << operation
            << ": "
            << problem
            << '\n';

        std::exit(EXIT_FAILURE);
    }
}

void print_section(const std::string& title) {
    std::cout << "\n=== " << title << " ===\n";
}

}  // namespace demo

int main() {
    using MovieTree = adbms::b_tree<int, std::string>;

    demo::print_section("1) Small tree (t=3) — 10 (key, value) inserts");

    MovieTree movies(3);

    const std::vector<std::pair<int, std::string>> entries{
        {41, "Inception"},
        {17, "Whiplash"},
        {76, "Parasite"},
        {9,  "Memento"},
        {23, "Dune"},
        {58, "Oppenheimer"},
        {88, "Joker"},
        {3,  "La La Land"},
        {12, "Spirited Away"},
        {30, "Goodfellas"}
    };

    for (const auto& entry : entries) {
        movies.insert(entry.first, entry.second);

        demo::validate_tree(
            movies,
            "insert(" + std::to_string(entry.first) + ")"
        );
    }

    std::cout << "size = " << movies.size() << '\n';

    movies.print();

    std::cout << "in-order: ";

    movies.in_order(
        [](int key, const std::string& value) {
            std::cout << key << '=' << value << "  ";
        }
    );

    std::cout << '\n';

    demo::print_section("2) Lookups + overwrite");

    const std::vector<int> lookup_keys{17, 23, 99};

    for (int key : lookup_keys) {
        std::cout
            << "  contains("
            << key
            << ") = "
            << (movies.contains(key) ? "yes" : "no")
            << '\n';
    }

    std::cout << "  at(58) = " << movies.at(58) << '\n';

    movies.insert(58, "Oppenheimer (IMAX)");

    std::cout
        << "  after overwrite — at(58) = "
        << movies.at(58)
        << "  size = "
        << movies.size()
        << '\n';

    try {
        movies.at(7);
    }
    catch (const std::out_of_range& ex) {
        std::cout
            << "  at(7) threw as expected: "
            << ex.what()
            << '\n';
    }

    demo::print_section("3) Erase — leaf, internal, root-collapse");

    const std::vector<int> removals{3, 17, 41, 88, 23};

    for (int key : removals) {
        const bool removed = movies.erase(key);

        demo::validate_tree(
            movies,
            "erase(" + std::to_string(key) + ")"
        );

        std::cout
            << "  erase("
            << key
            << ") -> "
            << (removed ? "ok" : "miss")
            << "  size="
            << movies.size()
            << '\n';
    }

    std::cout << "tree after erases:\n";
    movies.print();

    demo::print_section(
        "4) Sequential insert into a t=2 tree (worst case for a plain BST)"
    );

    adbms::b_tree<int, int> tree234(2);

    for (int value = 1; value <= 16; ++value) {
        tree234.insert(value, value * value);

        demo::validate_tree(
            tree234,
            "seq insert " + std::to_string(value)
        );
    }

    tree234.print();

    std::cout << "in-order: ";

    tree234.in_order(
        [](int key, int) {
            std::cout << key << ' ';
        }
    );

    std::cout << '\n';

    demo::print_section(
        "5) Height bound — t=50, insert 50 000 random keys"
    );

    adbms::b_tree<int, int> large_tree(50);

    std::mt19937 generator(0xB1A1E);

    std::vector<int> inserted_keys;
    inserted_keys.reserve(50000);

    for (int i = 0; i < 50000; ++i) {
        const int key =
            static_cast<int>(generator() & 0x7fffffff);

        large_tree.insert(key, key);
        inserted_keys.push_back(key);
    }

    demo::validate_tree(large_tree, "50k random inserts");

    int successful_hits = 0;

    for (int i = 0; i < 1000; ++i) {
        if (large_tree.contains(inserted_keys[i])) {
            ++successful_hits;
        }
    }

    std::cout
        << "  size = "
        << large_tree.size()
        << "  spot-check hits on first 1000 inserted keys = "
        << successful_hits
        << "/1000\n";

    demo::print_section(
        "6) Randomised stress test — 5 000 ops vs std::map oracle"
    );

    adbms::b_tree<int, int> stress_tree(4);
    std::map<int, int> reference;

    std::mt19937 stress_rng(0xA110C);
    std::uniform_int_distribution<int> distribution(0, 299);

    for (int iteration = 0; iteration < 5000; ++iteration) {
        const int key = distribution(stress_rng);

        if (stress_rng() & 1U) {
            stress_tree.insert(key, iteration);
            reference[key] = iteration;
        } else {
            stress_tree.erase(key);
            reference.erase(key);
        }

        if (iteration % 250 == 0) {
            demo::validate_tree(
                stress_tree,
                "stress " + std::to_string(iteration)
            );
        }

        assert(stress_tree.size() == reference.size());
        assert(
            stress_tree.contains(key) ==
            (reference.count(key) != 0)
        );
    }

    demo::validate_tree(stress_tree, "stress final");

    std::vector<int> actual_order;
    std::vector<int> expected_order;

    stress_tree.in_order(
        [&](int key, int) {
            actual_order.push_back(key);
        }
    );

    for (const auto& [key, value] : reference) {
        (void)value;
        expected_order.push_back(key);
    }

    assert(actual_order == expected_order);

    std::cout
        << "  passed: "
        << stress_tree.size()
        << " keys live, invariants hold, oracle agreed on every step.\n";

    std::cout << "\nAll B-Tree checks passed.\n";

    return EXIT_SUCCESS;
}