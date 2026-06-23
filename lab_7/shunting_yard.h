#pragma once
//
// shunting_yard.h
// ---------------------------------------------------------------------------
// THE CORE OF THIS LAB.
//
// Dijkstra's Shunting-Yard algorithm: convert an infix token stream (a WHERE
// expression) into Reverse Polish Notation (postfix), then evaluate the RPN
// against a single Row.
//
//   toRPN(tokens)        -> vector<Token>      (infix  -> postfix)
//   evalRPN(rpn, row)    -> Value              (postfix -> result)
//
// Boolean results are represented as integer Values 1 (true) / 0 (false),
// which lets AND/OR/NOT and the comparison operators share one value stack.
// ---------------------------------------------------------------------------

#include "lexer.h"
#include "value.h"
#include <vector>

// Convert an infix list of WHERE tokens into Reverse Polish Notation.
// The input may contain a trailing END token (it is ignored).
std::vector<Token> toRPN(const std::vector<Token> &tokens);

// Evaluate an RPN token list against one Row, returning a Value.
// For a WHERE predicate the top-level result is a 0/1 integer Value.
Value evalRPN(const std::vector<Token> &rpn, const Row &row);

// Convenience: compile + evaluate + coerce-to-bool, used by the executor.
bool evalPredicate(const std::vector<Token> &rpn, const Row &row);
