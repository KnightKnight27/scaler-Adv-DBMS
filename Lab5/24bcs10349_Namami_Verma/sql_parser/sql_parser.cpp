#include <iostream>
#include <vector>
#include <sstream>
#include <algorithm>

using namespace std;

struct Row {
    int id;
    string name;
    int salary;
};

vector<Row> table = {
    {1,"Alice",50000},
    {2,"Bob",60000},
    {3,"Charlie",45000},
    {4,"David",70000},
    {5,"Eva",55000}
};

void printRow(const Row& r,
              const vector<string>& cols) {

    for (auto col : cols) {
        if (col == "id")
            cout << r.id << " ";
        else if (col == "name")
            cout << r.name << " ";
        else if (col == "salary")
            cout << r.salary << " ";
    }

    cout << endl;
}

int main() {

    string query =
        "SELECT name,salary FROM employees "
        "WHERE salary > 50000";

    vector<string> cols = {"name","salary"};

    cout << "Query:\n"
         << query << "\n\n";

    cout << "Result:\n";

    for (auto &row : table) {
        if (row.salary > 50000)
            printRow(row, cols);
    }

    return 0;
}