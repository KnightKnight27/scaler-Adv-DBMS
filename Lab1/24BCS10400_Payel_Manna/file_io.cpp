#include <iostream>
#include <fstream>

using namespace std;

int main() {

    ofstream out("sample.txt");

    out << "Advanced DBMS Lab 1\n";
    out << "Understanding Linux File I/O\n";

    out.close();

    ifstream in("sample.txt");

    string line;

    while(getline(in, line)) {
        cout << line << endl;
    }

    in.close();

    return 0;
}