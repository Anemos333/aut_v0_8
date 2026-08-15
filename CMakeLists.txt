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
    COMPANY_NAME "AnemosMusic"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
    PLUGIN_MANUFACTURER_CODE Mtal
    PRODUCT_STATUS COMMERCIAL
    PLUGIN_CODE Matv
    FORMATS VST3 AU
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
        Source/ModernPitchEngine.cpp
        Source/Tempo.cpp
        Source/Preset.cpp
        Source/ControlRoomPage.cpp
        Source/NeumatonLabTheme.cpp
)

target_compile_definitions(Neumaton
    PRIVATE
        NEUMATON_CLEAN_MODERN_PITCH_ENGINE=1
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

option(NEUMATON_BUILD_CLEAN_ENGINE_TESTS
       "Build the clean ModernPitchEngine invariant tests" OFF)

if(NEUMATON_BUILD_CLEAN_ENGINE_TESTS)
    juce_add_console_app(CleanModernPitchEngineTest
        PRODUCT_NAME CleanModernPitchEngineTest
    )
    juce_generate_juce_header(CleanModernPitchEngineTest)
    target_sources(CleanModernPitchEngineTest
        PRIVATE
            Tests/CleanModernPitchEngineTest.cpp
            Source/ModernPitchEngine.cpp
            Source/Tempo.cpp
    )
    target_include_directories(CleanModernPitchEngineTest PRIVATE Source)
    target_compile_definitions(CleanModernPitchEngineTest
        PRIVATE
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0
    )
    target_link_libraries(CleanModernPitchEngineTest
        PRIVATE
            juce::juce_audio_basics
            juce::juce_core
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags
    )
    set_target_properties(CleanModernPitchEngineTest PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/clean-engine-tests"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/clean-engine-tests"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/clean-engine-tests"
        RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/clean-engine-tests"
        RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}/clean-engine-tests"
    )

    juce_add_console_app(VoiceEvidenceAnalyzerTest
        PRODUCT_NAME VoiceEvidenceAnalyzerTest
    )
    juce_generate_juce_header(VoiceEvidenceAnalyzerTest)
    target_sources(VoiceEvidenceAnalyzerTest
        PRIVATE
            Tests/VoiceEvidenceAnalyzerTest.cpp
    )
    target_include_directories(VoiceEvidenceAnalyzerTest PRIVATE Source)
    target_compile_definitions(VoiceEvidenceAnalyzerTest
        PRIVATE
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0
    )
    target_link_libraries(VoiceEvidenceAnalyzerTest
        PRIVATE
            juce::juce_audio_basics
            juce::juce_core
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags
    )
    set_target_properties(VoiceEvidenceAnalyzerTest PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/clean-engine-tests"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/clean-engine-tests"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/clean-engine-tests"
        RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/clean-engine-tests"
        RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}/clean-engine-tests"
    )

    juce_add_console_app(TransportContinuityTest
        PRODUCT_NAME TransportContinuityTest
    )
    juce_generate_juce_header(TransportContinuityTest)
    target_sources(TransportContinuityTest
        PRIVATE
            Tests/TransportContinuityTest.cpp
            Source/ModernPitchEngine.cpp
            Source/Tempo.cpp
    )
    target_include_directories(TransportContinuityTest PRIVATE Source)
    target_compile_definitions(TransportContinuityTest
        PRIVATE
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0
    )
    target_link_libraries(TransportContinuityTest
        PRIVATE
            juce::juce_audio_basics
            juce::juce_core
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags
    )
    set_target_properties(TransportContinuityTest PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/clean-engine-tests"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/clean-engine-tests"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/clean-engine-tests"
        RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/clean-engine-tests"
        RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}/clean-engine-tests"
    )


    juce_add_console_app(SupervisorContinuityTest
        PRODUCT_NAME SupervisorContinuityTest
    )
    juce_generate_juce_header(SupervisorContinuityTest)
    target_sources(SupervisorContinuityTest
        PRIVATE
            Tests/SupervisorContinuityTest.cpp
            Source/ModernPitchEngine.cpp
            Source/Tempo.cpp
    )
    target_include_directories(SupervisorContinuityTest PRIVATE Source)
    target_compile_definitions(SupervisorContinuityTest
        PRIVATE
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0
    )
    target_link_libraries(SupervisorContinuityTest
        PRIVATE
            juce::juce_audio_basics
            juce::juce_core
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags
    )
    set_target_properties(SupervisorContinuityTest PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/clean-engine-tests"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/clean-engine-tests"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/clean-engine-tests"
        RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/clean-engine-tests"
        RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}/clean-engine-tests"
    )

endif()
