#pragma once

#include <memory>
#include <stdexcept>
#include <type_traits>

namespace minidb {

template <typename T, typename U>
struct VariantHelper;

template <typename T, typename U>
struct VariantHelper {
  static void destroy(int index, void* data) {
    if (index == 0) reinterpret_cast<T*>(data)->~T();
    else reinterpret_cast<U*>(data)->~U();
  }
};

template <typename T, typename U, typename... Rest>
struct VariantHelper<T, U, Rest...> {
  static void destroy(int index, void* data) {
    if (index == 0) reinterpret_cast<T*>(data)->~T();
    else VariantHelper<U, Rest...>::destroy(index - 1, data);
  }
};

template <typename... Ts>
class Variant {
 public:
  Variant() : index_(-1) {}

  Variant(const Variant& other) : index_(other.index_) {
    if (index_ >= 0) clone(other);
  }

  Variant(Variant&& other) noexcept : index_(other.index_) {
    if (index_ >= 0) move(std::move(other));
    other.reset();
  }

  template <typename T>
  Variant(const T& value) : index_(index_of<T>()) {
    new (&storage_) T(value);
  }

  ~Variant() { reset(); }

  Variant& operator=(const Variant& other) {
    if (this != &other) {
      reset();
      index_ = other.index_;
      if (index_ >= 0) clone(other);
    }
    return *this;
  }

  template <typename T>
  bool is() const {
    return index_ == index_of<T>();
  }

  template <typename T>
  T& get() {
    if (!is<T>()) throw std::runtime_error("bad variant access");
    return *reinterpret_cast<T*>(&storage_);
  }

  template <typename T>
  const T& get() const {
    if (!is<T>()) throw std::runtime_error("bad variant access");
    return *reinterpret_cast<const T*>(&storage_);
  }

  int index() const { return index_; }

 private:
  template <typename T, int I = 0>
  static int index_of() {
    if (std::is_same<T, typename std::tuple_element<I, std::tuple<Ts...>>::type>::value) return I;
    return index_of<T, I + 1>();
  }

  template <int I>
  static int index_of() {
    return -1;
  }

  void reset() {
    if (index_ >= 0) destroy(index_);
    index_ = -1;
  }

  void destroy(int idx) {
    VariantHelper<Ts...>::destroy(idx, &storage_);
  }

  void clone(const Variant& other) {
    clone_impl(other, index_);
  }

  void clone_impl(const Variant& other, int idx) {
    if (idx == 0) new (&storage_) typename std::tuple_element<0, std::tuple<Ts...>>::type(other.template get<typename std::tuple_element<0, std::tuple<Ts...>>::type>());
    else clone_impl(other, idx - 1);
  }

  void move(Variant&& other) {
    move_impl(std::move(other), index_);
  }

  void move_impl(Variant&& other, int idx) {
    if (idx == 0) new (&storage_) typename std::tuple_element<0, std::tuple<Ts...>>::type(std::move(other.template get<typename std::tuple_element<0, std::tuple<Ts...>>::type>()));
    else move_impl(std::move(other), idx - 1);
  }

  typename std::aligned_storage<sizeof...(Ts), alignof...(Ts)>::type storage_;
  int index_;
};

}  // namespace minidb
