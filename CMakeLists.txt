cmake_minimum_required(VERSION 3.24)
project(ThreadPool)

set(CMAKE_CXX_STANDARD 11)

add_executable(ThreadPool main.cpp ThreadPool/ThreadPool.hpp ThreadPool/ThreadPool.cpp)
