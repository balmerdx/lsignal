cmake_minimum_required(VERSION 3.0.0)
project(lsignal_test VERSION 0.1.0)

add_executable(${CMAKE_PROJECT_NAME}
    tests/tests.cpp
	tests/test_basic.cpp
	tests/test_multithread.cpp
	lsignal.cpp
    )

set_property(TARGET ${CMAKE_PROJECT_NAME} PROPERTY CXX_STANDARD 20)
target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE -Wall -Wextra)