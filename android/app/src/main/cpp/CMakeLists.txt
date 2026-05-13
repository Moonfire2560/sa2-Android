cmake_minimum_required(VERSION 3.22.1)
project(sa2-android)

# Add SDL2 as a subdirectory
add_subdirectory(SDL)

# Paths to SA2 source
set(SA2_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../../..)
set(SA2_SRC ${SA2_ROOT}/src)
set(SA2_INCLUDE ${SA2_ROOT}/include)

# Include directories
include_directories(
    ${SA2_INCLUDE}
    ${SA2_INCLUDE}/game
    ${SA2_INCLUDE}/constants
    ${SA2_INCLUDE}/data
    ${SA2_INCLUDE}/gba
    ${SA2_INCLUDE}/lib
    ${SA2_INCLUDE}/platform
    ${SA2_SRC}
    ${CMAKE_CURRENT_BINARY_DIR}/SDL/include
    ${CMAKE_CURRENT_BINARY_DIR}/SDL/include/SDL2
)

# Preprocessor definitions
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
    -DENABLE_VRAM_VIEW=0
    -DNDEBUG
)

# Compiler flags
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wno-unused-variable -Wno-unused-function -Wno-incompatible-pointer-types -Wno-macro-redefined")
set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -O3 -ffast-math")

# Core game engine sources
set(SA2_CORE_SOURCES
    ${SA2_SRC}/core.c
    ${SA2_SRC}/sprite.c
    ${SA2_SRC}/task.c
    ${SA2_SRC}/background.c
    ${SA2_SRC}/bg_triangles.c
    ${SA2_SRC}/trig.c
    ${SA2_SRC}/malloc_ewram.c
    ${SA2_SRC}/malloc_vram.c
    ${SA2_SRC}/input_recorder.c
)

# Platform shared sources
file(GLOB SA2_PLATFORM_SHARED_SOURCES
    ${SA2_SRC}/platform/shared/*.c
    ${SA2_SRC}/platform/shared/audio/*.c
    ${SA2_SRC}/platform/shared/video/*.c
)

# Library sources
file(GLOB_RECURSE SA2_LIB_SOURCES
    ${SA2_SRC}/lib/m4a/*.c
)

# Game logic sources (SA2 specific) - collect all then filter
file(GLOB_RECURSE SA2_GAME_SOURCES_ALL
    ${SA2_SRC}/game/sa2/*.c
)

# Exclude problematic files with inline ASM or NONMATCH naked functions
list(FILTER SA2_GAME_SOURCES_ALL EXCLUDE REGEX ".*/cannon\\.c$")
list(FILTER SA2_GAME_SOURCES_ALL EXCLUDE REGEX ".*/input_buffer\\.c$")

set(SA2_GAME_SOURCES ${SA2_GAME_SOURCES_ALL})

# Shared game sources
file(GLOB_RECURSE SA2_SHARED_GAME_SOURCES
    ${SA2_SRC}/game/shared/*.c
)

# Platform main file
set(SA2_PLATFORM_MAIN
    ${CMAKE_CURRENT_SOURCE_DIR}/sa2_android_main.c
)

# Combine all sources
set(SA2_ALL_SOURCES
    ${SA2_CORE_SOURCES}
    ${SA2_PLATFORM_SHARED_SOURCES}
    ${SA2_LIB_SOURCES}
    ${SA2_GAME_SOURCES}
    ${SA2_SHARED_GAME_SOURCES}
    ${SA2_PLATFORM_MAIN}
)

# Create the shared library
add_library(sa2 SHARED ${SA2_ALL_SOURCES})

target_link_libraries(sa2
    SDL2
    log
    android
    GLESv2
    m
)

set_target_properties(sa2 PROPERTIES
    OUTPUT_NAME "sa2"
    VERSION 1.0
)

if(CMAKE_BUILD_TYPE MATCHES Release)
    target_compile_options(sa2 PRIVATE
        -fvisibility=hidden
        -ffunction-sections
        -fdata-sections
    )
    
    set_target_properties(sa2 PROPERTIES
        LINK_FLAGS "-Wl,--gc-sections"
    )
endif()