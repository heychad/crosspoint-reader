#include <SmallFileIdentity.h>

#include <cassert>
#include <cstdlib>
#include <new>

static int failAt = -1;
static int allocations = 0;
static int outstanding = 0;

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
  if (allocations++ == failAt) return nullptr;
  auto* ptr = std::malloc(size);
  if (ptr) ++outstanding;
  return ptr;
}
void operator delete[](void* ptr) noexcept {
  if (ptr) --outstanding;
  std::free(ptr);
}

struct File {
  uint32_t first;
  int calls = 0;
  bool settingsClusterList(uint32_t* out, size_t capacity, size_t* count) {
    ++calls;
    assert(capacity >= 2);
    out[0] = first;
    out[1] = first + 1;
    *count = 2;
    return true;
  }
};

struct LongFile {
  uint32_t first;
  bool settingsClusterList(uint32_t* out, size_t capacity, size_t* count) {
    if (capacity < 100) return false;
    for (size_t i = 0; i < 100; ++i) out[i] = first + i;
    *count = 100;
    return true;
  }
};

struct MoveFile {
  uint32_t cluster;
  bool empty = false;
  bool differentEntry = false;
  bool isFile() const { return true; }
  size_t fileSize() const { return empty ? 0 : 2; }
  size_t dataLength() const { return fileSize(); }
  size_t validLength() const { return fileSize(); }
  uint32_t firstCluster() const { return empty ? 0 : cluster; }
  bool moveDifferentEntry(const MoveFile&) const { return differentEntry; }
  bool settingsClusterList(uint32_t* out, size_t capacity, size_t* count) {
    if (empty || capacity < 2) return false;
    out[0] = cluster;
    out[1] = cluster + 1;
    *count = 2;
    return true;
  }
};

int main() {
  File first{2}, second{4}, overlap{3};
  failAt = 0;
  allocations = 0;
  assert(!settingsFileHasCheckedChain(first) && first.calls == 0 && outstanding == 0);
  for (int failed = 0; failed < 2; ++failed) {
    failAt = failed;
    allocations = 0;
    assert(!settingsFilesHaveDisjointChains(first, second) && first.calls == 0 && second.calls == 0 &&
           outstanding == 0);
  }
  failAt = 0;
  allocations = 0;
  assert(!moveFileHasCheckedChain(first) && first.calls == 0 && outstanding == 0);
  for (int failed = 0; failed < 2; ++failed) {
    failAt = failed;
    allocations = 0;
    assert(!moveFilesHaveDisjointChains(first, second) && first.calls == 0 && second.calls == 0 && outstanding == 0);
  }
  failAt = -1;
  allocations = 0;
  assert(settingsFileHasCheckedChain(first));
  assert(settingsFilesHaveDisjointChains(first, second));
  assert(!settingsFilesHaveDisjointChains(first, overlap));
  assert(moveFileHasCheckedChain(first));
  assert(moveFilesHaveDisjointChains(first, second));
  assert(!moveFilesHaveDisjointChains(first, overlap));
  LongFile longFirst{2}, longSecond{102}, longOverlap{98};
  assert(moveFilesHaveDisjointChains(longFirst, longSecond));
  assert(!moveFilesHaveDisjointChains(longFirst, longOverlap));
  MoveFile left{10}, disjoint{12}, alias{10}, overlapMove{11}, empty{0, true, false};
  assert(moveFilesHaveIndependentStorage(left, disjoint));  // Entry comparison can fail conservatively.
  assert(!moveFilesHaveIndependentStorage(left, alias));
  assert(!moveFilesHaveIndependentStorage(left, overlapMove));
  assert(!moveFilesHaveIndependentStorage(empty, empty));
  assert(!moveFilesHaveIndependentStorage(empty, left));
  empty.differentEntry = true;
  assert(moveFilesHaveIndependentStorage(empty, left));
  assert(outstanding == 0);
}
