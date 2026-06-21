#include "catch.hpp"

#include <vector>

#include "catalog/schema.h"
#include "catalog/tuple.h"
#include "catalog/value.h"

using namespace walterdb;

TEST_CASE("Value compare across numeric types and NULL ordering", "[types][value]") {
  REQUIRE(Value::make_integer(3).compare(Value::make_integer(5)) < 0);
  REQUIRE(Value::make_integer(5).compare(Value::make_double(5.0)) == 0);
  REQUIRE(Value::make_double(2.5).compare(Value::make_integer(3)) < 0);
  REQUIRE(Value::make_varchar("apple").compare(Value::make_varchar("banana")) < 0);

  // NULL sorts before any present value, and equals another NULL.
  REQUIRE(Value::make_null(TypeId::Integer).compare(Value::make_integer(-100)) < 0);
  REQUIRE(Value::make_null(TypeId::Integer).compare(Value::make_null(TypeId::Integer)) == 0);
}

TEST_CASE("Value::encode_key preserves order for integers and strings", "[types][value]") {
  REQUIRE(Value::make_integer(-5).encode_key() < Value::make_integer(5).encode_key());
  REQUIRE(Value::make_integer(100).encode_key() < Value::make_integer(2000).encode_key());
  REQUIRE(Value::make_varchar("aaa").encode_key() < Value::make_varchar("aab").encode_key());
  // NULL key sorts before any present-value key.
  REQUIRE(Value::make_null(TypeId::Integer).encode_key() <
          Value::make_integer(-9999).encode_key());
}

TEST_CASE("Value::encode_key preserves order for doubles incl. negatives", "[types][value]") {
  std::vector<double> ds = {-1e9, -3.5, -0.0, 0.0, 1.5, 42.0, 1e9};
  for (size_t i = 0; i + 1 < ds.size(); ++i) {
    if (ds[i] == ds[i + 1]) continue;
    REQUIRE(Value::make_double(ds[i]).encode_key() <= Value::make_double(ds[i + 1]).encode_key());
  }
  REQUIRE(Value::make_double(-1.0).encode_key() < Value::make_double(1.0).encode_key());
}

TEST_CASE("Schema lookups are case-insensitive and find the PK", "[types][schema]") {
  Schema s({{"id", TypeId::Integer, true},
            {"name", TypeId::Varchar, false},
            {"score", TypeId::Double, false}});
  REQUIRE(s.num_columns() == 3);
  REQUIRE(s.index_of("ID").value() == 0);
  REQUIRE(s.index_of("Name").value() == 1);
  REQUIRE_FALSE(s.index_of("missing").has_value());
  REQUIRE(s.primary_key_index().value() == 0);
}

TEST_CASE("Tuple encode/decode round-trips including NULLs", "[types][tuple]") {
  Schema s({{"id", TypeId::Integer, true},
            {"name", TypeId::Varchar, false},
            {"active", TypeId::Boolean, false},
            {"score", TypeId::Double, false}});

  Tuple t({Value::make_integer(42), Value::make_varchar("alice"),
           Value::make_boolean(true), Value::make_null(TypeId::Double)});

  std::string bytes = t.encode(s);
  Tuple back = Tuple::decode(s, bytes);

  REQUIRE(back.size() == 4);
  REQUIRE(back.value(0).as_integer() == 42);
  REQUIRE(back.value(1).as_varchar() == "alice");
  REQUIRE(back.value(2).as_boolean() == true);
  REQUIRE(back.value(3).is_null());
  REQUIRE(back.to_string() == "(42, alice, true, NULL)");
}
