#pragma once

// Defines the replaced_new.hpp allocator as a tally of the bytes every form of
// new holds live, for tests that assert a path keeps nothing after use.
// Include from one translation unit per test binary, in place of defining
// testAllocate / testRelease.

#include <cstddef>
#include <cstdlib>

#include "replaced_new.hpp"

namespace heap_tally {
inline size_t g_liveBytes = 0;
inline size_t g_allocs = 0;
// The size rides ahead of the block in a header as wide as new's alignment,
// so the block keeps that alignment.
constexpr size_t kSizeHeader = __STDCPP_DEFAULT_NEW_ALIGNMENT__;
static_assert(kSizeHeader >= sizeof(size_t));
}  // namespace heap_tally

void* testAllocate(std::size_t size) {
  void* p = std::malloc(size + heap_tally::kSizeHeader);
  if (!p) return nullptr;
  *static_cast<size_t*>(p) = size;
  heap_tally::g_liveBytes += size;
  ++heap_tally::g_allocs;
  return static_cast<char*>(p) + heap_tally::kSizeHeader;
}

void testRelease(void* p) noexcept {
  void* base = static_cast<char*>(p) - heap_tally::kSizeHeader;
  heap_tally::g_liveBytes -= *static_cast<size_t*>(base);
  std::free(base);
}
