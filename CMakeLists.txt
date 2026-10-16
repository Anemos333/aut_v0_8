cmake_minimum_required(VERSION 3.22)

project(NeumatonErgastirion
    VERSION 0.8.0
    LANGUAGES C CXX
)

if(APPLE)
    enable_language(OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
    juce
    GIT_REPOSITORY "https://github.com/juce-framework/JUCE.git"
    GIT_TAG        8.0.12
)
FetchContent_MakeAvailable(juce)

juce_add_binary_data(BinaryData
    NAMESPACE BinaryData
    SOURCES
        Resources/sfondo1.jpeg
        Resources/sfondo2.jpg
        Resources/sfondo3.jpeg
)

juce_add_plugin(Neumaton
    COMPANY_NAME "ERGASTIRION"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
    PLUGIN_MANUFACTURER_CODE Nina
    PRODUCT_STATUS COMMERCIAL
    PLUGIN_CODE Matv
    FORMATS VST3
    PRODUCT_NAME "Neumaton"
)

juce_generate_juce_header(Neumaton)

target_sources(Neumaton
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/ScaleDefinitions.cpp
        Source/CustomScalePresets.cpp
        Source/CustomScaleEditor.cpp
        Source/PitchCore.cpp
        Source/PitchEngineV1.cpp
        Source/SinglePathPitchRenderer.cpp
        Source/Tempo.cpp
        Source/Preset.cpp
        Source/ControlRoomPage.cpp
        Source/NeumatonLabTheme.cpp
)

target_compile_definitions(Neumaton
    PRIVATE
        NEUMATON_PITCH_ENGINE_V1=1
        NEUMATON_CLOSED_BETA=1
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(Neumaton
    PRIVATE
        BinaryData
        juce::juce_audio_basics
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_audio_plugin_client
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_core
        juce::juce_cryptography
        juce::juce_data_structures
        juce::juce_events
        juce::juce_graphics
        juce::juce_gui_basics
        juce::juce_gui_extra
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)


option(NEUMATON_BUILD_LICENSING_TESTS
       "Build the licensing and pack-integrity contract tests" OFF)

if(NEUMATON_BUILD_LICENSING_TESTS)
    juce_add_console_app(LicensingSkeletonTest
        PRODUCT_NAME LicensingSkeletonTest
    )
    juce_generate_juce_header(LicensingSkeletonTest)
    target_sources(LicensingSkeletonTest
        PRIVATE
            Tests/LicensingSkeletonTest.cpp
    )
    target_include_directories(LicensingSkeletonTest PRIVATE Source)
    target_compile_definitions(LicensingSkeletonTest
        PRIVATE
            NEUMATON_CLOSED_BETA=1
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0
    )
    target_link_libraries(LicensingSkeletonTest
        PRIVATE
            juce::juce_core
            juce::juce_cryptography
            juce::juce_data_structures
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags
    )
    set_target_properties(LicensingSkeletonTest PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/licensing-tests"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/licensing-tests"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/licensing-tests"
        RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/licensing-tests"
        RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}/licensing-tests"
    )
endif()

option(NEUMATON_BUILD_V1_TESTS
       "Build active V1 detector, renderer, scale and UI model tests" OFF)

if(NEUMATON_BUILD_V1_TESTS)
    add_executable(PitchCoreStressTest
        Tests/PitchCoreStressTest.cpp
        Source/PitchCore.cpp
    )
    add_executable(SinglePathPitchRendererTest
        Tests/SinglePathPitchRendererTest.cpp
        Source/PitchCore.cpp
        Source/SinglePathPitchRenderer.cpp
    )
    add_executable(ScaleDatabaseV1Test
        Tests/ScaleDatabaseV1Test.cpp
        Source/PitchCore.cpp
        Source/ScaleDefinitions.cpp
    )
    add_executable(ScaleDegreeDisplayTest
        Tests/ScaleDegreeDisplayTest.cpp
    )
    add_executable(ScaleEditorGeometryTest
        Tests/ScaleEditorGeometryTest.cpp
    )

    foreach(test_target IN ITEMS
            PitchCoreStressTest
            SinglePathPitchRendererTest
            ScaleDatabaseV1Test
            ScaleDegreeDisplayTest
            ScaleEditorGeometryTest)
        target_include_directories(${test_target} PRIVATE Source)
        set_target_properties(${test_target} PROPERTIES
            CXX_STANDARD 17
            CXX_STANDARD_REQUIRED ON
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/v1-tests"
        )
    endforeach()
endif()
