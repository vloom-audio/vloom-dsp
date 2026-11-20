// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vloom Audio contributors
//
// Replaces the global operator new and delete for the test program, to count
// allocations: a test can check that a processor's process() allocates none.
#include "AllocationCounter.hpp"

#include <atomic>
#include <cstdlib>
#include <new>

namespace {

std::atomic<std::size_t> allocations{0};

void* allocate(std::size_t size)
{
    allocations.fetch_add(1, std::memory_order_relaxed);
    if (void* memory = std::malloc(size == 0 ? 1 : size))
        return memory;
    throw std::bad_alloc();
}

} // namespace

namespace vloom::test {

std::size_t allocationCount() noexcept { return allocations.load(std::memory_order_relaxed); }

} // namespace vloom::test

void* operator new(std::size_t size) { return allocate(size); }
void* operator new[](std::size_t size) { return allocate(size); }
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t /*size*/) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t /*size*/) noexcept { std::free(memory); }
