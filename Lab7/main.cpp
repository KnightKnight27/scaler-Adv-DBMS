#include <iostream>
#include <vector>
#include <string>
#include <unordered_map>
#include <variant>

using Value = std::variant<double,std::string>;

struct Row
{
    std::unordered_map<std::string,Value> cols;
};

struct SelectQuery
{
    std::vector<std::string> columns;
    std::string from;
    std::string where_raw;
    std::string order_by;
    bool order_asc;
    int limit;
};

void shunting_demo();

SelectQuery parse_select(
    const std::string& sql);

std::vector<Row> execute(
    const SelectQuery& q,
    const std::vector<Row>& data);

void print_rows(
    const std::vector<Row>& rows);

int main()
{
    shunting_demo();

    std::vector<Row> students =
    {
        {
            {
                {"id",1.0},
                {"name",
                 std::string("Alice")},
                {"age",22.0},
                {"gpa",3.8}
            }
        },

        {
            {
                {"id",2.0},
                {"name",
                 std::string("Bob")},
                {"age",25.0},
                {"gpa",2.9}
            }
        },

        {
            {
                {"id",3.0},
                {"name",
                 std::string("Carol")},
                {"age",21.0},
                {"gpa",3.5}
            }
        },

        {
            {
                {"id",4.0},
                {"name",
                 std::string("Dave")},
                {"age",30.0},
                {"gpa",3.1}
            }
        }
    };

    std::string queries[] =
    {
        "SELECT id, name, gpa FROM students WHERE gpa > 3.0 ORDER BY gpa DESC LIMIT 3",

        "SELECT * FROM students WHERE age >= 22 && age <= 26"
    };

    for(const auto& sql : queries)
    {
        std::cout
            << "SQL: "
            << sql
            << "\n\n";

        SelectQuery q =
            parse_select(sql);

        auto result =
            execute(q,students);

        print_rows(result);

        std::cout<<"\n";
    }

    return 0;
}
