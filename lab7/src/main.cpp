#include <algorithm>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include "expr/ExpressionEvaluator.h"
#include "expr/Token.h"
#include "sql/QueryEngine.h"
#include "sql/SqlLexer.h"
#include "sql/SqlParser.h"
#include "sql/Table.h"

namespace {

std::string postfixToString(const std::vector<expr::Token>& tokens) {
    std::string out;
    for (size_t i = 0; i < tokens.size(); ++i) {
        if (i) out += ' ';
        out += expr::toString(tokens[i]);
    }
    return out;
}

void runExpressionDemo() {
    std::cout << "=== Part 1: Shunting-Yard expression evaluator ===\n";
    expr::ExpressionEvaluator evaluator;

    const std::vector<std::string> expressions = {
        "3 + 4 * 2 / (1 - 5)",
        "2 ^ 3 ^ 2",          // right-associative: 2 ^ (3 ^ 2) = 512
        "-2 ^ 2",             // unary minus binds tightest: (-2) ^ 2 = 4
        "10 % 3 + 1",
        "(1 + 2) * (3 + 4)",
        "-(3 + 4) * -2"
    };

    for (const std::string& e : expressions) {
        std::cout << std::left << std::setw(22) << e
                  << " | RPN: " << std::setw(20) << postfixToString(evaluator.toPostfix(e))
                  << " = " << evaluator.evaluate(e) << '\n';
    }
    std::cout << '\n';
}

sql::Table makeStudentsTable() {
    sql::Table table;
    table.columns = {"id", "name", "gpa", "age"};

    auto addRow = [&](double id, const std::string& name, double gpa, double age) {
        sql::Row row;
        row.set("id", id);
        row.set("name", name);
        row.set("gpa", gpa);
        row.set("age", age);
        table.rows.push_back(std::move(row));
    };

    addRow(1, "Aarav", 9.1, 20);
    addRow(2, "Diya", 7.4, 22);
    addRow(3, "Kabir", 8.6, 19);
    addRow(4, "Meera", 6.9, 21);
    addRow(5, "Rohan", 8.0, 23);
    return table;
}

void printTable(const sql::Table& table) {
    std::vector<size_t> widths(table.columns.size());
    for (size_t c = 0; c < table.columns.size(); ++c) {
        widths[c] = table.columns[c].size();
        for (const sql::Row& row : table.rows) {
            widths[c] = std::max(widths[c], sql::toString(row.at(table.columns[c])).size());
        }
    }

    auto printRow = [&](const std::vector<std::string>& cells) {
        for (size_t c = 0; c < cells.size(); ++c) {
            std::cout << (c ? " | " : "  ") << std::left << std::setw(static_cast<int>(widths[c]))
                      << cells[c];
        }
        std::cout << '\n';
    };

    printRow(table.columns);
    std::vector<std::string> separator(table.columns.size());
    for (size_t c = 0; c < table.columns.size(); ++c) separator[c] = std::string(widths[c], '-');
    printRow(separator);

    for (const sql::Row& row : table.rows) {
        std::vector<std::string> cells;
        cells.reserve(table.columns.size());
        for (const std::string& column : table.columns) cells.push_back(sql::toString(row.at(column)));
        printRow(cells);
    }
}

void runQuery(const sql::Table& students, const std::string& query) {
    std::cout << "> " << query << '\n';
    sql::SqlLexer lexer;
    sql::SqlParser parser(lexer.tokenize(query));
    sql::SelectStatement stmt = parser.parse();

    sql::QueryEngine engine;
    sql::Table result = engine.execute(stmt, students);
    printTable(result);
    std::cout << "(" << result.rows.size() << " row(s))\n\n";
}

void runSqlDemo() {
    std::cout << "=== Part 2: minimal SQL SELECT over vector<Row> ===\n";
    sql::Table students = makeStudentsTable();

    std::cout << "students:\n";
    printTable(students);
    std::cout << '\n';

    runQuery(students, "SELECT name, gpa FROM students WHERE gpa >= 8.0 AND age < 23");
    runQuery(students, "SELECT * FROM students WHERE age > 21 OR gpa >= 9.0");
    runQuery(students, "SELECT name FROM students WHERE name >= 'K'");
}

}  // namespace

int main() {
    try {
        runExpressionDemo();
        runSqlDemo();
    } catch (const std::exception& ex) {
        std::cerr << "error: " << ex.what() << '\n';
        return 1;
    }
    return 0;
}
