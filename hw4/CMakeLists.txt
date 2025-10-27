cmake_minimum_required(VERSION 3.10)
project(ext2-reader CXX)

set(CMAKE_CXX_STANDARD 17)

add_executable(
    src/ext2_reader.cpp
    src/main.cpp
)