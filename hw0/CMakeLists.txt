cmake_minimum_required(VERSION 4.2)
project(backend_start_hw0)

set(CMAKE_CXX_STANDARD 20)

add_executable(server Server.cpp)
add_executable(client Client.cpp)

target_link_libraries(server PRIVATE ws2_32)
target_link_libraries(client PRIVATE ws2_32)