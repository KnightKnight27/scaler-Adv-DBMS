#pragma once
#include <string>
#include <vector>
#include "common/types.h"
#include "record/schema.h"

namespace minidb {

// Tuple (de)serialization between a list of Values and the fixed-length byte
// layout stored in heap pages. Layout is field-by-field, little-endian for
// integers and zero-padded for VARCHAR, so it is portable and never relies on
// struct padding.
namespace tuple {

// Serialize `values` (which must match `schema`) into `out` (resized to
// schema.RecordSize()).
void Serialize(const Schema& schema, const std::vector<Value>& values,
               std::vector<char>& out);

// Deserialize a record buffer back into Values according to `schema`.
std::vector<Value> Deserialize(const Schema& schema, const char* data);

// Human-readable form, e.g. "(1, alice)".
std::string ToString(const Schema& schema, const std::vector<Value>& values);

}  // namespace tuple
}  // namespace minidb
