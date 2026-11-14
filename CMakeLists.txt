cmake_minimum_required(VERSION 3.22)

project(vloom_dsp
    VERSION 0.1.0
    DESCRIPTION "Audio effects and instruments for Vloom Audio"
    HOMEPAGE_URL "https://github.com/vloom-audio/vloom-dsp"
    LANGUAGES CXX)

# Tests and sanitizers are on only when this is the top-level project, so
# vloom-engine (which pulls this in with CPM) builds just the library.
if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    set(VLOOM_DSP_IS_TOP_LEVEL ON)
else()
    set(VLOOM_DSP_IS_TOP_LEVEL OFF)
endif()

option(VLOOM_DSP_BUILD_TESTS "Build vloom-dsp unit tests" ${VLOOM_DSP_IS_TOP_LEVEL})

if(NOT CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
endif()

include(${CMAKE_CURRENT_LIST_DIR}/cmake/CompilerWarnings.cmake)
if(VLOOM_DSP_IS_TOP_LEVEL)
    # A parent project sets its own sanitizer flags for everything it builds.
    include(${CMAKE_CURRENT_LIST_DIR}/cmake/Sanitizers.cmake)
endif()

add_subdirectory(src)

if(VLOOM_DSP_BUILD_TESTS)
    include(${CMAKE_CURRENT_LIST_DIR}/cmake/CPM.cmake)
    enable_testing()
    add_subdirectory(tests)
endif()
