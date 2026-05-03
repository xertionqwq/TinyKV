add_executable(benchmark benchmark.cpp)
target_link_libraries(benchmark PRIVATE tiny_kv)

add_executable(bench_ds bench_ds.cpp)
target_link_libraries(bench_ds PRIVATE tiny_kv)
