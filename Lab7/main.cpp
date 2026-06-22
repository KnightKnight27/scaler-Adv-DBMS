#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <unordered_map>
#include <stack>
#include <stdexcept>
#include <algorithm>
#include <cctype>
#include <iomanip>

// ---------------------------------------------------------------------
// SQL SELECT Statement Lexer & RPN Parser Engine
// Student: Ayush Kumar Patra (24bcs10474)
// Course: Advanced Database Management Systems (ADBMS)
// ---------------------------------------------------------------------

using TupleRow = std::unordered_map<std::string, std::string>;

enum class SqlLexemeType {
    IDENTIFIER,
    NUMBER,
    LITERAL_STR,
    OPERATOR,
    L_PAREN,
    R_PAREN,
    KEYWORD
};

struct LexicalToken {
    SqlLexemeType type;
    std::string text;
};

// Tokenizer / Lexical Analyzer
std::vector<LexicalToken> tokenize(const std::string& sql_input) {
    std::vector<LexicalToken> token_list;
    size_t index = 0;
    const size_t length = sql_input.size();

    while (index < length) {
        if (std::isspace(sql_input[index])) {
            ++index;
            continue;
        }

        if (sql_input[index] == '(') {
            token_list.push_back({SqlLexemeType::L_PAREN, "("});
            ++index;
            continue;
        }
        if (sql_input[index] == ')') {
            token_list.push_back({SqlLexemeType::R_PAREN, ")"});
            ++index;
            continue;
        }

        // Operators: comparison and inequality
        if (sql_input[index] == '<' || sql_input[index] == '>' || sql_input[index] == '!' || sql_input[index] == '=') {
            std::string op_str(1, sql_input[index]);
            if (index + 1 < length && sql_input[index + 1] == '=') {
                op_str += '=';
                ++index;
            }
            token_list.push_back({SqlLexemeType::OPERATOR, op_str});
            ++index;
            continue;
        }

        // String literals
        if (sql_input[index] == '\'') {
            std::string literal;
            ++index;
            while (index < length && sql_input[index] != '\'') {
                literal += sql_input[index++];
            }
            ++index; // skip closing quote
            token_list.push_back({SqlLexemeType::LITERAL_STR, literal});
            continue;
        }

        // Numeric values
        if (std::isdigit(sql_input[index])) {
            std::string num_str;
            while (index < length && (std::isdigit(sql_input[index]) || sql_input[index] == '.')) {
                num_str += sql_input[index++];
            }
            token_list.push_back({SqlLexemeType::NUMBER, num_str});
            continue;
        }

        // Identifiers and Keywords
        if (std::isalpha(sql_input[index]) || sql_input[index] == '_' || sql_input[index] == '*') {
            if (sql_input[index] == '*') {
                token_list.push_back({SqlLexemeType::IDENTIFIER, "*"});
                ++index;
                continue;
            }
            std::string word;
            while (index < length && (std::isalnum(sql_input[index]) || sql_input[index] == '_')) {
                word += sql_input[index++];
            }
            std::string upper_word = word;
            std::transform(upper_word.begin(), upper_word.end(), upper_word.begin(), ::toupper);
            
            if (upper_word == "SELECT" || upper_word == "FROM" || upper_word == "WHERE" ||
                upper_word == "AND" || upper_word == "OR" || upper_word == "NOT") {
                token_list.push_back({SqlLexemeType::KEYWORD, upper_word});
            } else {
                token_list.push_back({SqlLexemeType::IDENTIFIER, word});
            }
            continue;
        }

        ++index; // default fallback increment
    }
    return token_list;
}

// Operator priority
int operatorPriority(const std::string& op) {
    if (op == "OR")  return 1;
    if (op == "AND") return 2;
    if (op == "NOT") return 3;
    return 4;
}

// Convert infix clauses to postfix (Reverse Polish Notation) using Shunting-yard algorithm
std::vector<LexicalToken> shuntingYard(const std::vector<LexicalToken>& expr) {
    std::vector<LexicalToken> output_queue;
    std::stack<LexicalToken> operator_stack;

    for (const auto& tok : expr) {
        switch (tok.type) {
        case SqlLexemeType::IDENTIFIER:
        case SqlLexemeType::NUMBER:
        case SqlLexemeType::LITERAL_STR:
            output_queue.push_back(tok);
            break;

        case SqlLexemeType::OPERATOR:
        case SqlLexemeType::KEYWORD: {
            while (!operator_stack.empty() &&
                   operator_stack.top().type != SqlLexemeType::L_PAREN &&
                   operatorPriority(operator_stack.top().text) >= operatorPriority(tok.text)) {
                output_queue.push_back(operator_stack.top());
                operator_stack.pop();
            }
            operator_stack.push(tok);
            break;
        }

        case SqlLexemeType::L_PAREN:
            operator_stack.push(tok);
            break;

        case SqlLexemeType::R_PAREN:
            while (!operator_stack.empty() && operator_stack.top().type != SqlLexemeType::L_PAREN) {
                output_queue.push_back(operator_stack.top());
                operator_stack.pop();
            }
            if (!operator_stack.empty()) {
                operator_stack.pop(); // remove matching L_PAREN
            }
            break;
        }
    }

    while (!operator_stack.empty()) {
        output_queue.push_back(operator_stack.top());
        operator_stack.pop();
    }
    return output_queue;
}

// Evaluate postfix (RPN) filter logic against a table row
bool executeFilter(const std::vector<LexicalToken>& rpn_tokens, const TupleRow& row) {
    if (rpn_tokens.empty()) return true;

    std::stack<std::string> val_stack;

    for (const auto& tok : rpn_tokens) {
        if (tok.type == SqlLexemeType::NUMBER || tok.type == SqlLexemeType::LITERAL_STR) {
            val_stack.push(tok.text);
        } else if (tok.type == SqlLexemeType::IDENTIFIER) {
            auto iter = row.find(tok.text);
            if (iter == row.end()) {
                throw std::runtime_error("Column reference error: " + tok.text);
            }
            val_stack.push(iter->second);
        } else {
            if (val_stack.size() < 2) continue;
            std::string right_operand = val_stack.top(); val_stack.pop();
            std::string left_operand = val_stack.top(); val_stack.pop();

            auto compareNumbers = [&](auto compare_func) -> std::string {
                return compare_func(std::stod(left_operand), std::stod(right_operand)) ? "1" : "0";
            };

            if      (tok.text == "=")  val_stack.push(left_operand == right_operand ? "1" : "0");
            else if (tok.text == "!=") val_stack.push(left_operand != right_operand ? "1" : "0");
            else if (tok.text == ">")  val_stack.push(compareNumbers(std::greater<double>{}));
            else if (tok.text == "<")  val_stack.push(compareNumbers(std::less<double>{}));
            else if (tok.text == ">=") val_stack.push(compareNumbers(std::greater_equal<double>{}));
            else if (tok.text == "<=") val_stack.push(compareNumbers(std::less_equal<double>{}));
            else if (tok.text == "AND") val_stack.push((left_operand == "1" && right_operand == "1") ? "1" : "0");
            else if (tok.text == "OR")  val_stack.push((left_operand == "1" || right_operand == "1") ? "1" : "0");
        }
    }

    return !val_stack.empty() && val_stack.top() == "1";
}

// Compile and run SELECT execution plan
void processSelectQuery(const std::string& query, const std::vector<TupleRow>& relation) {
    auto tokens = tokenize(query);

    std::vector<std::string> selected_columns;
    std::vector<LexicalToken> filter_clause;

    size_t pos = 0;
    if (pos < tokens.size() && tokens[pos].text == "SELECT") {
        ++pos;
    }

    // Parse columns until FROM is reached
    while (pos < tokens.size() && tokens[pos].text != "FROM") {
        if (tokens[pos].type == SqlLexemeType::IDENTIFIER) {
            selected_columns.push_back(tokens[pos].text);
        }
        ++pos;
    }
    
    if (pos < tokens.size()) ++pos; // skip FROM keyword
    if (pos < tokens.size()) ++pos; // skip table identifier (e.g. "students")

    // Parse WHERE clause
    if (pos < tokens.size() && tokens[pos].text == "WHERE") {
        ++pos;
        while (pos < tokens.size()) {
            filter_clause.push_back(tokens[pos++]);
        }
    }

    auto postfix_expr = shuntingYard(filter_clause);

    // Compute column list for printing
    std::vector<std::string> table_header;
    if (!selected_columns.empty() && selected_columns[0] == "*") {
        if (!relation.empty()) {
            for (const auto& pair : relation[0]) {
                table_header.push_back(pair.first);
            }
        }
    } else {
        table_header = selected_columns;
    }

    // Print Header
    for (const auto& col_name : table_header) {
        std::cout << std::left << std::setw(18) << col_name;
    }
    std::cout << "\n" << std::string(18 * table_header.size(), '-') << "\n";

    // Print Rows matching filter
    for (const auto& row : relation) {
        if (executeFilter(postfix_expr, row)) {
            for (const auto& col_name : table_header) {
                auto cell = row.find(col_name);
                std::cout << std::left << std::setw(18)
                          << (cell != row.end() ? cell->second : "NULL");
            }
            std::cout << "\n";
        }
    }
    std::cout << "\n";
}

int main() {
    // Unique Dataset: students instead of employees
    std::vector<TupleRow> students = {
        {{"id","1"},{"name","Amelia"},   {"major","Computer Science"},{"gpa","3.8"},{"credits","90"}},
        {{"id","2"},{"name","Benjamin"}, {"major","Biology"},         {"gpa","3.2"},{"credits","60"}},
        {{"id","3"},{"name","Charlotte"},{"major","Computer Science"},{"gpa","3.9"},{"credits","110"}},
        {{"id","4"},{"name","Daniel"},   {"major","History"},         {"gpa","2.8"},{"credits","45"}},
        {{"id","5"},{"name","Emily"},    {"major","Computer Science"},{"gpa","3.5"},{"credits","75"}},
        {{"id","6"},{"name","Frederick"},{"major","Biology"},         {"gpa","3.4"},{"credits","95"}},
        {{"id","7"},{"name","Gabrielle"},{"major","History"},         {"gpa","3.6"},{"credits","80"}},
    };

    struct QueryTest {
        std::string description;
        std::string select_sql;
    };

    std::vector<QueryTest> test_scenarios = {
        {"Computer Science majors with GPA > 3.6",
         "SELECT name, gpa FROM students WHERE major = 'Computer Science' AND gpa > 3.6"},

        {"Non-History majors",
         "SELECT name, major FROM students WHERE major != 'History'"},

        {"Students with credits between 50 and 100 (inclusive)",
         "SELECT name, credits, major FROM students WHERE credits >= 50 AND credits <= 100"},

        {"Biology or History majors with GPA >= 3.2",
         "SELECT name, major, gpa FROM students WHERE (major = 'Biology' OR major = 'History') AND gpa >= 3.2"},

        {"Wildcard SELECT *",
         "SELECT * FROM students"}
    };

    for (const auto& test : test_scenarios) {
        std::cout << "=== " << test.description << " ===\n";
        std::cout << "SQL Query: " << test.select_sql << "\n\n";
        processSelectQuery(test.select_sql, students);
    }

    return 0;
}
