// main.cc — ADBMS Lab 7 demo / 24BCS10115 Gauri Shukla
//
// Part 1: shows the shunting-yard algorithm turning an infix WHERE clause into
//         postfix (RPN).
// Part 2: runs several SELECT queries through the full engine (tokenize ->
//         parse -> execute) against an in-memory `employees` table, and checks
//         the results with assertions.

#include "sql_engine.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

using namespace sqlmini;

namespace {

void banner(const std::string& s) { std::cout << "\n=== " << s << " ===\n"; }

void check(bool cond, const std::string& what) {
    if (!cond) { std::cerr << "CHECK FAILED: " << what << "\n"; std::exit(1); }
}

// Build the demo table once.
Table make_employees() {
    Table t;
    t.name    = "employees";
    t.columns = {"id", "name", "dept", "age", "salary"};
    auto add = [&](long long id, const std::string& nm, const std::string& dp,
                   long long age, long long sal) {
        t.rows.push_back(Row{{Value{id}, Value{nm}, Value{dp}, Value{age}, Value{sal}}});
    };
    add(1, "Aarav",  "Engineering", 29, 95000);
    add(2, "Diya",   "Sales",       24, 55000);
    add(3, "Rohan",  "Engineering", 35, 120000);
    add(4, "Meera",  "Marketing",   41, 88000);
    add(5, "Kabir",  "Sales",       28, 61000);
    add(6, "Ishita", "Engineering", 31, 99000);
    add(7, "Vivaan", "Marketing",   23, 47000);
    add(8, "Anaya",  "Engineering", 38, 134000);
    return t;
}

// Count rows in a result set.
std::size_t nrows(const Table& t) { return t.rows.size(); }

}  // namespace

int main() {
    // --------------------------------------------------------------------
    banner("Part 1) Shunting-Yard: infix WHERE -> postfix (RPN)");
    // --------------------------------------------------------------------
    const std::vector<std::string> exprs = {
        "age > 25 AND (dept = 'Sales' OR salary >= 100000)",
        "NOT age < 30 AND salary > 90000",
        "id = 1 OR id = 3 OR id = 8",
    };
    for (const std::string& e : exprs) {
        std::vector<Token> rpn = shunting_yard(tokenize(e));
        std::cout << "  infix : " << e << "\n";
        std::cout << "  RPN   : " << rpn_to_string(rpn) << "\n\n";
    }

    Table emp = make_employees();

    // --------------------------------------------------------------------
    banner("Part 2a) SELECT * FROM employees");
    // --------------------------------------------------------------------
    {
        Table r = execute(parse_select("SELECT * FROM employees"), emp);
        print_table(r, std::cout);
        check(nrows(r) == 8, "select * returns all rows");
    }

    // --------------------------------------------------------------------
    banner("Part 2b) WHERE with AND / OR / parentheses + projection");
    // --------------------------------------------------------------------
    {
        const std::string sql =
            "SELECT name, dept, salary FROM employees "
            "WHERE age > 25 AND (dept = 'Sales' OR salary >= 100000) "
            "ORDER BY salary DESC";
        std::cout << "SQL: " << sql << "\n";
        Table r = execute(parse_select(sql), emp);
        print_table(r, std::cout);
        // Rohan(120000), Anaya(134000) from Eng; Kabir(61000) from Sales (age 28).
        // Ordered by salary DESC -> Anaya, Rohan, Kabir.
        check(nrows(r) == 3, "three rows match");
        check(std::get<std::string>(r.rows[0].cells[0]) == "Anaya", "top earner first");
        check(std::get<std::string>(r.rows[2].cells[0]) == "Kabir", "lowest of the three last");
    }

    // --------------------------------------------------------------------
    banner("Part 2c) NOT, string comparison, and LIMIT");
    // --------------------------------------------------------------------
    {
        const std::string sql =
            "SELECT name, age FROM employees "
            "WHERE NOT dept = 'Engineering' "
            "ORDER BY age ASC LIMIT 2";
        std::cout << "SQL: " << sql << "\n";
        Table r = execute(parse_select(sql), emp);
        print_table(r, std::cout);
        // Non-engineering: Diya(24), Meera(41), Kabir(28), Vivaan(23).
        // age ASC, limit 2 -> Vivaan(23), Diya(24).
        check(nrows(r) == 2, "limit caps to 2 rows");
        check(std::get<std::string>(r.rows[0].cells[0]) == "Vivaan", "youngest non-eng first");
        check(std::get<long long>(r.rows[1].cells[1]) == 24, "second is age 24");
    }

    // --------------------------------------------------------------------
    banner("Part 2d) Per-row predicate evaluation via eval_rpn");
    // --------------------------------------------------------------------
    {
        std::vector<Token> pred =
            shunting_yard(tokenize("dept = 'Engineering' AND salary >= 100000"));
        std::cout << "predicate RPN: " << rpn_to_string(pred) << "\n";
        std::size_t hits = 0;
        for (const Row& row : emp.rows)
            if (eval_rpn(pred, emp, row)) {
                ++hits;
                std::cout << "  match: " << std::get<std::string>(row.cells[1]) << "\n";
            }
        check(hits == 2, "Rohan and Anaya match");   // 120000 and 134000
    }

    std::cout << "\nAll SQL engine checks passed.\n";
    return 0;
}
