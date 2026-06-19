#include <iostream>
#include <vector>
#include <sstream>
using namespace std;

struct Row {
    int id;
    string name;
    int age;
};

int main() {

    vector<Row> students = {
        {1,"Alice",20},
        {2,"Bob",22},
        {3,"Charlie",19}
    };

    string query;
    getline(cin, query);

    if(query=="SELECT * FROM students") {

        cout<<"ID\tNAME\tAGE\n";

        for(auto s : students) {
            cout<<s.id<<"\t"
                <<s.name<<"\t"
                <<s.age<<endl;
        }
    }

    else if(query=="SELECT name FROM students") {

        for(auto s : students)
            cout<<s.name<<endl;
    }

    else {
        cout<<"Invalid Query"<<endl;
    }

    return 0;
}