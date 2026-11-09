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

int main() {
  File first{2}, second{4}, overlap{3};
  failAt = 0;
  allocations = 0;
  assert(!settingsFileHasCheckedChain(first) && first.calls == 0 && outstanding == 0);
  for (int failed = 0; failed < 2; ++failed) {
    failAt = failed;
    allocations = 0;
    assert(!settingsFilesHaveDisjointChains(first, second) && first.calls == 0 && second.calls == 0 && outstanding == 0);
  }
  failAt = -1;
  allocations = 0;
  assert(settingsFileHasCheckedChain(first));
  assert(settingsFilesHaveDisjointChains(first, second));
  assert(!settingsFilesHaveDisjointChains(first, overlap));
  assert(outstanding == 0);
}
