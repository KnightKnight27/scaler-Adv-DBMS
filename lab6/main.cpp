// main.cpp  —  ADBMS Lab 6  |  Patel Jash  |  24bcs10632
//
// Driver file for testing the SQL processing engine.

#include "sql_engine.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

using namespace sql_processor;

namespace {

void print_header(const std::string& title) {
    std::cout << "\n=== " << title << " ===\n";
}

void assert_check(bool condition, const std::string& error_msg) {
    if (!condition) {
        std::cerr << "TEST FAILED: " << error_msg << "\n";
        std::exit(EXIT_FAILURE);
    }
}

Relation populate_employee_data() {
    Relation emp_rel;
    emp_rel.title = "employees";
    emp_rel.col_names = {"id", "name", "dept", "age", "salary"};

    auto append_record = [&](long long id,
                             const std::string& emp_name,
                             const std::string& emp_dept,
                             long long age,
                             long long salary) {
        emp_rel.rows.push_back(Tuple{{
            DataCell{id}, DataCell{emp_name}, DataCell{emp_dept}, DataCell{age}, DataCell{salary}
        }});
    };

    append_record(1, "Aarav",  "Engineering", 29,  95000);
    append_record(2, "Diya",   "Sales",       24,  55000);
    append_record(3, "Rohan",  "Engineering", 35, 120000);
    append_record(4, "Meera",  "Marketing",   41,  88000);
    append_record(5, "Kabir",  "Sales",       28,  61000);
    append_record(6, "Ishita", "Engineering", 31,  99000);
    append_record(7, "Vivaan", "Marketing",   23,  47000);
    append_record(8, "Anaya",  "Engineering", 38, 134000);

    return emp_rel;
}

std::size_t fetch_row_count(const Relation& rel) { return rel.rows.size(); }

}  // anonymous namespace

int main() {
    print_header("Section 1) Shunting-Yard Transformation");

    const std::vector<std::string> test_expressions = {
        "age > 25 AND (dept = 'Sales' OR salary >= 100000)",
        "NOT age < 30 AND salary > 90000",
        "id = 1 OR id = 3 OR id = 8",
    };

    for (const std::string& expr_str : test_expressions) {
        std::vector<SQLToken> post_seq = convert_to_postfix(scan_tokens(expr_str));
        std::cout << "  Infix Expression   : " << expr_str << "\n";
        std::cout << "  Postfix (RPN) Form : " << postfix_to_string(post_seq) << "\n\n";
    }

    Relation db_table = populate_employee_data();

    print_header("Section 2a) Simple SELECT * Evaluation");
    {
        Relation output = run_query(parse_query("SELECT * FROM employees"), db_table);
        render_relation(output, std::cout);
        assert_check(fetch_row_count(output) == 8, "Expected exactly 8 output rows");
    }

    print_header("Section 2b) Filtering with Complex WHERE and Sorting");
    {
        const std::string select_cmd =
            "SELECT name, dept, salary FROM employees "
            "WHERE age > 25 AND (dept = 'Sales' OR salary >= 100000) "
            "ORDER BY salary DESC";

        std::cout << "Query: " << select_cmd << "\n";
        Relation output = run_query(parse_query(select_cmd), db_table);
        render_relation(output, std::cout);

        assert_check(fetch_row_count(output) == 3, "Output should contain exactly 3 filtered rows");
        assert_check(std::get<std::string>(output.rows[0].columns[0]) == "Anaya", "First row should be highest paid");
        assert_check(std::get<std::string>(output.rows[2].columns[0]) == "Kabir", "Last row should be lowest paid of the 3");
    }

    print_header("Section 2c) Using NOT, ORDER BY ASC, and LIMIT Clauses");
    {
        const std::string select_cmd =
            "SELECT name, age FROM employees "
            "WHERE NOT dept = 'Engineering' "
            "ORDER BY age ASC LIMIT 2";

        std::cout << "Query: " << select_cmd << "\n";
        Relation output = run_query(parse_query(select_cmd), db_table);
        render_relation(output, std::cout);

        assert_check(fetch_row_count(output) == 2, "LIMIT 2 must cap rows to 2");
        assert_check(std::get<std::string>(output.rows[0].columns[0]) == "Vivaan", "Youngest should be Vivaan");
        assert_check(std::get<long long>(output.rows[1].columns[1]) == 24, "Second youngest age must be 24");
    }

    print_header("Section 2d) Direct RPN Expression Evaluation per Row");
    {
        std::vector<SQLToken> condition_rpn =
            convert_to_postfix(scan_tokens("dept = 'Engineering' AND salary >= 100000"));

        std::cout << "RPN Form: " << postfix_to_string(condition_rpn) << "\n";

        std::size_t matches = 0;
        for (const Tuple& row : db_table.rows) {
            if (evaluate_postfix(condition_rpn, db_table, row)) {
                ++matches;
                std::cout << "  Matched: " << std::get<std::string>(row.columns[1]) << "\n";
            }
        }

        assert_check(matches == 2, "Should match precisely 2 rows");
    }

    std::cout << "\nAll test cases executed successfully!\n";
    return EXIT_SUCCESS;
}
