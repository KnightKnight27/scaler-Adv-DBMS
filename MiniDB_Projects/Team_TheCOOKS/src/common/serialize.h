#pragma once

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>

// ---------------------------------------------------------------------------
// Byte-level serialization helpers.
//
// WALterDB hand-rolls all of its on-disk encoding (the assignment explicitly
// asks for "byte-level record encoding" -- no Protobuf / FlatBuffers).  This
// header provides:
//
//   * raw fixed-width load/store on a char* (used for in-page layout where the
//     caller owns the offset arithmetic), and
//   * ByteWriter / ByteReader, an append/consume pair over std::string used by
//     tuple encoding, WAL records, and SSTable blocks.
//
// Two encodings coexist on purpose:
//   * Fixed-width integers are stored *little-endian* -- cheap, matches host.
//   * Sort keys are encoded *order-preserving* (see encode_*_key) so that
//     lexicographic byte comparison reproduces numeric / string order, which
//     is what makes range scans over the KV StorageEngine correct.
// ---------------------------------------------------------------------------

namespace walterdb {

// ----- raw fixed-width access on a byte buffer (little-endian) --------------

inline void store_u16(char* p, uint16_t v) { std::memcpy(p, &v, sizeof(v)); }
inline void store_u32(char* p, uint32_t v) { std::memcpy(p, &v, sizeof(v)); }
inline void store_u64(char* p, uint64_t v) { std::memcpy(p, &v, sizeof(v)); }

inline uint16_t load_u16(const char* p) { uint16_t v; std::memcpy(&v, p, sizeof(v)); return v; }
inline uint32_t load_u32(const char* p) { uint32_t v; std::memcpy(&v, p, sizeof(v)); return v; }
inline uint64_t load_u64(const char* p) { uint64_t v; std::memcpy(&v, p, sizeof(v)); return v; }

// ----- append-style writer over std::string --------------------------------

class ByteWriter {
 public:
  void put_u8(uint8_t v) { buf_.push_back(static_cast<char>(v)); }

  void put_u16(uint16_t v) { append_fixed(&v, sizeof(v)); }
  void put_u32(uint32_t v) { append_fixed(&v, sizeof(v)); }
  void put_u64(uint64_t v) { append_fixed(&v, sizeof(v)); }
  void put_i32(int32_t v) { append_fixed(&v, sizeof(v)); }
  void put_i64(int64_t v) { append_fixed(&v, sizeof(v)); }
  void put_double(double v) { append_fixed(&v, sizeof(v)); }

  // Raw bytes, no length prefix.
  void put_bytes(std::string_view s) { buf_.append(s.data(), s.size()); }

  // Length-prefixed bytes (u32 length + payload) -- self-describing.
  void put_string(std::string_view s) {
    put_u32(static_cast<uint32_t>(s.size()));
    put_bytes(s);
  }

  const std::string& str() const { return buf_; }
  std::string take() { return std::move(buf_); }
  size_t size() const { return buf_.size(); }

 private:
  template <typename T>
  void append_fixed(const T* v, size_t n) {
    buf_.append(reinterpret_cast<const char*>(v), n);
  }
  std::string buf_;
};

// ----- consume-style reader over a byte range -------------------------------
//
// A malformed read (running off the end of the buffer) means the on-disk data
// is corrupt or a format bug exists -- both are exceptional, so we throw.

class ByteReader {
 public:
  explicit ByteReader(std::string_view data) : p_(data.data()), end_(data.data() + data.size()) {}

  uint8_t get_u8() { ensure(1); return static_cast<uint8_t>(*p_++); }

  uint16_t get_u16() { return read_fixed<uint16_t>(); }
  uint32_t get_u32() { return read_fixed<uint32_t>(); }
  uint64_t get_u64() { return read_fixed<uint64_t>(); }
  int32_t get_i32() { return read_fixed<int32_t>(); }
  int64_t get_i64() { return read_fixed<int64_t>(); }
  double get_double() { return read_fixed<double>(); }

  // Read exactly n raw bytes.
  std::string_view get_bytes(size_t n) {
    ensure(n);
    std::string_view out(p_, n);
    p_ += n;
    return out;
  }

  // Read a length-prefixed string written by ByteWriter::put_string.
  std::string_view get_string() {
    uint32_t n = get_u32();
    return get_bytes(n);
  }

  size_t remaining() const { return static_cast<size_t>(end_ - p_); }
  bool empty() const { return p_ == end_; }

 private:
  void ensure(size_t n) const {
    if (static_cast<size_t>(end_ - p_) < n) {
      throw std::runtime_error("ByteReader: read past end of buffer (corrupt data?)");
    }
  }
  template <typename T>
  T read_fixed() {
    ensure(sizeof(T));
    T v;
    std::memcpy(&v, p_, sizeof(T));
    p_ += sizeof(T);
    return v;
  }

  const char* p_;
  const char* end_;
};

// ----- order-preserving key encoding ----------------------------------------
//
// The KV StorageEngine compares keys with plain lexicographic byte comparison
// (memcmp / std::string::operator<).  To make an integer column usable as such
// a key we must encode it so that byte order == numeric order:
//   * store big-endian (most significant byte first), and
//   * flip the sign bit, mapping the signed range onto unsigned order
//     (INT64_MIN -> 0x0000..., -1 -> 0x7FFF..., 0 -> 0x8000..., INT64_MAX -> 0xFFFF...).

inline std::string encode_int64_key(int64_t v) {
  uint64_t u = static_cast<uint64_t>(v) ^ (uint64_t{1} << 63);
  std::string out(8, '\0');
  for (int i = 7; i >= 0; --i) {
    out[i] = static_cast<char>(u & 0xFF);
    u >>= 8;
  }
  return out;
}

inline int64_t decode_int64_key(std::string_view s) {
  if (s.size() != 8) throw std::runtime_error("decode_int64_key: bad length");
  uint64_t u = 0;
  for (int i = 0; i < 8; ++i) {
    u = (u << 8) | static_cast<uint8_t>(s[i]);
  }
  return static_cast<int64_t>(u ^ (uint64_t{1} << 63));
}

// Strings are already lexicographically ordered as raw bytes.
inline std::string encode_string_key(std::string_view s) { return std::string(s); }

}  // namespace walterdb
