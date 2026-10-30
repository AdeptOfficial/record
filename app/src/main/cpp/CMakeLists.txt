cmake_minimum_required(VERSION 3.22.1)

project(record_engine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(oboe REQUIRED CONFIG)

# Platform-free looper; tests/native builds the same sources on the host.
add_library(looper_core STATIC
    core/LooperCore.cpp
    core/WavWriter.cpp
)
target_include_directories(looper_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# Audio code is always optimised: an unoptimised debug build would drop audio.
# Target options come after the per-configuration flags, so -O2 wins over -O0.
target_compile_options(looper_core PRIVATE -Wall -Wextra -O2)

add_library(record_engine SHARED
    OboeLooper.cpp
    jni_bridge.cpp
)
target_compile_options(record_engine PRIVATE -Wall -Wextra -O2)
target_link_libraries(record_engine PRIVATE looper_core oboe::oboe android log)
