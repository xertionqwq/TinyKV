cmake_minimum_required(VERSION 3.15)
project(Tiny_KV VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -g -fsanitize=address")
endif()

include(FetchContent)
FetchContent_Declare(
  my_stl
  SOURCE_DIR /home/xertion/Code/My_STL
)
FetchContent_GetProperties(my_stl)
if(NOT my_stl_POPULATED)
  FetchContent_Populate(my_stl)
  add_subdirectory(${my_stl_SOURCE_DIR} ${my_stl_BINARY_DIR})
endif()

add_library(tiny_kv STATIC
    src/kv_store.cpp
    src/wal.cpp
    src/sstable_builder.cpp
    src/sstable_reader.cpp
    src/thread_pool.cpp
    src/db.cpp
)

target_include_directories(tiny_kv
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(tiny_kv
    PUBLIC
        my_stl
)

option(TINY_KV_BUILD_TESTS "Build tests" ON)
if(TINY_KV_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

option(TINY_KV_BUILD_BENCH "Build benchmarks" OFF)
if(TINY_KV_BUILD_BENCH)
    add_subdirectory(bench)
endif()
