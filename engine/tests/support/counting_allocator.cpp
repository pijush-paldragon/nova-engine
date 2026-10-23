// Same counting allocator games get from entry.cpp, so tests can assert "no allocations".
// Linked into every test executable exactly once.
#include <nova/core/alloc_stats.hpp>

#include <cstdlib>
#include <new>

#if defined(NOVA_TRACK_ALLOCS)
void* operator new(std::size_t size) {
    nova::memory::recordAllocation();
    if (void* p = std::malloc(size ? size : 1)) return p;
    std::abort();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
#endif
