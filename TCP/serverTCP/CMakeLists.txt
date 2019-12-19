cmake_minimum_required(VERSION 3.12)
project(serverTCP)
set(CMAKE_CXX_STANDARD 14)
set(SOURCE_FILES main.cpp)
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${GCC_COVERAGE_LINK_FLAGS} -static")
add_executable(
        serverTCP
        main.cpp
)
target_link_libraries( serverTCP pthread )