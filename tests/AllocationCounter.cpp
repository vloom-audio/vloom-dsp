// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vloom Audio contributors
#include "AllocationCounter.hpp"

#include <cstdlib>
#include <new>

// The global operators, replaced for the whole test executable so that
// AllocationCounter sees every allocation and free made through them.
// Over-aligned forms keep their default (no type here needs them).

namespace {

thread_local int counting = 0;
thread_local long allocationCount = 0;
thread_local long deallocationCount = 0;

void* allocate(std::size_t size) noexcept
{
    if (counting > 0)
        ++allocationCount;
    return std::malloc(size == 0 ? 1 : size);
}

void release(void* pointer) noexcept
{
    if (pointer == nullptr)
        return;
    if (counting > 0)
        ++deallocationCount;
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

namespace vloom::dsp::test {

AllocationCounter::AllocationCounter() noexcept
    : allocationsBefore_(allocationCount)
    , deallocationsBefore_(deallocationCount)
{
    ++counting;
}

AllocationCounter::~AllocationCounter() { --counting; }

long AllocationCounter::allocations() const noexcept { return allocationCount - allocationsBefore_; }
long AllocationCounter::deallocations() const noexcept { return deallocationCount - deallocationsBefore_; }

} // namespace vloom::dsp::test
