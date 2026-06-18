#ifndef SHUNTING_YARD_H
#define SHUNTING_YARD_H

#include <string>
#include <vector>
#include <unordered_map>

struct OpInfo { 
    int precedence; 
    bool right_assoc; 
};

extern const std::unordered_map<std::string, OpInfo> OPS;

// Core tokenization & RPN translations
std::vector<std::string> tokenize(const std::string& expr);
std::vector<std::string> to_rpn(const std::vector<std::string>& tokens);
double eval_rpn(const std::vector<std::string>& rpn, const std::unordered_map<std::string, double>& vars);

void shunting_demo();

#endif