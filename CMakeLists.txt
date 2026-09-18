cmake_minimum_required(VERSION 3.10)
project(lsignal_test VERSION 0.1.0)

find_package(Threads REQUIRED)

add_executable(${CMAKE_PROJECT_NAME}
    tests/tests.cpp
	tests/test_basic.cpp
	tests/test_api.cpp
	tests/test_lifetime.cpp
	tests/test_multithread.cpp
    )

set_property(TARGET ${CMAKE_PROJECT_NAME} PROPERTY CXX_STANDARD 20)

target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Threads::Threads)

set(SANITIZER "address" CACHE STRING "Sanitizer to build with: none, address, thread, memory")
set_property(CACHE SANITIZER PROPERTY STRINGS none address thread memory)

if(SANITIZER STREQUAL "address")
    # ASan+UBSan work correctly with the system libstdc++/libc.
    set(SANITIZE_ARG -fsanitize=address,undefined -fno-omit-frame-pointer)
elseif(SANITIZER STREQUAL "thread")
    set(SANITIZE_ARG -fsanitize=thread -fno-omit-frame-pointer)
elseif(SANITIZER STREQUAL "memory")
    # MemorySanitizer is only implemented by Clang.
    if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        message(FATAL_ERROR "SANITIZER=memory requires Clang. Reconfigure with -DCMAKE_CXX_COMPILER=clang++")
    endif()
    # MemorySanitizer needs every linked library (including libstdc++) built
    # with MSan instrumentation, otherwise it reports false positives/crashes.
    # See bugs.md.
    set(SANITIZE_ARG -fsanitize=memory -fno-omit-frame-pointer)
elseif(SANITIZER STREQUAL "none")
    set(SANITIZE_ARG "")
else()
    message(FATAL_ERROR "Unknown SANITIZER value: ${SANITIZER} (expected none, address, thread or memory)")
endif()

target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE -Wall -Wextra -g ${SANITIZE_ARG})
target_link_options(${CMAKE_PROJECT_NAME} PRIVATE ${SANITIZE_ARG})