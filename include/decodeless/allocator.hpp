// Copyright (c) 2024 Pyarelal Knowles, MIT License

#pragma once

#include <cstddef>
#include <cstdint>
#include <decodeless/allocator_concepts.hpp>
#include <memory>

namespace decodeless {

// Utility for a linear_memory_resource backed by either a memory
// resource or an allocator
template <memory_resource_or_allocator ResOrAlloc>
std::byte* allocate_bytes(ResOrAlloc& resOrAlloc, size_t bytes) {
    if constexpr (memory_resource<ResOrAlloc>)
        return static_cast<std::byte*>(resOrAlloc.allocate(bytes, 1u));
    else
        return resOrAlloc.allocate(bytes);
}

// Reallocate utility for a linear_memory_resource backed by either a memory
// resource or an allocator
template <realloc_resource_or_allocator ResOrAlloc>
std::byte* reallocate_bytes(ResOrAlloc& resOrAlloc, std::byte* original, size_t size) {
    if constexpr (memory_resource<ResOrAlloc>)
        return static_cast<std::byte*>(
            resOrAlloc.reallocate(static_cast<void*>(original), size, 1u));
    else
        return resOrAlloc.reallocate(original, size);
}

// A possibly-growable local linear arena allocator.
// - growable: The backing allocation may grow if it has reallocate() and the
//   call returns the same address.
// - local: Has per-allocator-instance state
// - linear: Gives monotonic/sequential but aligned allocations that cannot be
//   freed or reused. There is only a reset() call. Only trivially destructible
//   objects should be created from this.
// - arena: Allocations come from a single blob/pool and when it is exhausted
//   std::bad_alloc is thrown (unless a reallocate() is possible).
// Backed by either a STL style allocator or a concrete memory resource,
// although both need a reallocate() and max_size() call to enable growing.
// NOTE: currently expects std::byte allocators - use rebind_alloc from
// std::allocator_traits if needed
template <memory_resource_or_allocator ParentAllocator = std::allocator<std::byte>>
class linear_memory_resource {
public:
    static constexpr size_t INITIAL_SIZE = 1024 * 1024;
    using parent_allocator = ParentAllocator;

    linear_memory_resource(size_t                 initialSize = INITIAL_SIZE,
                           const ParentAllocator& parent = ParentAllocator())
        requires allocator<ParentAllocator>
        : m_parent(parent)
        , m_begin(allocate_bytes<ParentAllocator>(m_parent, initialSize))
        , m_next(reinterpret_cast<uintptr_t>(m_begin))
        , m_end(reinterpret_cast<uintptr_t>(m_begin) + initialSize) {}

    linear_memory_resource(size_t initialSize, ParentAllocator&& parent)
        requires memory_resource<ParentAllocator>
        : m_parent(std::move(parent))
        , m_begin(allocate_bytes(m_parent, initialSize))
        , m_next(reinterpret_cast<uintptr_t>(m_begin))
        , m_end(reinterpret_cast<uintptr_t>(m_begin) + initialSize) {}

    linear_memory_resource() = delete;
    linear_memory_resource(const linear_memory_resource& other) = delete;
    linear_memory_resource(linear_memory_resource&& other) noexcept = default;
    ~linear_memory_resource() { m_parent.deallocate(m_begin, capacity()); }
    linear_memory_resource& operator=(const linear_memory_resource& other) = delete;
    linear_memory_resource& operator=(linear_memory_resource&& other) noexcept = default;

    [[nodiscard]] constexpr void* allocate(std::size_t bytes, std::size_t align) {
        // Align
        uintptr_t result = m_next + ((-static_cast<ptrdiff_t>(m_next)) & (align - 1));

        // Allocate
        uintptr_t newNext = result + bytes;

        // Check for overflow and attempt to reallocate if possible
        if (newNext > m_end) {
            if constexpr (realloc_resource_or_allocator<ParentAllocator>) {
                // Allocate the larger of double the existing arena or enough to
                // fit what was just requested.
                size_t newSize = std::max(size(), 2 * capacity());

                // If double the reservation would overflow the backing
                // allocator, allocate exactly the maximum.
                if constexpr (has_max_size<ParentAllocator>) {
                    if (newSize > m_parent.max_size() && size() < m_parent.max_size()) {
                        newSize = m_parent.max_size();
                    }
                }

                // Verify the reallocation produced the same address.
                std::byte* addr = reallocate_bytes(m_parent, m_begin, newSize);
                if (addr != m_begin) {
                    throw std::bad_alloc();
                }

                m_end = reinterpret_cast<uintptr_t>(m_begin) + newSize;
            } else {
                throw std::bad_alloc();
            }
        }

        // Safe to update m_next as no exceptions were thrown.
        m_next = newNext;

        return reinterpret_cast<void*>(result);
    }

    // Deallocates memory. This operation is a no-op for linear_memory_resource
    // as individual deallocations are not supported.
    constexpr void deallocate(void* p, std::size_t bytes) {
        // Do nothing
        (void)p;
        (void)bytes;
    }

    // Clear all allocations to begin allocating from scratch, invalidating all
    // previously allocated memory.
    void reset() { m_next = reinterpret_cast<uintptr_t>(m_begin); }

    // Reallocate the parent allocation to exactly the size of all current
    // allocations.
    void truncate()
        requires realloc_resource_or_allocator<ParentAllocator>
    {
        std::byte* addr = reallocate_bytes(m_parent, m_begin, size());
        if (addr != m_begin) {
            throw std::bad_alloc();
        }
        m_end = m_next;
    }

    // Returns a pointer to the arena/parent allocation.
    void* data() const { return reinterpret_cast<void*>(m_begin); }

    // Returns the total number of bytes allocated within the arena
    size_t size() const { return m_next - reinterpret_cast<uintptr_t>(m_begin); }

    // Returns the size of the arena/parent allocation
    size_t capacity() const { return m_end - reinterpret_cast<uintptr_t>(m_begin); }

private:
    ParentAllocator m_parent;
    std::byte*      m_begin;
    uintptr_t       m_next;
    uintptr_t       m_end;
};

// Stateful STL-compatible allocator adaptor that holds a pointer to the
// concrete memory resource
template <trivially_destructible T, memory_resource MemoryResource>
class memory_resource_ref {
public:
    using resource_type = MemoryResource;
    using value_type = T;

    memory_resource_ref(resource_type& resource)
        : m_resource(&resource) {}

    // Needed by msvc
    template <class U>
    memory_resource_ref(const memory_resource_ref<U, MemoryResource>& other)
        : m_resource(&other.resource()) {}

    [[nodiscard]] constexpr T* allocate(std::size_t n) {
        return static_cast<T*>(m_resource->allocate(n * sizeof(T), alignof(T)));
    }

    constexpr void deallocate(T* p, std::size_t n) {
        return m_resource->deallocate(static_cast<void*>(p), n);
    }

    bool operator==(const memory_resource_ref& other) const {
        return m_resource == other.m_resource;
    }

    bool operator!=(const memory_resource_ref& other) const {
        return m_resource != other.m_resource;
    }

    resource_type& resource() const { return *m_resource; }

    // Needed by msvc
    template <class U>
    struct rebind {
        using other = memory_resource_ref<U, MemoryResource>;
    };

protected:
    resource_type* m_resource;
};

// STL compatible allocator with an implicit linear_memory_resource memory
// resource. The need for this emphasizes why std::pmr is a thing - the
// MemoryResource would ideally not affect the type.
template <trivially_destructible T, memory_resource MemoryResource = linear_memory_resource<>>
using linear_allocator = memory_resource_ref<T, MemoryResource>;

} // namespace decodeless
