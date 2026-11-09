#pragma once
#include <memory>
#include <new>
#include <utility>

inline bool failNextBackupAllocation = false;
template <class T, class... Args>
std::unique_ptr<T> makeUniqueNoThrow(Args&&... args) {
  if (failNextBackupAllocation) {
    failNextBackupAllocation = false;
    return {};
  }
  return std::unique_ptr<T>(new (std::nothrow) T(std::forward<Args>(args)...));
}
