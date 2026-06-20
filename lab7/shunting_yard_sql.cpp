// ============================================================================
// Lab 7: Dijkstra's Shunting-Yard Expression Evaluator
//        + Minimal SQL SELECT Parser over vector<Row>
//
// Features:
//   1. Tokenizer for arithmetic, comparison, logical, and parenthesized exprs
//   2. Shunting-Yard algorithm (infix → postfix / RPN conversion)
//   3. Postfix expression evaluator (numeric results)
//   4. Minimal SQL parser:  SELECT cols FROM table WHERE expr ORDER BY col
//   5. Interactive REPL for running SQL queries on in-memory table
// ============================================================================

#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <stack>
#include <queue>
#include <unordered_map>
#include <algorithm>
#include <iomanip>
#include <cctype>
#include <stdexcept>
#include <functional>
#include <cmath>

using namespace std;


string toUpper(const string& s) {
    string r = s;
    transform(r.begin(), r.end(), r.begin(), ::toupper);
    return r;
}

string toLower(const string& s) {
    string r = s;
    transform(r.begin(), r.end(), r.begin(), ::tolower);
    return r;
}

string trim(const string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

bool isNumber(const string& s) {
    if (s.empty()) return false;
    size_t start = 0;
    if (s[0] == '-' || s[0] == '+') start = 1;
    bool dot = false;
    for (size_t i = start; i < s.size(); i++) {
        if (s[i] == '.') {
            if (dot) return false;
            dot = true;
        } else if (!isdigit(s[i])) {
            return false;
        }
    }
    return start < s.size();
}

enum class TokenType {
    NUMBER,       // e.g. 42, 3.14
    STRING_LIT,   // e.g. 'Engineering'
    IDENTIFIER,   // column name or keyword
    OPERATOR,     // +  -  *  /  %  =  !=  <  >  <=  >=  AND  OR  NOT
    LPAREN,       // (
    RPAREN,       // )
    COMMA,        // ,
    STAR,         // * (when used as SELECT *)
    END_OF_INPUT
};

struct Token {
    TokenType type;
    string    value;
};

// Tokenize an expression / SQL string into a list of tokens
vector<Token> tokenize(const string& input) {
    vector<Token> tokens;
    int i = 0, n = input.size();

    while (i < n) {
       
        if (isspace(input[i])) { i++; continue; }

        
        if (input[i] == '\'') {
            i++; 
            string lit;
            while (i < n && input[i] != '\'') lit += input[i++];
            if (i < n) i++; 
            tokens.push_back({TokenType::STRING_LIT, lit});
            continue;
        }


        if (i + 1 < n) {
            string two = input.substr(i, 2);
            if (two == "!=" || two == "<=" || two == ">=") {
                tokens.push_back({TokenType::OPERATOR, two});
                i += 2;
                continue;
            }
        }


        if (input[i] == '(') { tokens.push_back({TokenType::LPAREN, "("}); i++; continue; }
        if (input[i] == ')') { tokens.push_back({TokenType::RPAREN, ")"}); i++; continue; }
        if (input[i] == ',') { tokens.push_back({TokenType::COMMA,  ","}); i++; continue; }
        if (input[i] == '+' || input[i] == '-' || input[i] == '/' || input[i] == '%') {
            tokens.push_back({TokenType::OPERATOR, string(1, input[i])}); i++; continue;
        }
        if (input[i] == '<' || input[i] == '>' || input[i] == '=') {
            tokens.push_back({TokenType::OPERATOR, string(1, input[i])}); i++; continue;
        }


        if (input[i] == '*') {

            if (tokens.empty() || tokens.back().type == TokenType::COMMA ||
                (tokens.back().type == TokenType::IDENTIFIER &&
                 (toUpper(tokens.back().value) == "SELECT" || toUpper(tokens.back().value) == ","))) {
                tokens.push_back({TokenType::STAR, "*"});
            } else {
                tokens.push_back({TokenType::OPERATOR, "*"});
            }
            i++; continue;
        }

      
        if (isdigit(input[i])) {
            string num;
            while (i < n && (isdigit(input[i]) || input[i] == '.')) num += input[i++];
            tokens.push_back({TokenType::NUMBER, num});
            continue;
        }

        // identifier or keyword (AND, OR, NOT, SELECT, FROM, WHERE, ORDER, BY, ASC, DESC)
        if (isalpha(input[i]) || input[i] == '_') {
            string word;
            while (i < n && (isalnum(input[i]) || input[i] == '_')) word += input[i++];

            string up = toUpper(word);
            if (up == "AND" || up == "OR" || up == "NOT") {
                tokens.push_back({TokenType::OPERATOR, up});
            } else {
                tokens.push_back({TokenType::IDENTIFIER, word});
            }
            continue;
        }

        // unknown character — skip
        i++;
    }

    tokens.push_back({TokenType::END_OF_INPUT, ""});
    return tokens;
}



int precedence(const string& op) {
    if (op == "NOT")                          return 7;
    if (op == "*" || op == "/" || op == "%")   return 6;
    if (op == "+" || op == "-")               return 5;
    if (op == "<" || op == ">" || op == "<=" || op == ">=") return 4;
    if (op == "=" || op == "!=")              return 3;
    if (op == "AND")                          return 2;
    if (op == "OR")                           return 1;
    return 0;
}

// Left-associative for all except NOT (right-associative, unary)
bool isLeftAssoc(const string& op) {
    return op != "NOT";
}

bool isOperator(const Token& t) {
    return t.type == TokenType::OPERATOR;
}

queue<Token> shuntingYard(const vector<Token>& tokens) {
    queue<Token> output;
    stack<Token> opStack;

    for (const Token& tok : tokens) {
        if (tok.type == TokenType::END_OF_INPUT) break;

        if (tok.type == TokenType::NUMBER ||
            tok.type == TokenType::STRING_LIT ||
            tok.type == TokenType::IDENTIFIER) {
            output.push(tok);
        }
        else if (isOperator(tok)) {
            while (!opStack.empty() && isOperator(opStack.top()) &&
                   ((isLeftAssoc(tok.value) && precedence(tok.value) <= precedence(opStack.top().value)) ||
                    (!isLeftAssoc(tok.value) && precedence(tok.value) < precedence(opStack.top().value)))) {
                output.push(opStack.top());
                opStack.pop();
            }
            opStack.push(tok);
        }
        else if (tok.type == TokenType::LPAREN) {
            opStack.push(tok);
        }
        else if (tok.type == TokenType::RPAREN) {
            while (!opStack.empty() && opStack.top().type != TokenType::LPAREN) {
                output.push(opStack.top());
                opStack.pop();
            }
            if (!opStack.empty()) opStack.pop(); // pop '('
        }
    }

    while (!opStack.empty()) {
        output.push(opStack.top());
        opStack.pop();
    }

    return output;
}


struct Value {
    double numVal  = 0;
    string strVal;
    bool   isStr   = false;

    Value() {}
    Value(double d) : numVal(d), isStr(false) {}
    Value(const string& s) : strVal(s), isStr(true) {}

    string toString() const {
        if (isStr) return strVal;
        if (numVal == (int)numVal) return to_string((int)numVal);
        return to_string(numVal);
    }
};


using Row = unordered_map<string, Value>;

Value evaluatePostfix(queue<Token> rpn, const Row& row) {
    stack<Value> st;

    while (!rpn.empty()) {
        Token tok = rpn.front();
        rpn.pop();

        if (tok.type == TokenType::NUMBER) {
            st.push(Value(stod(tok.value)));
        }
        else if (tok.type == TokenType::STRING_LIT) {
            st.push(Value(tok.value));
        }
        else if (tok.type == TokenType::IDENTIFIER) {
           
            auto it = row.find(tok.value);
            if (it == row.end()) {
                
                for (auto& kv : row) {
                    if (toLower(kv.first) == toLower(tok.value)) {
                        st.push(kv.second);
                        goto found;
                    }
                }
                throw runtime_error("Unknown column: " + tok.value);
            } else {
                st.push(it->second);
            }
            found:;
        }
        else if (tok.type == TokenType::OPERATOR) {
            
            if (tok.value == "NOT") {
                if (st.empty()) throw runtime_error("NOT: missing operand");
                Value a = st.top(); st.pop();
                st.push(Value(a.numVal == 0 ? 1.0 : 0.0));
                continue;
            }

           
            if (st.size() < 2) throw runtime_error("Operator " + tok.value + ": insufficient operands");
            Value b = st.top(); st.pop();
            Value a = st.top(); st.pop();

            
            if (tok.value == "+" ) { st.push(Value(a.numVal + b.numVal)); }
            else if (tok.value == "-" ) { st.push(Value(a.numVal - b.numVal)); }
            else if (tok.value == "*" ) { st.push(Value(a.numVal * b.numVal)); }
            else if (tok.value == "/" ) {
                if (b.numVal == 0) throw runtime_error("Division by zero");
                st.push(Value(a.numVal / b.numVal));
            }
            else if (tok.value == "%" ) { st.push(Value(fmod(a.numVal, b.numVal))); }
            
            else if (tok.value == "=" ) {
                if (a.isStr || b.isStr)
                    st.push(Value(a.toString() == b.toString() ? 1.0 : 0.0));
                else
                    st.push(Value(a.numVal == b.numVal ? 1.0 : 0.0));
            }
            else if (tok.value == "!=") {
                if (a.isStr || b.isStr)
                    st.push(Value(a.toString() != b.toString() ? 1.0 : 0.0));
                else
                    st.push(Value(a.numVal != b.numVal ? 1.0 : 0.0));
            }
            else if (tok.value == "<" ) {
                if (a.isStr || b.isStr)
                    st.push(Value(a.toString() < b.toString() ? 1.0 : 0.0));
                else
                    st.push(Value(a.numVal < b.numVal ? 1.0 : 0.0));
            }
            else if (tok.value == ">" ) {
                if (a.isStr || b.isStr)
                    st.push(Value(a.toString() > b.toString() ? 1.0 : 0.0));
                else
                    st.push(Value(a.numVal > b.numVal ? 1.0 : 0.0));
            }
            else if (tok.value == "<=") {
                if (a.isStr || b.isStr)
                    st.push(Value(a.toString() <= b.toString() ? 1.0 : 0.0));
                else
                    st.push(Value(a.numVal <= b.numVal ? 1.0 : 0.0));
            }
            else if (tok.value == ">=") {
                if (a.isStr || b.isStr)
                    st.push(Value(a.toString() >= b.toString() ? 1.0 : 0.0));
                else
                    st.push(Value(a.numVal >= b.numVal ? 1.0 : 0.0));
            }
           
            else if (tok.value == "AND") { st.push(Value((a.numVal != 0 && b.numVal != 0) ? 1.0 : 0.0)); }
            else if (tok.value == "OR" ) { st.push(Value((a.numVal != 0 || b.numVal != 0) ? 1.0 : 0.0)); }
            else {
                throw runtime_error("Unknown operator: " + tok.value);
            }
        }
    }

    if (st.empty()) throw runtime_error("Expression evaluation yielded no result");
    return st.top();
}



struct SQLQuery {
    vector<string> columns;     // empty means SELECT *
    bool           selectAll = false;
    string         tableName;
    vector<Token>  whereExpr;   // tokens of WHERE expression (for shunting-yard)
    bool           hasWhere = false;
    string         orderByCol;
    bool           orderAsc = true;
    bool           hasOrderBy = false;
};

SQLQuery parseSQL(const string& sql) {
    vector<Token> tokens = tokenize(sql);
    SQLQuery q;
    int pos = 0;

    auto peek = [&]() -> Token { return tokens[pos]; };
    auto advance = [&]() -> Token { return tokens[pos++]; };
    auto expectKW = [&](const string& kw) {
        Token t = advance();
        if (toUpper(t.value) != kw)
            throw runtime_error("Expected '" + kw + "' but got '" + t.value + "'");
    };

    // SELECT
    expectKW("SELECT");

    
    if (peek().type == TokenType::STAR ||
        (peek().type == TokenType::OPERATOR && peek().value == "*")) {
        q.selectAll = true;
        advance(); // consume *
    } else {
        while (true) {
            Token col = advance();
            if (col.type != TokenType::IDENTIFIER)
                throw runtime_error("Expected column name, got '" + col.value + "'");
            q.columns.push_back(col.value);
            if (peek().type == TokenType::COMMA) {
                advance(); // skip comma
            } else {
                break;
            }
        }
    }

    // FROM
    expectKW("FROM");
    Token tbl = advance();
    q.tableName = tbl.value;

    // Optional WHERE
    if (peek().type != TokenType::END_OF_INPUT && toUpper(peek().value) == "WHERE") {
        advance(); // consume WHERE
        q.hasWhere = true;

        // Collect all tokens until ORDER or end
        while (peek().type != TokenType::END_OF_INPUT) {
            if (toUpper(peek().value) == "ORDER") break;
            q.whereExpr.push_back(advance());
        }
        q.whereExpr.push_back({TokenType::END_OF_INPUT, ""});
    }

    // Optional ORDER BY
    if (peek().type != TokenType::END_OF_INPUT && toUpper(peek().value) == "ORDER") {
        advance(); // ORDER
        expectKW("BY");
        q.hasOrderBy = true;
        Token col = advance();
        q.orderByCol = col.value;

        if (peek().type != TokenType::END_OF_INPUT) {
            string dir = toUpper(peek().value);
            if (dir == "ASC" || dir == "DESC") {
                q.orderAsc = (dir == "ASC");
                advance();
            }
        }
    }

    return q;
}


struct Table {
    string         name;
    vector<string> columnOrder;   // ordered column names
    vector<Row>    rows;
};

// Execute a parsed SQL query against the tables
vector<vector<string>> executeSQL(const SQLQuery& q,
                                  const unordered_map<string, Table>& db,
                                  vector<string>& outColumns) {
    // Find table
    auto it = db.find(toLower(q.tableName));
    if (it == db.end()) throw runtime_error("Unknown table: " + q.tableName);
    const Table& table = it->second;

    // Determine output columns
    if (q.selectAll) {
        outColumns = table.columnOrder;
    } else {
        outColumns = q.columns;
    }

    // Filter rows with WHERE
    vector<Row> filtered;
    if (q.hasWhere) {
        queue<Token> rpn = shuntingYard(q.whereExpr);
        for (const Row& row : table.rows) {
            try {
                Value result = evaluatePostfix(rpn, row);
                // Re-parse rpn each time since queue is consumed
                if (result.numVal != 0) {
                    filtered.push_back(row);
                }
            } catch (...) {
                // Row doesn't match
            }
        }
        // Fix: re-build rpn for each row (queue is consumed after evaluation)
        filtered.clear();
        queue<Token> rpnTemplate = shuntingYard(q.whereExpr);

        // Store rpn as vector so we can re-use it
        vector<Token> rpnVec;
        while (!rpnTemplate.empty()) {
            rpnVec.push_back(rpnTemplate.front());
            rpnTemplate.pop();
        }

        for (const Row& row : table.rows) {
            queue<Token> rpnCopy;
            for (auto& t : rpnVec) rpnCopy.push(t);
            try {
                Value result = evaluatePostfix(rpnCopy, row);
                if (result.numVal != 0) {
                    filtered.push_back(row);
                }
            } catch (...) { /* skip non-matching */ }
        }
    } else {
        filtered = table.rows;
    }

    // ORDER BY
    if (q.hasOrderBy) {
        string col = q.orderByCol;
        bool asc = q.orderAsc;
        sort(filtered.begin(), filtered.end(), [&](const Row& a, const Row& b) {
            Value va, vb;
            // Case-insensitive column lookup
            for (auto& kv : a) {
                if (toLower(kv.first) == toLower(col)) { va = kv.second; break; }
            }
            for (auto& kv : b) {
                if (toLower(kv.first) == toLower(col)) { vb = kv.second; break; }
            }

            if (va.isStr || vb.isStr) {
                return asc ? va.toString() < vb.toString() : va.toString() > vb.toString();
            }
            return asc ? va.numVal < vb.numVal : va.numVal > vb.numVal;
        });
    }

    // Project columns
    vector<vector<string>> result;
    for (const Row& row : filtered) {
        vector<string> rowData;
        for (const string& col : outColumns) {
            bool found = false;
            for (auto& kv : row) {
                if (toLower(kv.first) == toLower(col)) {
                    rowData.push_back(kv.second.toString());
                    found = true;
                    break;
                }
            }
            if (!found) rowData.push_back("NULL");
        }
        result.push_back(rowData);
    }

    return result;
}

void printResultTable(const vector<string>& columns,
                      const vector<vector<string>>& rows) {
    // Calculate column widths
    vector<int> widths(columns.size());
    for (size_t i = 0; i < columns.size(); i++) {
        widths[i] = columns[i].size();
    }
    for (const auto& row : rows) {
        for (size_t i = 0; i < row.size() && i < columns.size(); i++) {
            widths[i] = max(widths[i], (int)row[i].size());
        }
    }

    // Print separator line
    auto printSep = [&]() {
        cout << "+";
        for (size_t i = 0; i < columns.size(); i++) {
            cout << string(widths[i] + 2, '-') << "+";
        }
        cout << endl;
    };

    // Header
    printSep();
    cout << "|";
    for (size_t i = 0; i < columns.size(); i++) {
        cout << " " << left << setw(widths[i]) << columns[i] << " |";
    }
    cout << endl;
    printSep();

    // Data rows
    for (const auto& row : rows) {
        cout << "|";
        for (size_t i = 0; i < columns.size(); i++) {
            string val = (i < row.size()) ? row[i] : "NULL";
            cout << " " << left << setw(widths[i]) << val << " |";
        }
        cout << endl;
    }
    printSep();

    cout << rows.size() << " row(s) returned." << endl;
}


void demonstrateShuntingYard(const string& expression) {
    cout << "\n╔══════════════════════════════════════════════════════════╗" << endl;
    cout << "║  Shunting-Yard: Infix → Postfix (RPN) Conversion       ║" << endl;
    cout << "╚══════════════════════════════════════════════════════════╝" << endl;
    cout << "  Input (infix): " << expression << endl;

    vector<Token> tokens = tokenize(expression);

    // Step-by-step trace
    queue<Token> outputQ;
    stack<Token> opStack;
    int step = 0;

    cout << "\n  Step-by-step trace:" << endl;
    cout << "  " << string(60, '-') << endl;
    cout << "  " << left << setw(6) << "Step"
         << setw(16) << "Token"
         << setw(20) << "Action"
         << "Output Queue" << endl;
    cout << "  " << string(60, '-') << endl;

    for (const Token& tok : tokens) {
        if (tok.type == TokenType::END_OF_INPUT) break;
        step++;
        string action;

        if (tok.type == TokenType::NUMBER || tok.type == TokenType::IDENTIFIER ||
            tok.type == TokenType::STRING_LIT) {
            outputQ.push(tok);
            action = "→ Output";
        }
        else if (isOperator(tok)) {
            while (!opStack.empty() && isOperator(opStack.top()) &&
                   ((isLeftAssoc(tok.value) && precedence(tok.value) <= precedence(opStack.top().value)) ||
                    (!isLeftAssoc(tok.value) && precedence(tok.value) < precedence(opStack.top().value)))) {
                outputQ.push(opStack.top());
                opStack.pop();
            }
            opStack.push(tok);
            action = "→ Op Stack";
        }
        else if (tok.type == TokenType::LPAREN) {
            opStack.push(tok);
            action = "→ Op Stack";
        }
        else if (tok.type == TokenType::RPAREN) {
            while (!opStack.empty() && opStack.top().type != TokenType::LPAREN) {
                outputQ.push(opStack.top());
                opStack.pop();
            }
            if (!opStack.empty()) opStack.pop();
            action = "Pop until (";
        }

        // Print the current output queue
        string qStr;
        queue<Token> tempQ = outputQ;
        while (!tempQ.empty()) {
            if (!qStr.empty()) qStr += " ";
            qStr += tempQ.front().value;
            tempQ.pop();
        }

        cout << "  " << left << setw(6) << step
             << setw(16) << tok.value
             << setw(20) << action
             << qStr << endl;
    }

    // Flush remaining operators
    while (!opStack.empty()) {
        step++;
        outputQ.push(opStack.top());
        string qStr;
        queue<Token> tempQ = outputQ;
        while (!tempQ.empty()) {
            if (!qStr.empty()) qStr += " ";
            qStr += tempQ.front().value;
            tempQ.pop();
        }
        cout << "  " << left << setw(6) << step
             << setw(16) << opStack.top().value
             << setw(20) << "Flush stack"
             << qStr << endl;
        opStack.pop();
    }
    cout << "  " << string(60, '-') << endl;

    // Final RPN
    string rpnStr;
    while (!outputQ.empty()) {
        if (!rpnStr.empty()) rpnStr += " ";
        rpnStr += outputQ.front().value;
        outputQ.pop();
    }
    cout << "  Output (postfix/RPN): " << rpnStr << endl;

    // Evaluate numerically if possible (no column references)
    try {
        queue<Token> rpn = shuntingYard(tokens);
        Row emptyRow;
        Value result = evaluatePostfix(rpn, emptyRow);
        cout << "  Evaluated result:    " << result.toString() << endl;
    } catch (...) {
        cout << "  (Contains column references — needs row context to evaluate)" << endl;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Part 8 — Sample data & main()
// ─────────────────────────────────────────────────────────────────────────────

Table buildEmployeeTable() {
    Table t;
    t.name = "employees";
    t.columnOrder = {"id", "name", "department", "salary", "age"};

    auto addRow = [&](int id, const string& name, const string& dept, double salary, int age) {
        Row r;
        r["id"]         = Value((double)id);
        r["name"]       = Value(name);
        r["department"] = Value(dept);
        r["salary"]     = Value(salary);
        r["age"]        = Value((double)age);
        t.rows.push_back(r);
    };

    addRow(1,  "Alice",   "Engineering", 95000,  30);
    addRow(2,  "Bob",     "Engineering", 88000,  28);
    addRow(3,  "Charlie", "Marketing",   72000,  35);
    addRow(4,  "Diana",   "Engineering", 102000, 32);
    addRow(5,  "Eve",     "HR",          65000,  29);
    addRow(6,  "Frank",   "Marketing",   78000,  40);
    addRow(7,  "Grace",   "HR",          70000,  33);
    addRow(8,  "Hank",    "Engineering", 115000, 45);
    addRow(9,  "Ivy",     "Marketing",   68000,  26);
    addRow(10, "Jack",    "HR",          60000,  24);

    return t;
}

int main() {
    cout << "=========================================================" << endl;
    cout << "  Lab 7: Shunting-Yard Evaluator + SQL SELECT Parser     " << endl;
    cout << "=========================================================" << endl;

    // ── Part A: Shunting-Yard demonstrations ──
    cout << "\n━━━ PART A: Shunting-Yard Algorithm Demonstrations ━━━" << endl;

    demonstrateShuntingYard("3 + 4 * 2 / ( 1 - 5 )");
    demonstrateShuntingYard("( 10 + 20 ) * 3 - 5");
    demonstrateShuntingYard("salary > 80000 AND department = 'Engineering'");

    // ── Part B: SQL SELECT parser over vector<Row> ──
    cout << "\n\n━━━ PART B: SQL SELECT Parser over vector<Row> ━━━" << endl;

    // Build in-memory database
    unordered_map<string, Table> db;
    Table emp = buildEmployeeTable();
    db[toLower(emp.name)] = emp;

    // Show the full table
    cout << "\n── Sample Data: employees table ──" << endl;
    {
        vector<string> cols = emp.columnOrder;
        vector<vector<string>> allRows;
        for (auto& row : emp.rows) {
            vector<string> r;
            for (auto& c : cols) r.push_back(row.at(c).toString());
            allRows.push_back(r);
        }
        printResultTable(cols, allRows);
    }

    // Demo queries
    vector<string> demoQueries = {
        "SELECT * FROM employees",
        "SELECT name, salary FROM employees WHERE salary > 80000",
        "SELECT name, department, salary FROM employees WHERE department = 'Engineering' AND salary >= 95000",
        "SELECT name, age FROM employees ORDER BY age DESC",
        "SELECT name, salary FROM employees WHERE department = 'HR' OR department = 'Marketing' ORDER BY salary ASC",
    };

    for (const string& sql : demoQueries) {
        cout << "\n── Query: " << sql << " ──" << endl;
        try {
            SQLQuery q = parseSQL(sql);
            vector<string> outCols;
            auto results = executeSQL(q, db, outCols);
            printResultTable(outCols, results);
        } catch (const exception& e) {
            cout << "  ERROR: " << e.what() << endl;
        }
    }

    // ── Part C: Interactive REPL ──
    cout << "\n\n━━━ PART C: Interactive SQL REPL ━━━" << endl;
    cout << "Available table: employees (id, name, department, salary, age)" << endl;
    cout << "Type SQL queries or 'exit' to quit.\n" << endl;

    string line;
    while (true) {
        cout << "sql> ";
        if (!getline(cin, line)) break;
        line = trim(line);
        if (line.empty()) continue;
        if (toLower(line) == "exit" || toLower(line) == "quit") {
            cout << "Goodbye!" << endl;
            break;
        }

        // Check if it's a pure expression (no SELECT keyword)
        if (toUpper(line).find("SELECT") == string::npos) {
            cout << "Evaluating expression: " << line << endl;
            try {
                vector<Token> tokens = tokenize(line);
                queue<Token> rpn = shuntingYard(tokens);
                Row emptyRow;
                Value result = evaluatePostfix(rpn, emptyRow);
                cout << "  = " << result.toString() << endl;
            } catch (const exception& e) {
                cout << "  ERROR: " << e.what() << endl;
            }
            continue;
        }

        try {
            SQLQuery q = parseSQL(line);
            vector<string> outCols;
            auto results = executeSQL(q, db, outCols);
            printResultTable(outCols, results);
        } catch (const exception& e) {
            cout << "  ERROR: " << e.what() << endl;
        }
    }

    return 0;
}
