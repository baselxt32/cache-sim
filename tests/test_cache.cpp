#include "cache.hpp"

#include <cassert>
#include <fstream>
#include <iostream>

using namespace cachesim;

void test_lru() {
    Memory memory(100);
    Cache cache({32, 2, 16, 1}, "lru", true, memory);
    cache.access(0, 4, false);
    cache.access(16, 4, false);
    cache.access(0, 4, false);
    cache.access(32, 4, false);
    cache.access(16, 4, false);
    assert(cache.stats().hits == 1);
    assert(cache.stats().misses == 4);
    assert(cache.stats().evictions == 2);
    assert(cache.stats().compulsory == 3);
    assert(cache.stats().capacity == 1);
}

void test_fifo() {
    Memory memory(100);
    Cache cache({32, 2, 16, 1}, "fifo", true, memory);
    cache.access(0, 4, false);
    cache.access(16, 4, false);
    cache.access(0, 4, false);
    cache.access(32, 4, false);
    cache.access(16, 4, false);
    assert(cache.stats().hits == 2);
    assert(cache.stats().misses == 3);
    assert(cache.stats().evictions == 1);
}

void test_writeback() {
    Memory memory(100);
    Cache cache({16, 1, 16, 1}, "lru", true, memory);
    cache.access(0, 4, true);
    cache.access(16, 4, false);
    assert(cache.stats().writebacks == 1);
    assert(cache.stats().evictions == 1);
    assert(memory.accesses() == 3);
}

void test_no_write_allocate() {
    Memory memory(100);
    Cache cache({16, 1, 16, 1}, "lru", false, memory);
    cache.access(0, 4, true);
    cache.access(0, 4, true);
    assert(cache.stats().misses == 2);
    assert(cache.stats().policy_misses == 1);
    assert(cache.stats().writebacks == 0);
    assert(memory.accesses() == 2);
}

void test_conflict() {
    Memory memory(100);
    Cache cache({64, 1, 16, 1}, "lru", true, memory);
    cache.access(0, 4, false);
    cache.access(64, 4, false);
    cache.access(0, 4, false);
    assert(cache.stats().compulsory == 2);
    assert(cache.stats().conflict == 1);
}

void test_trace() {
    const char* path = "tiny.trace";
    {
        std::ofstream out(path);
        out << "I 00000000 4\nL 00000100 4\nS 00000100 4\n"
               "I 00000004 4\nL 00000120 4\n";
    }
    Hierarchy sim({16, 1, 16, 1}, {16, 1, 16, 1}, nullptr,
                  "lru", true, 10);
    const auto stats = sim.run(path, 2, 1.5);
    assert(stats.fetches == 2 && stats.loads == 2 && stats.stores == 1);
    assert(stats.l1i.misses == 1 && stats.l1i.hits == 1);
    assert(stats.l1d.misses == 2 && stats.l1d.hits == 1);
    assert(stats.l1d.writebacks == 1);
    assert(stats.memory_accesses == 4);
    assert(stats.access_cycles == 45);
    assert(stats.amat == 9.0);
    assert(stats.estimated_cpi == 21.5);
    std::remove(path);
}

int main() {
    test_lru();
    test_fifo();
    test_writeback();
    test_no_write_allocate();
    test_conflict();
    test_trace();
    std::cout << "PASS 6 cache tests\n";
}
