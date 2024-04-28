// Copyright (c) 2024 Pyarelal Knowles, MIT License

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <decodeless/allocator.hpp>
#include <decodeless/pmr_allocator.hpp>
#include <gtest/gtest.h>
#include <initializer_list>

using namespace decodeless;

struct NullAllocator {
    using value_type = std::byte;
    value_type* allocate(std::size_t n) {
        EXPECT_FALSE(allocated);
        allocated = true;
        (void)n;
        return nullptr;
    }
    void deallocate(value_type* p, std::size_t n) noexcept {
        EXPECT_TRUE(allocated);
        (void)p;
        (void)n;
    }
    bool allocated = false;
};

TEST(Allocate, Object) {
    linear_memory_resource<NullAllocator> memory(23);

    // byte can be placed anywhere
    EXPECT_EQ(memory.allocate(sizeof(char), alignof(char)), reinterpret_cast<void*>(0));
    EXPECT_EQ(memory.bytesAllocated(), 1);

    // int after the byte must have 3 bytes padding, placed at 4 and taking 4
    EXPECT_EQ(memory.allocate(sizeof(int), alignof(int)), reinterpret_cast<void*>(4));
    EXPECT_EQ(memory.bytesAllocated(), 8);

    // double after int must have 4 bytes padding, placed at 8, taking 8 more
    EXPECT_EQ(memory.allocate(sizeof(double), alignof(double)), reinterpret_cast<void*>(8));
    EXPECT_EQ(memory.bytesAllocated(), 16);

    // another byte to force some padding, together with another int won't fit
    EXPECT_EQ(memory.bytesReserved() - memory.bytesAllocated(), 7);
    EXPECT_EQ(memory.allocate(sizeof(char), alignof(char)), reinterpret_cast<void*>(16));
    EXPECT_EQ(memory.bytesReserved() - memory.bytesAllocated(),
              6); // plenty left for an int, but not aligned
    EXPECT_THROW((void)memory.allocate(sizeof(int), alignof(int)), std::bad_alloc);
}

TEST(Allocate, Array) {
    linear_memory_resource<NullAllocator> memory(32);

    // byte can be placed anywhere
    EXPECT_EQ(memory.allocate(sizeof(char) * 3, alignof(char)), reinterpret_cast<char*>(0));
    EXPECT_EQ(memory.bytesAllocated(), 3);

    // 2 ints after the 3rd byte must have 3 bytes padding, placed at 4 and taking 8
    EXPECT_EQ(memory.allocate(sizeof(int) * 2, alignof(int)), reinterpret_cast<int*>(4));
    EXPECT_EQ(memory.bytesAllocated(), 12);

    // 2 doubles after 12 bytes must have 4 bytes padding, placed at 16, taking 16 more
    EXPECT_EQ(memory.allocate(sizeof(double) * 2, alignof(double)), reinterpret_cast<double*>(16));
    EXPECT_EQ(memory.bytesAllocated(), 32);
}

TEST(Allocate, Initialize) {
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

// Relaxed test case for MSVC where the debug vector allocates extra crap
TEST(Allocate, VectorRelaxed) {
    linear_memory_resource alloc(100);
    EXPECT_EQ(alloc.bytesAllocated(), 0);
    EXPECT_EQ(alloc.bytesReserved(), 100);
    std::vector<uint8_t, linear_allocator<uint8_t>> vec(10, alloc);
    EXPECT_GE(alloc.bytesAllocated(), 10);
    auto allocated = alloc.bytesAllocated();
    vec.reserve(20);
    EXPECT_GT(alloc.bytesAllocated(), allocated);
    EXPECT_THROW(vec.reserve(100), std::bad_alloc);
}

TEST(Allocate, Vector) {
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
    EXPECT_EQ(alloc.bytesAllocated(), 0);
    EXPECT_EQ(alloc.bytesReserved(), 30);

    // Yes, this is possible but don't do it. std::vector can easily reallocate
    // which will leave unused holes in the linear allocator.
    std::vector<uint8_t, linear_allocator<uint8_t>> vec(10, alloc);
    EXPECT_EQ(alloc.bytesAllocated(), 10);
    vec.reserve(20);
    EXPECT_EQ(alloc.bytesAllocated(), 30);
    EXPECT_THROW(vec.reserve(21), std::bad_alloc);
}

TEST(Allocate, PmrAllocator) {
    pmr_linear_memory_resource<std::allocator<std::byte>> res(100);
    EXPECT_EQ(res.bytesAllocated(), 0);
    EXPECT_EQ(res.bytesReserved(), 100);
    std::pmr::polymorphic_allocator<std::byte> alloc(&res);
    std::span<uint8_t>                         bytes = create::array<uint8_t>(alloc, 10);
    EXPECT_EQ(bytes.size(), 10);
    EXPECT_EQ(res.bytesAllocated(), 10);
}

TEST(Allocate, PmrVectorRelaxed) {
    pmr_linear_memory_resource<std::allocator<std::byte>> res(100);
    EXPECT_EQ(res.bytesAllocated(), 0);
    EXPECT_EQ(res.bytesReserved(), 100);
    std::pmr::vector<uint8_t> vec(10, &res);
    EXPECT_GE(res.bytesAllocated(), 10);
    auto allocated = res.bytesAllocated();
    vec.reserve(20);
    EXPECT_GT(res.bytesAllocated(), allocated);
    EXPECT_THROW(vec.reserve(100), std::bad_alloc);
}

TEST(Allocate, Readme) {
    // could also be decodeless::mapped_file_allocator<std::byte> from
    // decodeless_writer
    using parent_allocator = std::allocator<std::byte>;
    decodeless::linear_memory_resource<parent_allocator> memory(1024);

    std::span<int> array = decodeless::create::array<int>(memory, {(int)1, 3, 6, 10, 15});
    EXPECT_EQ(array.size(), 5);
    EXPECT_EQ(array[4], 15);
    EXPECT_EQ(memory.bytesAllocated(), sizeof(int) * 5);

    double* alignedDouble = decodeless::create::object(memory, 42.0);
    EXPECT_EQ(*alignedDouble, 42.0);
    EXPECT_EQ(memory.bytesAllocated(), sizeof(int) * 5 + sizeof(double) + 4);

    decodeless::pmr_linear_memory_resource     res(100);
    std::pmr::polymorphic_allocator<std::byte> alloc(&res); // interface abstraction
    std::span<uint8_t> bytes = decodeless::create::array<uint8_t>(alloc, 10);
    EXPECT_EQ(bytes.size(), 10);
    EXPECT_EQ(res.bytesAllocated(), 10);
}

TEST(Allocate, References) {
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
    EXPECT_EQ(res.bytesAllocated(), sizeof(int) * 6);

    std::vector init{42};
    { [[maybe_unused]] std::span<int> r = create::array(res, init); }
    { [[maybe_unused]] std::span<int> r = create::array(res, init); }
    { [[maybe_unused]] std::span<int> r = create::array(alloc, init); }
    { [[maybe_unused]] std::span<int> r = create::array(alloc_c, init); }
    { [[maybe_unused]] std::span<int> r = create::array(alloc_r, init); }
    { [[maybe_unused]] std::span<int> r = create::array(alloc_cr, init); }
    EXPECT_EQ(res.bytesAllocated(), sizeof(int) * 12);
}
