cmake_minimum_required(VERSION 3.10)
project(BTree C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

set(BTREE_LIB b_tree)

add_library(${BTREE_LIB} STATIC
    src/b_tree.c
)

add_executable(demo 
    src/demo.c
)

target_link_libraries(demo 
    ${BTREE_LIB}
)

add_subdirectory(test)