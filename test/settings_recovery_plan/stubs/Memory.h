#pragma once
#include <memory>
#include <new>
#include <type_traits>

inline bool failNextBackupAllocation = false;
template <class T>
std::unique_ptr<T> makeUniqueNoThrow(size_t size) {
  static_assert(std::is_unbounded_array_v<T>);
  if (failNextBackupAllocation) {
    failNextBackupAllocation = false;
    return {};
  }
  return std::unique_ptr<T>(new (std::nothrow) std::remove_extent_t<T>[size]);
}
