// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vloom Audio contributors
//
// Replaces the global operator new and delete for the whole test program, so
// tests can check that real-time code neither allocates nor frees:
// allocationCount() counts every allocation on any thread; AllocationCounter
// counts allocations and frees on its own thread while it lives. Over-aligned
// forms keep their default (no type here needs them).
#include "AllocationCounter.hpp"

#include <atomic>
#include <cstdlib>
#include <new>

namespace {

std::atomic<std::size_t> totalAllocations{0};
thread_local int counting = 0;
thread_local long threadAllocations = 0;
thread_local long threadDeallocations = 0;

void* allocate(std::size_t size) noexcept
{
    totalAllocations.fetch_add(1, std::memory_order_relaxed);
    if (counting > 0)
        ++threadAllocations;
    return std::malloc(size == 0 ? 1 : size);
}

void release(void* pointer) noexcept
{
    if (pointer == nullptr)
        return;
    if (counting > 0)
        ++threadDeallocations;
    std::free(pointer);
}

void* allocateOrThrow(std::size_t size)
{
    void* pointer = allocate(size);
    if (pointer == nullptr)
        throw std::bad_alloc();
    return pointer;
}

} // namespace

void* operator new(std::size_t size) { return allocateOrThrow(size); }
void* operator new[](std::size_t size) { return allocateOrThrow(size); }
void* operator new(std::size_t size, const std::nothrow_t&) noexcept { return allocate(size); }
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept { return allocate(size); }
void operator delete(void* pointer) noexcept { release(pointer); }
void operator delete[](void* pointer) noexcept { release(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { release(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { release(pointer); }
void operator delete(void* pointer, const std::nothrow_t&) noexcept { release(pointer); }
void operator delete[](void* pointer, const std::nothrow_t&) noexcept { release(pointer); }

namespace vloom::test {

std::size_t allocationCount() noexcept { return totalAllocations.load(std::memory_order_relaxed); }

} // namespace vloom::test

namespace vloom::dsp::test {

AllocationCounter::AllocationCounter() noexcept
    : allocationsBefore_(threadAllocations)
    , deallocationsBefore_(threadDeallocations)
{
    ++counting;
}

AllocationCounter::~AllocationCounter() { --counting; }

long AllocationCounter::allocations() const noexcept { return threadAllocations - allocationsBefore_; }
long AllocationCounter::deallocations() const noexcept { return threadDeallocations - deallocationsBefore_; }

} // namespace vloom::dsp::test
