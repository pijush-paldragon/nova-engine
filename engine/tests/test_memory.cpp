#include <doctest/doctest.h>
#include <nova/core/alloc_stats.hpp>
#include <nova/core/frame_arena.hpp>
#include <nova/core/handle.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <thread>

using namespace nova;

TEST_CASE("HandlePool detects stale handles and reuses slots") {
    struct Tex { int id; };
    HandlePool<Tex> pool;
    auto h1 = pool.insert({1});
    REQUIRE(pool.get(h1));
    CHECK(pool.get(h1)->id == 1);
    CHECK(pool.size() == 1);
    CHECK(pool.remove(h1));
    CHECK(pool.get(h1) == nullptr);
    CHECK(!pool.remove(h1));
    auto h2 = pool.insert({2});
    CHECK(h2.index == h1.index);
    CHECK(h2.generation != h1.generation);
    CHECK(pool.get(h1) == nullptr);
    CHECK(pool.get(h2)->id == 2);
    CHECK(pool.get(Handle<Tex>{}) == nullptr);
    CHECK(pool.get(Handle<Tex>{999, 1}) == nullptr);
}

TEST_CASE("HandlePool generation never becomes 0 on wrap") {
    HandlePool<int> pool;
    auto h = pool.insert(1);
    pool.debugSetGeneration(h.index, UINT32_MAX);
    CHECK(pool.remove(Handle<int>{h.index, UINT32_MAX}));
    auto h2 = pool.insert(2);
    CHECK(h2.index == h.index);
    CHECK(h2.generation == 1);
}

TEST_CASE("HandlePool destroys values on remove and clear") {
    auto tracker = std::make_shared<int>(0);
    HandlePool<std::shared_ptr<int>> pool;
    auto a = pool.insert(std::shared_ptr<int>(tracker));
    pool.insert(std::shared_ptr<int>(tracker));
    CHECK(tracker.use_count() == 3);
    pool.remove(a);
    CHECK(tracker.use_count() == 2);
    int visited = 0;
    pool.forEach([&](Handle<std::shared_ptr<int>>, std::shared_ptr<int>&) { ++visited; });
    CHECK(visited == 1);
    pool.clear();
    CHECK(tracker.use_count() == 1);
    CHECK(pool.size() == 0);
}

TEST_CASE("FrameArena aligns, resets, and survives overflow") {
    FrameArena arena{256};
    auto* p = arena.allocate(3, 1);
    auto* q = arena.allocate(8, 16);
    CHECK(reinterpret_cast<std::uintptr_t>(q) % 16 == 0);
    CHECK(p != q);
    auto big = arena.allocArray<float>(1000); // > capacity -> overflow, still usable
    CHECK(big.size() == 1000);
    big[999] = 1.f;
    CHECK(big[0] == 0.f); // value-initialised
    CHECK(arena.overflowBytesLastFrame() > 0);
    arena.reset();
    CHECK(arena.used() == 0);
    CHECK(arena.capacity() >= 256 + 4000);
    arena.allocArray<float>(1000);
    CHECK(arena.overflowBytesLastFrame() == 0); // grown primary block now fits
}

TEST_CASE("allocationCount increases monotonically on the game thread") {
    memory::markGameThread();
    auto before = memory::allocationCount();
    memory::recordAllocation();
    CHECK(memory::allocationCount() == before + 1);
}

TEST_CASE("allocationCount ignores allocations on other threads") {
    memory::markGameThread();
    auto before = memory::allocationCount();
    std::thread worker([] { memory::recordAllocation(); }); // e.g. a driver's background thread
    worker.join();
    CHECK(memory::allocationCount() == before);
}

TEST_CASE("DriverAllocScope excludes allocations made inside driver calls") {
    memory::markGameThread();
    auto before = memory::allocationCount();
    {
        memory::DriverAllocScope driver;
        memory::recordAllocation();
        {
            memory::DriverAllocScope nested;
            memory::recordAllocation();
        }
        memory::recordAllocation();
    }
    CHECK(memory::allocationCount() == before);
    memory::recordAllocation();
    CHECK(memory::allocationCount() == before + 1); // counting resumes after the scope
}
