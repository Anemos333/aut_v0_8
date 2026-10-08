cmake_minimum_required(VERSION 3.22)
project(NeumatonPresetV1Tests LANGUAGES C CXX)

if(NOT TARGET Neumaton)
    add_subdirectory(../.. plugin)
endif()

# Separate test configuration: the production CMake target remains unchanged.
target_compile_definitions(Neumaton PRIVATE JUCE_MODAL_LOOPS_PERMITTED=1)
add_executable(FactoryPresetV1Test ../FactoryPresetV1Test.cpp)
target_compile_features(FactoryPresetV1Test PRIVATE cxx_std_17)
target_include_directories(FactoryPresetV1Test PRIVATE
    ../../Source
    $<TARGET_PROPERTY:Neumaton,JUCE_GENERATED_SOURCES_DIRECTORY>
    $<TARGET_PROPERTY:BinaryData,INTERFACE_INCLUDE_DIRECTORIES>
    $<TARGET_PROPERTY:juce::juce_core,INTERFACE_INCLUDE_DIRECTORIES>)
target_compile_definitions(FactoryPresetV1Test PRIVATE
    JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 JUCE_MODAL_LOOPS_PERMITTED=1)
target_link_libraries(FactoryPresetV1Test PRIVATE Neumaton)
