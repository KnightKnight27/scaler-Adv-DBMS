#include <algorithm>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

using namespace std;

// Functions implemented in shunting_yard.cpp.
string toUpper(string text);
vector<string> tokenizeExpression(const string& expression);
vector<string> toRpn(const vector<string>& tokens);
double evaluateRpn(const vector<string>& rpn, const unordered_map<string, double>& variables);
void shuntingDemo();

using Value = variant<double, string>;

struct Row {
    unordered_map<string, Value> columns;
};

double numericValue(const Row& row, const string& column) {
    auto found = row.columns.find(column);
    if(found == row.columns.end()) return 0.0;

    if(const double* number = get_if<double>(&found->second)) return *number;

    if(const string* text = get_if<string>(&found->second)) {
        try {
            return stod(*text);
        }
        catch(const exception&) {
            return 0.0;
        }
    }

    return 0.0;
}

struct SelectQuery {
    vector<string> columns;  // Empty means SELECT *
    string table;
    string whereExpression;
    string orderBy;
    bool ascending = true;
    int limit = -1;
};

vector<string> sqlWords(const string& sql) {
    istringstream input(sql);
    vector<string> words;
    string word;

    while(input >> word) {
        if(!word.empty() && word.back() == ';') word.pop_back();
        words.push_back(word);
    }
    return words;
}

void addColumns(const string& text, vector<string>& columns) {
    stringstream input(text);
    string column;

    while(getline(input, column, ',')) {
        if(column.empty()) continue;
        if(column == "*") columns.clear();
        else columns.push_back(column);
    }
}

SelectQuery parseSelect(const string& sql) {
    vector<string> words = sqlWords(sql);
    SelectQuery query;
    size_t index = 0;

    if(words.empty() || toUpper(words[index]) != "SELECT") {
        throw runtime_error("Query must start with SELECT");
    }
    ++index;

    while(index < words.size() && toUpper(words[index]) != "FROM") {
        addColumns(words[index], query.columns);
        ++index;
    }

    if(index >= words.size() || toUpper(words[index]) != "FROM") {
        throw runtime_error("Missing FROM clause");
    }
    ++index;

    if(index >= words.size()) throw runtime_error("Missing table name");
    query.table = words[index++];

    while(index < words.size()) {
        string keyword = toUpper(words[index]);

        if(keyword == "WHERE") {
            ++index;
            string expression;

            while(index < words.size() &&
                  toUpper(words[index]) != "ORDER" &&
                  toUpper(words[index]) != "LIMIT") {
                if(!expression.empty()) expression += ' ';
                expression += words[index++];
            }
            query.whereExpression = expression;
        }
        else if(keyword == "ORDER") {
            ++index;
            if(index >= words.size() || toUpper(words[index]) != "BY") {
                throw runtime_error("ORDER must be followed by BY");
            }
            ++index;

            if(index >= words.size()) throw runtime_error("Missing ORDER BY column");
            query.orderBy = words[index++];

            if(index < words.size()) {
                string direction = toUpper(words[index]);
                if(direction == "ASC" || direction == "DESC") {
                    query.ascending = direction == "ASC";
                    ++index;
                }
            }
        }
        else if(keyword == "LIMIT") {
            ++index;
            if(index >= words.size()) throw runtime_error("Missing LIMIT value");
            query.limit = stoi(words[index++]);
        }
        else {
            throw runtime_error("Unknown SQL clause: " + words[index]);
        }
    }

    return query;
}

vector<Row> execute(const SelectQuery& query, const vector<Row>& data) {
    vector<string> whereRpn;
    if(!query.whereExpression.empty()) {
        whereRpn = toRpn(tokenizeExpression(query.whereExpression));
    }

    vector<Row> result;

    for(const Row& row : data) {
        if(!whereRpn.empty()) {
            unordered_map<string, double> variables;
            for(const auto& column : row.columns) {
                variables[column.first] = numericValue(row, column.first);
            }

            if(evaluateRpn(whereRpn, variables) == 0) continue;
        }

        if(query.columns.empty()) {
            result.push_back(row);
        }
        else {
            Row projected;
            for(const string& column : query.columns) {
                auto found = row.columns.find(column);
                if(found != row.columns.end()) {
                    projected.columns[column] = found->second;
                }
            }
            result.push_back(projected);
        }
    }

    if(!query.orderBy.empty()) {
        sort(result.begin(), result.end(), [&](const Row& left, const Row& right) {
            double leftValue = numericValue(left, query.orderBy);
            double rightValue = numericValue(right, query.orderBy);
            return query.ascending ? leftValue < rightValue : leftValue > rightValue;
        });
    }

    if(query.limit >= 0 && static_cast<int>(result.size()) > query.limit) {
        result.resize(query.limit);
    }

    return result;
}

void printValue(const Value& value) {
    if(const double* number = get_if<double>(&value)) cout << *number;
    else if(const string* text = get_if<string>(&value)) cout << *text;
}

void printRows(const vector<Row>& rows, const vector<string>& selectedColumns) {
    for(const Row& row : rows) {
        if(selectedColumns.empty()) {
            for(const auto& column : row.columns) {
                cout << column.first << '=';
                printValue(column.second);
                cout << "  ";
            }
        }
        else {
            for(const string& name : selectedColumns) {
                auto found = row.columns.find(name);
                if(found != row.columns.end()) {
                    cout << name << '=';
                    printValue(found->second);
                    cout << "  ";
                }
            }
        }
        cout << '\n';
    }
}

int main() {
    try {
        shuntingDemo();

        vector<Row> students = {
            {{{"id", 1.0}, {"name", string("Alice")}, {"age", 22.0}, {"gpa", 3.8}}},
            {{{"id", 2.0}, {"name", string("Bob")}, {"age", 25.0}, {"gpa", 2.9}}},
            {{{"id", 3.0}, {"name", string("Carol")}, {"age", 21.0}, {"gpa", 3.5}}},
            {{{"id", 4.0}, {"name", string("Dave")}, {"age", 30.0}, {"gpa", 3.1}}}
        };

        vector<string> queries = {
            "SELECT id, name, gpa FROM students WHERE gpa > 3.0 ORDER BY gpa DESC LIMIT 3",
            "SELECT * FROM students WHERE age >= 22 AND age <= 26"
        };

        for(const string& sql : queries) {
            cout << "SQL: " << sql << '\n';
            SelectQuery query = parseSelect(sql);
            vector<Row> result = execute(query, students);
            printRows(result, query.columns);
            cout << '\n';
        }
    }
    catch(const exception& error) {
        cerr << "Error: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
