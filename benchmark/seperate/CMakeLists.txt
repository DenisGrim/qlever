cmake_minimum_required(VERSION 3.16)
project(Benchmark)

set(CMAKE_BUILD_TYPE Release)
add_subdirectory(./ips4o)

# On systems where pthread symbols live directly in libc (glibc >= 2.34),
# CMake's FindThreads no longer adds -pthread, so ips4o's _REENTRANT guard
# never enables its `parallel` namespace. Force the flag explicitly.
target_compile_options(ips4o INTERFACE -pthread)

set(BENCHMARK_MODES
    "benchmark_row:ROW_MODE"
    "benchmark_col_normalsort:COL_MODE_NORMAL_SORT"
    "benchmark_col_permsort:COL_MODE_PERM_SORT"
)

# for every mode, makes one '{mode}_ips4o' which uses ips4o parallel sort
foreach(entry IN LISTS BENCHMARK_MODES)
    string(REPLACE ":" ";" pair "${entry}")
    list(GET pair 0 name)
    list(GET pair 1 def)

    add_executable(${name} benchmark.cpp)
    target_compile_definitions(${name} PRIVATE ${def})
    target_link_libraries(${name} PRIVATE ips4o)

    add_executable(${name}_ips4o benchmark.cpp)
    target_compile_definitions(${name}_ips4o PRIVATE ${def} IPS4O)
    target_link_libraries(${name}_ips4o PRIVATE ips4o)
endforeach()
