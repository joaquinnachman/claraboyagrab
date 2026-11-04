cmake_minimum_required(VERSION 3.22)

project(ClaraboyaGrab VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Runtime de Visual C++ estático: el .vst3 no necesita instalar nada extra en la PC.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(CLARABOYA_BUILD_TESTS "Compilar el programa de pruebas (solo desarrollo)" OFF)

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.9
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_binary_data(ClaraboyaGrabData
    SOURCES
        Resources/BebasNeue-Regular.ttf
        Resources/Montserrat-SemiBold.ttf
        Resources/Montserrat-Bold.ttf)

# Motor: descargas, búsqueda en YouTube Music y conversión a WAV.
set(GRAB_ENGINE_SOURCES
    Source/Resolver.cpp
    Source/Tools.cpp
    Source/GrabEngine.cpp)

juce_add_plugin(ClaraboyaGrab
    COMPANY_NAME              "Claraboya Estudio"
    COMPANY_WEBSITE           "https://linktr.ee/claraboyaestudio"
    PLUGIN_MANUFACTURER_CODE  Clby
    PLUGIN_CODE               CGrb
    PRODUCT_NAME              "Claraboya Grab"
    FORMATS                   VST3 Standalone
    IS_SYNTH                  FALSE
    NEEDS_MIDI_INPUT          FALSE
    NEEDS_MIDI_OUTPUT         FALSE
    IS_MIDI_EFFECT            FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD   FALSE
    VST3_CATEGORIES           Fx Tools
    NEEDS_CURL                TRUE
    BUNDLE_ID                 ar.claraboya.grab)

target_sources(ClaraboyaGrab PRIVATE
    ${GRAB_ENGINE_SOURCES}
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_include_directories(ClaraboyaGrab PRIVATE Source)

target_compile_definitions(ClaraboyaGrab PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=1
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(ClaraboyaGrab
    PRIVATE
        ClaraboyaGrabData
        juce::juce_audio_utils
        juce::juce_audio_formats
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Textos con tildes: MSVC tiene que leer el código como UTF-8.
target_compile_options(ClaraboyaGrab PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

if(CLARABOYA_BUILD_TESTS)
    juce_add_console_app(ClaraboyaGrabTests PRODUCT_NAME "ClaraboyaGrabTests" NEEDS_CURL TRUE)
    target_sources(ClaraboyaGrabTests PRIVATE ${GRAB_ENGINE_SOURCES} Tests/TestMain.cpp)
    target_include_directories(ClaraboyaGrabTests PRIVATE Source)
    target_compile_definitions(ClaraboyaGrabTests PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=1
        JUCE_STANDALONE_APPLICATION=1)
    target_link_libraries(ClaraboyaGrabTests PRIVATE
        juce::juce_core
        juce::juce_events
        juce::juce_data_structures
        juce::juce_audio_formats
        juce::juce_recommended_config_flags)

    juce_add_console_app(ClaraboyaGrabSnapshot PRODUCT_NAME "ClaraboyaGrabSnapshot" NEEDS_CURL TRUE)
    target_sources(ClaraboyaGrabSnapshot PRIVATE ${GRAB_ENGINE_SOURCES}
        Source/PluginProcessor.cpp Source/PluginEditor.cpp Tests/Snapshot.cpp)
    target_include_directories(ClaraboyaGrabSnapshot PRIVATE Source)
    target_compile_definitions(ClaraboyaGrabSnapshot PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=1 JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(ClaraboyaGrabSnapshot PRIVATE
        ClaraboyaGrabData
        juce::juce_audio_utils
        juce::juce_audio_formats
        juce::juce_recommended_config_flags)
endif()
