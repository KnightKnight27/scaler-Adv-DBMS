#include "lsm/manifest.h"
#include <cstdio>
#include <fstream>
#include <sstream>

namespace minidb {

void Manifest::load() {
  std::ifstream in(path_);
  if (!in.is_open()) return;
  std::string keyword;
  while (in >> keyword) {
    if (keyword == "NEXTSEQ")       in >> next_seq;
    else if (keyword == "NEXTFILE") in >> next_file;
    else if (keyword == "SSTABLE")  { std::string path; in >> path; sstables.push_back(path); }
  }
}

void Manifest::save() const {
  std::string tmp = path_ + ".tmp";
  {
    std::ofstream out(tmp, std::ios::trunc);
    out << "NEXTSEQ " << next_seq << "\n";
    out << "NEXTFILE " << next_file << "\n";
    for (const std::string& s : sstables) out << "SSTABLE " << s << "\n";
  }
  std::rename(tmp.c_str(), path_.c_str());  // atomic replace
}

}  // namespace minidb
