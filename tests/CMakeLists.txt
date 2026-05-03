function(add_tiny_kv_test name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE tiny_kv)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

add_tiny_kv_test(test_kv_store)
add_tiny_kv_test(test_sstable)
add_tiny_kv_test(test_thread_pool)
