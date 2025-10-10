cmake_minimum_required(VERSION 3.10)
project(BTree C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

add_executable(mylsof 
    src/lsof.c
)
