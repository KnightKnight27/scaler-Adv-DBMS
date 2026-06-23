#pragma once

#include <new>
#include <type_traits>
#include <utility>

namespace minidb {

template <typename T>
class Optional {
 public:
  Optional() noexcept : engaged_(false) {}

  Optional(const T& value) : engaged_(true) {
    new (&storage_) T(value);
  }

  Optional(T&& value) : engaged_(true) {
    new (&storage_) T(std::move(value));
  }

  Optional(const Optional& other) : engaged_(other.engaged_) {
    if (other.engaged_) new (&storage_) T(*other);
  }

  Optional(Optional&& other) noexcept : engaged_(other.engaged_) {
    if (other.engaged_) new (&storage_) T(std::move(*other));
  }

  ~Optional() { reset(); }

  Optional& operator=(const Optional& other) {
    if (this != &other) {
      reset();
      if (other.engaged_) new (&storage_) T(*other);
      engaged_ = other.engaged_;
    }
    return *this;
  }

  Optional& operator=(Optional&& other) noexcept {
    if (this != &other) {
      reset();
      if (other.engaged_) new (&storage_) T(std::move(*other));
      engaged_ = other.engaged_;
    }
    return *this;
  }

  explicit operator bool() const noexcept { return engaged_; }

  T& operator*() { return *reinterpret_cast<T*>(&storage_); }
  const T& operator*() const { return *reinterpret_cast<const T*>(&storage_); }

  T* operator->() { return reinterpret_cast<T*>(&storage_); }
  const T* operator->() const { return reinterpret_cast<const T*>(&storage_); }

  bool has_value() const noexcept { return engaged_; }

  void reset() {
    if (engaged_) {
      reinterpret_cast<T*>(&storage_)->~T();
      engaged_ = false;
    }
  }

  template <typename... Args>
  void emplace(Args&&... args) {
    reset();
    new (&storage_) T(std::forward<Args>(args)...);
    engaged_ = true;
  }

 private:
  typename std::aligned_storage<sizeof(T), alignof(T)>::type storage_;
  bool engaged_;
};

}  // namespace minidb
