// Parser error-path tests for MiniDB.
// Verifies the parser throws on malformed SQL rather than silently accepting.
#include <cassert>
#include <cstdio>
#include <iostream>
#include <stdexcept>
#include <string>

#include "parser/parser.h"
#include "parser/tokenizer.h"

using namespace minidb;
using namespace std;

static void MustThrow(const string& sql) {
  try {
    Tokenizer tk(sql);
    auto tokens = tk.TokenizeAll();
    Parser p(std::move(tokens));
    p.ParseStatement();
    cerr << "expected throw for: " << sql << "\n";
    assert(false);
  } catch (const runtime_error&) {
    // expected
  }
}

int main() {
  MustThrow("SELEC * FROM t;");
  MustThrow("INSERT INTO t VALUES;");
  MustThrow("DELETE FROM;");
  MustThrow("SELECT FROM WHERE x = 1;");
  MustThrow("SELECT * FROM t WHERE;");
  MustThrow("INSERT INTO t (a) VALUES (1");
  cout << "ALL PARSER ERROR TESTS PASSED" << endl;
  return 0;
}
