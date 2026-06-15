#include <iostream>
#include <vector>

#include "types.h"
#include "lexer.h"
#include "parser.h"
#include "executor.h"

int main() {
    try {
        // ========== DEMO 1: Expression Evaluation ==========
        std::cout << "=== DEMO 1: SHUNTING-YARD EXPRESSION EVALUATION ===\n\n";
        
        ExpressionEvaluator evaluator;
        struct TestExpr {
            std::string expr;
            std::unordered_map<std::string, double> vars;
        };
        
        TestExpr exprs[] = {
            { "age * 2 + salary / 1000 > 100", {{"age", 30}, {"salary", 50000}} },
            { "gpa > 3.0 && age < 25", {{"gpa", 3.8}, {"age", 22}} },
            { "2 + 3 * 4", {} },
        };
        
        for (const auto& test : exprs) {
            auto tokens = evaluator.tokenize(test.expr);
            auto rpn = evaluator.to_rpn(tokens);
            double result = evaluator.eval_rpn(rpn, test.vars);
            
            std::cout << "Expression: " << test.expr << "\n";
            std::cout << "RPN:        ";
            for (const auto& t : rpn) std::cout << t << " ";
            std::cout << "\nResult:     " << (result ? "TRUE" : "FALSE") << "\n\n";
        }

        // ========== DEMO 2: SQL Query Parsing & Execution ==========
        std::cout << "=== DEMO 2: SQL QUERY EXECUTION ===\n\n";

        // Sample data
        std::vector<Row> students = {
            {{{ "id", 1.0 }, { "name", std::string("Alice") }, { "age", 22.0 }, { "gpa", 3.8 }}},
            {{{ "id", 2.0 }, { "name", std::string("Bob")   }, { "age", 25.0 }, { "gpa", 2.9 }}},
            {{{ "id", 3.0 }, { "name", std::string("Carol")  }, { "age", 21.0 }, { "gpa", 3.5 }}},
            {{{ "id", 4.0 }, { "name", std::string("Dave")   }, { "age", 30.0 }, { "gpa", 3.1 }}},
            {{{ "id", 5.0 }, { "name", std::string("Eve")    }, { "age", 23.0 }, { "gpa", 3.9 }}},
        };

        std::string queries[] = {
            "SELECT id, name, gpa FROM students WHERE gpa > 3.0 ORDER BY gpa DESC LIMIT 3",
            "SELECT * FROM students WHERE age >= 22 && age <= 26",
            "SELECT name, gpa FROM students WHERE gpa >= 3.5 ORDER BY gpa ASC",
        };

        QueryExecutor executor;

        for (const auto& sql : queries) {
            std::cout << "SQL: " << sql << "\n";
            
            Lexer lexer(sql);
            auto tokens = lexer.tokenize();
            
            SelectParser parser(tokens);
            auto query = parser.parse();
            
            auto results = executor.execute(query, students);
            executor.print_rows(results);
            std::cout << "\n";
        }

        // ========== DEMO 3: Complex WHERE with Arithmetic ==========
        std::cout << "=== DEMO 3: COMPLEX WHERE CLAUSES ===\n\n";

        std::vector<Row> employees = {
            {{{ "name", std::string("Alice") }, { "salary", 50000.0 }, { "exp", 5.0 }}},
            {{{ "name", std::string("Bob")   }, { "salary", 70000.0 }, { "exp", 8.0 }}},
            {{{ "name", std::string("Carol")  }, { "salary", 45000.0 }, { "exp", 2.0 }}},
            {{{ "name", std::string("Dave")   }, { "salary", 80000.0 }, { "exp", 10.0 }}},
        };

        std::string complex_queries[] = {
            "SELECT name, salary FROM employees WHERE salary > 50000 && exp >= 8",
            "SELECT name FROM employees WHERE salary / 1000 > 50",
        };

        for (const auto& sql : complex_queries) {
            std::cout << "SQL: " << sql << "\n";
            
            Lexer lexer(sql);
            auto tokens = lexer.tokenize();
            
            SelectParser parser(tokens);
            auto query = parser.parse();
            
            auto results = executor.execute(query, employees);
            executor.print_rows(results);
            std::cout << "\n";
        }

        std::cout << "✓ All demos completed successfully!\n\n";

    } catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
