#include "catch.hpp"

#include <limits>
#include <string>
#include <vector>

#include "common/serialize.h"
#include "common/status.h"
#include "common/rid.h"

using namespace axiomdb;

TEST_CASE("Status carries code and message", "[common][status]") {
  Status ok;  // default-constructs to success
  REQUIRE(ok.ok());
  REQUIRE(ok.code() == StatusCode::Ok);

  Status nf = Status::not_found("no such key");
  REQUIRE_FALSE(nf.ok());
  REQUIRE(nf.code() == StatusCode::NotFound);
  REQUIRE(nf.to_string() == "NotFound: no such key");
}

TEST_CASE("ByteWriter/ByteReader round-trip fixed-width values", "[common][serialize]") {
  ByteWriter w;
  w.put_u8(0xAB);
  w.put_u32(0xDEADBEEF);
  w.put_i64(-1234567890123LL);
  w.put_double(3.14159);
  w.put_string("hello world");
  w.put_u16(0x1234);

  ByteReader r(w.str());
  REQUIRE(r.get_u8() == 0xAB);
  REQUIRE(r.get_u32() == 0xDEADBEEFu);
  REQUIRE(r.get_i64() == -1234567890123LL);
  REQUIRE(r.get_double() == Approx(3.14159));
  REQUIRE(std::string(r.get_string()) == "hello world");
  REQUIRE(r.get_u16() == 0x1234);
  REQUIRE(r.empty());
}

TEST_CASE("ByteReader throws on over-read", "[common][serialize]") {
  ByteWriter w;
  w.put_u16(7);
  ByteReader r(w.str());
  REQUIRE(r.get_u16() == 7);
  REQUIRE_THROWS(r.get_u32());
}

TEST_CASE("encode_int64_key preserves numeric order", "[common][serialize]") {
  // A spread of values including the extremes and a sign change.
  std::vector<int64_t> vals = {
      std::numeric_limits<int64_t>::min(), -1000000, -5, -1, 0, 1, 5, 1000000,
      std::numeric_limits<int64_t>::max()};

  for (size_t i = 0; i + 1 < vals.size(); ++i) {
    std::string a = encode_int64_key(vals[i]);
    std::string b = encode_int64_key(vals[i + 1]);
    INFO("comparing " << vals[i] << " < " << vals[i + 1]);
    REQUIRE(a < b);                              // byte order == numeric order
    REQUIRE(decode_int64_key(a) == vals[i]);     // round-trips
  }
  REQUIRE(decode_int64_key(encode_int64_key(vals.back())) == vals.back());
}

TEST_CASE("RID equality and validity", "[common][rid]") {
  RID a{3, 7};
  RID b{3, 7};
  RID c{4, 7};
  REQUIRE(a == b);
  REQUIRE(a != c);
  REQUIRE(a.valid());
  REQUIRE_FALSE(RID{}.valid());
}
