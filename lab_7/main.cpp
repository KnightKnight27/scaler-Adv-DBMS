//
// main.cpp
// ---------------------------------------------------------------------------
// Driver: builds sample tables, runs demonstration queries, then runs a set of
// assert-based self-tests that prove the shunting-yard conversion (precedence,
// associativity, parentheses) and the end-to-end query results are correct.
// ---------------------------------------------------------------------------

#include "lexer.h"
#include "shunting_yard.h"
#include "sql.h"
#include "value.h"

#include <iostream>
#include <cassert>
#include <set>
#include <string>
#include <vector>

// --- sample data ------------------------------------------------------------

static Catalog buildCatalog() {
    Catalog cat;

    // employees(name, id, age, dept)
    Table employees;
    employees.push_back({{"name", std::string("Alice")}, {"id", 1LL}, {"age", 30LL}, {"dept", std::string("eng")}});
    employees.push_back({{"name", std::string("Bob")},   {"id", 2LL}, {"age", 22LL}, {"dept", std::string("sales")}});
    employees.push_back({{"name", std::string("Carol")}, {"id", 3LL}, {"age", 41LL}, {"dept", std::string("eng")}});
    employees.push_back({{"name", std::string("Dave")},  {"id", 4LL}, {"age", 17LL}, {"dept", std::string("eng")}});
    employees.push_back({{"name", std::string("Eve")},   {"id", 5LL}, {"age", 28LL}, {"dept", std::string("hr")}});
    cat["employees"] = employees;

    // numbers(n, label)
    Table numbers;
    numbers.push_back({{"n", 1LL},  {"label", std::string("one")}});
    numbers.push_back({{"n", 6LL},  {"label", std::string("six")}});
    numbers.push_back({{"n", 12LL}, {"label", std::string("twelve")}});
    numbers.push_back({{"n", 20LL}, {"label", std::string("twenty")}});
    cat["numbers"] = numbers;

    return cat;
}

// --- helpers for tests ------------------------------------------------------

// Turn an RPN token list into a compact space-separated string of texts,
// e.g. "age 18 > dept eng = ... AND OR".
static std::string rpnToString(const std::vector<Token> &rpn) {
    std::string out;
    for (size_t i = 0; i < rpn.size(); ++i) {
        if (i) out += ' ';
        out += rpn[i].text;
    }
    return out;
}

static std::vector<Token> infixOf(const std::string &whereExpr) {
    Lexer lex(whereExpr);
    return lex.tokenize();
}

// Collect the set of "name" values from an employees result set.
static std::set<std::string> namesOf(const Table &t) {
    std::set<std::string> s;
    for (const Row &r : t) {
        auto it = r.find("name");
        if (it != r.end()) s.insert(asStr(it->second));
    }
    return s;
}

// --- demonstration ----------------------------------------------------------

static void demo(const Catalog &cat, const std::string &sql) {
    std::cout << "SQL> " << sql << "\n";
    Table res = runQuery(sql, cat);
    printResult(res);
    std::cout << "\n";
}

int main() {
    Catalog cat = buildCatalog();

    std::cout << "============================================================\n";
    std::cout << " Lab 7 - Shunting-Yard Expression Evaluator + SQL SELECT\n";
    std::cout << "============================================================\n\n";

    // ---- demonstration queries ----
    demo(cat, "SELECT name FROM employees WHERE age > 18 AND (dept = 'eng' OR id < 3)");
    demo(cat, "SELECT * FROM employees WHERE NOT (age >= 30)");
    demo(cat, "SELECT name, age FROM employees WHERE age + 2 * 3 > 25");
    demo(cat, "SELECT name, dept FROM employees WHERE dept = 'eng' OR dept = 'hr'");
    demo(cat, "SELECT * FROM numbers WHERE n / 2 >= 3 AND n != 12");
    demo(cat, "SELECT * FROM employees");

    // ========================================================================
    //  SELF-TESTS
    // ========================================================================
    std::cout << "------------------------------------------------------------\n";
    std::cout << "Running self-tests...\n";

    // --- (A) shunting-yard RPN correctness (precedence / assoc / parens) ----

    // 1. Arithmetic precedence: * binds tighter than +.
    //    age + 2 * 3   ->   age 2 3 * +
    {
        auto rpn = toRPN(infixOf("age + 2 * 3"));
        assert(rpnToString(rpn) == "age 2 3 * +");
    }

    // 2. Comparison below arithmetic: age + 2*3 > 25 -> age 2 3 * + 25 >
    {
        auto rpn = toRPN(infixOf("age + 2 * 3 > 25"));
        assert(rpnToString(rpn) == "age 2 3 * + 25 >");
    }

    // 3. AND binds tighter than OR; parentheses override.
    //    age > 18 AND (dept = 'eng' OR id < 3)
    //    -> age 18 > dept eng = id 3 < OR AND
    {
        auto rpn = toRPN(infixOf("age > 18 AND (dept = 'eng' OR id < 3)"));
        assert(rpnToString(rpn) == "age 18 > dept eng = id 3 < OR AND");
    }

    // 4. Without parens, AND > OR precedence groups the AND first.
    //    a = 1 OR b = 2 AND c = 3  ->  a 1 = b 2 = c 3 = AND OR
    {
        auto rpn = toRPN(infixOf("a = 1 OR b = 2 AND c = 3"));
        assert(rpnToString(rpn) == "a 1 = b 2 = c 3 = AND OR");
    }

    // 5. Unary NOT has the highest precedence among logical operators, so
    //    without parentheses it binds only to the immediately following
    //    operand: NOT age >= 30  ->  age NOT 30 >=   (NOT(age) compared to 30).
    //    To negate the whole comparison you must parenthesise it (see test 5b).
    {
        auto rpn = toRPN(infixOf("NOT age >= 30"));
        assert(rpnToString(rpn) == "age NOT 30 >=");
    }

    // 5b. Parenthesised negation of a comparison:
    //     NOT (age >= 30)  ->  age 30 >= NOT
    {
        auto rpn = toRPN(infixOf("NOT (age >= 30)"));
        assert(rpnToString(rpn) == "age 30 >= NOT");
    }

    // 6. Nested NOT + parentheses: NOT (a = 1 AND b = 2)
    //    -> a 1 = b 2 = AND NOT
    {
        auto rpn = toRPN(infixOf("NOT (a = 1 AND b = 2)"));
        assert(rpnToString(rpn) == "a 1 = b 2 = AND NOT");
    }

    // 7. Left-associativity of subtraction: 10 - 3 - 2 -> 10 3 - 2 -
    {
        auto rpn = toRPN(infixOf("10 - 3 - 2"));
        assert(rpnToString(rpn) == "10 3 - 2 -");
    }

    // --- (B) RPN evaluation against a row -----------------------------------
    {
        Row r = {{"age", 28LL}, {"dept", std::string("eng")}, {"id", 5LL}};
        // 28 + 2*3 = 34 > 25  -> true
        assert(evalPredicate(toRPN(infixOf("age + 2 * 3 > 25")), r) == true);
        // dept = 'eng' -> true ; dept = 'sales' -> false
        assert(evalPredicate(toRPN(infixOf("dept = 'eng'")), r) == true);
        assert(evalPredicate(toRPN(infixOf("dept = 'sales'")), r) == false);
        // NOT (age >= 30) -> NOT(false) -> true
        assert(evalPredicate(toRPN(infixOf("NOT (age >= 30)")), r) == true);
        // arithmetic value result
        assert(asInt(evalRPN(toRPN(infixOf("age + 2 * 3")), r)) == 34);
    }

    // --- (C) end-to-end query result sets -----------------------------------

    // Q1: age > 18 AND (dept='eng' OR id<3)
    //   Alice(30,eng) yes; Bob(22,sales,id2) id<3 yes; Carol(41,eng) yes;
    //   Dave(17) no (age); Eve(28,hr,id5) no.
    {
        Table res = runQuery(
            "SELECT name FROM employees WHERE age > 18 AND (dept = 'eng' OR id < 3)", cat);
        std::set<std::string> got = namesOf(res);
        std::set<std::string> want = {"Alice", "Bob", "Carol"};
        assert(got == want);
    }

    // Q2: NOT (age >= 30)  -> ages < 30: Bob(22), Dave(17), Eve(28)
    {
        Table res = runQuery("SELECT * FROM employees WHERE NOT (age >= 30)", cat);
        std::set<std::string> got = namesOf(res);
        std::set<std::string> want = {"Bob", "Dave", "Eve"};
        assert(got == want);
    }

    // Q3: age + 2*3 > 25  -> age > 19: Alice(30),Bob(22),Carol(41),Eve(28)
    {
        Table res = runQuery("SELECT name, age FROM employees WHERE age + 2 * 3 > 25", cat);
        std::set<std::string> got = namesOf(res);
        std::set<std::string> want = {"Alice", "Bob", "Carol", "Eve"};
        assert(got == want);
    }

    // Q4: numbers n/2 >= 3 AND n != 12  -> n in {6,20} (12 excluded, 1 too small)
    {
        Table res = runQuery("SELECT * FROM numbers WHERE n / 2 >= 3 AND n != 12", cat);
        std::set<long long> got;
        for (const Row &row : res) got.insert(asInt(row.at("n")));
        std::set<long long> want = {6, 20};
        assert(got == want);
    }

    // Q5: no WHERE -> all 5 employees.
    {
        Table res = runQuery("SELECT name FROM employees", cat);
        assert(res.size() == 5);
    }

    // Q6: projection picks only requested columns.
    {
        Table res = runQuery("SELECT name FROM employees WHERE id = 1", cat);
        assert(res.size() == 1);
        assert(res[0].size() == 1);               // only "name"
        assert(res[0].count("name") == 1);
        assert(asStr(res[0].at("name")) == "Alice");
    }

    std::cout << "ALL TESTS PASSED\n";
    return 0;
}
