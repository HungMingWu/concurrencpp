cmake_minimum_required(VERSION 3.0)
project(async_file_processing)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_COMPILER clang++-6.0)

set(CLANG_COROUTINES_COMPILE_FLAGS "-Wall -Wextra -std=gnu++2a -fcoroutines-ts -stdlib=libc++")

set(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} ${CLANG_COROUTINES_COMPILE_FLAGS}")
#set(CMAKE_BUILD_TYPE Release)
#set(CMAKE_BUILD_TYPE RELWITHDEBINFO)

include_directories(../../concurrencpp/include)
link_directories(../../build/concurrencpp)

add_executable(
    async_file_processing
    src/main.cpp
)
target_link_libraries(async_file_processing concurrencpp pthread rt)
