// Copyright (c) 2024 Pyarelal Knowles, MIT License

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <span>

#if __has_include(<ranges>)
    #include <ranges>
#endif

namespace decodeless {

template <class Resource>
concept ResourceType = requires(Resource resource) {
    {
        resource.allocate(std::declval<std::size_t>(), std::declval<std::size_t>())
    } -> std::same_as<void*>;
    {
        resource.deallocate(std::declval<void*>(),
                             std::declval<std::size_t>())
    } -> std::same_as<void>;
};

template <class Allocator>
concept AllocatorType = requires(Allocator allocator, typename Allocator::value_type) {
    {
        allocator.allocate(std::declval<std::size_t>())
    } -> std::same_as<typename Allocator::value_type*>;
    {
        allocator.deallocate(std::declval<typename Allocator::value_type*>(),
                             std::declval<std::size_t>())
    } -> std::same_as<void>;
};

template <class Allocator>
concept CanReallocate =
    AllocatorType<Allocator> && requires(Allocator allocator, typename Allocator::value_type) {
        {
            allocator.reallocate(std::declval<typename Allocator::value_type*>(),
                                 std::declval<std::size_t>())
        } -> std::same_as<typename Allocator::value_type*>;
    };

template <class T>
concept HasMaxSize = AllocatorType<T> && requires(T allocator, typename T::value_type) {
    { allocator.max_size() } -> std::same_as<std::size_t>;
};

template <typename T>
concept TriviallyDestructible = std::is_trivially_destructible_v<T>;

// A possibly-growable local linear arena allocator.
// - growable: The backing allocation may grow if it has reallocate() and the
//   call returns the same address.
// - local: Has per-allocator-instance state
// - linear: Gives monotonic/sequential but aligned allocations that cannot be
//   freed or reused. There is only a reset() call. Only trivially destructible
//   objects should be created from this.
// - arena: Allocations come from a single blob/pool and when it is exhausted
//   std::bad_alloc is thrown (unless a reallocate() is possible).
template <AllocatorType ParentAllocator = std::allocator<std::byte>>
class linear_memory_resource {
public:
    static constexpr size_t INITIAL_SIZE = 1024 * 1024;
    using parent_allocator = ParentAllocator;
    linear_memory_resource() = delete;
    linear_memory_resource(const linear_memory_resource& other) = delete;
    linear_memory_resource(linear_memory_resource&& other) noexcept = default;
    linear_memory_resource(size_t                 initialSize = INITIAL_SIZE,
                           const ParentAllocator& parentAllocator = ParentAllocator())
        : m_parentAllocator(parentAllocator)
        , m_begin(m_parentAllocator.allocate(initialSize))
        , m_next(reinterpret_cast<uintptr_t>(m_begin))
        , m_end(reinterpret_cast<uintptr_t>(m_begin) + initialSize) {}
    ~linear_memory_resource() { m_parentAllocator.deallocate(m_begin, bytesAllocated()); }
    linear_memory_resource& operator=(const linear_memory_resource& other) = delete;
    linear_memory_resource& operator=(linear_memory_resource&& other) noexcept = default;

    [[nodiscard]] constexpr void* allocate(std::size_t bytes, std::size_t align) {
        // Align
        uintptr_t result = m_next + ((-static_cast<ptrdiff_t>(m_next)) & (align - 1));

        // Allocate
        m_next = result + bytes;

        // Check for overflow and attempt to reallocate if possible
        if (m_next > m_end) {
            if constexpr (CanReallocate<ParentAllocator>) {
                // Allocate the larger of double the existing arena or enough to
                // fit what was just requested.
                size_t newSize = std::max(bytesAllocated(), 2 * bytesReserved());

                // If double the reservation would overflow the backing
                // allocator, allocate exactly the maximum.
                if constexpr (HasMaxSize<ParentAllocator>) {
                    if (newSize > m_parentAllocator.max_size() &&
                        bytesAllocated() < m_parentAllocator.max_size()) {
                        newSize = m_parentAllocator.max_size();
                    }
                }

                // Verify the reallocation produced the same address.
                std::byte* addr = m_parentAllocator.reallocate(m_begin, newSize);
                if (addr != m_begin) {
                    throw std::bad_alloc();
                }
            } else {
                throw std::bad_alloc();
            }
        }
        return reinterpret_cast<void*>(result);
    }

    constexpr void deallocate(void* p, std::size_t bytes) {
        // Do nothing
        (void)p;
        (void)bytes;
    }

    size_t bytesAllocated() const { return m_next - reinterpret_cast<uintptr_t>(m_begin); }
    size_t bytesReserved() const { return m_end - reinterpret_cast<uintptr_t>(m_begin); }
    void   reset() { m_next = reinterpret_cast<uintptr_t>(m_begin); }

    // Returns a pointer to the arena/parent allocation.
    void* arena() const { return reinterpret_cast<void*>(m_begin); }

private:
    ParentAllocator m_parentAllocator;
    std::byte*      m_begin;
    uintptr_t       m_next;
    uintptr_t       m_end;
};

// Workaround copyable STL allocators by passing around a pointer/reference to
// the object with state.
template <TriviallyDestructible T, ResourceType MemoryResource>
class aligned_allocator_ref {
public:
    using memory_resource = MemoryResource;
    using value_type = T;

    aligned_allocator_ref(memory_resource& resource)
        : m_resource(&resource) {}

    // Needed by msvc
    template <class U>
    aligned_allocator_ref(const aligned_allocator_ref<U, MemoryResource>& other)
        : m_resource(&other.resource()) {}

    [[nodiscard]] constexpr T* allocate(std::size_t n) {
        return static_cast<T*>(m_resource->allocate(n * sizeof(T), alignof(T)));
    }

    constexpr void deallocate(T* p, std::size_t n) {
        return m_resource->deallocate(static_cast<void*>(p), n);
    }

    memory_resource& resource() const { return *m_resource; }

    // Needed by msvc
    template <class U>
    struct rebind {
        using other = aligned_allocator_ref<U, MemoryResource>;
    };

protected:
    memory_resource* m_resource;
};

// STL compatible allocator with an implicit constructor from
// linear_memory_resource. Emphasizes why std::pmr is a thing - ParentAllocator
// shouldn't affect the type.
template <TriviallyDestructible T, ResourceType MemoryResource = linear_memory_resource<>>
using linear_allocator = aligned_allocator_ref<T, MemoryResource>;

template <class T, ResourceType MemoryResource, class... Args>
T* create(MemoryResource& memoryResource, Args&&... args) {
    return std::construct_at<T>(linear_allocator<T, MemoryResource>(memoryResource).allocate(1),
                                std::forward<Args>(args)...);
};

template <class T, ResourceType MemoryResource>
std::span<T> createArray(MemoryResource& memoryResource, size_t size) {
    auto result =
        std::span(linear_allocator<T, MemoryResource>(memoryResource).allocate(size), size);
    for (auto& obj : result)
        std::construct_at<T>(&obj);
    return result;
};

#ifdef __cpp_lib_ranges
template <std::ranges::input_range Range, ResourceType MemoryResource>
std::span<std::ranges::range_value_t<Range>> createArray(MemoryResource& memoryResource,
                                                         const Range&    range) {
    using T = std::ranges::range_value_t<Range>;
    auto size = std::ranges::size(range);
    auto result =
        std::span(linear_allocator<T, MemoryResource>(memoryResource).allocate(size), size);
    auto out = result.begin();
    for (auto& in : range)
        std::construct_at<T>(&*out++, in);
    return result;
};
#endif

} // namespace decodeless
