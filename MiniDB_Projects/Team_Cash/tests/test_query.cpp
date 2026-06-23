// Tests for the query layer: parser, optimizer choices, end-to-end execution.
#include <filesystem>

#include "check.h"
#include "engine.h"
#include "optimizer.h"
#include "parser.h"

using namespace minidb;

static SelectStmt* asSelect(Statement* s) { return static_cast<SelectStmt*>(s); }

int main() {
    // parser builds the expected shape
    auto st = parse("SELECT a.x, b.y FROM a JOIN b ON a.id = b.aid WHERE a.x > 5 AND b.y = 'z'");
    CHECK(st->kind == StmtKind::Select);
    CHECK(asSelect(st.get())->hasJoin);
    CHECK(asSelect(st.get())->where.size() == 2);

    std::string dir = "q_test_data";
    std::filesystem::remove_all(dir);
    {
        Engine eng(dir);
        eng.execute("CREATE TABLE students (id INT, name TEXT, grade INT)");
        eng.execute("INSERT INTO students VALUES (1, 'A', 95)");
        eng.execute("INSERT INTO students VALUES (2, 'B', 80)");
        eng.execute("INSERT INTO students VALUES (3, 'C', 70)");
        eng.execute("INSERT INTO students VALUES (4, 'D', 91)");

        // optimizer: index for primary-key equality, scan otherwise
        Optimizer opt(eng.catalog());
        {
            auto s = parse("SELECT * FROM students WHERE id = 2");
            CHECK(opt.optimize(*asSelect(s.get()))->kind == PlanKind::IndexScan);
        }
        {
            auto s = parse("SELECT * FROM students WHERE grade > 80");
            auto plan = opt.optimize(*asSelect(s.get()));
            CHECK(plan->kind == PlanKind::Filter);
            CHECK(plan->child->kind == PlanKind::SeqScan);
        }

        // results
        CHECK(eng.execute("SELECT name FROM students WHERE grade > 80").rows.size() == 2);
        auto r = eng.execute("SELECT name FROM students WHERE id = 3");
        CHECK(r.rows.size() == 1 && r.rows[0][0].s == "C");

        // join
        eng.execute("CREATE TABLE marks (id INT, sid INT, subj TEXT)");
        eng.execute("INSERT INTO marks VALUES (1, 1, 'Math')");
        eng.execute("INSERT INTO marks VALUES (2, 4, 'Art')");
        auto rj = eng.execute(
            "SELECT students.name, marks.subj FROM marks JOIN students ON marks.sid = students.id");
        CHECK(rj.rows.size() == 2);

        // delete + duplicate primary key rejection
        eng.execute("DELETE FROM students WHERE id = 2");
        CHECK(eng.execute("SELECT id FROM students WHERE id = 2").rows.empty());
        bool threw = false;
        try { eng.execute("INSERT INTO students VALUES (1, 'dup', 1)"); } catch (...) { threw = true; }
        CHECK(threw);
        eng.close();
    }
    // persistence across restart (index rebuilt on load)
    {
        Engine eng(dir);
        CHECK(!eng.catalog().tableNames().empty());
        auto r = eng.execute("SELECT name FROM students WHERE id = 4");
        CHECK(r.rows.size() == 1 && r.rows[0][0].s == "D");
        eng.close();
    }
    std::filesystem::remove_all(dir);

    REPORT();
}
