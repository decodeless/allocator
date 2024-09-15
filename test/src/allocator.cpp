// Copyright (c) 2024 Pyarelal Knowles, MIT License

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <decodeless/allocator.hpp>
#include <decodeless/allocator_construction.hpp>
#include <decodeless/pmr_allocator.hpp>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <initializer_list>
#include <utility>

using namespace decodeless;

template <std::byte* ptr = nullptr>
struct ConstAllocator {
    using value_type = std::byte;
    static value_type* allocate(std::size_t n) {
        EXPECT_GT(n, 0);
        EXPECT_FALSE(allocated);
        allocated = true;
        return ptr;
    }
    static void deallocate(value_type* p, std::size_t n) noexcept {
        EXPECT_EQ(p, ptr);
        EXPECT_GT(n, 0);
        EXPECT_TRUE(allocated);
        allocated = false;
    }
    static bool allocated;
};

template <std::byte* ptr>
bool ConstAllocator<ptr>::allocated = false;

using NullAllocator = ConstAllocator<nullptr>;

template <std::byte* ptr = nullptr>
struct ReallocConstAllocator {
    using value_type = std::byte;
    static value_type* allocate(std::size_t n) {
        EXPECT_EQ(size, 0);
        EXPECT_GT(n, 0);
        size = n;
        return ptr;
    }
    static value_type* reallocate(value_type* p, std::size_t n) noexcept {
        EXPECT_EQ(p, ptr);
        EXPECT_GT(n, 0);
        size = n;
        return ptr;
    }
    static void deallocate(value_type* p, std::size_t n) noexcept {
        EXPECT_EQ(p, ptr);
        EXPECT_GT(size, 0);
        EXPECT_GT(n, 0);
        size = 0;
    }
    static size_t size;
};

template <std::byte* ptr>
size_t ReallocConstAllocator<ptr>::size = 0;

using ReallocNullAllocator = ReallocConstAllocator<nullptr>;

struct Allocate : testing::Test {
    virtual void TearDown() {
        // Check for leaks
        EXPECT_FALSE(NullAllocator::allocated);
        EXPECT_EQ(ReallocNullAllocator::size, 0);
    }
};

static_assert(realloc_allocator<ReallocNullAllocator>);

template <std::byte* ptr = nullptr>
struct ConstMemoryResource {
    void* allocate(std::size_t n, std::size_t) {
        EXPECT_GT(n, 0);
        EXPECT_FALSE(allocated);
        allocated = true;
        return ptr;
    }
    void deallocate(void* p, std::size_t n) noexcept {
        EXPECT_EQ(p, ptr);
        EXPECT_GT(n, 0);
        EXPECT_TRUE(allocated);
        allocated = false;
    }
    ConstMemoryResource() = default;
    ConstMemoryResource(const ConstMemoryResource& other) = delete;
    ConstMemoryResource(ConstMemoryResource&& other) noexcept
        : allocated(other.allocated) {
        other.allocated = false;
    }
    ConstMemoryResource& operator=(const ConstMemoryResource& other) = delete;
    ConstMemoryResource& operator=(ConstMemoryResource&& other) noexcept {
        std::swap(allocated = false, other.allocated);
        return *this;
    }
    ~ConstMemoryResource() { EXPECT_FALSE(allocated); }
    bool allocated = false;
};

using NullMemoryResource = ConstMemoryResource<nullptr>;

template <std::byte* ptr = nullptr>
struct ReallocConstMemoryResource {
    void* allocate(std::size_t n, std::size_t) {
        EXPECT_GT(n, 0);
        EXPECT_EQ(size, 0);
        size = n;
        return ptr;
    }
    void* reallocate(void* p, std::size_t n, std::size_t) noexcept {
        EXPECT_EQ(p, ptr);
        EXPECT_GT(n, 0);
        size = n;
        return ptr;
    }
    void deallocate(void* p, std::size_t n) noexcept {
        EXPECT_GT(size, 0);
        EXPECT_EQ(p, ptr);
        EXPECT_GT(n, 0);
        size = 0;
    }
    ReallocConstMemoryResource() = default;
    ReallocConstMemoryResource(const ReallocConstMemoryResource& other) = delete;
    ReallocConstMemoryResource(ReallocConstMemoryResource&& other) noexcept
        : size(other.size) {
        other.size = 0;
    }
    ReallocConstMemoryResource& operator=(const ReallocConstMemoryResource& other) = delete;
    ReallocConstMemoryResource& operator=(ReallocConstMemoryResource&& other) noexcept {
        std::swap(size = 0, other.size);
        return *this;
    }
    ~ReallocConstMemoryResource() { EXPECT_EQ(size, 0); }
    size_t size = 0;
};

using ReallocNullMemoryResource = ReallocConstMemoryResource<nullptr>;

static_assert(realloc_memory_resource<ReallocNullMemoryResource>);
static_assert(realloc_allocator<memory_resource_ref<std::byte, ReallocNullMemoryResource>>,
              "linear_allocator should enable reallocate when possible");
static_assert(!realloc_allocator<linear_allocator<std::byte, std::allocator<std::byte>>>,
              "std::allocator does not reallocate");
static_assert(!realloc_memory_resource<linear_memory_resource<ReallocNullAllocator>>,
              "linear memory does not reallocate, only its parent");
static_assert(!std::default_initializable<linear_memory_resource<NullAllocator>>);
static_assert(!std::default_initializable<linear_memory_resource<NullMemoryResource>>);
static_assert(std::default_initializable<linear_memory_resource<ReallocNullAllocator>>);
static_assert(!std::default_initializable<linear_memory_resource<ReallocNullMemoryResource>>);

TEST_F(Allocate, Object) {
    linear_memory_resource<NullAllocator> memory(23);

    // byte can be placed anywhere
    EXPECT_EQ(memory.allocate(sizeof(char), alignof(char)), reinterpret_cast<void*>(0));
    EXPECT_EQ(memory.size(), 1);

    // int after the byte must have 3 bytes padding, placed at 4 and taking 4
    EXPECT_EQ(memory.allocate(sizeof(int), alignof(int)), reinterpret_cast<void*>(4));
    EXPECT_EQ(memory.size(), 8);

    // double after int must have 4 bytes padding, placed at 8, taking 8 more
    EXPECT_EQ(memory.allocate(sizeof(double), alignof(double)), reinterpret_cast<void*>(8));
    EXPECT_EQ(memory.size(), 16);

    // another byte to force some padding, together with another int won't fit
    EXPECT_EQ(memory.capacity() - memory.size(), 7);
    EXPECT_EQ(memory.allocate(sizeof(char), alignof(char)), reinterpret_cast<void*>(16));
    EXPECT_EQ(memory.capacity() - memory.size(),
              6); // plenty left for an int, but not aligned
    EXPECT_THROW((void)memory.allocate(sizeof(int), alignof(int)), std::bad_alloc);
}

TEST_F(Allocate, Array) {
    linear_memory_resource<NullAllocator> memory(32);

    // byte can be placed anywhere
    EXPECT_EQ(memory.allocate(sizeof(char) * 3, alignof(char)), reinterpret_cast<char*>(0));
    EXPECT_EQ(memory.size(), 3);

    // 2 ints after the 3rd byte must have 3 bytes padding, placed at 4 and taking 8
    EXPECT_EQ(memory.allocate(sizeof(int) * 2, alignof(int)), reinterpret_cast<int*>(4));
    EXPECT_EQ(memory.size(), 12);

    // 2 doubles after 12 bytes must have 4 bytes padding, placed at 16, taking 16 more
    EXPECT_EQ(memory.allocate(sizeof(double) * 2, alignof(double)), reinterpret_cast<double*>(16));
    EXPECT_EQ(memory.size(), 32);
}

TEST_F(Allocate, EmptyNonrealloc) {
    linear_memory_resource<NullAllocator> memory(42);
    EXPECT_EQ(memory.size(), 0);
    EXPECT_EQ(memory.capacity(), 42);
}

TEST_F(Allocate, EmptyReallocDefault) {
    linear_memory_resource<ReallocNullAllocator> memory;
    EXPECT_EQ(memory.size(), 0);
    EXPECT_EQ(memory.capacity(), 0);
}

TEST_F(Allocate, EmptyRealloc) {
    linear_memory_resource<ReallocNullMemoryResource> memory{ReallocNullMemoryResource()};
    EXPECT_EQ(memory.size(), 0);
    EXPECT_EQ(memory.capacity(), 0);
}

TEST_F(Allocate, ZeroInitialRealloc) {
    linear_memory_resource<ReallocNullAllocator> memory{0, ReallocNullAllocator()};
    EXPECT_EQ(memory.size(), 0);
    EXPECT_EQ(memory.capacity(), 0);
}

TEST_F(Allocate, ZeroInitialReallocMemoryResource) {
    linear_memory_resource<ReallocNullMemoryResource> memory{0, ReallocNullMemoryResource()};
    EXPECT_EQ(memory.size(), 0);
    EXPECT_EQ(memory.capacity(), 0);
}

TEST_F(Allocate, Truncate) {
    linear_memory_resource<ReallocNullAllocator> memory;
    std::ignore = memory.allocate(1, 1);
    EXPECT_EQ(memory.size(), 1);
    EXPECT_GE(memory.capacity(), 1);
    EXPECT_EQ(memory.parent().size, memory.capacity());
    memory.truncate();
    EXPECT_EQ(memory.size(), 1);
    EXPECT_EQ(memory.capacity(), memory.size()); // capacity must be reduced to the size
    EXPECT_EQ(memory.parent().size, memory.capacity());
}

TEST_F(Allocate, TruncateEmpty) {
    linear_memory_resource<ReallocNullAllocator> memory;
    EXPECT_EQ(memory.parent().size, 0);
    memory.truncate();
    EXPECT_EQ(memory.parent().size, 0);
}

TEST_F(Allocate, TruncateReset) {
    linear_memory_resource<ReallocNullAllocator> memory;
    std::ignore = memory.allocate(1, 1);
    EXPECT_EQ(memory.size(), 1);
    EXPECT_EQ(memory.capacity(), 1);
    memory.reset();
    EXPECT_EQ(memory.size(), 0);
    EXPECT_EQ(memory.capacity(), 1);
    memory.truncate();
    EXPECT_EQ(memory.size(), 0);
    EXPECT_EQ(memory.capacity(), 0);
    EXPECT_EQ(memory.parent().size, memory.capacity());
}

TEST_F(Allocate, EmptyAllocate) {
    linear_memory_resource<ReallocNullAllocator> memory;
    EXPECT_EQ(memory.parent().size, 0);
    std::ignore = memory.allocate(1, 1);
    EXPECT_EQ(memory.parent().size, 1);
}

TEST_F(Allocate, Initialize) {
    linear_memory_resource memory(1024);
    std::span<uint8_t>     raw = create::array<uint8_t>(memory, 1024);
    std::ranges::fill(raw, 0xeeu);
    memory.reset();

    int* i = create::object<int>(memory);
    EXPECT_EQ(i, reinterpret_cast<int*>(raw.data()));
    EXPECT_EQ(*i, 0);

    int* j = create::object<int>(memory, 42);
    EXPECT_EQ(i + 1, j);
    EXPECT_EQ(*j, 42);

    std::span<int> span = create::array<int>(memory, 10);
    EXPECT_EQ(j + 1, span.data());
    EXPECT_EQ(span[0], 0);

    std::span<int> span2 = create::array(memory, std::vector{0, 1, 2});
    std::span<int> span3 = create::array<int>(memory, {3, 4, 5});
    EXPECT_EQ(span2[0], 0);
    EXPECT_EQ(span2[1], 1);
    EXPECT_EQ(span2[2], 2);
    EXPECT_EQ(span3[0], 3);
    EXPECT_EQ(span3[1], 4);
    EXPECT_EQ(span3[2], 5);
}

TEST_F(Allocate, Realloc) {
    linear_memory_resource<ReallocNullAllocator> alloc(4);
    EXPECT_EQ(alloc.parent().size, 4);
    (void)alloc.allocate(sizeof(int), alignof(int));
    EXPECT_EQ(alloc.parent().size, 1 * sizeof(int));
    (void)alloc.allocate(sizeof(int), alignof(int));
    EXPECT_EQ(alloc.parent().size, 2 * sizeof(int));

    // Allocate exact size for allocations exceeding double capacity
    (void)alloc.allocate(sizeof(int) * 1000, alignof(int));
    EXPECT_EQ(alloc.parent().size, 1002 * sizeof(int));

    // Doubling of capacity for allocations under double existing capacity
    (void)alloc.allocate(sizeof(int), alignof(int));
    EXPECT_EQ(alloc.parent().size, 2004 * sizeof(int));

    // Truncate should truncate the parent allocator
    alloc.truncate();
    EXPECT_EQ(alloc.parent().size, 1003 * sizeof(int));
}

struct EqualityTestAlloc {
    using value_type = std::byte;
    static value_type* allocate(std::size_t) { return nullptr; }
    static void        deallocate(value_type*, std::size_t) noexcept {}
};

TEST_F(Allocate, Equality) {
    linear_memory_resource<EqualityTestAlloc>         r0(4);
    linear_memory_resource<EqualityTestAlloc>         r1(4);
    linear_memory_resource<std::allocator<std::byte>> r2(4);
    linear_allocator<int, EqualityTestAlloc>          a0(r0);
    linear_allocator<int, EqualityTestAlloc>          a1(r1);
    linear_allocator<int, std::allocator<std::byte>>  a2(r2);
    linear_allocator<int, EqualityTestAlloc>          c0(a0);
    linear_allocator<int, EqualityTestAlloc>          c1(a1);
    linear_allocator<int, std::allocator<std::byte>>  c2(a2);
    EXPECT_EQ(a0, c0);
    EXPECT_EQ(a1, c1);
    EXPECT_EQ(a2, c2);
    EXPECT_NE(a0, c1);
    EXPECT_NE(a1, c0);
}

// Relaxed test case for MSVC where the debug vector allocates extra crap
TEST_F(Allocate, VectorRelaxed) {
    linear_memory_resource alloc(100);
    EXPECT_EQ(alloc.size(), 0);
    EXPECT_EQ(alloc.capacity(), 100);
    std::vector<uint8_t, linear_allocator<uint8_t>> vec(10, alloc);
    EXPECT_GE(alloc.size(), 10);
    auto allocated = alloc.size();
    vec.reserve(20);
    EXPECT_GT(alloc.size(), allocated);
    EXPECT_THROW(vec.reserve(100), std::bad_alloc);
}

TEST_F(Allocate, Vector) {
    bool debug =
#if defined(NDEBUG)
        false;
#else
        true;
#endif
    bool msvc =
#if defined(_MSC_VER)
        true;
#else
        false;
#endif
    if (debug && msvc) {
        GTEST_SKIP() << "Skipping test - msvc debug vector makes extraneous allocations";
    }

    linear_memory_resource alloc(30);
    EXPECT_EQ(alloc.size(), 0);
    EXPECT_EQ(alloc.capacity(), 30);

    // Yes, this is possible but don't do it. std::vector can easily reallocate
    // which will leave unused holes in the linear allocator.
    std::vector<uint8_t, linear_allocator<uint8_t>> vec(10, alloc);
    EXPECT_EQ(alloc.size(), 10);
    vec.reserve(20);
    EXPECT_EQ(alloc.size(), 30);
    EXPECT_THROW(vec.reserve(21), std::bad_alloc);
}

TEST_F(Allocate, PmrAllocator) {
    pmr_linear_memory_resource<std::allocator<std::byte>> res(100);
    EXPECT_EQ(res.size(), 0);
    EXPECT_EQ(res.capacity(), 100);
    std::pmr::polymorphic_allocator<std::byte> alloc(&res);
    std::span<uint8_t>                         bytes = create::array<uint8_t>(alloc, 10);
    EXPECT_EQ(bytes.size(), 10);
    EXPECT_EQ(res.size(), 10);
}

TEST_F(Allocate, PmrVectorRelaxed) {
    pmr_linear_memory_resource<std::allocator<std::byte>> res(100);
    EXPECT_EQ(res.size(), 0);
    EXPECT_EQ(res.capacity(), 100);
    std::pmr::vector<uint8_t> vec(10, &res);
    EXPECT_GE(res.size(), 10);
    auto allocated = res.size();
    vec.reserve(20);
    EXPECT_GT(res.size(), allocated);
    EXPECT_THROW(vec.reserve(100), std::bad_alloc);
}

TEST_F(Allocate, ArrayFromView) {
    using namespace std::views;
    auto                   running_sum = [l = 0](int i) mutable { return std::exchange(l, l + i); };
    std::vector            ints{1, 2, 3, 4, 5};
    linear_memory_resource alloc(100);
    std::span<int> array = decodeless::create::array(alloc, ints | transform(running_sum));
    EXPECT_THAT(array, testing::ElementsAre(0, 1, 3, 6, 10));
}

TEST_F(Allocate, Readme) {
    // could also be decodeless::mapped_file_allocator<std::byte> from
    // decodeless_writer
    using parent_allocator = std::allocator<std::byte>;
    decodeless::linear_memory_resource<parent_allocator> memory(1024);

    std::span<int> array = decodeless::create::array<int>(memory, {(int)1, 3, 6, 10, 15});
    EXPECT_EQ(array.size(), 5);
    EXPECT_EQ(array[4], 15);
    EXPECT_EQ(memory.size(), sizeof(int) * 5);

    double* alignedDouble = decodeless::create::object(memory, 42.0);
    EXPECT_EQ(*alignedDouble, 42.0);
    EXPECT_EQ(memory.size(), sizeof(int) * 5 + sizeof(double) + 4);

    decodeless::pmr_linear_memory_resource     res(100);
    std::pmr::polymorphic_allocator<std::byte> alloc(&res); // interface abstraction
    std::span<uint8_t> bytes = decodeless::create::array<uint8_t>(alloc, 10);
    EXPECT_EQ(bytes.size(), 10);
    EXPECT_EQ(res.size(), 10);
}

TEST_F(Allocate, References) {
    linear_memory_resource            res(100);
    auto&                             res_r(res);
    linear_allocator<std::byte>       alloc(res);
    const linear_allocator<std::byte> alloc_c(alloc);
    auto&                             alloc_r(alloc);
    const auto&                       alloc_cr(alloc);

    { [[maybe_unused]] std::span<int> r = create::array<int>(res, 1); }
    { [[maybe_unused]] std::span<int> r = create::array<int>(res_r, 1); }
    { [[maybe_unused]] std::span<int> r = create::array<int>(alloc, 1); }
    { [[maybe_unused]] std::span<int> r = create::array<int>(alloc_c, 1); }
    { [[maybe_unused]] std::span<int> r = create::array<int>(alloc_r, 1); }
    { [[maybe_unused]] std::span<int> r = create::array<int>(alloc_cr, 1); }
    EXPECT_EQ(res.size(), sizeof(int) * 6);

    std::vector init{42};
    { [[maybe_unused]] std::span<int> r = create::array(res, init); }
    { [[maybe_unused]] std::span<int> r = create::array(res, init); }
    { [[maybe_unused]] std::span<int> r = create::array(alloc, init); }
    { [[maybe_unused]] std::span<int> r = create::array(alloc_c, init); }
    { [[maybe_unused]] std::span<int> r = create::array(alloc_r, init); }
    { [[maybe_unused]] std::span<int> r = create::array(alloc_cr, init); }
    EXPECT_EQ(res.size(), sizeof(int) * 12);
}

// Test allocation of zero bytes to ensure it handles no-operation requests
// correctly
TEST_F(Allocate, ZeroBytes) {
    linear_memory_resource<NullAllocator> memory(23);
    EXPECT_EQ(memory.allocate(0, 1), nullptr);
    EXPECT_EQ(memory.size(), 0);
}

// Test allocation that exactly matches the remaining capacity to verify
// boundary conditions
TEST_F(Allocate, ExactCapacity) {
    linear_memory_resource<NullAllocator> memory(23);
    EXPECT_EQ(memory.allocate(23, 1), reinterpret_cast<void*>(0));
    EXPECT_EQ(memory.size(), 23);
}

// Test repeated allocations that gradually use up the memory to ensure
// consistent behavior as memory fills
TEST_F(Allocate, RepeatedAllocations) {
    linear_memory_resource<NullAllocator> memory(23);
    for (size_t i = 0; i < 23; i++) {
        EXPECT_EQ(memory.allocate(1, 1), reinterpret_cast<void*>(i));
        EXPECT_EQ(memory.size(), i + 1);
    }
}

// Verify behavior when attempting to allocate more memory than available,
// beyond just expecting std::bad_alloc
TEST_F(Allocate, OutOfMemory) {
    linear_memory_resource<NullAllocator> memory(23);
    EXPECT_THROW((void)memory.allocate(24, 1), std::bad_alloc);
    EXPECT_EQ(memory.size(), 0);
}

// Test the allocator's response to unusual or extreme alignment requirements
TEST_F(Allocate, UnusualAlignment) {
    linear_memory_resource<NullAllocator> memory(23);
    EXPECT_EQ(memory.allocate(sizeof(int), 16), reinterpret_cast<void*>(0));
    EXPECT_EQ(memory.size(), sizeof(int));
}

// Test allocation of a large object to verify handling of large allocations
TEST_F(Allocate, LargeAllocation) {
    linear_memory_resource<NullAllocator> memory(200'000'000);
    EXPECT_EQ(memory.allocate(123'456'789, 1), reinterpret_cast<void*>(0));
    EXPECT_EQ(memory.size(), 123'456'789);
}

// More detailed tests focusing on alignment
TEST_F(Allocate, Alignment) {
    linear_memory_resource<NullAllocator> memory(1024);

    // Test alignment for different types
    EXPECT_EQ(memory.allocate(sizeof(char), alignof(char)), reinterpret_cast<void*>(0));
    EXPECT_EQ(memory.size(), sizeof(char));

    // Since char typically has an alignment of 1 and size of 1, the next int (usually 4 bytes
    // alignment) should start at 4
    EXPECT_EQ(memory.allocate(sizeof(int), alignof(int)), reinterpret_cast<void*>(4));
    EXPECT_EQ(memory.size(), 4 + sizeof(int));

    // Double typically requires alignment of 8, so it should start at the next multiple of 8 after
    // the last int
    EXPECT_EQ(memory.allocate(sizeof(double), alignof(double)), reinterpret_cast<void*>(8));
    EXPECT_EQ(memory.size(), 8 + sizeof(double));

    // Long long typically requires alignment of 8 and should follow the double
    EXPECT_EQ(memory.allocate(sizeof(long long), alignof(long long)), reinterpret_cast<void*>(16));
    EXPECT_EQ(memory.size(), 16 + sizeof(long long));

    // Test alignment for arrays
    EXPECT_EQ(memory.allocate(sizeof(char) * 3, alignof(char)), reinterpret_cast<char*>(24));
    EXPECT_EQ(memory.size(), 24 + sizeof(char) * 3);

    EXPECT_EQ(memory.allocate(sizeof(int) * 2, alignof(int)), reinterpret_cast<int*>(28));
    EXPECT_EQ(memory.size(), 28 + sizeof(int) * 2);

    EXPECT_EQ(memory.allocate(sizeof(double) * 2, alignof(double)), reinterpret_cast<double*>(40));
    EXPECT_EQ(memory.size(), 40 + sizeof(double) * 2);

#if defined(_MSC_VER)
    #pragma warning(push)
    #pragma warning(disable : 4324) // "structure was padded due to alignment specifier"
#endif

    // Test alignment for objects with non-standard alignment
    struct AlignedStruct {
        int data;
        alignas(16) int alignedData;
    };

#if defined(_MSC_VER)
    #pragma warning(pop)
#endif

    // AlignedStruct requires alignment of 16, so it should start at the next multiple of 16
    EXPECT_EQ(memory.allocate(sizeof(AlignedStruct), alignof(AlignedStruct)),
              reinterpret_cast<void*>(64));
    EXPECT_EQ(memory.size(), 64 + sizeof(AlignedStruct));
}

TEST_F(Allocate, MemoryResource) {
    linear_memory_resource<NullMemoryResource> memory{4, NullMemoryResource()};
    EXPECT_EQ(memory.size(), 0);
    EXPECT_EQ(memory.capacity(), 4);
    std::ignore = memory.allocate(4, 4);
    EXPECT_THROW(std::ignore = memory.allocate(4, 4), std::bad_alloc);
    EXPECT_EQ(memory.size(), 4);
    EXPECT_EQ(memory.capacity(), 4);
}

TEST_F(Allocate, ReallocMemoryResource) {
    linear_memory_resource<ReallocNullMemoryResource> memory{ReallocNullMemoryResource()};
    EXPECT_EQ(memory.size(), 0);
    EXPECT_EQ(memory.capacity(), 0);
    std::ignore = memory.allocate(4, 4);
    std::ignore = memory.allocate(4, 4);
    EXPECT_EQ(memory.size(), 8);
    EXPECT_EQ(memory.capacity(), 8);
}

TEST_F(Allocate, AllocatorBackedMove) {
    linear_memory_resource<NullAllocator> memory(8);
    auto                                  a = reinterpret_cast<uintptr_t>(memory.allocate(4, 4));
    linear_memory_resource<NullAllocator> moved_memory = std::move(memory);
    auto b = reinterpret_cast<uintptr_t>(moved_memory.allocate(4, 4));
    EXPECT_EQ(a + 4, b);
}

TEST_F(Allocate, MemoryResourceBackedMove) {
    linear_memory_resource<NullMemoryResource> memory(8, NullMemoryResource());
    auto a = reinterpret_cast<uintptr_t>(memory.allocate(4, 4));
    linear_memory_resource<NullMemoryResource> moved_memory = std::move(memory);
    auto b = reinterpret_cast<uintptr_t>(moved_memory.allocate(4, 4));
    EXPECT_EQ(a + 4, b);
}

TEST_F(Allocate, ReallocAllocatorBackedMove) {
    linear_memory_resource<ReallocNullAllocator> memory(8);
    auto a = reinterpret_cast<uintptr_t>(memory.allocate(4, 4));
    linear_memory_resource<ReallocNullAllocator> moved_memory = std::move(memory);
    auto b = reinterpret_cast<uintptr_t>(moved_memory.allocate(4, 4));
    EXPECT_EQ(a + 4, b);
}

TEST_F(Allocate, ReallocMemoryResourceBackedMove) {
    linear_memory_resource<ReallocNullMemoryResource> memory(8, ReallocNullMemoryResource());
    auto a = reinterpret_cast<uintptr_t>(memory.allocate(4, 4));
    linear_memory_resource<ReallocNullMemoryResource> moved_memory = std::move(memory);
    auto b = reinterpret_cast<uintptr_t>(moved_memory.allocate(4, 4));
    EXPECT_EQ(a + 4, b);
}

std::byte g_mem;

TEST_F(Allocate, AllocatorBackedPmrMove) {
    pmr_linear_memory_resource<ConstAllocator<&g_mem>> memory(12);
    auto a = reinterpret_cast<uintptr_t>(memory.allocate(4, 4));
    pmr_linear_memory_resource<ConstAllocator<&g_mem>> moved_constructed_memory(std::move(memory));
    auto b = reinterpret_cast<uintptr_t>(moved_constructed_memory.allocate(4, 4));
    ConstAllocator<&g_mem>::allocated =
        false; // workaround for test not actually supporting multiple allocations
    pmr_linear_memory_resource<ConstAllocator<&g_mem>> moved_assigned_memory(1);
    moved_assigned_memory = std::move(moved_constructed_memory);
    ConstAllocator<&g_mem>::allocated =
        true; // workaround for test not actually supporting multiple allocations
    auto c = reinterpret_cast<uintptr_t>(moved_assigned_memory.allocate(4, 4));
    EXPECT_EQ(a + 4, b);
    EXPECT_EQ(a + 8, c);
    EXPECT_EQ(moved_assigned_memory.size(), 12);
}

TEST_F(Allocate, MemoryResourceBackedPmrMove) {
    pmr_linear_memory_resource<ConstMemoryResource<&g_mem>> memory(12,
                                                                   ConstMemoryResource<&g_mem>());
    auto a = reinterpret_cast<uintptr_t>(memory.allocate(4, 4));
    pmr_linear_memory_resource<ConstMemoryResource<&g_mem>> moved_constructed_memory(
        std::move(memory));
    auto b = reinterpret_cast<uintptr_t>(moved_constructed_memory.allocate(4, 4));
    pmr_linear_memory_resource<ConstMemoryResource<&g_mem>> moved_assigned_memory(
        1, ConstMemoryResource<&g_mem>());
    moved_assigned_memory = std::move(moved_constructed_memory);
    auto c = reinterpret_cast<uintptr_t>(moved_assigned_memory.allocate(4, 4));
    EXPECT_EQ(a + 4, b);
    EXPECT_EQ(a + 8, c);
    EXPECT_EQ(moved_assigned_memory.size(), 12);
}

TEST_F(Allocate, ReallocAllocatorBackedPmrMove) {
    pmr_linear_memory_resource<ReallocConstAllocator<&g_mem>> memory(8);
    auto a = reinterpret_cast<uintptr_t>(memory.allocate(4, 4));
    pmr_linear_memory_resource<ReallocConstAllocator<&g_mem>> moved_memory = std::move(memory);
    auto b = reinterpret_cast<uintptr_t>(moved_memory.allocate(4, 4));
    EXPECT_EQ(a + 4, b);
}

TEST_F(Allocate, ReallocMemoryResourceBackedPmrMove) {
    pmr_linear_memory_resource<ReallocConstMemoryResource<&g_mem>> memory{
        8, ReallocConstMemoryResource<&g_mem>()};
    auto a = reinterpret_cast<uintptr_t>(memory.allocate(4, 4));
    pmr_linear_memory_resource<ReallocConstMemoryResource<&g_mem>> moved_memory = std::move(memory);
    auto b = reinterpret_cast<uintptr_t>(moved_memory.allocate(4, 4));
    EXPECT_EQ(a + 4, b);
}

TEST_F(Allocate, ConstructReallocAllocatorDefault) {
    linear_memory_resource<ReallocConstAllocator<&g_mem>> memory;
    EXPECT_EQ(memory.size(), 0);
    EXPECT_EQ(memory.capacity(), 0);
    auto a = reinterpret_cast<uintptr_t>(memory.allocate(4, 4));
    EXPECT_EQ(memory.size(), 4);
    EXPECT_EQ(memory.capacity(), 4);
    EXPECT_EQ(a, reinterpret_cast<uintptr_t>(&g_mem));
}

TEST_F(Allocate, ConstructReallocAllocator) {
    linear_memory_resource<ReallocConstAllocator<&g_mem>> memory{ReallocConstAllocator<&g_mem>()};
    EXPECT_EQ(memory.size(), 0);
    EXPECT_EQ(memory.capacity(), 0);
    auto a = reinterpret_cast<uintptr_t>(memory.allocate(4, 4));
    EXPECT_EQ(memory.size(), 4);
    EXPECT_EQ(memory.capacity(), 4);
    EXPECT_EQ(a, reinterpret_cast<uintptr_t>(&g_mem));
}

TEST_F(Allocate, ConstructReallocMemoryResource) {
    linear_memory_resource<ReallocConstMemoryResource<&g_mem>> memory{
        ReallocConstMemoryResource<&g_mem>()};
    EXPECT_EQ(memory.size(), 0);
    EXPECT_EQ(memory.capacity(), 0);
    auto a = reinterpret_cast<uintptr_t>(memory.allocate(4, 4));
    EXPECT_EQ(memory.size(), 4);
    EXPECT_EQ(memory.capacity(), 4);
    EXPECT_EQ(a, reinterpret_cast<uintptr_t>(&g_mem));
}

TEST_F(Allocate, ConstructAllocatorPmr) {
    pmr_linear_memory_resource<ConstAllocator<&g_mem>> memory(4, ConstAllocator<&g_mem>());
    EXPECT_EQ(memory.size(), 0);
    EXPECT_EQ(memory.capacity(), 4);
    auto a = reinterpret_cast<uintptr_t>(memory.allocate(4, 4));
    EXPECT_EQ(memory.size(), 4);
    EXPECT_EQ(memory.capacity(), 4);
    EXPECT_EQ(a, reinterpret_cast<uintptr_t>(&g_mem));
}

TEST_F(Allocate, ConstructMemoryResourcePmr) {
    pmr_linear_memory_resource<ConstMemoryResource<&g_mem>> memory(4,
                                                                   ConstMemoryResource<&g_mem>());
    EXPECT_EQ(memory.size(), 0);
    EXPECT_EQ(memory.capacity(), 4);
    auto a = reinterpret_cast<uintptr_t>(memory.allocate(4, 4));
    EXPECT_EQ(memory.size(), 4);
    EXPECT_EQ(memory.capacity(), 4);
    EXPECT_EQ(a, reinterpret_cast<uintptr_t>(&g_mem));
}

TEST_F(Allocate, ConstructReallocAllocatorDefaultPmr) {
    pmr_linear_memory_resource<ReallocConstAllocator<&g_mem>> memory;
    EXPECT_EQ(memory.size(), 0);
    EXPECT_EQ(memory.capacity(), 0);
    auto a = reinterpret_cast<uintptr_t>(memory.allocate(4, 4));
    EXPECT_EQ(memory.size(), 4);
    EXPECT_EQ(memory.capacity(), 4);
    EXPECT_EQ(a, reinterpret_cast<uintptr_t>(&g_mem));
}

TEST_F(Allocate, ConstructReallocAllocatorPmr) {
    pmr_linear_memory_resource<ReallocConstAllocator<&g_mem>> memory{
        ReallocConstAllocator<&g_mem>()};
    EXPECT_EQ(memory.size(), 0);
    EXPECT_EQ(memory.capacity(), 0);
    auto a = reinterpret_cast<uintptr_t>(memory.allocate(4, 4));
    EXPECT_EQ(memory.size(), 4);
    EXPECT_EQ(memory.capacity(), 4);
    EXPECT_EQ(a, reinterpret_cast<uintptr_t>(&g_mem));
}

TEST_F(Allocate, ConstructReallocMemoryResourcePmr) {
    pmr_linear_memory_resource<ReallocConstMemoryResource<&g_mem>> memory{
        ReallocConstMemoryResource<&g_mem>()};
    EXPECT_EQ(memory.size(), 0);
    EXPECT_EQ(memory.capacity(), 0);
    auto a = reinterpret_cast<uintptr_t>(memory.allocate(4, 4));
    EXPECT_EQ(memory.size(), 4);
    EXPECT_EQ(memory.capacity(), 4);
    EXPECT_EQ(a, reinterpret_cast<uintptr_t>(&g_mem));
}

struct int2 {
    int2() = default;
    int2(int x_, int y_)
        : x(x_)
        , y(y_) {}
    bool operator==(const int2& other) const { return x == other.x && y == other.y; }
    int  x = 123;
    int  y = 123;
};

TEST(Construct, MemoryResource) {
    linear_memory_resource<std::allocator<std::byte>> memory(10000);
    EXPECT_EQ(*decodeless::create::object<int>(memory), 0);
    EXPECT_EQ(*decodeless::create::object<int>(memory, 42), 42);
    EXPECT_EQ(*decodeless::create::object<int2>(memory), int2(123, 123));
    EXPECT_EQ(*decodeless::create::object<int2>(memory, 42, 42), int2(42, 42));
    EXPECT_EQ(*decodeless::create::object<int2>(memory, int2(42, 42)), int2(42, 42));
}

TEST(Construct, Allocator) {
    linear_memory_resource<std::allocator<std::byte>>      memory(10000);
    linear_allocator<std::byte, std::allocator<std::byte>> allocator(memory);
    EXPECT_EQ(*decodeless::create::object<int>(allocator), 0);
    EXPECT_EQ(*decodeless::create::object<int>(allocator, 42), 42);
    EXPECT_EQ(*decodeless::create::object<int2>(allocator), int2(123, 123));
    EXPECT_EQ(*decodeless::create::object<int2>(allocator, 42, 42), int2(42, 42));
    EXPECT_EQ(*decodeless::create::object<int2>(allocator, int2(42, 42)), int2(42, 42));
}
