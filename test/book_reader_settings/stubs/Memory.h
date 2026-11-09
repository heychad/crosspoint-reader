#pragma once
#include <memory>
#include <new>
#include <utility>
inline bool failNextTestAllocation = false;
template <typename T>
  requires std::is_array_v<T>
std::unique_ptr<T> makeUniqueNoThrow(size_t count) {
  if (std::exchange(failNextTestAllocation, false)) return {};
  using Element = std::remove_extent_t<T>;
  return std::unique_ptr<T>(new (std::nothrow) Element[count]());
}
template <typename T, typename... Args>
  requires(!std::is_array_v<T>)
std::unique_ptr<T> makeUniqueNoThrow(Args&&... args) {
  if (std::exchange(failNextTestAllocation, false)) return {};
  return std::unique_ptr<T>(new (std::nothrow) T(std::forward<Args>(args)...));
}
