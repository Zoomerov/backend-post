cmake_minimum_required(VERSION 4.2)
project(hw2)

set(CMAKE_CXX_STANDARD 23)

include(FetchContent)

FetchContent_Declare(
        bcrypt_lib
        GIT_REPOSITORY https://github.com/hilch/Bcrypt.cpp.git
        GIT_TAG master
)



FetchContent_MakeAvailable(bcrypt_lib)

find_package(CURL CONFIG REQUIRED)
find_package(redis++ CONFIG REQUIRED)
find_package(jwt-cpp CONFIG REQUIRED)
find_package(httplib CONFIG REQUIRED)

add_executable(hw2 main.cpp)

set_property(TARGET bcrypt PROPERTY
        INTERFACE_INCLUDE_DIRECTORIES "${bcrypt_lib_SOURCE_DIR}"
)

target_compile_definitions(hw2 PRIVATE
        _WIN32_WINNT=0x0A00
        WINVER=0x0A00
)

target_link_libraries(hw2 PRIVATE CURL::libcurl)
target_link_libraries(hw2 PRIVATE redis++::redis++)
target_link_libraries(hw2 PRIVATE jwt-cpp::jwt-cpp)
target_link_libraries(hw2 PRIVATE bcrypt)
target_link_libraries(hw2 PRIVATE httplib::httplib)
