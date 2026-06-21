#include "catch.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <cstdio>
#include <string>

#include "common/serialize.h"
#include "recovery/wal_manager.h"

using namespace walterdb;

namespace {
std::string wal_path(const char* tag) {
  return std::string("/tmp/walterdb_wal_") + tag + "_" + std::to_string(::getpid()) + ".wal";
}
}  // namespace

TEST_CASE("WAL round-trips records and resumes LSNs across reopen", "[wal]") {
  std::string path = wal_path("rt");
  ::remove(path.c_str());
  {
    WalManager wal(path);
    wal.log_begin(1);
    wal.log_insert(1, 7, "row-A");
    wal.log_insert(1, 7, "row-B");
    wal.log_commit(1);
    wal.sync();
  }
  {
    WalManager wal(path);  // reopen: should see all 4 and resume LSNs
    auto recs = wal.read_all();
    REQUIRE(recs.size() == 4);
    REQUIRE(recs[0].type == LogType::Begin);
    REQUIRE(recs[1].type == LogType::Insert);
    REQUIRE(recs[1].table_id == 7);
    REQUIRE(recs[1].row_image == "row-A");
    REQUIRE(recs[3].type == LogType::Commit);
    REQUIRE(recs[0].lsn == 0);
    REQUIRE(recs[3].lsn == 3);

    lsn_t next = wal.log_abort(2);  // continues numbering
    REQUIRE(next == 4);
  }
  ::remove(path.c_str());
}

TEST_CASE("WAL drops a torn trailing record", "[wal]") {
  std::string path = wal_path("torn");
  ::remove(path.c_str());
  {
    WalManager wal(path);
    wal.log_begin(1);
    wal.log_insert(1, 1, "good");
    wal.log_commit(1);
    wal.sync();
  }
  // Simulate a crash mid-append: a length frame promising bytes that never came.
  int fd = ::open(path.c_str(), O_RDWR | O_APPEND);
  REQUIRE(fd >= 0);
  char hdr[4];
  store_u32(hdr, 9999);  // claims a huge record body that isn't there
  ::write(fd, hdr, 4);
  ::write(fd, "partial", 7);
  ::close(fd);

  WalManager wal(path);
  auto recs = wal.read_all();
  REQUIRE(recs.size() == 3);  // torn tail ignored, valid prefix intact
  REQUIRE(recs.back().type == LogType::Commit);
  ::remove(path.c_str());
}

TEST_CASE("WAL truncate clears the log", "[wal]") {
  std::string path = wal_path("trunc");
  ::remove(path.c_str());
  WalManager wal(path);
  wal.log_begin(1);
  wal.log_commit(1);
  REQUIRE(wal.read_all().size() == 2);
  wal.truncate();
  REQUIRE(wal.read_all().empty());
  REQUIRE(wal.empty());
  ::remove(path.c_str());
}
