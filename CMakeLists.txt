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

# --- SCARICAMENTO AUTOMATICO DI JUCE ---
include(FetchContent)
FetchContent_Declare(
    juce
    GIT_REPOSITORY "https://github.com/juce-framework/JUCE.git"
    GIT_TAG        8.0.12 # Scarica l'ultima versione stabile di JUCE 8
)
FetchContent_MakeAvailable(juce)


# ----------------------------------------

# Se hai Antigravity come cartella locale o repo, assicurati che il percorso non sia fisso (C:/...)
# Se è una cartella dentro al tuo progetto, usa ${CMAKE_CURRENT_SOURCE_DIR}/nome_cartella


# Binary data for background images
juce_add_binary_data(BinaryData
    NAMESPACE BinaryData
    SOURCES
        Resources/sfondo1.jpeg
       # Resources/sfondo4.jfif
        Resources/sfondo2.jpg
       # Resources/sfondo5.jpeg
        Resources/sfondo3.jpeg
)

# Plugin target — VST3 only
juce_add_plugin(Neumaton
    COMPANY_NAME "AnemosMusic"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
    PLUGIN_MANUFACTURER_CODE Mtal
    # PLUGIN_CODE Matc
    PRODUCT_STATUS COMMERCIAL
    PLUGIN_CODE Matv
    FORMATS VST3 AU
    # PRODUCT_NAME "Microtonal Autotune"
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
        Source/ModernPitchOutputStage.cpp
        Source/NeumatonRidgeLedger.cpp
        Source/NeumatonOutputRenderer.cpp
        Source/ScaleLock.cpp
        Source/Tempo.cpp
        Source/Preset.cpp
        Source/ControlRoomPage.cpp
        Source/NeumatonLabTheme.cpp
      
)

target_compile_definitions(Neumaton
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

# include(${CMAKE_CURRENT_SOURCE_DIR}/Tests/AutotuneTests.cmake)


# -----------------------------------------------------------------------------
# V3 Stage D exclusive-audio validation. These targets are opt-in and never
# become plug-in formats. The production plug-in on this experimental branch
# is compiled with V3 as its only audible renderer.
option(NEUMATON_BUILD_V3_AUDIO_RENDERER_TESTS
       "Build V3 Stage D exclusive audio validation executables" OFF)

if(NEUMATON_BUILD_V3_AUDIO_RENDERER_TESTS)
    add_executable(V3AudioRendererStandalone
        Tests/OutputV3AudioRendererStandalone.cpp
        Source/NeumatonRidgeLedger.cpp
        Source/NeumatonOutputRenderer.cpp
    )
    target_include_directories(V3AudioRendererStandalone PRIVATE Source)
    target_compile_features(V3AudioRendererStandalone PRIVATE cxx_std_17)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(V3AudioRendererStandalone PRIVATE
            -Wall -Wextra -Wpedantic -Werror -Wno-mismatched-new-delete)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_compile_options(V3AudioRendererStandalone PRIVATE
            -Wall -Wextra -Wpedantic -Werror)
    elseif(MSVC)
        target_compile_options(V3AudioRendererStandalone PRIVATE /W4 /WX)
    endif()

    juce_add_console_app(V3AudioRendererEngine
        PRODUCT_NAME V3AudioRendererEngine
    )
    juce_generate_juce_header(V3AudioRendererEngine)
    target_sources(V3AudioRendererEngine
        PRIVATE
            Tests/OutputV3AudioRendererEngine.cpp
            Source/ModernPitchEngine.cpp
        Source/ModernPitchOutputStage.cpp
            Source/NeumatonRidgeLedger.cpp
            Source/NeumatonOutputRenderer.cpp
            Source/ScaleLock.cpp
            Source/Tempo.cpp
    )
    target_include_directories(V3AudioRendererEngine PRIVATE Source)
    target_compile_definitions(V3AudioRendererEngine
        PRIVATE
            NEUMATON_V6_CSV_DIAGNOSTICS=0
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0
    )
    target_link_libraries(V3AudioRendererEngine
        PRIVATE
            juce::juce_audio_basics
            juce::juce_core
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags
    )
    set_target_properties(V3AudioRendererStandalone V3AudioRendererEngine PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/v3-audio-renderer-tests"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/v3-audio-renderer-tests"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/v3-audio-renderer-tests"
        RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/v3-audio-renderer-tests"
        RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}/v3-audio-renderer-tests"
    )
endif()
