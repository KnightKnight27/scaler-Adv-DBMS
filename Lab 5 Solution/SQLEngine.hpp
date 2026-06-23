#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <iostream>

// Type alias representing a single database row mapping column names to values
using Row = std::unordered_map<std::string, double>;



// SQLEngine Class Declaration

class SQLEngine {
private:
    // --- Helper Primitives ---
    // Returns the operational weight/priority of mathematical and logical tokens
    int GetPrecedence(const std::string& op) const;
    
    // Verifies if a given string token parses as a valid numeric literal
    bool IsNumber(const std::string& token) const;

    // Determines if a token is a recognized operator based on precedence rules
    bool IsOperator(const std::string& token) const;

    // --- Query Compilation Pipeline ---
    // Breaks a raw WHERE string clause into localized atomic tokens
    std::vector<std::string> Tokenize(const std::string& expression) const;

    // Reorders infix expression tokens into an evaluation-ready postfix vector
    std::vector<std::string> InfixToPostfix(const std::vector<std::string>& tokens) const;
    
    // Evaluates a structured postfix expression against a row's column mapping
    double EvaluatePostfix(const std::vector<std::string>& postfix, const Row& row) const;

public:
    // --- Public Execution API ---
    // Compiles and runs a standard SELECT ... WHERE statement against a dynamic database table
    void ExecuteSelect(const std::string& query, const std::vector<Row>& table) const;
};