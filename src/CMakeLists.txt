add_library(vloom_dsp)
add_library(vloom::dsp ALIAS vloom_dsp)

target_sources(vloom_dsp PRIVATE
    Oversampler.cpp
    Version.cpp
    Wavetable.cpp

    # effects
    effects/Bitcrusher.cpp
    effects/Chorus.cpp
    effects/Compressor.cpp
    effects/DeEsser.cpp
    effects/Delay.cpp
    effects/Eq.cpp
    effects/Filter.cpp
    effects/Flanger.cpp
    effects/Gain.cpp
    effects/Gate.cpp
    effects/Limiter.cpp
    effects/Phaser.cpp
    effects/Reverb.cpp
    effects/Saturator.cpp
    effects/TransientShaper.cpp
    effects/Tremolo.cpp
    effects/Utility.cpp

    # instruments
    instruments/Synth.cpp)

target_include_directories(vloom_dsp PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>)
target_compile_features(vloom_dsp PUBLIC cxx_std_20)
target_compile_definitions(vloom_dsp PRIVATE VLOOM_DSP_VERSION="${PROJECT_VERSION}")
# The engine may link it into a shared library or a plugin.
set_target_properties(vloom_dsp PROPERTIES POSITION_INDEPENDENT_CODE ON)
vloom_dsp_set_warnings(vloom_dsp)
