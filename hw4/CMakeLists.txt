cmake_minimum_required(VERSION 3.10)
project(ext2-reader CXX)

set(CMAKE_CXX_STANDARD 17)

set(APP_NAME ext2-reader)

add_executable(${APP_NAME} 
    src/ext2_reader.cpp
    src/main.cpp
)