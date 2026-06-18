#include <algorithm>
#include <cctype>
#include <iostream>
#include <stack>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

struct Employee {
    std::string name;
    int id = 0;
    int age = 0;
};

struct ParsedQuery {
    std::string selectedColumn;
    std::string tableName;
    std::vector<std::string> postfixWhere;
};

std::string toUpper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) {
                       return std::toupper(c);
                   });
    return s;
}

std::vector<std::string> tokenize(const std::string& q) {

    std::vector<std::string> tokens;

    size_t i = 0;

    while (i < q.size()) {

        if (std::isspace(q[i])) {
            i++;
            continue;
        }

        if (std::isalpha(q[i]) || q[i] == '_') {

            std::string word;

            while (i < q.size() &&
                  (std::isalnum(q[i]) ||
                   q[i] == '_')) {
                word += q[i++];
            }

            std::string up = toUpper(word);

            if (up == "AND" || up == "OR")
                tokens.push_back(up);
            else
                tokens.push_back(word);

            continue;
        }

        if (std::isdigit(q[i])) {

            std::string num;

            while (i < q.size() &&
                   std::isdigit(q[i])) {
                num += q[i++];
            }

            tokens.push_back(num);
            continue;
        }

        if ((q[i]=='>' || q[i]=='<' || q[i]=='!') &&
             i+1<q.size() &&
             q[i+1]=='=') {

            tokens.push_back(
                std::string(1,q[i]) +
                q[i+1]);

            i+=2;
            continue;
        }

        tokens.push_back(
            std::string(1,q[i]));

        i++;
    }

    return tokens;
}

int precedence(const std::string& op) {

    if(op=="OR") return 1;
    if(op=="AND") return 2;

    if(op=="=" ||
       op=="!=")
        return 3;

    if(op=="<" ||
       op==">" ||
       op=="<="||
       op==">=")
        return 4;

    return 0;
}

bool isOperator(const std::string& s) {

    return s=="AND" ||
           s=="OR"  ||
           s=="="   ||
           s=="!="  ||
           s=="<"   ||
           s==">"   ||
           s=="<="  ||
           s==">=";
}

std::vector<std::string>
toPostfix(
    const std::vector<std::string>& tokens) {

    std::vector<std::string> output;
    std::stack<std::string> ops;

    for(const auto& token : tokens) {

        if(token=="(") {

            ops.push(token);
        }

        else if(token==")") {

            while(!ops.empty() &&
                  ops.top()!="(") {

                output.push_back(
                    ops.top());

                ops.pop();
            }

            ops.pop();
        }

        else if(isOperator(token)) {

            while(!ops.empty() &&
                  ops.top()!="(" &&
                  precedence(ops.top())
                  >= precedence(token)) {

                output.push_back(
                    ops.top());

                ops.pop();
            }

            ops.push(token);
        }

        else {

            output.push_back(token);
        }
    }

    while(!ops.empty()) {

        output.push_back(
            ops.top());

        ops.pop();
    }

    return output;
}

ParsedQuery parseQuery(
    const std::string& sql) {

    auto tokens = tokenize(sql);

    ParsedQuery query;

    size_t i = 0;

    if(toUpper(tokens[i])!="SELECT")
        throw std::runtime_error("SELECT expected");

    query.selectedColumn =
        tokens[++i];

    i++;

    if(toUpper(tokens[i])!="FROM")
        throw std::runtime_error("FROM expected");

    query.tableName =
        tokens[++i];

    i++;

    if(toUpper(tokens[i])!="WHERE")
        throw std::runtime_error("WHERE expected");

    i++;

    std::vector<std::string>
        whereTokens;

    while(i < tokens.size())
        whereTokens.push_back(
            tokens[i++]);

    query.postfixWhere =
        toPostfix(whereTokens);

    return query;
}

int getFieldValue(
    const Employee& e,
    const std::string& col) {

    if(col=="id")
        return e.id;

    if(col=="age")
        return e.age;

    throw std::runtime_error(
        "Unknown column");
}

bool evaluatePostfix(
    const std::vector<std::string>& postfix,
    const Employee& e) {

    std::stack<int> st;

    for(const auto& token : postfix) {

        if(!isOperator(token)) {

            if(std::isdigit(token[0]))
                st.push(
                    std::stoi(token));

            else
                st.push(
                    getFieldValue(
                        e,
                        token));

            continue;
        }

        int rhs = st.top();
        st.pop();

        int lhs = st.top();
        st.pop();

        if(token==">")
            st.push(lhs>rhs);

        else if(token=="<")
            st.push(lhs<rhs);

        else if(token==">=")
            st.push(lhs>=rhs);

        else if(token=="<=")
            st.push(lhs<=rhs);

        else if(token=="=")
            st.push(lhs==rhs);

        else if(token=="!=")
            st.push(lhs!=rhs);

        else if(token=="AND")
            st.push(lhs && rhs);

        else if(token=="OR")
            st.push(lhs || rhs);
    }

    return st.top();
}

void runQuery(
    const std::string& sql,
    const std::vector<Employee>& employees) {

    auto query =
        parseQuery(sql);

    std::cout
        << "\nQuery: "
        << sql
        << "\n";

    for(const auto& e : employees) {

        if(!evaluatePostfix(
                query.postfixWhere,
                e))
            continue;

        if(query.selectedColumn=="name")
            std::cout
                << e.name
                << '\n';

        else if(query.selectedColumn=="id")
            std::cout
                << e.id
                << '\n';

        else if(query.selectedColumn=="age")
            std::cout
                << e.age
                << '\n';
    }
}

int main() {

    std::vector<Employee> employees = {
        {"Rama Krishnan",1,19},
        {"Aarav",2,20},
        {"Karan",3,19},
        {"Sneha",4,21},
        {"Vivaan",5,20},
        {"Ishaan",6,31},
        {"Meera",7,22},
        {"Devansh",8,33}
    };

    runQuery(
      "SELECT name FROM employees WHERE id >= 3 OR age < 20",
      employees);

    runQuery(
      "SELECT name FROM employees WHERE id > 3 AND age >= 30",
      employees);

    runQuery(
      "SELECT id FROM employees WHERE ( age < 25 AND id != 2 ) OR age >= 30",
      employees);

    return 0;
}