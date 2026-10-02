cmake_minimum_required(VERSION 3.22)

# この 2 つは project() より前に書かないと効かない
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")
set(CMAKE_OSX_ARCHITECTURES "x86_64;arm64" CACHE STRING "Build architectures")

# Windows: C++ ランタイムを DLL に組み込む (VC++ 再頒布可能パッケージが無い PC でも読み込めるように)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" CACHE STRING "MSVC runtime")

project(SpecShaderVST VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# PluginLab の共通部品 (LabLook.h / LabTest.h / LabRobust.h)。隣に PluginLab があればそれを、無ければ同梱の lab/ を使う
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../PluginLab/juce/common/LabTest.h")
    set(LAB_COMMON "${CMAKE_CURRENT_SOURCE_DIR}/../PluginLab/juce/common")
else()
    set(LAB_COMMON "${CMAKE_CURRENT_SOURCE_DIR}/lab")
endif()

# 共有の JUCE (../_deps/JUCE) があればそれを使う。無ければ取得する
set(_juce_shared "${CMAKE_CURRENT_SOURCE_DIR}/../_deps/JUCE")
if(EXISTS "${_juce_shared}/modules/juce_core/juce_core.h")
    add_subdirectory("${_juce_shared}" JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.15
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# 検証用ビルドなどでインストールしたくないときは -DLAB_INSTALL=OFF
# ビルド後にプラグインフォルダへコピーするか。Windows のコピー先は管理者権限が要るので既定 OFF
if(APPLE)
    set(_lab_install_default ON)
else()
    set(_lab_install_default OFF)
endif()
option(LAB_INSTALL "Copy the plugin to the system plug-in folder after build" ${_lab_install_default})
if(APPLE)
    set(LAB_FORMATS VST3 AU)
else()
    set(LAB_FORMATS VST3)
endif()
if(MSVC)
    add_compile_options(/utf-8)
endif()

juce_add_plugin(SpecShaderVST
    PRODUCT_NAME "SpecShader"
    COMPANY_NAME "MIDy"
    BUNDLE_ID "com.midy.specshadervst"
    PLUGIN_MANUFACTURER_CODE "MIDy"
    PLUGIN_CODE "SpSh"
    FORMATS ${LAB_FORMATS}
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    VST3_CATEGORIES "Instrument" "Synth"
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    COPY_PLUGIN_AFTER_BUILD ${LAB_INSTALL})

target_sources(SpecShaderVST PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/ShaderEngine.cpp
    Source/OffscreenGL.cpp)
target_include_directories(SpecShaderVST PRIVATE "${LAB_COMMON}")

target_compile_definitions(SpecShaderVST PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    GL_SILENCE_DEPRECATION=1)

target_link_libraries(SpecShaderVST
    PRIVATE juce::juce_audio_utils juce::juce_dsp juce::juce_opengl juce::juce_gui_extra
    PUBLIC juce::juce_recommended_config_flags)

# 検証用オフラインドライバ (lab.py test / render / snapshot が使う)
option(LAB_BUILD_TESTS "Build the offline test driver" OFF)
if(LAB_BUILD_TESTS)
    juce_add_console_app(SpecShaderVSTTest PRODUCT_NAME "SpecShaderVSTTest")
    target_sources(SpecShaderVSTTest PRIVATE tests/TestMain.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp Source/ShaderEngine.cpp Source/OffscreenGL.cpp)
    target_include_directories(SpecShaderVSTTest PRIVATE "${LAB_COMMON}")
    target_compile_definitions(SpecShaderVSTTest PUBLIC
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=1 GL_SILENCE_DEPRECATION=1
        JucePlugin_Name="SpecShaderVST")
    target_link_libraries(SpecShaderVSTTest
        PRIVATE juce::juce_audio_utils juce::juce_dsp juce::juce_opengl juce::juce_gui_extra
        PUBLIC juce::juce_recommended_config_flags)
endif()
