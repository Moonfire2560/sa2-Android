cmake_minimum_required(VERSION 3.22.1)
project(sa2-android)

# Add SDL2 as a subdirectory
add_subdirectory(SDL)

# Paths to SA2 source (relative to this CMakeLists.txt)
set(SA2_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../../..)
set(SA2_SRC ${SA2_ROOT}/src)
set(SA2_INCLUDE ${SA2_ROOT}/include)

# Include directories
include_directories(
    ${SA2_INCLUDE}
    ${SA2_INCLUDE}/game
    ${SA2_INCLUDE}/constants
    ${SA2_SRC}
    ${CMAKE_CURRENT_BINARY_DIR}/SDL/include
    ${CMAKE_CURRENT_BINARY_DIR}/SDL/include/SDL2
)

# Preprocessor definitions for widescreen Android build
add_definitions(
    -D__ANDROID__
    -DPLATFORM_SDL=1
    -DENABLE_AUDIO=1
    -DPLATFORM_GBA=0
    -DGAME=GAME_SA2
    -DGAME_LANGUAGE=LANGUAGE_MULTI
    -DREVISION=0
    -DSONIC_ADVANCE_2=1
    -DWIDESCREEN_HACK=1
    -DDISPLAY_WIDTH=426
    -DDISPLAY_HEIGHT=240
    -DNDEBUG
)

# Compiler flags
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wno-unused-variable -Wno-unused-function")
set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -O3 -ffast-math")

# For now, create a minimal test library
add_library(sa2 SHARED
    test_main.c
)

target_link_libraries(sa2
    SDL2
    log
    android
    GLESv2
)