
find_package(GTest REQUIRED)

set(BTREE_TESTS b_tree_test)

add_executable(${BTREE_TESTS} 
    test.cpp
)

target_link_libraries(${BTREE_TESTS}  
    b_tree 
    GTest::GTest 
    GTest::Main
)

target_include_directories(${BTREE_TESTS} PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)