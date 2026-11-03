cmake_minimum_required(VERSION 4.2)
project(hw1)

set(CMAKE_CXX_STANDARD 20)

find_package(CURL CONFIG REQUIRED)
find_package(Boost CONFIG REQUIRED)

add_executable(hedgedcurl hedgedcurl.cpp)

target_link_libraries(hedgedcurl PRIVATE CURL::libcurl)
target_include_directories(hedgedcurl PRIVATE "D:/cpp_libs/boost_1_92_0")
target_link_libraries(hedgedcurl PRIVATE ws2_32 mswsock)