#include "catch.hpp"

#include <unistd.h>

#include <array>
#include <cstdio>
#include <string>

#include "common/config.h"
#include "storage/disk_manager.h"
#include "storage/slotted_page.h"

using namespace walterdb;

namespace {
std::string temp_db_path(const char* tag) {
  return std::string("/tmp/walterdb_test_") + tag + "_" + std::to_string(::getpid()) + ".wdb";
}
}  // namespace

TEST_CASE("SlottedPage insert / get / free-space", "[storage][page]") {
  std::array<char, PAGE_SIZE> buf{};
  SlottedPage page(buf.data());
  page.init();

  REQUIRE(page.num_slots() == 0);
  REQUIRE(page.free_space_ptr() == PAGE_SIZE);

  auto s0 = page.insert("hello");
  auto s1 = page.insert("a longer record here");
  auto s2 = page.insert("");  // zero-length record is allowed

  REQUIRE(s0.has_value());
  REQUIRE(s1.has_value());
  REQUIRE(s2.has_value());
  REQUIRE(*s0 == 0);
  REQUIRE(*s1 == 1);
  REQUIRE(*s2 == 2);
  REQUIRE(page.num_slots() == 3);

  REQUIRE(page.get(*s0).value() == "hello");
  REQUIRE(page.get(*s1).value() == "a longer record here");
  REQUIRE(page.get(*s2).value() == "");
}

TEST_CASE("SlottedPage erase tombstones without disturbing neighbours", "[storage][page]") {
  std::array<char, PAGE_SIZE> buf{};
  SlottedPage page(buf.data());
  page.init();

  auto a = page.insert("alpha");
  auto b = page.insert("bravo");
  auto c = page.insert("charlie");

  REQUIRE(page.erase(*b));
  REQUIRE_FALSE(page.erase(*b));         // double-erase is a no-op failure
  REQUIRE_FALSE(page.get(*b).has_value());
  REQUIRE_FALSE(page.is_live(*b));

  // Neighbours unaffected, slot ids stable (outstanding RIDs stay valid).
  REQUIRE(page.get(*a).value() == "alpha");
  REQUIRE(page.get(*c).value() == "charlie");
  REQUIRE(page.num_slots() == 3);
}

TEST_CASE("SlottedPage update_in_place requires equal length", "[storage][page]") {
  std::array<char, PAGE_SIZE> buf{};
  SlottedPage page(buf.data());
  page.init();
  auto s = page.insert("12345");
  REQUIRE(page.update_in_place(*s, "ABCDE"));        // same length (5) OK
  REQUIRE(page.get(*s).value() == "ABCDE");
  REQUIRE_FALSE(page.update_in_place(*s, "toolong")); // different length rejected
  REQUIRE(page.get(*s).value() == "ABCDE");           // unchanged after rejection
}

TEST_CASE("SlottedPage reports being full", "[storage][page]") {
  std::array<char, PAGE_SIZE> buf{};
  SlottedPage page(buf.data());
  page.init();

  std::string big(100, 'x');
  int inserted = 0;
  while (page.insert(big).has_value()) ++inserted;
  REQUIRE(inserted > 0);
  // Once full, even a 1-byte record should fail if no contiguous room remains.
  size_t room = page.free_space_for_insert();
  if (room == 0) REQUIRE_FALSE(page.insert("y").has_value());
}

TEST_CASE("SlottedPage next_page_id and lsn round-trip", "[storage][page]") {
  std::array<char, PAGE_SIZE> buf{};
  SlottedPage page(buf.data());
  page.init();
  REQUIRE(page.next_page_id() == INVALID_PAGE_ID);
  page.set_next_page_id(42);
  REQUIRE(page.next_page_id() == 42);
  page.set_lsn(123456789);
  REQUIRE(page.lsn() == 123456789);
}

TEST_CASE("DiskManager allocates, persists, and reopens", "[storage][disk]") {
  std::string path = temp_db_path("disk");
  ::remove(path.c_str());

  page_id_t pid0, pid1;
  {
    DiskManager dm(path);
    REQUIRE(dm.num_pages() == 0);
    pid0 = dm.allocate_page();
    pid1 = dm.allocate_page();
    REQUIRE(pid0 == 0);
    REQUIRE(pid1 == 1);
    REQUIRE(dm.num_pages() == 2);

    std::array<char, PAGE_SIZE> w{};
    SlottedPage page(w.data());
    page.init();
    page.insert("persisted record");
    REQUIRE(dm.write_page(pid1, w.data()).ok());
    dm.sync();
  }

  // Reopen: the file should report 2 pages and the record should survive.
  {
    DiskManager dm(path);
    REQUIRE(dm.num_pages() == 2);
    std::array<char, PAGE_SIZE> r{};
    REQUIRE(dm.read_page(pid1, r.data()).ok());
    SlottedPage page(r.data());
    REQUIRE(page.get(0).value() == "persisted record");
  }
  ::remove(path.c_str());
}
