#pragma once

// Replaces every unaligned global operator new and delete with one allocate /
// release pair the including test defines, so no block is ever freed by a
// form other than the one that took it (ASan keeps any form left unreplaced).
// Include from one translation unit per test binary.

#include <cstddef>
#include <new>

// Returns nullptr to refuse: the throwing forms then throw std::bad_alloc and
// the nothrow forms return nullptr. Never called with 0.
void* testAllocate(std::size_t size);
// Never called with nullptr.
void testRelease(void* p) noexcept;

void* operator new(std::size_t size) {
  if (void* p = testAllocate(size ? size : 1)) return p;
  throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
  return testAllocate(size ? size : 1);
}
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
  return testAllocate(size ? size : 1);
}
void operator delete(void* p) noexcept {
  if (p) testRelease(p);
}
void operator delete[](void* p) noexcept { ::operator delete(p); }
void operator delete(void* p, std::size_t) noexcept { ::operator delete(p); }
void operator delete[](void* p, std::size_t) noexcept { ::operator delete(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { ::operator delete(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { ::operator delete(p); }
