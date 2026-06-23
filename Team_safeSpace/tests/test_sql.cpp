// End-to-end SQL: CREATE TABLE, INSERT, SELECT (projection, WHERE, AND/OR),
// JOIN, and DELETE — all driven through Database::Execute.

#include <algorithm>
#include <cstdio>
#include <string>

#include "engine/database.h"
#include "tests/test_util.h"

using namespace minidb;

int main() {
  const std::string file = "test_sql.db";
  std::remove(file.c_str());

  // MiniDB is a single-process embedded engine: only one Database may hold a
  // file at a time. We scope the first instance so its destructor flushes
  // everything to disk before we reopen it below (which doubles as a
  // durability check).
  {
  Database db(file);

  db.Execute("CREATE TABLE authors (id INT, name VARCHAR(32))");
  db.Execute("CREATE TABLE books (id INT, title VARCHAR(64), author_id INT, year INT)");

  CHECK_EQ(db.Execute("INSERT INTO authors VALUES (1, 'Ada'), (2, 'Linus'), (3, 'Grace')").affected, 3);
  for (int i = 0; i < 20; i++) {
    int author = (i % 3) + 1;
    db.Execute("INSERT INTO books VALUES (" + std::to_string(i) + ", 'Book" + std::to_string(i) +
               "', " + std::to_string(author) + ", " + std::to_string(2000 + i) + ")");
  }

  // SELECT * counts all rows.
  CHECK_EQ(db.Execute("SELECT * FROM books").affected, 20);

  // Projection + column names.
  ResultSet pr = db.Execute("SELECT id, year FROM books");
  CHECK_EQ(static_cast<int>(pr.columns.size()), 2);
  CHECK(pr.columns[0] == "id" && pr.columns[1] == "year");

  // WHERE with comparison.
  CHECK_EQ(db.Execute("SELECT id FROM books WHERE year > 2015").affected, 4);  // 2016..2019
  CHECK_EQ(db.Execute("SELECT id FROM books WHERE author_id = 1").affected, 7);  // i=0,3,..,18

  // WHERE with AND / OR.
  // author_id=1 -> i in {0,3,6,9,12,15,18}; year>=2010 -> i>=10; intersection {12,15,18}.
  CHECK_EQ(db.Execute("SELECT id FROM books WHERE year >= 2010 AND author_id = 1").affected, 3);
  CHECK_EQ(db.Execute("SELECT id FROM books WHERE id = 0 OR id = 19").affected, 2);

  // String equality.
  CHECK_EQ(db.Execute("SELECT id FROM authors WHERE name = 'Linus'").affected, 1);

  // JOIN: each book joins to exactly one author -> 20 rows; filter by author.
  ResultSet j = db.Execute(
      "SELECT books.id, authors.name FROM books JOIN authors ON books.author_id = authors.id "
      "WHERE authors.name = 'Ada'");
  CHECK_EQ(j.affected, 7);
  CHECK(j.columns[1] == "authors.name");

  // DELETE with predicate, then re-count.
  CHECK_EQ(db.Execute("DELETE FROM books WHERE author_id = 3").affected, 6);  // i=2,5,8,11,14,17
  CHECK_EQ(db.Execute("SELECT * FROM books").affected, 14);

  // A parse error is reported, not crashed.
  bool threw = false;
  try {
    db.Execute("SELCT oops");
  } catch (const std::exception &) {
    threw = true;
  }
  CHECK(threw);
  }  // db flushes + closes here

  // Persistence across reopen: data + catalog survive.
  {
    Database db2(file);
    CHECK_EQ(db2.Execute("SELECT * FROM books").affected, 14);
    CHECK_EQ(db2.Execute("SELECT * FROM authors").affected, 3);
  }

  std::remove(file.c_str());
  return minidb::test::summary("sql");
}
