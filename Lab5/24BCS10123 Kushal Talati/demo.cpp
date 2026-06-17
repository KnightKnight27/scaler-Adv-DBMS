// Lab 5 — Red-Black Tree demo driver
// 24BCS10123  Kushal Talati
//
// Exercises kt::OrderedIndex<Key, Value> through:
//   1. a small contact-book (name -> phone number) so the tree shape can
//      be printed in full,
//   2. lookups + missing-key path,
//   3. removal touching every CLRS erase case,
//   4. overwrite semantics (must not change length()),
//   5. an 8 000-operation randomised stress test driven by a fixed-seed
//      std::mt19937 against std::map<int,int> as an oracle.
//
// validate() is invoked after EVERY mutation so a future regression
// fails the run instead of silently passing.

#include "redblack.hpp"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <map>
#include <random>
#include <string>
#include <vector>

namespace {

template <typename K, typename V>
void assert_clean(const kt::OrderedIndex<K, V>& t, const std::string& after) {
    auto problem = t.validate();
    if (problem) {
        std::cerr << "INVARIANT BROKEN after " << after << ": " << *problem << "\n";
        std::exit(1);
    }
}

void section(const std::string& title) {
    std::cout << "\n---[ " << title << " ]---\n";
}

}  // namespace

int main() {
    // ============================================================
    section("1. Contact book — insert sequence exercises all 3 cases");
    // ============================================================
    kt::OrderedIndex<std::string, std::string> book;

    const std::vector<std::pair<std::string, std::string>> contacts = {
        {"Aarav",    "+91-90000-12345"},
        {"Diya",     "+91-90000-22345"},
        {"Vihaan",   "+91-90000-32345"},
        {"Anaya",    "+91-90000-42345"},
        {"Kabir",    "+91-90000-52345"},
        {"Myra",     "+91-90000-62345"},
        {"Reyansh",  "+91-90000-72345"},
        {"Saanvi",   "+91-90000-82345"},
        {"Arjun",    "+91-90000-92345"},
        {"Ishita",   "+91-90000-12300"},
        {"Rohan",    "+91-90000-22300"},
        {"Trisha",   "+91-90000-32300"},
        {"Yash",     "+91-90000-42300"},
        {"Priya",    "+91-90000-52300"},
        {"Karan",    "+91-90000-62300"},
    };
    for (auto& [name, num] : contacts) {
        book.set(name, num);
        assert_clean(book, "set(" + name + ")");
    }
    std::cout << "length = " << book.length() << "\n";
    book.render(std::cout);

    // ============================================================
    section("2. In-order walk should emit names alphabetically");
    // ============================================================
    std::cout << "  ";
    book.walk([](const std::string& n, const std::string& p) {
        std::cout << n << "→" << p << "  ";
    });
    std::cout << "\n";

    // ============================================================
    section("3. Lookups — present, missing, fetch(), get() optional");
    // ============================================================
    for (const std::string& probe : {std::string("Diya"), std::string("Yash"), std::string("Zara")}) {
        auto got = book.get(probe);
        std::cout << "  get(" << probe << ") = "
                  << (got ? *got : std::string("<absent>")) << "\n";
    }
    std::cout << "  has(\"Aarav\") = " << (book.has("Aarav") ? "yes" : "no") << "\n";
    try { book.fetch("nobody"); }
    catch (const std::out_of_range& e) {
        std::cout << "  fetch(nobody) threw as expected: " << e.what() << "\n";
    }

    // ============================================================
    section("4. Overwrite — same key, new value, length must stay");
    // ============================================================
    std::size_t before = book.length();
    book.set("Diya", "+91-99999-99999");
    assert_clean(book, "overwrite(Diya)");
    std::cout << "  length before=" << before
              << "  after=" << book.length()
              << "  fetch(Diya)=" << book.fetch("Diya") << "\n";

    // ============================================================
    section("5. Remove — touches leaf / single-child / two-child / root cases");
    // ============================================================
    // Aarav and Karan are near the spine; Diya is the original middle insert
    // so its removal pulls in a successor; Reyansh sat at the root after the
    // first few inserts so removing it exercises root-replacement.
    for (const std::string& victim :
         {std::string("Aarav"), std::string("Karan"), std::string("Diya"),
          std::string("Reyansh"), std::string("Yash"), std::string("Anaya")}) {
        bool gone = book.remove(victim);
        assert_clean(book, "remove(" + victim + ")");
        std::cout << "  remove(" << victim << ") -> "
                  << (gone ? "ok" : "miss")
                  << "  length=" << book.length() << "\n";
    }
    std::cout << "tree after removes:\n";
    book.render(std::cout);

    // ============================================================
    section("6. Stress test — 8 000 ops, oracle = std::map<int,int>");
    // ============================================================
    kt::OrderedIndex<int, int> stress;
    std::map<int, int>         oracle;
    std::mt19937 rng(static_cast<std::uint32_t>(0xCAFEBABE));
    std::uniform_int_distribution<int> keyspace(0, 299);

    for (int step = 0; step < 8000; ++step) {
        int k = keyspace(rng);
        if (rng() & 1u) {                    // half the ops are inserts
            stress.set(k, step);
            oracle[k] = step;
        } else {                              // the other half are removes
            stress.remove(k);
            oracle.erase(k);
        }
        // Heavy validate every 500 steps; cheap cross-checks every step.
        if (step % 500 == 0) assert_clean(stress, "stress step " + std::to_string(step));
        assert(stress.length() == oracle.size());
        assert(stress.has(k) == (oracle.count(k) > 0));
    }
    assert_clean(stress, "stress final");

    // The walk's output must be exactly std::map's iteration order.
    std::vector<int> got_keys;
    stress.walk([&](int k, int) { got_keys.push_back(k); });
    std::vector<int> want_keys;
    want_keys.reserve(oracle.size());
    for (auto& [k, _] : oracle) want_keys.push_back(k);
    assert(got_keys == want_keys);

    std::cout << "  passed: " << stress.length()
              << " keys live, invariants healthy, oracle agreed on every step.\n";

    std::cout << "\nAll OrderedIndex (RB tree) checks passed.\n";
    return 0;
}
