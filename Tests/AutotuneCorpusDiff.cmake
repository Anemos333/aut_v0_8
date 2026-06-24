option(AUTOTUNE_BUILD_CORPUS_DIFF "Build exhaustive corpus wet/dry and wet/wet diff runner" ON)

if (BUILD_TESTING AND AUTOTUNE_BUILD_TESTS AND AUTOTUNE_BUILD_CORPUS_DIFF)
    juce_add_console_app(AutotuneCorpusDiff
        PRODUCT_NAME "Autotune Corpus Diff")

    juce_generate_juce_header(AutotuneCorpusDiff)

    target_sources(AutotuneCorpusDiff PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/CorpusAudioDiffRunner.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../Source/ModernPitchEngine.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../Source/ScaleDefinitions.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../Source/Tempo.cpp)

    target_include_directories(AutotuneCorpusDiff PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/../Source)

    target_link_libraries(AutotuneCorpusDiff PRIVATE
        juce::juce_audio_basics
        juce::juce_audio_formats
        juce::juce_core
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

    target_compile_definitions(AutotuneCorpusDiff PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0)
endif()
