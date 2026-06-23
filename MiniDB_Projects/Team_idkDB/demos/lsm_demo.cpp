#include <filesystem>
#include <iostream>

#include "minidb/common/trace.h"
#include "minidb/lsm/lsm_tree.h"

int main(int argc, char **argv) {
  const std::filesystem::path dir =
      argc > 1 ? std::filesystem::path(argv[1])
               : std::filesystem::temp_directory_path() / "minidb_lsm_demo";
  std::filesystem::create_directories(dir);
  minidb::Trace::SetEnabled(true);

  std::cout << "LSM demo path: " << dir << '\n';
  minidb::LsmTree lsm(dir, 3);
  lsm.Put(1, "one");
  lsm.Put(2, "two");
  lsm.Put(3, "three");
  lsm.Put(4, "four");
  lsm.Put(5, "five");
  lsm.Flush();
  std::cout << "Lookup key 4 before compaction: " << lsm.Get(4).value_or("<missing>") << '\n';
  lsm.Delete(2);
  lsm.Flush();
  lsm.Compact();
  std::cout << "Lookup key 2 after tombstone+compaction: "
            << lsm.Get(2).value_or("<missing>") << '\n';
  return 0;
}
