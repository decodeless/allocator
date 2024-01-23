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

## Usage

This is a header only library with no dependencies other than C++20. A
convenient and tested way to use the library is with cmake's `FetchContent`:

```
include(FetchContent)
FetchContent_Declare(
    decodeless_allocator
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(decodeless_allocator)

target_link_libraries(myproject PRIVATE decodeless::allocator)
```

## Code Example

```
// could also be decodeless::mapped_file_allocator<std::byte> from
// decodeless_writer
using parent_allocator = std::allocator<std::byte>;
decodeless::linear_memory_resource<parent_allocator> memory(1024);

std::span<int> array =
    decodeless::createArray<std::initializer_list<int>>(memory, {1, 3, 6, 10, 15});
EXPECT_EQ(array.size(), 5);
EXPECT_EQ(array[4], 15);
EXPECT_EQ(memory.bytesAllocated(), sizeof(int) * 5);

double* alignedDouble = decodeless::create<double>(memory, 42.0);
EXPECT_EQ(*alignedDouble, 42.0);
EXPECT_EQ(memory.bytesAllocated(), sizeof(int) * 5 + sizeof(double) + 4);
```

## Contributing

Issues and pull requests are most welcome, thank you! Note the
[DCO](CONTRIBUTING) and MIT [LICENSE](LICENSE).
