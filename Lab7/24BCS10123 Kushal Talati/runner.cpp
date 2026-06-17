// Lab 7 — driver for the shunting-yard SQL engine
// 24BCS10123  Kushal Talati
//
// Section (a) traces the shunting-yard turning an infix WHERE clause into
// postfix. Sections (b)-(e) run SELECT statements end-to-end through
// kt::QueryEngine against an in-memory `employees` relation, asserting the
// result after each one. Every check prints [ok]/[XX]; the first failure
// aborts with a non-zero exit code.

#include "mini_sql.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

void section(const std::string& s) { std::cout << "\n>>> " << s << "\n"; }

void expect(bool ok, const std::string& what) {
    std::cout << (ok ? "  [ok] " : "  [XX] ") << what << "\n";
    if (!ok) std::exit(1);
}

// The demo relation, built once.
kt::Relation employees() {
    kt::Relation r;
    r.name    = "employees";
    r.heading = {"id", "name", "dept", "age", "salary"};
    auto row = [&](long long id, const std::string& nm, const std::string& dept,
                   long long age, long long salary) {
        r.tuples.push_back(kt::Tuple{{kt::Cell{id}, kt::Cell{nm}, kt::Cell{dept},
                                      kt::Cell{age}, kt::Cell{salary}}});
    };
    row(1, "Aarav",  "Engineering", 29,  95000);
    row(2, "Diya",   "Sales",       24,  55000);
    row(3, "Rohan",  "Engineering", 35, 120000);
    row(4, "Meera",  "Marketing",   41,  88000);
    row(5, "Kabir",  "Sales",       28,  61000);
    row(6, "Ishita", "Engineering", 31,  99000);
    row(7, "Vivaan", "Marketing",   23,  47000);
    row(8, "Anaya",  "Engineering", 38, 134000);
    return r;
}

const std::string& text_at(const kt::Relation& r, std::size_t row, std::size_t col) {
    return std::get<std::string>(r.tuples[row].fields[col]);
}
long long int_at(const kt::Relation& r, std::size_t row, std::size_t col) {
    return std::get<long long>(r.tuples[row].fields[col]);
}

}  // namespace

int main() {
    const kt::QueryEngine db;

    // ----------------------------------------------------------------------
    section("(a) Shunting-yard: infix WHERE -> postfix (RPN)");
    // ----------------------------------------------------------------------
    const std::vector<std::string> clauses = {
        "age > 25 AND (dept = 'Sales' OR salary >= 100000)",
        "NOT age < 30 AND salary > 90000",
        "id = 1 OR id = 3 OR id = 8",
    };
    for (const std::string& clause : clauses) {
        const auto postfix = kt::QueryEngine::to_postfix(db.lex(clause));
        std::cout << "  infix   : " << clause << "\n";
        std::cout << "  postfix : " << kt::QueryEngine::postfix_text(postfix) << "\n\n";
    }
    expect(kt::QueryEngine::postfix_text(kt::QueryEngine::to_postfix(db.lex(clauses[0])))
               == "age 25 > dept 'Sales' = salary 100000 >= OR AND",
           "AND/OR/parens shunting-yard produces the expected RPN");

    const kt::Relation emp = employees();

    // ----------------------------------------------------------------------
    section("(b) SELECT * FROM employees");
    // ----------------------------------------------------------------------
    {
        const kt::Relation r = db.run("SELECT * FROM employees", emp);
        kt::QueryEngine::render(r, std::cout);
        expect(r.tuples.size() == 8, "SELECT * returns all 8 rows");
        expect(r.heading.size() == 5, "all 5 columns are projected");
    }

    // ----------------------------------------------------------------------
    section("(c) projection + WHERE (AND/OR/parens) + ORDER BY salary DESC");
    // ----------------------------------------------------------------------
    {
        const std::string sql =
            "SELECT name, dept, salary FROM employees "
            "WHERE age > 25 AND (dept = 'Sales' OR salary >= 100000) "
            "ORDER BY salary DESC";
        std::cout << "  SQL: " << sql << "\n";
        const kt::Relation r = db.run(sql, emp);
        kt::QueryEngine::render(r, std::cout);
        // Engineering high earners Rohan(120000) & Anaya(134000); Sales Kabir(61000, age 28).
        // salary DESC -> Anaya, Rohan, Kabir.
        expect(r.tuples.size() == 3, "exactly three rows satisfy the predicate");
        expect(text_at(r, 0, 0) == "Anaya", "highest salary is listed first");
        expect(text_at(r, 2, 0) == "Kabir", "lowest of the three is listed last");
    }

    // ----------------------------------------------------------------------
    section("(d) NOT + text comparison + ORDER BY age ASC + LIMIT 2");
    // ----------------------------------------------------------------------
    {
        const std::string sql =
            "SELECT name, age FROM employees "
            "WHERE NOT dept = 'Engineering' "
            "ORDER BY age ASC LIMIT 2";
        std::cout << "  SQL: " << sql << "\n";
        const kt::Relation r = db.run(sql, emp);
        kt::QueryEngine::render(r, std::cout);
        // Non-engineering: Diya(24), Meera(41), Kabir(28), Vivaan(23) -> ASC, top 2.
        expect(r.tuples.size() == 2, "LIMIT caps the result to two rows");
        expect(text_at(r, 0, 0) == "Vivaan", "youngest non-engineer comes first");
        expect(int_at(r, 1, 1) == 24, "second row is age 24");
    }

    // ----------------------------------------------------------------------
    section("(e) Compile a predicate once, evaluate it per row");
    // ----------------------------------------------------------------------
    {
        const auto pred = kt::QueryEngine::to_postfix(
            db.lex("dept = 'Engineering' AND salary >= 100000"));
        std::cout << "  predicate postfix: " << kt::QueryEngine::postfix_text(pred) << "\n";
        std::size_t hits = 0;
        for (const kt::Tuple& row : emp.tuples)
            if (kt::QueryEngine::matches(pred, emp, row)) {
                ++hits;
                std::cout << "    match: " << std::get<std::string>(row.fields[1]) << "\n";
            }
        expect(hits == 2, "Rohan and Anaya are the two high-earning engineers");
    }

    std::cout << "\nAll mini-SQL engine checks passed.\n";
    return 0;
}
