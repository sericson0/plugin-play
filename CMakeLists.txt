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
    ICON_BIG      "${CMAKE_SOURCE_DIR}/Resources/exe_logo.png"
)

juce_generate_juce_header(PluginPlay)

# ── Embedded resources (logo shown in the header) ────────────────────────────
juce_add_binary_data(PluginPlayData
    SOURCES
        Resources/exe_logo.png)

target_sources(PluginPlay
    PRIVATE
        Source/Main.cpp
        Source/Theme.cpp
        Source/Audio/AudioEngine.cpp
        Source/Audio/WasapiEndpoints.cpp
        Source/Plugins/PluginScanner.cpp
        Source/Setup/VirtualCable.cpp
        Source/UI/MainComponent.cpp
        Source/UI/ChainView.cpp
        Source/UI/PluginPicker.cpp
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
        PluginPlayData
        juce::juce_audio_utils
        juce::juce_audio_processors
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

target_include_directories(PluginPlay PRIVATE ${CMAKE_SOURCE_DIR}/Source)
