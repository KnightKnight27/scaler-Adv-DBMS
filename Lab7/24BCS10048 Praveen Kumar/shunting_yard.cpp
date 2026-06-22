/*
 * =============================================================================
 *  Lab 7 -- Part B: Shunting-Yard Expression Evaluator
 * =============================================================================
 *
 *  Course  : Advanced DBMS (Scaler)
 *  Author  : Praveen Kumar
 *  Date    : 2026-06-22
 *
 *  Purpose : Implement Dijkstra's shunting-yard algorithm to convert an
 *            infix expression to postfix (Reverse Polish Notation) and
 *            evaluate it.
 *
 *  In a database context this is how the WHERE clause evaluator works:
 *  after the lexer splits "age > 25 AND id < 5 OR dept = HR" into tokens,
 *  the expression evaluator must respect precedence:
 *      AND binds tighter than OR  (just like * binds tighter than +)
 *      Parentheses override everything
 *
 *  The shunting-yard algorithm is an alternative to recursive descent.
 *  Both solve the same problem in different styles.
 *
 *  Supported operators (for arithmetic demo):
 *      +  -  *  /  ^          (standard arithmetic)
 *  Supported logical operators (for SQL demo):
 *      AND  OR  NOT           (AND > OR in precedence)
 *      >  <  =  >=  <=  !=   (comparisons)
 *
 *  Build  : g++ -std=c++17 -O2 -Wall -Wextra -o shunting_yard shunting_yard.cpp
 *  Run    : ./shunting_yard
 * =============================================================================
 */

#include <iostream>
#include <string>
#include <vector>
#include <stack>
#include <deque>
#include <stdexcept>
#include <sstream>
#include <cmath>
#include <iomanip>
#include <map>

/* ===========================================================================
 *  Token types for the expression evaluator
 * =========================================================================== */

enum class Kind { NUMBER, OP, LPAREN, RPAREN };

struct ETok {
    Kind        kind;
    std::string text;
    double      num;        /* valid when kind == NUMBER */
};

/* ===========================================================================
 *  Operator metadata: precedence and associativity
 * =========================================================================== */

struct OpInfo {
    int  prec;          /* higher = tighter binding */
    bool left_assoc;    /* true = left, false = right (e.g., ^ is right) */
};

static const std::map<std::string, OpInfo> OP_TABLE = {
    {"OR",  {1, true}},
    {"AND", {2, true}},
    {"=",   {3, true}},
    {"!=",  {3, true}},
    {"<",   {3, true}},
    {">",   {3, true}},
    {"<=",  {3, true}},
    {">=",  {3, true}},
    {"+",   {4, true}},
    {"-",   {4, true}},
    {"*",   {5, true}},
    {"/",   {5, true}},
    {"^",   {6, false}},    /* exponentiation, right-associative */
    {"NOT", {7, false}},    /* unary NOT, right-associative, highest */
};

static bool is_op(const std::string &s) { return OP_TABLE.count(s) > 0; }

static const OpInfo &op_info(const std::string &s)
{
    auto it = OP_TABLE.find(s);
    if (it == OP_TABLE.end())
        throw std::runtime_error("Unknown operator: " + s);
    return it->second;
}

/* ===========================================================================
 *  Tokenizer for infix expressions
 *
 *  Handles:
 *    integers and decimals  (42, 3.14)
 *    multi-char operators   (<=, >=, !=)
 *    keywords               (AND, OR, NOT)
 *    identifiers treated as named values (age, id, dept)
 *    parentheses
 * =========================================================================== */

static std::vector<ETok> lex_expr(const std::string &src)
{
    std::vector<ETok> out;
    size_t i = 0;

    auto peek = [&]() -> char { return i < src.size() ? src[i] : '\0'; };
    auto next = [&]() -> char { return src[i++]; };

    while (i < src.size()) {
        if (std::isspace(peek())) { next(); continue; }

        /* number */
        if (std::isdigit(peek()) || (peek() == '-' && out.empty())) {
            std::string s;
            if (peek() == '-') s += next();
            while (i < src.size() && (std::isdigit(peek()) || peek() == '.'))
                s += next();
            out.push_back({Kind::NUMBER, s, std::stod(s)});
            continue;
        }

        /* identifier or keyword (AND, OR, NOT, or a named value) */
        if (std::isalpha(peek()) || peek() == '_') {
            std::string word;
            while (i < src.size() && (std::isalnum(peek()) || peek() == '_'))
                word += next();
            /* uppercase for keyword matching */
            std::string up = word;
            for (char &c : up) c = (char)std::toupper(c);

            if (up == "AND" || up == "OR" || up == "NOT") {
                out.push_back({Kind::OP, up, 0});
            } else {
                /* treat identifier as numeric 0 placeholder for structure demo */
                out.push_back({Kind::NUMBER, word, 0});
            }
            continue;
        }

        /* two-char operators */
        char c = next();
        if ((c == '<' || c == '>' || c == '!') && peek() == '=') {
            std::string op(1, c); op += '='; next();
            out.push_back({Kind::OP, op, 0});
            continue;
        }

        switch (c) {
        case '(': out.push_back({Kind::LPAREN, "(", 0}); break;
        case ')': out.push_back({Kind::RPAREN, ")", 0}); break;
        case '+': out.push_back({Kind::OP, "+", 0}); break;
        case '-': out.push_back({Kind::OP, "-", 0}); break;
        case '*': out.push_back({Kind::OP, "*", 0}); break;
        case '/': out.push_back({Kind::OP, "/", 0}); break;
        case '^': out.push_back({Kind::OP, "^", 0}); break;
        case '<': out.push_back({Kind::OP, "<", 0}); break;
        case '>': out.push_back({Kind::OP, ">", 0}); break;
        case '=': out.push_back({Kind::OP, "=", 0}); break;
        default: break;
        }
    }

    return out;
}

/* ===========================================================================
 *  Shunting-Yard: infix tokens -> postfix (RPN) token list
 *
 *  Algorithm (Dijkstra 1961):
 *    For each token t:
 *      NUMBER  -> push to output queue
 *      OP      -> while top of op-stack has >= precedence (and left-assoc):
 *                     pop top to output
 *                 push t onto op-stack
 *      LPAREN  -> push onto op-stack
 *      RPAREN  -> pop from op-stack to output until LPAREN found;
 *                 discard the LPAREN
 *    At end: drain op-stack to output
 * =========================================================================== */

static std::vector<ETok> shunting_yard(const std::vector<ETok> &tokens)
{
    std::vector<ETok> output;
    std::stack<ETok>  op_stack;

    for (const auto &t : tokens) {
        if (t.kind == Kind::NUMBER) {
            output.push_back(t);
        }
        else if (t.kind == Kind::OP) {
            const OpInfo &ti = op_info(t.text);

            while (!op_stack.empty() &&
                   op_stack.top().kind == Kind::OP) {
                const OpInfo &top = op_info(op_stack.top().text);
                bool should_pop =
                    (ti.left_assoc  && top.prec >= ti.prec) ||
                    (!ti.left_assoc && top.prec >  ti.prec);
                if (!should_pop) break;
                output.push_back(op_stack.top());
                op_stack.pop();
            }
            op_stack.push(t);
        }
        else if (t.kind == Kind::LPAREN) {
            op_stack.push(t);
        }
        else if (t.kind == Kind::RPAREN) {
            while (!op_stack.empty() && op_stack.top().kind != Kind::LPAREN) {
                output.push_back(op_stack.top());
                op_stack.pop();
            }
            if (op_stack.empty())
                throw std::runtime_error("Mismatched parentheses");
            op_stack.pop(); /* discard LPAREN */
        }
    }

    while (!op_stack.empty()) {
        if (op_stack.top().kind == Kind::LPAREN)
            throw std::runtime_error("Mismatched parentheses");
        output.push_back(op_stack.top());
        op_stack.pop();
    }

    return output;
}

/* ===========================================================================
 *  Postfix (RPN) evaluator for arithmetic expressions
 *
 *  For non-numeric tokens (identifiers like "age") the value is 0 in this
 *  demo since we focus on the structural / precedence transformation, not
 *  actual SQL evaluation (Part A handles that with full row data).
 * =========================================================================== */

static double eval_rpn(const std::vector<ETok> &rpn)
{
    std::stack<double> stk;

    for (const auto &t : rpn) {
        if (t.kind == Kind::NUMBER) {
            stk.push(t.num);
        } else if (t.kind == Kind::OP) {
            /* NOT is unary */
            if (t.text == "NOT") {
                if (stk.empty()) throw std::runtime_error("Stack underflow");
                double a = stk.top(); stk.pop();
                stk.push(a == 0.0 ? 1.0 : 0.0);
                continue;
            }
            /* all others are binary */
            if (stk.size() < 2) throw std::runtime_error("Stack underflow");
            double b = stk.top(); stk.pop();
            double a = stk.top(); stk.pop();
            if (t.text == "+")  stk.push(a + b);
            else if (t.text == "-")  stk.push(a - b);
            else if (t.text == "*")  stk.push(a * b);
            else if (t.text == "/")  stk.push(b != 0 ? a / b : 0);
            else if (t.text == "^")  stk.push(std::pow(a, b));
            else if (t.text == "AND") stk.push((a != 0 && b != 0) ? 1.0 : 0.0);
            else if (t.text == "OR")  stk.push((a != 0 || b != 0) ? 1.0 : 0.0);
            else if (t.text == "=")  stk.push(a == b ? 1.0 : 0.0);
            else if (t.text == "!=") stk.push(a != b ? 1.0 : 0.0);
            else if (t.text == "<")  stk.push(a <  b ? 1.0 : 0.0);
            else if (t.text == ">")  stk.push(a >  b ? 1.0 : 0.0);
            else if (t.text == "<=") stk.push(a <= b ? 1.0 : 0.0);
            else if (t.text == ">=") stk.push(a >= b ? 1.0 : 0.0);
        }
    }

    return stk.empty() ? 0.0 : stk.top();
}

/* ===========================================================================
 *  Demo runner: prints the step-by-step conversion
 * =========================================================================== */

static void demo(const std::string &infix, const std::string &note = "")
{
    std::cout << "\n  Infix   : " << infix << "\n";
    if (!note.empty()) std::cout << "  Note    : " << note << "\n";

    auto tokens = lex_expr(infix);
    auto rpn    = shunting_yard(tokens);

    /* Print postfix */
    std::cout << "  Postfix : ";
    for (const auto &t : rpn) std::cout << t.text << " ";
    std::cout << "\n";

    /* Print evaluation (only for pure arithmetic where identifiers are 0) */
    bool has_ident = false;
    for (const auto &t : tokens)
        if (t.kind == Kind::NUMBER && !std::isdigit(t.text[0]) && t.text[0] != '-')
            has_ident = true;

    if (!has_ident) {
        double result = eval_rpn(rpn);
        /* print as integer if whole number */
        if (result == (long long)result)
            std::cout << "  Result  : " << (long long)result << "\n";
        else
            std::cout << "  Result  : " << result << "\n";
    }
}

/* ===========================================================================
 *  Step-by-step trace
 * =========================================================================== */

static void trace(const std::string &infix)
{
    std::cout << "\n  Step-by-step shunting-yard trace for: " << infix << "\n";
    std::cout << "  " << std::string(60, '-') << "\n";
    std::cout << "  " << std::left << std::setw(14) << "Token"
              << std::setw(30) << "Output queue"
              << "Op stack\n";
    std::cout << "  " << std::string(60, '-') << "\n";

    auto tokens = lex_expr(infix);
    std::vector<ETok> output;
    std::stack<ETok>  op_stack;

    auto show = [&](const std::string &tok_str) {
        std::string out_str;
        for (const auto &t : output) out_str += t.text + " ";

        /* Build stack string (bottom to top) */
        std::stack<ETok> tmp = op_stack;
        std::vector<std::string> stk_items;
        while (!tmp.empty()) { stk_items.push_back(tmp.top().text); tmp.pop(); }
        std::string stk_str;
        for (int j = (int)stk_items.size()-1; j >= 0; --j)
            stk_str += stk_items[j] + " ";

        std::cout << "  " << std::left << std::setw(14) << tok_str
                  << std::setw(30) << out_str
                  << stk_str << "\n";
    };

    for (const auto &t : tokens) {
        if (t.kind == Kind::NUMBER) {
            output.push_back(t);
            show(t.text);
        }
        else if (t.kind == Kind::OP) {
            const OpInfo &ti = op_info(t.text);
            while (!op_stack.empty() && op_stack.top().kind == Kind::OP) {
                const OpInfo &top = op_info(op_stack.top().text);
                bool should_pop =
                    (ti.left_assoc && top.prec >= ti.prec) ||
                    (!ti.left_assoc && top.prec > ti.prec);
                if (!should_pop) break;
                output.push_back(op_stack.top());
                op_stack.pop();
            }
            op_stack.push(t);
            show(t.text);
        }
        else if (t.kind == Kind::LPAREN) {
            op_stack.push(t);
            show("(");
        }
        else if (t.kind == Kind::RPAREN) {
            while (!op_stack.empty() && op_stack.top().kind != Kind::LPAREN) {
                output.push_back(op_stack.top());
                op_stack.pop();
            }
            if (!op_stack.empty()) op_stack.pop();
            show(")");
        }
    }
    while (!op_stack.empty()) {
        output.push_back(op_stack.top());
        op_stack.pop();
        show("[drain]");
    }

    std::cout << "\n  Final postfix: ";
    for (const auto &t : output) std::cout << t.text << " ";
    std::cout << "  ->  " << eval_rpn(output) << "\n";
}

/* ===========================================================================
 *  main
 * =========================================================================== */

int main()
{
    std::cout << "============================================================\n";
    std::cout << "  Lab 7 -- Part B: Shunting-Yard Expression Evaluator\n";
    std::cout << "============================================================\n";

    std::cout << "\n--- Arithmetic expressions (precedence + associativity) ---\n";
    demo("2 + 3 * 4",         "* before +: result is 14, not 20");
    demo("(2 + 3) * 4",       "parens force + first: result is 20");
    demo("2 ^ 3 ^ 2",         "right-assoc ^: 2^(3^2) = 512, not (2^3)^2 = 64");
    demo("10 - 2 - 3",        "left-assoc -: (10-2)-3 = 5");
    demo("100 / 5 / 4",       "left-assoc /: (100/5)/4 = 5");
    demo("2 + 3 * 4 - 1",     "mixed: 2 + 12 - 1 = 13");

    std::cout << "\n--- SQL WHERE clause precedence (AND binds tighter than OR) ---\n";
    demo("1 AND 0 OR 1",  "AND first: (1 AND 0) OR 1 = 1");
    demo("1 OR 0 AND 0",  "AND first: 1 OR (0 AND 0) = 1");
    demo("NOT 0 AND 1",   "NOT > AND: (NOT 0) AND 1 = 1");

    std::cout << "\n--- Step-by-step trace ---\n";
    trace("2 + 3 * 4");
    trace("(2 + 3) * 4");

    std::cout << "\n============================================================\n";
    std::cout << "  Done.\n";
    std::cout << "============================================================\n";

    return 0;
}
