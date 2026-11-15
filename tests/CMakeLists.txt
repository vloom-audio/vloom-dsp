# Pinned to the commit of the v3.7.1 tag (tags can be moved), as in vloom-engine.
CPMAddPackage(
    NAME Catch2
    GITHUB_REPOSITORY catchorg/Catch2
    VERSION 3.7.1
    GIT_TAG fa43b77429ba76c462b1898d6cd2f2d7a9416b14
    GIT_SHALLOW FALSE)

# Test files mirror include/vloom/dsp/: tests/<dir>/<Name>Tests.cpp
add_executable(vloom_dsp_tests
    BiquadTests.cpp
    SmoothedValueTests.cpp
    effects/EqTests.cpp
    effects/GainTests.cpp
    VersionTests.cpp)

target_link_libraries(vloom_dsp_tests PRIVATE vloom::dsp Catch2::Catch2WithMain)
vloom_dsp_set_warnings(vloom_dsp_tests)

list(APPEND CMAKE_MODULE_PATH ${Catch2_SOURCE_DIR}/extras)
include(Catch)
catch_discover_tests(vloom_dsp_tests)
