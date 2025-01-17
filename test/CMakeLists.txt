# Copyright (c) 2024-2025 Pyarelal Knowles, MIT License

cmake_minimum_required(VERSION 3.20)

find_package(GTest QUIET)
if(NOT GTest_FOUND)
  include(FetchContent)
  FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
    GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(googletest)
endif()

# Unit tests
add_executable(${PROJECT_NAME}_tests src/allocator.cpp)
target_link_libraries(
  ${PROJECT_NAME}_tests
  decodeless::allocator
  gtest_main
  gmock_main)

if(MSVC)
  target_compile_options(${PROJECT_NAME}_tests PRIVATE /W4 /WX)
  target_compile_definitions(${PROJECT_NAME}_tests PRIVATE WIN32_LEAN_AND_MEAN=1 NOMINMAX)
else()
  target_compile_options(${PROJECT_NAME}_tests PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Werror
    -ftemplate-backtrace-limit=0
    # Note: some of these are ABI breaking and are only safe here due to
    # all dependencies being header-only
    # https://gcc.gnu.org/onlinedocs/libstdc++/manual/using_macros.html
    $<$<CONFIG:Debug>:-D_GLIBCXX_ASSERTIONS>
    $<$<CONFIG:Debug>:-D_GLIBCXX_DEBUG>
    $<$<CONFIG:Debug>:-D_GLIBCXX_DEBUG_PEDANTIC>
    $<$<CONFIG:Debug>:-D_GLIBCXX_CONCEPT_CHECKS>
    $<$<CONFIG:Debug>:-D_GLIBCXX_SANITIZE_VECTOR>
    )
  option(DECODELESS_TESTING_ENABLE_ASAN "Compile decodeless tests with sanitizers" ON)
  if(DECODELESS_TESTING_ENABLE_ASAN)
    target_compile_options(${PROJECT_NAME}_tests PRIVATE
      $<$<CONFIG:Debug>:-fsanitize=address,undefined,leak>)
    target_link_options(${PROJECT_NAME}_tests PRIVATE
      $<$<CONFIG:Debug>:-fsanitize=address,undefined,leak>)
  endif()
  if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    # Ugly detection for a working _GLIBCXX_DEBUG_BACKTRACE config, but the
    # feature itself is useful
    include(glibcxx_debug_backtrace.cmake)
    if(GLIBCXX_DEBUG_BACKTRACE_SUPPORTED)
      target_compile_definitions(
        ${PROJECT_NAME}_tests
        PRIVATE $<$<CONFIG:Debug>:_GLIBCXX_DEBUG_BACKTRACE>)
      target_link_libraries(${PROJECT_NAME}_tests
                            $<$<CONFIG:Debug>:${GLIBCXX_DEBUG_BACKTRACE_LIBRARY}>)
      target_compile_features(${PROJECT_NAME}_tests
                              PRIVATE ${GLIBCXX_DEBUG_BACKTRACE_CXX_FEATURE})
    endif()
  endif()
endif()

include(GoogleTest)
gtest_discover_tests(${PROJECT_NAME}_tests)
