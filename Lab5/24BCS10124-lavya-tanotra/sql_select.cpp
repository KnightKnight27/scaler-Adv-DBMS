// Name: Lavya Tanotra
// Roll No: 24BCS10124
// Lab 5: Shunting-Yard expression evaluator + minimal SQL SELECT parser
//
// Part 1: Dijkstra's shunting-yard turns an infix WHERE expression into RPN and
//         evaluates it (arithmetic, comparisons, AND/OR) against a row's columns.
// Part 2: a tiny SELECT engine runs  SELECT <cols> FROM t [WHERE <expr>]  over an
//         in-memory vector<Row>, exactly like a WHERE clause filters fetched rows.

#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>
#include <stack>
#include <sstream>
#include <cctype>

using Row = std::unordered_map<std::string, int>;

// ---- tokenizer ----------------------------------------------------------------
static std::vector<std::string> lex(const std::string& s) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < s.size()) {
        if (std::isspace((unsigned char)s[i])) { ++i; continue; }
        if (std::isalpha((unsigned char)s[i])) {            // identifier / keyword
            size_t j = i; while (j < s.size() && (std::isalnum((unsigned char)s[j]) || s[j]=='_')) ++j;
            out.push_back(s.substr(i, j - i)); i = j;
        } else if (std::isdigit((unsigned char)s[i])) {     // number
            size_t j = i; while (j < s.size() && std::isdigit((unsigned char)s[j])) ++j;
            out.push_back(s.substr(i, j - i)); i = j;
        } else if (s[i]=='>'||s[i]=='<'||s[i]=='=') {        // comparison (>, <, >=, <=, ==)
            std::string op(1, s[i++]);
            if (i < s.size() && s[i]=='=') op += s[i++];
            out.push_back(op);
        } else { out.emplace_back(1, s[i++]); }             // + - * / ( )
    }
    return out;
}

// ---- shunting-yard: precedence ------------------------------------------------
static int prec(const std::string& op) {
    if (op=="*"||op=="/") return 5;
    if (op=="+"||op=="-") return 4;
    if (op==">"||op=="<"||op==">="||op=="<="||op=="==") return 3;
    if (op=="AND") return 2;
    if (op=="OR")  return 1;
    return 0;
}
static bool isOp(const std::string& t) { return prec(t) > 0; }

static std::vector<std::string> toRPN(const std::vector<std::string>& toks) {
    std::vector<std::string> out;
    std::stack<std::string> ops;
    for (const auto& t : toks) {
        if (t=="(") ops.push(t);
        else if (t==")") {
            while (!ops.empty() && ops.top()!="(") { out.push_back(ops.top()); ops.pop(); }
            if (!ops.empty()) ops.pop();                    // discard "("
        } else if (isOp(t)) {
            while (!ops.empty() && isOp(ops.top()) && prec(ops.top()) >= prec(t)) {
                out.push_back(ops.top()); ops.pop();
            }
            ops.push(t);
        } else out.push_back(t);                            // operand (number or column)
    }
    while (!ops.empty()) { out.push_back(ops.top()); ops.pop(); }
    return out;
}

// ---- evaluate RPN against a row -----------------------------------------------
static int evalRPN(const std::vector<std::string>& rpn, const Row& row) {
    std::stack<int> st;
    for (const auto& t : rpn) {
        if (isOp(t)) {
            int b = st.top(); st.pop();
            int a = st.top(); st.pop();
            int r = 0;
            if      (t=="+") r = a + b;      else if (t=="-") r = a - b;
            else if (t=="*") r = a * b;      else if (t=="/") r = b ? a / b : 0;
            else if (t==">") r = a > b;      else if (t=="<") r = a < b;
            else if (t==">=")r = a >= b;     else if (t=="<=")r = a <= b;
            else if (t=="==")r = (a == b);
            else if (t=="AND") r = (a && b); else if (t=="OR") r = (a || b);
            st.push(r);
        } else if (std::isdigit((unsigned char)t[0])) {
            st.push(std::stoi(t));
        } else {                                            // column reference
            auto it = row.find(t);
            st.push(it == row.end() ? 0 : it->second);
        }
    }
    return st.empty() ? 0 : st.top();
}

// ---- minimal SELECT runner ----------------------------------------------------
// supports:  SELECT a,b,... FROM t [WHERE <expr>]
static void runSelect(const std::string& query, const std::vector<Row>& table) {
    std::istringstream in(query);
    std::string kw; in >> kw;                               // SELECT
    std::string colList; in >> colList;                     // a,b,c
    std::string from; in >> from;                           // FROM
    std::string tname; in >> tname;                         // table name (ignored, one table)

    std::vector<std::string> cols;
    std::stringstream cs(colList); std::string c;
    while (std::getline(cs, c, ',')) cols.push_back(c);

    std::string whereWord; std::vector<std::string> rpn;
    if (in >> whereWord && whereWord == "WHERE") {
        std::string rest, w; while (in >> w) rest += w + " ";
        rpn = toRPN(lex(rest));
    }

    // header
    for (size_t i = 0; i < cols.size(); ++i) std::cout << cols[i] << (i+1<cols.size()? " | ":"");
    std::cout << "\n" << std::string(cols.size()*5, '-') << "\n";

    int matched = 0;
    for (const Row& r : table) {
        if (!rpn.empty() && !evalRPN(rpn, r)) continue;     // WHERE filter
        for (size_t i = 0; i < cols.size(); ++i) {
            auto it = r.find(cols[i]);
            std::cout << (it==r.end()?0:it->second) << (i+1<cols.size()? " | ":"");
        }
        std::cout << "\n";
        ++matched;
    }
    std::cout << "(" << matched << " rows)\n\n";
}

int main() {
    // in-memory "table": employees(id, age, salary, dept_id)
    std::vector<Row> employees = {
        {{"id",1},{"age",30},{"salary",500},{"dept_id",1}},
        {{"id",2},{"age",25},{"salary",300},{"dept_id",2}},
        {{"id",3},{"age",40},{"salary",700},{"dept_id",1}},
        {{"id",4},{"age",35},{"salary",450},{"dept_id",3}},
    };

    std::cout << "=== Part 1: shunting-yard eval on row id=3 ===\n";
    Row sample = employees[2];
    auto rpn = toRPN(lex("age > 30 AND salary >= 500"));
    std::cout << "  (age>30 AND salary>=500) = " << evalRPN(rpn, sample) << "\n\n";

    std::cout << "=== Part 2: SELECT queries ===\n";
    runSelect("SELECT id,age,salary FROM employees", employees);
    runSelect("SELECT id,salary FROM employees WHERE salary > 400 AND age < 40", employees);
    runSelect("SELECT id,dept_id FROM employees WHERE dept_id == 1 OR age > 34", employees);
    return 0;
}
