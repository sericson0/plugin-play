# Plugin Play — free/donation VST3 chain host for DJs.
# Licensed under the GNU General Public License v3.0 (see LICENSE). Uses JUCE and
# the Steinberg VST3 SDK under their GPLv3 grants (see THIRD_PARTY_NOTICES.md).
cmake_minimum_required(VERSION 3.22)

# macOS: universal binary (Apple silicon + Intel) and a 14.4 OS floor — the
# first release with Core Audio process taps (CATapDescription), the per-app
# capture API the Mac input model is built on. Must be set before project().
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
set(CMAKE_OSX_DEPLOYMENT_TARGET "14.4" CACHE STRING "")

project(PluginPlay VERSION 1.0.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# The process-tap capture module is Objective-C++ (Core Audio's tap API is an
# Objective-C class).
if (APPLE)
    enable_language(OBJCXX)
    set(CMAKE_OBJCXX_STANDARD 17)
    set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
endif()

# Statically link the MSVC runtime (/MT) so the app launches on a clean Windows
# machine that has no Visual C++ redistributable — the "single simple download"
# goal. Must be set BEFORE JUCE is fetched so JUCE's own targets use the same
# runtime; mixing /MT and /MD across libraries is a link error. CMP0091 (NEW by
# default at cmake ≥ 3.15) makes CMAKE_MSVC_RUNTIME_LIBRARY authoritative.
if (MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

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
    # Centre-cropped variant for the small (16/32 px) icon entries, so the play
    # triangle stays legible in the taskbar/title bar instead of the whole frame
    # shrinking to a smudge.
    ICON_SMALL    "${CMAKE_SOURCE_DIR}/Resources/exe_logo_small.png"
    # macOS TCC: microphone permission covers reading input devices (e.g. a
    # virtual cable); NSAudioCaptureUsageDescription is the 14.4+ process-tap
    # prompt shown when the user picks an app as the input source. Both are
    # ignored on Windows.
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Plugin Play needs audio input access so it can run your effects on the input you pick."
    PLIST_TO_MERGE "<plist version=\"1.0\"><dict>
                        <key>NSAudioCaptureUsageDescription</key>
                        <string>Plugin Play captures the audio of the app you pick as an input source, so it can run your effects on it.</string>
                    </dict></plist>"
)

juce_generate_juce_header(PluginPlay)

# ── Embedded resources (logo shown in the header) ────────────────────────────
juce_add_binary_data(PluginPlayData
    SOURCES
        Resources/exe_logo_transparent.png
        Resources/full_logo.png)

target_sources(PluginPlay
    PRIVATE
        Source/Main.cpp
        Source/Theme.cpp
        Source/Audio/AudioEngine.cpp
        Source/Audio/AppRouting.cpp
        Source/Audio/LoopbackCapture.cpp
        Source/Audio/WasapiEndpoints.cpp
        Source/Plugins/PluginScanner.cpp
        Source/Setup/UpdateCheck.cpp
        Source/Setup/VirtualCable.cpp
        Source/UI/MainComponent.cpp
        Source/UI/ChainView.cpp
        Source/UI/PluginPicker.cpp
        Source/UI/SupportPanel.cpp
        Source/UI/WelcomePopup.cpp
)

if (APPLE)
    # Per-app capture via Core Audio process taps (macOS 14.4+). ARC just for this
    # file — JUCE itself builds without it.
    target_sources(PluginPlay PRIVATE Source/Audio/ProcessTap.mm)
    set_source_files_properties(Source/Audio/ProcessTap.mm
        PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
endif()

target_compile_definitions(PluginPlay
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_PLUGINHOST_VST3=1
        JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:PluginPlay,JUCE_PRODUCT_NAME>"
        JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:PluginPlay,JUCE_VERSION>"
)

if (WIN32)
    # JUCE 8 ships its own ASIO interop header, so JUCE_ASIO needs no Steinberg SDK.
    target_compile_definitions(PluginPlay
        PRIVATE
            JUCE_ASIO=1
            JUCE_WASAPI=1
            JUCE_DIRECTSOUND=1
    )
elseif (APPLE)
    # Mac DJs expect AudioUnit plugins alongside VST3; JUCE hosts them via the
    # same AudioPluginFormatManager (CoreAudioKit comes in through juce_audio_utils).
    target_compile_definitions(PluginPlay
        PRIVATE
            JUCE_PLUGINHOST_AU=1
    )
endif()

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

# Compile sources as UTF-8 so literal Unicode (bullets, ×, symbols, typographic
# punctuation) is read correctly by MSVC instead of being mangled to the system
# code page. Both flags are needed: /utf-8 sets the source and execution charset.
if (MSVC)
    target_compile_options(PluginPlay PRIVATE /utf-8)
endif()
