#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <iostream>

using Row = std::unordered_map<std::string, double>;

class SQLEngine {
private:
    int GetPrecedence(const std::string& op) const;
    
    bool IsNumber(const std::string& token) const;

    bool IsOperator(const std::string& token) const;

    std::vector<std::string> Tokenize(const std::string& expression) const;
    std::vector<std::string> InfixToPostfix(const std::vector<std::string>& tokens) const;
    
    double EvaluatePostfix(const std::vector<std::string>& postfix, const Row& row) const;

public:
    void ExecuteSelect(const std::string& query, const std::vector<Row>& table) const;
};