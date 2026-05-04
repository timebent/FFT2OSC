cmake_minimum_required(VERSION 3.15)
project(juce-fft-osc LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)

find_package(JUCE CONFIG QUIET)

# If JUCE isn't available on the system, fetch it automatically via FetchContent
if (NOT JUCE_FOUND)
    include(FetchContent)
    FetchContent_Declare(
        juce
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.12
    )
    FetchContent_MakeAvailable(juce)
endif()

juce_add_console_app(juce_fft_osc
    PRODUCT_NAME "JUCE FFT OSC"
    VERSION 0.1.0
)

target_sources(juce_fft_osc PRIVATE
    src/Main.cpp
    src/FFTOSC.cpp
    src/FFTOSC.h
)

target_compile_definitions(juce_fft_osc PRIVATE JUCE_APPLICATION_NO_GUI=1)

target_link_libraries(juce_fft_osc PRIVATE
    juce::juce_core
    juce::juce_events
    juce::juce_audio_basics
    juce::juce_audio_formats
    juce::juce_audio_devices
    juce::juce_dsp
    juce::juce_osc
)
