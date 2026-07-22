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
# V3 Stage B validation targets. These are opt-in and never become part of the
# plug-in binary. The two executables compile the same production engine with
# the shadow call removed/present, then CI compares their raw audio byte-for-byte.
option(NEUMATON_BUILD_SHADOW_LEDGER_TESTS
       "Build V3 Stage B shadow-ledger validation executables" OFF)

if(NEUMATON_BUILD_SHADOW_LEDGER_TESTS)
    function(neumaton_add_shadow_identity_target target_name shadow_enabled)
        juce_add_console_app(${target_name}
            PRODUCT_NAME ${target_name}
        )
        juce_generate_juce_header(${target_name})
        target_sources(${target_name}
            PRIVATE
                Tests/OutputV3ShadowLedgerIdentity.cpp
                Source/ModernPitchEngine.cpp
                Source/NeumatonRidgeLedger.cpp
                Source/ScaleLock.cpp
                Source/Tempo.cpp
        )
        target_include_directories(${target_name} PRIVATE Source)
        target_compile_definitions(${target_name}
            PRIVATE
                NEUMATON_OUTPUT_V3_SHADOW_LEDGER=${shadow_enabled}
                NEUMATON_V6_CSV_DIAGNOSTICS=0
                JUCE_WEB_BROWSER=0
                JUCE_USE_CURL=0
        )
        target_link_libraries(${target_name}
            PRIVATE
                juce::juce_audio_basics
                juce::juce_core
            PUBLIC
                juce::juce_recommended_config_flags
                juce::juce_recommended_warning_flags
        )
        set_target_properties(${target_name} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/shadow-ledger-tests"
            RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/shadow-ledger-tests"
            RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/shadow-ledger-tests"
            RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/shadow-ledger-tests"
            RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}/shadow-ledger-tests"
        )
    endfunction()

    neumaton_add_shadow_identity_target(ShadowLedgerIdentityOff 0)
    neumaton_add_shadow_identity_target(ShadowLedgerIdentityOn 1)
endif()
