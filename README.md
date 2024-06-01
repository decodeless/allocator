# decodeless_allocator

[`decodeless`](https://github.com/decodeless) (previously no-decode) is a
collection of utility libraries for conveniently reading and writing files via
memory mapping. Components can be used individually or combined.

`decodeless_allocator` is a possibly-growable local linear arena allocator.
- growable: The backing allocation may grow if it has reallocate() and the
  call returns the same address.
- local: Has per-allocator-instance state
- linear: Gives monotonic/sequential but aligned allocations that cannot be
  freed or reused. There is only a reset() call. Only trivially destructible
  objects should be created from this.
- arena: Allocations come from a single blob/pool and when it is exhausted
  std::bad_alloc is thrown (unless a reallocate() is possible).
- STL compatible, although this probably isn't useful as it's a linear
  allocator.

But why? I want to write C++ structures to a file and read them back without
decoding or deserializing byte by byte because that's slow. If I read it back
into memory I must take care of alignment. Allocators do that already and a
linear allocator would tightly pack the data for a file. In fact, allocators can
be chained, so the data it allocates could come directly from a memory mapped
file. That is exactly what
[`decodeless_writer`](https://github.com/decodeless/writer) does, using this
library internally.

Note: objects created by the allocator must be trivially destructible because no
destructor is ever called. This is by design because it is expected objects are
written to disk.

For allocator type erasure,
[`decodeless/pmr_allocator.hpp`](include/decodeless/pmr_allocator.hpp)
implements
[`std::pmr::memory_resource`](https://en.cppreference.com/w/cpp/memory/memory_resource).
This allows non-templated code to allocate from different allocator types (e.g.
via `std::pmr::polymorphic_allocator`) and without including the allocator's
header. Note that decodeless_allocator requires trivially destructible types but
`std::pmr::polymorphic_allocator` hides this validation otherwise enforced by
`decodeless::linear_allocator`.

This library includes utility functions `decodeless::create::object()` and
`decodeless::create::array()` to construct objects from an allocator or memory
resource.

## Example

```
// could also be decodeless::mapped_file_allocator<std::byte> from
// decodeless_writer
using parent_allocator = std::allocator<std::byte>;
decodeless::linear_memory_resource<parent_allocator> memory(1024);

std::span<int> array = decodeless::create::array<int>(memory, {1, 3, 6, 10, 15});
EXPECT_EQ(array.size(), 5);
EXPECT_EQ(array[4], 15);
EXPECT_EQ(memory.size(), sizeof(int) * 5);

double* alignedDouble = decodeless::create::object(memory, 42.0);
EXPECT_EQ(*alignedDouble, 42.0);
EXPECT_EQ(memory.size(), sizeof(int) * 5 + sizeof(double) + 4);
```

Using the polymorphic allocator:

```
decodeless::pmr_linear_memory_resource     res(100);
std::pmr::polymorphic_allocator<std::byte> alloc(&res); // interface abstraction
std::span<uint8_t> bytes = decodeless::create::array<uint8_t>(alloc, 10);
EXPECT_EQ(bytes.size(), 10);
EXPECT_EQ(res.size(), 10);
```

## Dependencies

None. Just needs C++20.

## Cmake Integration

This is a header only library with no dependencies other than C++20. A
convenient and tested way to use the library is with cmake's `FetchContent`:

```
include(FetchContent)
FetchContent_Declare(
    decodeless_allocator
    GIT_REPOSITORY https://github.com/decodeless/allocator.git
    GIT_TAG release_tag
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(decodeless_allocator)

target_link_libraries(myproject PRIVATE decodeless::allocator)
```

If using in a library, a config file is provided for
`find_package(decodeless_allocator REQUIRED CONFIG PATHS ...)`, which trivially
includes CMakeLists.txt. See
[decodeless_writer](https://github.com/decodeless/writer/blob/main/CMakeLists.txt)
for an example.

## `memory_resource` and `allocator`

A memory resource is the object that actually owns the memory being allocated.
The allocator is a copyable pointer to the memory resource. This separation
comes directly from consistency with STL allocators. Sometimes the allocator
indirection is needed and sometimes inlining allocation code in the memory
resource improves performance, e.g. when chaining allocators. For typical
`decodeless` use cases, `std::pmr::polymorphic_allocator` is likely preferable.
For convenience, `decodeless::memory_resource` and `decodeless::allocator` are
actually C++
[`concepts`](https://en.cppreference.com/w/cpp/language/constraints).

## Contributing

Issues and pull requests are most welcome, thank you! Note the
[DCO](CONTRIBUTING) and MIT [LICENSE](LICENSE).
