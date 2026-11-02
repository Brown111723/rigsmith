cmake_minimum_required(VERSION 3.25)

project(Rigsmith VERSION 0.7.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(MSVC)
    # Static CRT so the exe and the VST3 run on machines without the VC++ redistributable.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# ---------------------------------------------------------------------------
# Dependencies (pinned). Local checkouts can be supplied to skip downloading:
#   -DRIGSMITH_JUCE_DIR=...  -DRIGSMITH_NAM_DIR=...  -DRIGSMITH_EIGEN_DIR=...
# ---------------------------------------------------------------------------
set(RIGSMITH_JUCE_DIR "" CACHE PATH "Optional local JUCE checkout")
set(RIGSMITH_NAM_DIR "" CACHE PATH "Optional local NeuralAmpModelerCore checkout")
set(RIGSMITH_EIGEN_DIR "" CACHE PATH "Optional Eigen include directory")

if(RIGSMITH_JUCE_DIR)
    add_subdirectory(${RIGSMITH_JUCE_DIR} ${CMAKE_BINARY_DIR}/juce EXCLUDE_FROM_ALL)
else()
    FetchContent_Declare(juce
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 9.0.4
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(juce)
endif()

if(RIGSMITH_NAM_DIR)
    set(NAM_DIR ${RIGSMITH_NAM_DIR})
else()
    FetchContent_Declare(namcore
        GIT_REPOSITORY https://github.com/sdatkinson/NeuralAmpModelerCore.git
        GIT_TAG 0b3d3c97b0859a3a8c92a8628c4dd89a25eb5842
        GIT_SUBMODULES "Dependencies/eigen" "Dependencies/AudioDSPTools"
        SOURCE_SUBDIR _sources_only_)   # don't run NAM's own CMake (it builds tools we don't need)
    FetchContent_MakeAvailable(namcore)
    set(NAM_DIR ${namcore_SOURCE_DIR})
endif()

if(RIGSMITH_EIGEN_DIR)
    set(EIGEN_DIR ${RIGSMITH_EIGEN_DIR})
else()
    set(EIGEN_DIR ${NAM_DIR}/Dependencies/eigen)
endif()

# ---------------------------------------------------------------------------
# NAM core as a static library, always optimised (debug Eigen is unusably slow)
# ---------------------------------------------------------------------------
file(GLOB NAM_SOURCES ${NAM_DIR}/NAM/*.cpp ${NAM_DIR}/NAM/*/*.cpp)
add_library(nam_core STATIC ${NAM_SOURCES})
target_include_directories(nam_core SYSTEM PUBLIC
    ${NAM_DIR}
    ${NAM_DIR}/NAM
    ${NAM_DIR}/Dependencies/nlohmann
    ${NAM_DIR}/Dependencies/AudioDSPTools/dsp
    ${EIGEN_DIR})
target_compile_definitions(nam_core PUBLIC NAM_SAMPLE_FLOAT NAM_ENABLE_A2_FAST)
if(WIN32)
    target_compile_definitions(nam_core PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN _USE_MATH_DEFINES)
endif()
if(MSVC)
    target_compile_options(nam_core PRIVATE /O2 /Ob2 /bigobj /W0 /fp:fast)
else()
    target_compile_options(nam_core PRIVATE -O3 -w)
endif()

# ---------------------------------------------------------------------------
# The plug-in: one engine, two formats (Standalone .exe + VST3)
# ---------------------------------------------------------------------------
juce_add_plugin(Rigsmith
    COMPANY_NAME "Rigsmith"
    COMPANY_WEBSITE "https://github.com/brown111723/rigsmith"
    BUNDLE_ID "com.rigsmith.rigsmith"
    PRODUCT_NAME "Rigsmith"
    VERSION ${PROJECT_VERSION}
    PLUGIN_MANUFACTURER_CODE Rgsm
    PLUGIN_CODE Rgs1
    FORMATS Standalone VST3
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx Distortion
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Rigsmith needs audio input to process your guitar.")

target_sources(Rigsmith PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Engine/Nodes.cpp
    Source/Engine/Blocks.cpp
    Source/Engine/NamBlock.cpp
    Source/Engine/PluginBlock.cpp
    Source/Engine/Engine.cpp
    Source/Engine/Tuner.cpp
    Source/Engine/Fx.cpp
    Source/Engine/Randomiser.cpp
    Source/Engine/IniFile.cpp
    Source/Engine/Gear.cpp
    Source/Engine/Captures.cpp
    Source/T3K/Tone3000.cpp
    Source/UI/Theme.cpp
    Source/UI/Icons.cpp
    Source/UI/GraphView.cpp
    Source/UI/Inspector.cpp
    Source/UI/IoPanel.cpp
    Source/UI/Library.cpp
    Source/UI/Header.cpp
    Source/UI/FxDisplay.cpp
    Source/UI/Settings.cpp
    Source/UI/Tone3000Panel.cpp)

target_include_directories(Rigsmith PRIVATE Source)

target_compile_definitions(Rigsmith PUBLIC
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_AU=0
    JUCE_PLUGINHOST_LV2=0
    JUCE_PLUGINHOST_LADSPA=0
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    $<$<PLATFORM_ID:Windows>:JUCE_ASIO=1>
    $<$<PLATFORM_ID:Windows>:JUCE_WASAPI=1>
    $<$<PLATFORM_ID:Windows>:JUCE_DIRECTSOUND=1>
    $<$<PLATFORM_ID:Linux>:JUCE_USE_XINPUT=0>
    $<$<PLATFORM_ID:Linux>:JUCE_JACK=1>)

if(MSVC)
    target_compile_options(Rigsmith PRIVATE /bigobj /MP)
endif()

# TONE3000: the app's publishable key (t3k_pub_..., safe to ship). Create one at
# tone3000.com -> Settings -> API Keys. Without it, users can paste their own key in Settings.
set(RIGSMITH_T3K_KEY "" CACHE STRING "TONE3000 publishable key built into Rigsmith")
if(RIGSMITH_T3K_KEY)
    target_compile_definitions(Rigsmith PRIVATE RIGSMITH_T3K_KEY="${RIGSMITH_T3K_KEY}")
endif()

# Optional VST2 hosting. Steinberg no longer licenses the VST2 SDK, so it can't be
# downloaded here; point this at a folder that contains pluginterfaces/vst2.x/aeffect.h
# (found in VST3 SDK zips up to 3.6.x) and VST2 plug-ins become scannable.
set(RIGSMITH_VST2_SDK "" CACHE PATH "Folder containing pluginterfaces/vst2.x (enables VST2 hosting)")
if(RIGSMITH_VST2_SDK AND EXISTS "${RIGSMITH_VST2_SDK}/pluginterfaces/vst2.x/aeffect.h")
    target_compile_definitions(Rigsmith PUBLIC JUCE_PLUGINHOST_VST=1)
    target_include_directories(Rigsmith PRIVATE "${RIGSMITH_VST2_SDK}")
    message(STATUS "Rigsmith: VST2 hosting enabled (${RIGSMITH_VST2_SDK})")
else()
    message(STATUS "Rigsmith: VST2 hosting off (set RIGSMITH_VST2_SDK to enable)")
endif()

target_link_libraries(Rigsmith
    PRIVATE
        # NAM registers its architectures (WaveNet, LSTM...) with static initialisers;
        # a normal static link would silently drop them, so link the whole archive.
        "$<LINK_LIBRARY:WHOLE_ARCHIVE,nam_core>"
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_dsp
        juce::juce_gui_extra
        juce::juce_cryptography
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# Headless engine tests:  cmake -DRIGSMITH_TESTS=ON ...  then run RigsmithTests <NAM example_models dir>
# ---------------------------------------------------------------------------
option(RIGSMITH_TESTS "Build the headless engine tests" OFF)
if(RIGSMITH_TESTS)
    juce_add_console_app(RigsmithTests PRODUCT_NAME "RigsmithTests")
    target_sources(RigsmithTests PRIVATE
        Tests/EngineTests.cpp
        Source/Engine/Nodes.cpp
        Source/Engine/Blocks.cpp
        Source/Engine/NamBlock.cpp
        Source/Engine/PluginBlock.cpp
        Source/Engine/Engine.cpp
        Source/Engine/Tuner.cpp
        Source/Engine/Fx.cpp
        Source/Engine/Randomiser.cpp
    Source/Engine/IniFile.cpp
    Source/Engine/Gear.cpp
    Source/Engine/Captures.cpp
    Source/T3K/Tone3000.cpp)
    target_include_directories(RigsmithTests PRIVATE Source)
    target_compile_definitions(RigsmithTests PRIVATE
        JUCE_PLUGINHOST_VST3=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_USE_XINPUT=0 JUCE_MODAL_LOOPS_PERMITTED=1
        RIGSMITH_EXAMPLE_MODELS="${NAM_DIR}/example_models")
    target_link_libraries(RigsmithTests PRIVATE
        "$<LINK_LIBRARY:WHOLE_ARCHIVE,nam_core>" juce::juce_audio_utils juce::juce_audio_processors juce::juce_dsp juce::juce_gui_extra juce::juce_cryptography
        juce::juce_recommended_config_flags)
endif()
