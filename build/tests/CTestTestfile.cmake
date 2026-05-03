# CMake generated Testfile for 
# Source directory: /home/xertion/Code/Tiny_KV/tests
# Build directory: /home/xertion/Code/Tiny_KV/build/tests
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(test_kv_store "/home/xertion/Code/Tiny_KV/build/tests/test_kv_store")
set_tests_properties(test_kv_store PROPERTIES  _BACKTRACE_TRIPLES "/home/xertion/Code/Tiny_KV/tests/CMakeLists.txt;4;add_test;/home/xertion/Code/Tiny_KV/tests/CMakeLists.txt;7;add_tiny_kv_test;/home/xertion/Code/Tiny_KV/tests/CMakeLists.txt;0;")
add_test(test_sstable "/home/xertion/Code/Tiny_KV/build/tests/test_sstable")
set_tests_properties(test_sstable PROPERTIES  _BACKTRACE_TRIPLES "/home/xertion/Code/Tiny_KV/tests/CMakeLists.txt;4;add_test;/home/xertion/Code/Tiny_KV/tests/CMakeLists.txt;8;add_tiny_kv_test;/home/xertion/Code/Tiny_KV/tests/CMakeLists.txt;0;")
add_test(test_thread_pool "/home/xertion/Code/Tiny_KV/build/tests/test_thread_pool")
set_tests_properties(test_thread_pool PROPERTIES  _BACKTRACE_TRIPLES "/home/xertion/Code/Tiny_KV/tests/CMakeLists.txt;4;add_test;/home/xertion/Code/Tiny_KV/tests/CMakeLists.txt;9;add_tiny_kv_test;/home/xertion/Code/Tiny_KV/tests/CMakeLists.txt;0;")
