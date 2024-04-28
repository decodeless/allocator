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
concept memory_resource = requires(Resource& resource) {
    {
        resource.allocate(std::declval<std::size_t>(), std::declval<std::size_t>())
    } -> std::same_as<void*>;
    {
        resource.deallocate(std::declval<void*>(),
                             std::declval<std::size_t>())
    } -> std::same_as<void>;
};

template <class Resource>
concept realloc_memory_resource = memory_resource<Resource> && requires(Resource& resource) {
    {
        resource.reallocate(std::declval<void*>(), std::declval<std::size_t>(),
                            std::declval<std::size_t>())
    } -> std::same_as<void*>;
};

template <class Allocator>
concept allocator = requires(Allocator& allocator, typename Allocator::value_type) {
    {
        allocator.allocate(std::declval<std::size_t>())
    } -> std::same_as<typename Allocator::value_type*>;
    {
        allocator.deallocate(std::declval<typename Allocator::value_type*>(),
                             std::declval<std::size_t>())
    } -> std::same_as<void>;
};

template <class Allocator>
concept realloc_allocator =
    allocator<Allocator> && requires(Allocator& allocator, typename Allocator::value_type) {
        {
            allocator.reallocate(std::declval<typename Allocator::value_type*>(),
                                 std::declval<std::size_t>())
        } -> std::same_as<typename Allocator::value_type*>;
    };

template <class ResOrAlloc>
concept memory_resource_or_allocator = (memory_resource<ResOrAlloc> || allocator<ResOrAlloc>);

template <class ResOrAlloc>
concept realloc_resource_or_allocator =
    realloc_memory_resource<ResOrAlloc> || realloc_allocator<ResOrAlloc>;

template <class ResOrAlloc>
concept has_max_size = memory_resource_or_allocator<ResOrAlloc> && requires(ResOrAlloc allocator) {
    { allocator.max_size() } -> std::same_as<std::size_t>;
};

template <memory_resource_or_allocator ResOrAlloc>
std::byte* allocate_bytes(ResOrAlloc& resOrAlloc, size_t bytes) {
    if constexpr (memory_resource<ResOrAlloc>)
        return static_cast<std::byte*>(resOrAlloc.allocate(bytes, 1u));
    else
        return resOrAlloc.allocate(bytes);
}

template <realloc_resource_or_allocator ResOrAlloc>
std::byte* reallocate_bytes(ResOrAlloc& resOrAlloc, std::byte* original, size_t size) {
    if constexpr (memory_resource<ResOrAlloc>)
        return static_cast<std::byte*>(
            resOrAlloc.reallocate(static_cast<void*>(original), size, 1u));
    else
        return resOrAlloc.reallocate(original, size);
}

template <typename T>
concept trivially_destructible = std::is_trivially_destructible_v<T>;

// A possibly-growable local linear arena allocator.
// - growable: The backing allocation may grow if it has reallocate() and the
//   call returns the same address.
// - local: Has per-allocator-instance state
// - linear: Gives monotonic/sequential but aligned allocations that cannot be
//   freed or reused. There is only a reset() call. Only trivially destructible
//   objects should be created from this.
// - arena: Allocations come from a single blob/pool and when it is exhausted
//   std::bad_alloc is thrown (unless a reallocate() is possible).
// Backed by either a STL style allocator (typically just a pointer) or a
// concrete memory resource, although both need a reallocate() and max_size()
// call to enable growing.
// NOTE: currently expects std::byte allocators - use rebind_alloc from
// std::allocator_traits if needed
template <memory_resource_or_allocator ParentAllocator = std::allocator<std::byte>>
class linear_memory_resource {
public:
    static constexpr size_t INITIAL_SIZE = 1024 * 1024;
    using parent_allocator = ParentAllocator;

    linear_memory_resource(size_t                 initialSize = INITIAL_SIZE,
                           const ParentAllocator& parentAllocator = ParentAllocator())
        requires allocator<ParentAllocator>
        : m_parentAllocator(parentAllocator)
        , m_begin(allocate_bytes<ParentAllocator>(m_parentAllocator, initialSize))
        , m_next(reinterpret_cast<uintptr_t>(m_begin))
        , m_end(reinterpret_cast<uintptr_t>(m_begin) + initialSize) {}

    linear_memory_resource(size_t initialSize, ParentAllocator&& parentAllocator)
        requires memory_resource<ParentAllocator>
        : m_parentAllocator(std::move(parentAllocator))
        , m_begin(allocate_bytes(m_parentAllocator, initialSize))
        , m_next(reinterpret_cast<uintptr_t>(m_begin))
        , m_end(reinterpret_cast<uintptr_t>(m_begin) + initialSize) {}

    linear_memory_resource() = delete;
    linear_memory_resource(const linear_memory_resource& other) = delete;
    linear_memory_resource(linear_memory_resource&& other) noexcept = default;
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
            if constexpr (realloc_resource_or_allocator<ParentAllocator>) {
                // Allocate the larger of double the existing arena or enough to
                // fit what was just requested.
                size_t newSize = std::max(bytesAllocated(), 2 * bytesReserved());

                // If double the reservation would overflow the backing
                // allocator, allocate exactly the maximum.
                if constexpr (has_max_size<ParentAllocator>) {
                    if (newSize > m_parentAllocator.max_size() &&
                        bytesAllocated() < m_parentAllocator.max_size()) {
                        newSize = m_parentAllocator.max_size();
                    }
                }

                // Verify the reallocation produced the same address.
                std::byte* addr = reallocate_bytes(m_parentAllocator, m_begin, newSize);
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

protected:
    ParentAllocator m_parentAllocator;

private:
    std::byte*      m_begin;
    uintptr_t       m_next;
    uintptr_t       m_end;
};

// Workaround copyable STL allocators by passing around a pointer/reference to
// the object with state.
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

    resource_type& resource() const { return *m_resource; }

    // Needed by msvc
    template <class U>
    struct rebind {
        using other = memory_resource_ref<U, MemoryResource>;
    };

protected:
    resource_type* m_resource;
};

// STL compatible allocator with an implicit constructor from
// linear_memory_resource. Emphasizes why std::pmr is a thing - ParentAllocator
// shouldn't affect the type.
template <trivially_destructible T, memory_resource MemoryResource = linear_memory_resource<>>
using linear_allocator = memory_resource_ref<T, MemoryResource>;

namespace create {

// Utility calls to construct objects from a decodeless memory resource
namespace from_resource {

template <trivially_destructible T, memory_resource MemoryResource>
T* object(MemoryResource& memoryResource, const T& init) {
    return std::construct_at<T>(linear_allocator<T, MemoryResource>(memoryResource).allocate(1),
                                init);
};

template <trivially_destructible T, memory_resource MemoryResource, class... Args>
T* object(MemoryResource& memoryResource, Args&&... args) {
    return std::construct_at<T>(linear_allocator<T, MemoryResource>(memoryResource).allocate(1),
                                std::forward<Args>(args)...);
};

template <trivially_destructible T, memory_resource MemoryResource>
std::span<T> array(MemoryResource& memoryResource, size_t size) {
    auto result =
        std::span(linear_allocator<T, MemoryResource>(memoryResource).allocate(size), size);
    for (auto& obj : result)
        std::construct_at<T>(&obj);
    return result;
};

#ifdef __cpp_lib_ranges
template <trivially_destructible T, std::ranges::input_range Range = std::initializer_list<T>,
          memory_resource MemoryResource>
    requires std::convertible_to<std::ranges::range_value_t<Range>, T>
std::span<T> array(MemoryResource& memoryResource, Range&& range) {
    auto size = std::ranges::size(range);
    auto result =
        std::span(linear_allocator<T, MemoryResource>(memoryResource).allocate(size), size);
    auto out = result.begin();
    for (auto& in : range)
        std::construct_at<T>(&*out++, in);
    return result;
};

// Overload to deduce T from the Range type. Convenient but not always desired
template <std::ranges::input_range Range, memory_resource MemoryResource>
auto array(MemoryResource& memoryResource, Range&& range) {
    return array<std::ranges::range_value_t<Range>, Range, MemoryResource>(
        memoryResource, std::forward<Range>(range));
}
#endif

} // namespace from_resource

// Utility calls to construct objects from an STL compatible allocator
namespace from_allocator {

template <trivially_destructible T, allocator Allocator>
using allocator_rebind_t = typename std::allocator_traits<Allocator>::template rebind_alloc<T>;

template <trivially_destructible T, allocator Allocator>
T* object(const Allocator& allocator, const T& init) {
    return std::construct_at<T>(allocator_rebind_t<T, Allocator>(allocator).allocate(1), init);
};

template <trivially_destructible T, allocator Allocator, class... Args>
T* object(const Allocator& allocator, Args&&... args) {
    return std::construct_at<T>(allocator_rebind_t<T, Allocator>(allocator).allocate(1),
                                std::forward<Args>(args)...);
};

template <trivially_destructible T, allocator Allocator>
std::span<T> array(const Allocator& allocator, size_t size) {
    auto result = std::span(allocator_rebind_t<T, Allocator>(allocator).allocate(size), size);
    for (auto& obj : result)
        std::construct_at<T>(&obj);
    return result;
};

#ifdef __cpp_lib_ranges
template <trivially_destructible T, std::ranges::input_range Range = std::initializer_list<T>,
          allocator Allocator>
    requires std::convertible_to<std::ranges::range_value_t<Range>, T>
std::span<T> array(const Allocator& allocator, Range&& range) {
    auto size = std::ranges::size(range);
    auto result = std::span(allocator_rebind_t<T, Allocator>(allocator).allocate(size), size);
    auto out = result.begin();
    for (auto& in : range)
        std::construct_at<T>(&*out++, in);
    return result;
};

// Overload to deduce T from the Range type. Convenient but not always desired
template <std::ranges::input_range Range, allocator Allocator>
auto array(const Allocator& allocator, Range&& range) {
    return array<std::ranges::range_value_t<Range>, Range, Allocator>(allocator,
                                                                      std::forward<Range>(range));
}
#endif

} // namespace from_allocator

using namespace from_resource;
using namespace from_allocator;

} // namespace create

} // namespace decodeless
