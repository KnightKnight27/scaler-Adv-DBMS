// Lab 5 (Part 1) - Shunting-Yard expression evaluator (header)
//
// Turns an infix expression like "age > 25 && salary * 1.1 < 90000" into
// postfix (RPN) using Dijkstra's shunting-yard algorithm, then evaluates
// the RPN with a stack. Variable names are looked up in a map the caller
// supplies (this is how the SQL executor feeds row column values in).

#ifndef LAB5_SHUNTING_YARD_H_
#define LAB5_SHUNTING_YARD_H_

#include <string>
#include <unordered_map>
#include <vector>

namespace lab5 {

// Split an expression string into tokens: numbers, identifiers,
// operators (+ - * / < > <= >= = != && ||) and parentheses.
std::vector<std::string> tokenize(const std::string& expr);

// Convert infix tokens to postfix (RPN) using shunting-yard.
std::vector<std::string> to_rpn(const std::vector<std::string>& tokens);

// Evaluate an RPN token list. Numbers are parsed directly; anything else
// is treated as a variable name and looked up in `vars`. Comparisons and
// logical operators return 1.0 (true) or 0.0 (false).
double eval_rpn(const std::vector<std::string>& rpn,
                const std::unordered_map<std::string, double>& vars);

}  // namespace lab5

#endif  // LAB5_SHUNTING_YARD_H_
