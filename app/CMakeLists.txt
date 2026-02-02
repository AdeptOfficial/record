cmake_minimum_required(VERSION 3.22.1)

project("ditto_looper")

find_package(oboe REQUIRED CONFIG)

add_library(
    audio_engine
    SHARED
    src/main/cpp/AudioEngine.cpp
)

target_link_libraries(
    audio_engine
    android
    log
    oboe::oboe
)
