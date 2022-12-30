cmake_minimum_required(VERSION 3.24)
project(lab8 C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -pthread")

add_executable(lab8 main3.c)
