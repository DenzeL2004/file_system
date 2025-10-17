cmake_minimum_required(VERSION 3.10)
project(FAT16-Reader C)

set(CMAKE_C_STANDARD 11)

set(APP_NAME fat16_reader)

add_executable(${APP_NAME} 
    src/fat16_reader.c
)