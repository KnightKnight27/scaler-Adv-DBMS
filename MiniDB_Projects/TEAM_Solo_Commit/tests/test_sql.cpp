// End-to-end SQL: CREATE, INSERT, SELECT with WHERE / projection, JOIN, and DELETE.
#include "database.h"
#include "test_util.h"

using namespace minidb;

int main() {
    std::remove("/tmp/minidb_test_sql.db");
    std::remove("/tmp/minidb_test_sql.db.wal");
    Database db("/tmp/minidb_test_sql.db", 16);

    CHECK(db.Execute("CREATE TABLE students (id INT PRIMARY KEY, name VARCHAR, age INT)").ok);
    CHECK(db.Execute("CREATE TABLE enroll (sid INT, course VARCHAR)").ok);

    for (int i = 1; i <= 6; ++i)
        db.Execute("INSERT INTO students VALUES (" + std::to_string(i) + ", 'n" +
                   std::to_string(i) + "', " + std::to_string(18 + i) + ")");
    db.Execute("INSERT INTO enroll VALUES (1,'DB'),(1,'OS'),(3,'AI')");

    CHECK(db.Execute("SELECT * FROM students").rows.size() == 6);

    // ages are 19..24; age>=22 -> ids 4,5,6; then id!=5 -> ids 4,6 (two rows).
    Result w = db.Execute("SELECT id, name FROM students WHERE age >= 22 AND id != 5");
    CHECK(w.rows.size() == 2);
    CHECK(w.schema.Count() == 2);

    Result pt = db.Execute("SELECT name FROM students WHERE id = 3");
    CHECK(pt.rows.size() == 1);
    CHECK(pt.rows[0].GetValue(0).AsString() == "n3");

    Result j = db.Execute("SELECT * FROM students INNER JOIN enroll ON students.id = enroll.sid");
    CHECK(j.rows.size() == 3);              // (1,DB),(1,OS),(3,AI)
    CHECK(j.schema.Count() == 5);           // 3 student cols + 2 enroll cols

    CHECK(db.Execute("DELETE FROM students WHERE id = 4").affected == 1);
    CHECK(db.Execute("SELECT * FROM students").rows.size() == 5);

    // Errors are reported, not crashes.
    CHECK(!db.Execute("SELECT * FROM nosuchtable").ok);
    CHECK(!db.Execute("SELECT bogus FROM students").ok);

    return minidb_test::Done("sql");
}
