#include <algorithm>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

using namespace std;

string toUpper(string text);
vector<string> tokenizeExpression(const string& expression);
vector<string> toRpn(const vector<string>& tokens);
double evaluateRpn(const vector<string>& rpn, const unordered_map<string, double>& variables);
void shuntingDemo();

using Value = variant<double, string>;

struct Row {
    unordered_map<string, Value> columns;
};

double numericValue(const Row& datasetRow, const string& targetColumn) {
    auto locate = datasetRow.columns.find(targetColumn);
    if (locate == datasetRow.columns.end()) return 0.0;

    if (const double* numPtr = get_if<double>(&locate->second)) return *numPtr;
    if (const string* strPtr = get_if<string>(&locate->second)) {
        try {
            return stod(*strPtr);
        }
        catch (const exception&) {
            return 0.0;
        }
    }
    return 0.0;
}

struct SelectQuery {
    vector<string> columns;
    string table;
    string whereExpression;
    string orderBy;
    bool ascending = true;
    int limit = -1;
};

vector<string> sqlWords(const string& sql) {
    istringstream stream(sql);
    vector<string> collection;
    string unit;

    while (stream >> unit) {
        if (!unit.empty() && unit.back() == ';') {
            unit.pop_back();
        }
        collection.push_back(unit);
    }
    return collection;
}

void addColumns(const string& rawText, vector<string>& targetFields) {
    stringstream separator(rawText);
    string element;

    while (getline(separator, element, ',')) {
        if (element.empty()) continue;
        if (element == "*") targetFields.clear();
        else targetFields.push_back(element);
    }
}

SelectQuery parseSelect(const string& sql) {
    vector<string> segments = sqlWords(sql);
    SelectQuery state;
    auto it = segments.begin();

    if (it == segments.end() || toUpper(*it) != "SELECT") {
        throw runtime_error("Query must start with SELECT");
    }
    ++it;

    while (it != segments.end() && toUpper(*it) != "FROM") {
        addColumns(*it, state.columns);
        ++it;
    }

    if (it == segments.end() || toUpper(*it) != "FROM") {
        throw runtime_error("Missing FROM clause");
    }
    ++it;

    if (it == segments.end()) throw runtime_error("Missing table name");
    state.table = *it;
    ++it;

    while (it != segments.end()) {
        string keyWord = toUpper(*it);

        if (keyWord == "WHERE") {
            ++it;
            string builder;
            while (it != segments.end() && toUpper(*it) != "ORDER" && toUpper(*it) != "LIMIT") {
                if (!builder.empty()) builder += ' ';
                builder += *it;
                ++it;
            }
            state.whereExpression = builder;
        }
        else if (keyWord == "ORDER") {
            ++it;
            if (it == segments.end() || toUpper(*it) != "BY") {
                throw runtime_error("ORDER must be followed by BY");
            }
            ++it;

            if (it == segments.end()) throw runtime_error("Missing ORDER BY column");
            state.orderBy = *it;
            ++it;

            if (it != segments.end()) {
                string ordering = toUpper(*it);
                if (ordering == "ASC" || ordering == "DESC") {
                    state.ascending = (ordering == "ASC");
                    ++it;
                }
            }
        }
        else if (keyWord == "LIMIT") {
            ++it;
            if (it == segments.end()) throw runtime_error("Missing LIMIT value");
            state.limit = stoi(*it);
            ++it;
        }
        else {
            throw runtime_error("Unknown SQL clause: " + *it);
        }
    }
    return state;
}

vector<Row> execute(const SelectQuery& statement, const vector<Row>& sourceData) {
    vector<string> evaluatedWhereRpn;
    if (!statement.whereExpression.empty()) {
        evaluatedWhereRpn = toRpn(tokenizeExpression(statement.whereExpression));
    }

    vector<Row> outputSet;

    for (const auto& dataRow : sourceData) {
        if (!evaluatedWhereRpn.empty()) {
            unordered_map<string, double> runtimeVariables;
            for (const auto& attribute : dataRow.columns) {
                runtimeVariables[attribute.first] = numericValue(dataRow, attribute.first);
            }
            if (evaluateRpn(evaluatedWhereRpn, runtimeVariables) == 0.0) continue;
        }

        if (statement.columns.empty()) {
            outputSet.push_back(dataRow);
        }
        else {
            Row spatialProjection;
            for (const auto& filterCol : statement.columns) {
                auto innerMatch = dataRow.columns.find(filterCol);
                if (innerMatch != dataRow.columns.end()) {
                    spatialProjection.columns[filterCol] = innerMatch->second;
                }
            }
            outputSet.push_back(spatialProjection);
        }
    }

    if (!statement.orderBy.empty()) {
        sort(outputSet.begin(), outputSet.end(), [&](const Row& lhs, const Row& rhs) {
            double vLeft = numericValue(lhs, statement.orderBy);
            double vRight = numericValue(rhs, statement.orderBy);
            return statement.ascending ? (vLeft < vRight) : (vLeft > vRight);
        });
    }

    if (statement.limit >= 0 && static_cast<int>(outputSet.size()) > statement.limit) {
        outputSet.resize(statement.limit);
    }
    return outputSet;
}

void printValue(const Value& entity) {
    if (const double* numericRepresentation = get_if<double>(&entity)) cout << *numericRepresentation;
    else if (const string* dynamicString = get_if<string>(&entity)) cout << *dynamicString;
}

void printRows(const vector<Row>& finalRows, const vector<string>& headerProjection) {
    for (const auto& matrixRow : finalRows) {
        if (headerProjection.empty()) {
            for (const auto& pairNode : matrixRow.columns) {
                cout << pairNode.first << '=';
                printValue(pairNode.second);
                cout << "  ";
            }
        }
        else {
            for (const auto& sequenceLabel : headerProjection) {
                auto structuralFind = matrixRow.columns.find(sequenceLabel);
                if (structuralFind != matrixRow.columns.end()) {
                    cout << sequenceLabel << '=';
                    printValue(structuralFind->second);
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

        for (const string& queryText : queries) {
            cout << "SQL: " << queryText << '\n';
            SelectQuery structuredPlan = parseSelect(queryText);
            vector<Row> computationResult = execute(structuredPlan, students);
            printRows(computationResult, structuredPlan.columns);
            cout << '\n';
        }
    }
    catch (const exception& ex) {
        cerr << "Error: " << ex.what() << '\n';
        return 1;
    }
    return 0;
}