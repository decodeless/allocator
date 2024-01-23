# Copyright (c) 2024 Pyarelal Knowles, MIT License

cmake_minimum_required(VERSION 3.20)

project(decodeless_allocator)

set(CMAKE_CXX_STANDARD 20)

add_library(decodeless_allocator INTERFACE)
target_include_directories(decodeless_allocator INTERFACE include)

# cmake namespace style alias
add_library(decodeless::allocator ALIAS decodeless_allocator)

if(BUILD_TESTING)
  option(BUILD_DECODELESS_TESTING "Enable decodeless testing" ON)
  if(BUILD_DECODELESS_TESTING)
    enable_testing()
    add_subdirectory(test)
  endif()
endif()
