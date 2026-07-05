cmake_minimum_required(VERSION 3.22)
project(PluginPlay VERSION 0.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Fetch JUCE ───────────────────────────────────────────────────────────────
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.12
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(JUCE)

# ── App target ───────────────────────────────────────────────────────────────
juce_add_gui_app(PluginPlay
    PRODUCT_NAME  "Plugin Play"
    COMPANY_NAME  "Plugin Play"
    BUNDLE_ID     "com.pluginplay.app"
)

juce_generate_juce_header(PluginPlay)

target_sources(PluginPlay
    PRIVATE
        Source/Main.cpp
        Source/Theme.cpp
        Source/Audio/AudioEngine.cpp
        Source/Plugins/PluginScanner.cpp
        Source/UI/MainComponent.cpp
        Source/UI/ChainView.cpp
)

# JUCE 8 ships its own ASIO interop header, so JUCE_ASIO needs no Steinberg SDK.
target_compile_definitions(PluginPlay
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_PLUGINHOST_VST3=1
        JUCE_ASIO=1
        JUCE_WASAPI=1
        JUCE_DIRECTSOUND=1
        JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:PluginPlay,JUCE_PRODUCT_NAME>"
        JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:PluginPlay,JUCE_VERSION>"
)

target_link_libraries(PluginPlay
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_processors
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

target_include_directories(PluginPlay PRIVATE ${CMAKE_SOURCE_DIR}/Source)
