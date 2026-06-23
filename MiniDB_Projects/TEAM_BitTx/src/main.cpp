#include "catalog/catalog.h"
#include "optimizer/optimizer.h"
#include "optimizer/stats.h"
#include "parser/parser.h"
#include "parser/tokenizer.h"
#include "planner/planner.h"
#include "storage/disk_manager.h"

#include <iostream>
#include <memory>
#include <string>

using namespace minidb;
using namespace std;

int main(int argc, char** argv) {
  string dbPath = "/tmp/minidb_main.db";
  remove(dbPath.c_str());
  DiskManager dm(dbPath);
  CatalogManager catalog(&dm);
  Planner planner(&catalog);
  auto stats = StatsCollector::Collect(&catalog);
  Optimizer optimizer(&catalog, stats);

  cout << "minidb v0.1 - type 'exit' to quit" << endl;
  string line;
  while (cout << "minidb> " && getline(cin, line)) {
    if (line.empty())
      continue;
    if (line == "exit" || line == "quit")
      break;
    try {
      Tokenizer tk(line);
      auto tokens = tk.TokenizeAll();
      Parser parser(move(tokens));
      auto stmt = parser.ParseStatement();
      if (!stmt) {
        cout << "Parse error" << endl;
        continue;
      }
      auto exec = planner.Plan(*stmt);
      if (exec) {
        stats = StatsCollector::Collect(&catalog);
        Optimizer opt(&catalog, stats);
        exec = opt.Optimize(move(exec));
        exec->Init();
        Tuple out;
        while (exec->Next(&out)) {
          cout << out.ToString() << endl;
        }
      } else {
        cout << "OK" << endl;
      }
    } catch (const exception& e) {
      cout << "Error: " << e.what() << endl;
    }
  }
  return 0;
}