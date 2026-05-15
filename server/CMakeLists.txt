add_executable(server server.cpp)
add_executable(epoll_server epoll_server.cpp)
target_include_directories(server PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(server PRIVATE tiny_kv)
