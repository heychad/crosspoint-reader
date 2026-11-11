#pragma once
#include <memory>
#include <new>
#include <type_traits>
#include <utility>
inline bool failAllocation = false;
template <typename T, typename... A>
std::unique_ptr<T> makeUniqueNoThrow(A&&... args) {
  if (failAllocation) return nullptr;
  return std::unique_ptr<T>(new (std::nothrow) T(std::forward<A>(args)...));
}
template <typename T>
std::unique_ptr<T> makeUniqueNoThrow(size_t count) {
  if (failAllocation) return nullptr;
  using E = std::remove_extent_t<T>;
  return std::unique_ptr<T>(new (std::nothrow) E[count]());
}
