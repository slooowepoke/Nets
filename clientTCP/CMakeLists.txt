cmake_minimum_required(VERSION 3.14)
project(clientTCP C)

set(CMAKE_C_STANDARD 99)
set(SOURCE_FILES main.c)
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${GCC_COVERAGE_LINK_FLAGS} -static")
add_executable(clientTCP main.c)