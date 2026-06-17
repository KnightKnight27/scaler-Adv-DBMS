// main.cpp — ADBMS Lab 7 demo / 24BCS10288 Vibhuti Bhatnagar
//
// Part 1 — show the shunting-yard algorithm converting an infix WHERE
//          expression into postfix (RPN).
// Part 2 — run real SELECT statements end-to-end (lex -> parse -> run)
//          against an in-memory `books` table, with each result checked by
//          assertion.

#include "sql_engine.hpp"

#include <cassert>
#include <iostream>
#include <string>
#include <vector>

using adbms::sql::Cell;
using adbms::sql::Relation;
using adbms::sql::Row;

namespace {

void section(const std::string& h) { std::cout << "\n=== " << h << " ===\n"; }

void must(bool cond, const std::string& msg) {
    if (!cond) { std::cerr << "FAIL: " << msg << "\n"; std::exit(1); }
}

// In-memory table used by every Part-2 query.
//   id (int) | title (str) | author (str) | year (int) | pages (int) | genre (str)
Relation make_books() {
    Relation t;
    t.name    = "books";
    t.columns = {"id", "title", "author", "year", "pages", "genre"};
    auto add = [&](std::int64_t id, const std::string& title,
                   const std::string& author, std::int64_t year,
                   std::int64_t pages, const std::string& genre) {
        t.rows.push_back(Row{
            Cell{id}, Cell{title}, Cell{author}, Cell{year}, Cell{pages}, Cell{genre}
        });
    };
    add(1,  "1984",                       "George Orwell",       1949, 328, "Fiction");
    add(2,  "Animal Farm",                "George Orwell",       1945, 112, "Fiction");
    add(3,  "Sapiens",                    "Yuval Noah Harari",   2011, 443, "History");
    add(4,  "Homo Deus",                  "Yuval Noah Harari",   2015, 450, "History");
    add(5,  "The Pragmatic Programmer",   "Andrew Hunt",         1999, 320, "Tech");
    add(6,  "Clean Code",                 "Robert Martin",       2008, 464, "Tech");
    add(7,  "Designing Data Intensive Applications",
                                          "Martin Kleppmann",    2017, 616, "Tech");
    add(8,  "Atomic Habits",              "James Clear",         2018, 320, "Self-help");
    add(9,  "Deep Work",                  "Cal Newport",         2016, 304, "Self-help");
    add(10, "The Lean Startup",           "Eric Ries",           2011, 336, "Business");
    add(11, "Zero to One",                "Peter Thiel",         2014, 224, "Business");
    add(12, "Don't Make Me Think",        "Steve Krug",          2014, 216, "Tech");
    return t;
}

}  // namespace

int main() {
    using namespace adbms::sql;

    // ------------------------------------------------------------------
    section("Part 1) Shunting-Yard: infix WHERE -> postfix (RPN)");
    // ------------------------------------------------------------------
    const std::vector<std::string> exprs = {
        "year >= 2000 AND (genre = 'Tech' OR pages > 400)",
        "NOT genre = 'Fiction' AND year < 2010",
        "title LIKE 'The %' OR author = 'George Orwell'",
        "id = 1 OR id = 3 OR id = 5 OR id = 7",
    };
    for (const std::string& e : exprs) {
        auto rpn = to_rpn(lex(e));
        std::cout << "  infix : " << e << "\n";
        std::cout << "  RPN   : " << rpn_trace(rpn) << "\n\n";
    }

    Relation books = make_books();

    // ------------------------------------------------------------------
    section("Part 2a) SELECT * FROM books — projects every column");
    // ------------------------------------------------------------------
    {
        Relation r = run(parse("SELECT * FROM books"), books);
        print(r);
        must(r.rows.size() == 12, "select * returns all 12 books");
    }

    // ------------------------------------------------------------------
    section("Part 2b) WHERE with AND / OR / parentheses + projection + ORDER BY DESC");
    // ------------------------------------------------------------------
    {
        const std::string sql =
            "SELECT title, year, pages FROM books "
            "WHERE year >= 2000 AND (genre = 'Tech' OR pages > 400) "
            "ORDER BY pages DESC";
        std::cout << "SQL: " << sql << "\n";
        Relation r = run(parse(sql), books);
        print(r);
        // Matches:
        //   Sapiens (2011, 443, History) — fails genre, passes pages>400 -> in
        //   Homo Deus (2015, 450, History) — pages>400 -> in
        //   Clean Code (2008, 464, Tech) -> in
        //   DDIA (2017, 616, Tech) -> in
        //   Don't Make Me Think (2014, 216, Tech) -> in (Tech, no pages req.)
        //   Pragmatic Programmer is 1999 -> excluded by year
        must(r.rows.size() == 5, "five books match the AND/OR filter");
        must(std::get<std::string>(r.rows[0][0]) == "Designing Data Intensive Applications",
             "top result by pages DESC is DDIA (616 pp)");
    }

    // ------------------------------------------------------------------
    section("Part 2c) LIKE wildcard + LIMIT");
    // ------------------------------------------------------------------
    {
        const std::string sql =
            "SELECT title, author FROM books "
            "WHERE title LIKE 'The %' "
            "ORDER BY title ASC LIMIT 2";
        std::cout << "SQL: " << sql << "\n";
        Relation r = run(parse(sql), books);
        print(r);
        // "The Pragmatic Programmer", "The Lean Startup"  (and would be Zero to One does not start with 'The')
        // sorted ASC -> "The Lean Startup", "The Pragmatic Programmer", limit 2.
        must(r.rows.size() == 2, "LIMIT 2 caps the result");
        must(std::get<std::string>(r.rows[0][0]) == "The Lean Startup",
             "ASC order picks 'The Lean Startup' first");
    }

    // ------------------------------------------------------------------
    section("Part 2d) NOT + escaped single quote ('') in literal");
    // ------------------------------------------------------------------
    {
        // The literal 'Don''t Make Me Think' uses '' as an escape for a single
        // quote inside a string — same rule SQLite / PostgreSQL use.
        const std::string sql =
            "SELECT title, year FROM books "
            "WHERE NOT genre = 'Tech' AND year >= 2010 "
            "ORDER BY year ASC";
        std::cout << "SQL: " << sql << "\n";
        Relation r = run(parse(sql), books);
        print(r);
        // Non-tech, year >= 2010 :
        //   Sapiens 2011, Homo Deus 2015, Atomic Habits 2018, Deep Work 2016,
        //   The Lean Startup 2011, Zero to One 2014  → 6 rows
        must(r.rows.size() == 6, "six non-tech books from 2010+");
        must(std::get<std::int64_t>(r.rows[0][1]) == 2011, "earliest first");
    }

    // ------------------------------------------------------------------
    section("Part 2dd) '' escape for a single quote inside a SQL literal");
    // ------------------------------------------------------------------
    {
        // SQL convention: a single quote inside a string literal is doubled.
        // The lexer recognises '' and emits the literal as Don't.
        const std::string sql =
            "SELECT title, pages FROM books WHERE title = 'Don''t Make Me Think'";
        std::cout << "SQL: " << sql << "\n";
        Relation r = run(parse(sql), books);
        print(r);
        must(r.rows.size() == 1, "exactly one book matches the escaped literal");
        must(std::get<std::string>(r.rows[0][0]) == "Don't Make Me Think",
             "lexer decoded the '' into a single quote");
    }

    // ------------------------------------------------------------------
    section("Part 2e) COUNT(*) aggregation");
    // ------------------------------------------------------------------
    {
        Relation r = run(parse("SELECT COUNT(*) FROM books WHERE genre = 'Tech'"), books);
        print(r);
        must(r.rows.size() == 1 && std::get<std::int64_t>(r.rows[0][0]) == 4,
             "four Tech books");
    }

    // ------------------------------------------------------------------
    section("Part 2f) Direct per-row evaluation via run_rpn()");
    // ------------------------------------------------------------------
    {
        auto rpn = to_rpn(lex("author = 'George Orwell' OR pages < 250"));
        std::cout << "predicate RPN: " << rpn_trace(rpn) << "\n";
        std::size_t hits = 0;
        for (const Row& row : books.rows) {
            if (run_rpn(rpn, books, row)) {
                ++hits;
                std::cout << "  match: " << std::get<std::string>(row[1]) << "\n";
            }
        }
        // Orwell (2 books) + pages < 250: Zero to One (224), Don't Make Me Think (216)
        // = 4 matches (Orwell's Animal Farm 112 hits both clauses but counts once).
        must(hits == 4, "four rows satisfy the predicate");
    }

    std::cout << "\nAll SQL engine checks passed.\n";
    return 0;
}
