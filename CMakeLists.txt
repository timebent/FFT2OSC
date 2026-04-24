cmake_minimum_required(VERSION 3.15)
project(juce-fft-osc LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)

## Ensure JUCE is available. If not present, fetch the JUCE source and
# add it as a subdirectory so CMake can build the JUCE targets directly.
include(FetchContent)
FetchContent_Declare(
    juce
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG master
    GIT_SHALLOW TRUE
)
FetchContent_GetProperties(juce)
if(NOT juce_POPULATED)
    FetchContent_Populate(juce)
endif()

add_subdirectory(${juce_SOURCE_DIR} ${juce_BINARY_DIR} EXCLUDE_FROM_ALL)

juce_add_console_app(juce_fft_osc
    PRODUCT_NAME "JUCE FFT OSC"
    VERSION 0.1.0
)

# Ensure the JUCE amalgamated header is generated for this target
juce_generate_juce_header(juce_fft_osc)

target_sources(juce_fft_osc PRIVATE
    src/Main.cpp
    src/FFTOSC.cpp
    src/FFTOSC.h
)

target_compile_definitions(juce_fft_osc PRIVATE JUCE_APPLICATION_NO_GUI=1)

target_link_libraries(juce_fft_osc PRIVATE
    juce::juce_core
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_dsp
    juce::juce_osc
)
