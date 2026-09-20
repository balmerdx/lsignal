cmake_minimum_required(VERSION 3.10)
project(lsignal_test VERSION 0.1.0)

find_package(Threads REQUIRED)

set(TEST_SOURCES
    tests/tests.cpp
    tests/test_basic.cpp
    tests/test_api.cpp
    tests/test_lifetime.cpp
    tests/test_multithread.cpp
    )

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

# Shared setup for both test executables below - same test sources, same
# sanitizer/warning flags, only the lsignal variant they're built against differs.
function(lsignal_configure_test_target target_name)
    set_property(TARGET ${target_name} PROPERTY CXX_STANDARD 20)
    target_link_libraries(${target_name} PRIVATE Threads::Threads)
    target_compile_options(${target_name} PRIVATE -Wall -Wextra -g ${SANITIZE_ARG})
    target_link_options(${target_name} PRIVATE ${SANITIZE_ARG})
endfunction()

# lsignal_with_cpp: lsignal.h + lsignal.cpp (the default, actively developed
# distribution form - see root README.md's "lsignal_with_cpp vs
# lsignal_header_only" section).
add_executable(lsignal_test ${TEST_SOURCES} lsignal_with_cpp/lsignal.cpp)
target_include_directories(lsignal_test PRIVATE lsignal_with_cpp)
target_compile_definitions(lsignal_test PRIVATE LSIGNAL_WITH_CPP)
lsignal_configure_test_target(lsignal_test)

# lsignal_header_only: the original single-file header, kept and tested
# alongside lsignal_with_cpp rather than as an untested archival copy.
add_executable(lsignal_test_header_only ${TEST_SOURCES})
target_include_directories(lsignal_test_header_only PRIVATE lsignal_header_only)
lsignal_configure_test_target(lsignal_test_header_only)
