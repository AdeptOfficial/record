cmake_minimum_required(VERSION 3.18.1)
project("superpowered_android")

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Path to Superpowered root (you already have this correct)
set(SUPERPOWERED_DIR ${CMAKE_SOURCE_DIR}/Superpowered)

# Include Superpowered headers
include_directories(
        ${SUPERPOWERED_DIR}
)

# Import static Superpowered library
add_library(SuperpoweredLib STATIC IMPORTED)
set_target_properties(SuperpoweredLib PROPERTIES
        IMPORTED_LOCATION ${SUPERPOWERED_DIR}/libSuperpoweredAndroid${ANDROID_ABI}.a
)

# ✅ Add your real source files here
add_library(native-lib SHARED
        ${CMAKE_SOURCE_DIR}/native-lib.cpp
        ${CMAKE_SOURCE_DIR}/SuperpoweredRecorder.cpp
        ${SUPERPOWERED_DIR}/OpenSource/SuperpoweredAndroidAudioIO.cpp
        Superpowered/OpenSource
)

# Link everything
target_link_libraries(
        native-lib
        SuperpoweredLib
        android
        OpenSLES
        log
)
