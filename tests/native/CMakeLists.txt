# Host build of the platform-free looper core and its tests (no Android SDK needed).
#   cmake -S tests/native -B build/native -DSANITIZE=address   # or thread, or none
#   cmake --build build/native && ctest --test-dir build/native
cmake_minimum_required(VERSION 3.22)
project(record_native_tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(SANITIZE "none" CACHE STRING "none, address (with undefined) or thread")

set(CORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../app/src/main/cpp)
add_library(looper_core STATIC ${CORE_DIR}/core/LooperCore.cpp ${CORE_DIR}/core/WavWriter.cpp)
target_include_directories(looper_core PUBLIC ${CORE_DIR})
find_package(Threads REQUIRED)

set(FLAGS -Wall -Wextra)
if(SANITIZE STREQUAL "address")
    list(APPEND FLAGS -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=all)
    set(LINK_FLAGS -fsanitize=address,undefined)
elseif(SANITIZE STREQUAL "thread")
    list(APPEND FLAGS -O1 -g -fsanitize=thread)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        # TSan does not model the snapshot seqlock's fence; every field it orders is
        # itself atomic, so ignoring the fence cannot hide a data race.
        list(APPEND FLAGS -Wno-tsan)
    endif()
    set(LINK_FLAGS -fsanitize=thread)
else()
    list(APPEND FLAGS -O2)
endif()
target_compile_options(looper_core PRIVATE ${FLAGS})
target_link_options(looper_core PUBLIC ${LINK_FLAGS})

add_executable(looper_core_test LooperCoreTest.cpp)
target_compile_options(looper_core_test PRIVATE ${FLAGS})
target_link_libraries(looper_core_test PRIVATE looper_core)

add_executable(soak_test SoakTest.cpp)
target_compile_options(soak_test PRIVATE ${FLAGS})
if(SANITIZE STREQUAL "none")
    target_compile_definitions(soak_test PRIVATE SOAK_COUNT_ALLOCATIONS)
endif()
target_link_libraries(soak_test PRIVATE looper_core Threads::Threads)

enable_testing()
add_test(NAME looper_core_test COMMAND looper_core_test)
add_test(NAME soak_short COMMAND soak_test 20 1)
