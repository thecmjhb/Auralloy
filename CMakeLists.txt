cmake_minimum_required(VERSION 3.22)
project(Auralloy VERSION 0.0.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE: use a local checkout with -DAURALLOY_JUCE_DIR=path, otherwise it is fetched from GitHub (pinned commit).
set(AURALLOY_JUCE_DIR "" CACHE PATH "Path to an existing JUCE checkout (optional)")
if(AURALLOY_JUCE_DIR)
    add_subdirectory(${AURALLOY_JUCE_DIR} juce EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG d60b7a78030661574a615b586d9325ba7a7fa7dd)
    FetchContent_MakeAvailable(JUCE)
endif()

# ------------------------------------------------------------------------------------------------
# The app
juce_add_gui_app(Auralloy
    PRODUCT_NAME "Auralloy"
    COMPANY_NAME "Auralloy"
    VERSION 0.0.1
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/assets/icon.png"
    NEEDS_WEB_BROWSER FALSE)

set(AURALLOY_CORE_SOURCES
    src/app/Core.cpp
    src/app/Theme.cpp
    src/app/Widgets.cpp
    src/app/Views.cpp
    src/app/MainComponent.cpp
    src/app/TitleBar.cpp)

target_sources(Auralloy PRIVATE src/app/Main.cpp ${AURALLOY_CORE_SOURCES})

target_compile_definitions(Auralloy PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="Auralloy"
    JUCE_APPLICATION_VERSION_STRING="0.0.1"
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_USE_WINDOWS_MEDIA_FORMAT=1
    JUCE_MODAL_LOOPS_PERMITTED=1)

target_link_libraries(Auralloy
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ------------------------------------------------------------------------------------------------
# Tests (cmake -DAURALLOY_TESTS=ON)
option(AURALLOY_TESTS "Build the test programs" OFF)
if(AURALLOY_TESTS)
    enable_testing()
    foreach(t Analysis Stems Mashup Timeline)
        add_executable(${t}Test tests/${t}Test.cpp)
        target_compile_features(${t}Test PRIVATE cxx_std_17)
        add_test(NAME ${t} COMMAND ${t}Test)
    endforeach()

    add_executable(Benchmark benchmarks/benchmark.cpp)
    target_compile_features(Benchmark PRIVATE cxx_std_17)

    juce_add_console_app(AuralloyAppTest PRODUCT_NAME "AuralloyAppTest")
    target_sources(AuralloyAppTest PRIVATE tests/AppTest.cpp ${AURALLOY_CORE_SOURCES})
    target_compile_definitions(AuralloyAppTest PRIVATE
        JUCE_MODAL_LOOPS_PERMITTED=1 JUCE_USE_MP3AUDIOFORMAT=1 JUCE_USE_WINDOWS_MEDIA_FORMAT=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(AuralloyAppTest PRIVATE juce::juce_audio_utils juce::juce_dsp juce::juce_recommended_config_flags)
endif()

# ------------------------------------------------------------------------------------------------
# Maintainer tools (cmake -DAURALLOY_TOOLS=ON): brand asset renderer and README demo recorder
option(AURALLOY_TOOLS "Build the asset and demo tools" OFF)
if(AURALLOY_TOOLS)
    juce_add_console_app(AuralloyAssets PRODUCT_NAME "AuralloyAssets")
    target_sources(AuralloyAssets PRIVATE tools/MakeAssets.cpp src/app/Theme.cpp)
    target_compile_definitions(AuralloyAssets PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(AuralloyAssets PRIVATE juce::juce_audio_utils juce::juce_dsp juce::juce_recommended_config_flags)

    juce_add_console_app(AuralloyDemo PRODUCT_NAME "AuralloyDemo")
    target_sources(AuralloyDemo PRIVATE tools/RecordDemo.cpp ${AURALLOY_CORE_SOURCES})
    target_compile_definitions(AuralloyDemo PRIVATE JUCE_MODAL_LOOPS_PERMITTED=1 JUCE_USE_MP3AUDIOFORMAT=1 JUCE_USE_WINDOWS_MEDIA_FORMAT=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(AuralloyDemo PRIVATE juce::juce_audio_utils juce::juce_dsp juce::juce_recommended_config_flags)
endif()
