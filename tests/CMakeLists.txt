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
    DelayLineTests.cpp
    effects/BitcrusherTests.cpp
    effects/ChorusTests.cpp
    effects/CompressorTests.cpp
    effects/DeEsserTests.cpp
    effects/DelayTests.cpp
    effects/EqTests.cpp
    effects/FilterTests.cpp
    effects/FlangerTests.cpp
    effects/GainTests.cpp
    effects/GateTests.cpp
    effects/LimiterTests.cpp
    effects/PhaserTests.cpp
    effects/ReverbTests.cpp
    effects/SaturatorTests.cpp
    effects/TransientShaperTests.cpp
    effects/TremoloTests.cpp
    effects/UtilityTests.cpp
    instruments/DrumSamplerTests.cpp
    instruments/SamplerTests.cpp
    instruments/SynthTests.cpp
    NoteValueTests.cpp
    OversamplerTests.cpp
    SampleTests.cpp
    SmoothedValueTests.cpp
    SvfTests.cpp
    VersionTests.cpp
    WavetableTests.cpp

    # Counts allocations (replaces the global operator new) for the
    # "no allocation in process()" checks.
    support/AllocationCounter.cpp)

target_link_libraries(vloom_dsp_tests PRIVATE vloom::dsp Catch2::Catch2WithMain)
target_include_directories(vloom_dsp_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
vloom_dsp_set_warnings(vloom_dsp_tests)

list(APPEND CMAKE_MODULE_PATH ${Catch2_SOURCE_DIR}/extras)
include(Catch)
catch_discover_tests(vloom_dsp_tests)
