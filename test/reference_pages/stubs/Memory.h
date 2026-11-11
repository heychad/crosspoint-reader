#pragma once
#include <memory>
#include <new>
#include <type_traits>
inline bool failReferenceAllocation = false;
template <typename T>
  requires std::is_unbounded_array_v<T>
std::unique_ptr<T> makeUniqueNoThrow(size_t count) {
  if (failReferenceAllocation) return nullptr;
  return std::unique_ptr<T>(new (std::nothrow) std::remove_extent_t<T>[count]());
}
