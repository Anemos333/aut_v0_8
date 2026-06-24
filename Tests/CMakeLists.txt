cmake_minimum_required(VERSION 3.22)
project(AutotuneStandaloneRegression LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(AUTOTUNE_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
set(AUTOTUNE_CORPUS_DIR "" CACHE PATH "Optional directory containing WAV files for CorpusRegression")

set(AUTOTUNE_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../Source")
set(AUTOTUNE_TEST_STUB_DIR "${CMAKE_CURRENT_LIST_DIR}/test_stubs")

function(add_autotune_core_test target source)
    add_executable(${target}
        "${CMAKE_CURRENT_LIST_DIR}/${source}"
        "${AUTOTUNE_SOURCE_DIR}/ModernPitchEngine.cpp"
        "${AUTOTUNE_SOURCE_DIR}/Tempo.cpp")
    target_include_directories(${target} PRIVATE
        "${AUTOTUNE_TEST_STUB_DIR}"
        "${AUTOTUNE_SOURCE_DIR}")
    if (MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

add_autotune_core_test(AutotuneCommercialRegression CommercialRegression.cpp)
add_autotune_core_test(AutotuneSanitizerSmoke SanitizerSmoke.cpp)
add_autotune_core_test(AutotuneCorpusRegression CorpusRegression.cpp)
add_autotune_core_test(AutotuneBaselineProbe BaselineProbe.cpp)
add_autotune_core_test(AutotuneLowRegisterProbe LowRegisterProbe.cpp)

add_executable(AutotuneScaleTempoStrict ScaleTempoStrictTest.cpp
    "${AUTOTUNE_SOURCE_DIR}/ModernPitchEngine.cpp"
    "${AUTOTUNE_SOURCE_DIR}/Tempo.cpp"
    "${AUTOTUNE_SOURCE_DIR}/ScaleDefinitions.cpp")
target_include_directories(AutotuneScaleTempoStrict PRIVATE
    "${AUTOTUNE_TEST_STUB_DIR}"
    "${AUTOTUNE_SOURCE_DIR}")
if (MSVC)
    target_compile_options(AutotuneScaleTempoStrict PRIVATE /W4 /permissive-)
else()
    target_compile_options(AutotuneScaleTempoStrict PRIVATE -Wall -Wextra -Wpedantic)
endif()

if (AUTOTUNE_ENABLE_SANITIZERS AND NOT MSVC)
    foreach(target AutotuneCommercialRegression AutotuneSanitizerSmoke)
        target_compile_options(${target} PRIVATE -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined)
        target_compile_definitions(${target} PRIVATE AUTOTUNE_SANITIZER_BUILD=1)
        target_link_options(${target} PRIVATE -fsanitize=address,undefined)
    endforeach()
endif()

enable_testing()
add_test(NAME CommercialRegression
    COMMAND AutotuneCommercialRegression "${CMAKE_BINARY_DIR}/commercial_regression.csv")
add_test(NAME SanitizerSmoke COMMAND AutotuneSanitizerSmoke)
add_test(NAME ScaleTempoStrict COMMAND AutotuneScaleTempoStrict)

if (AUTOTUNE_CORPUS_DIR AND EXISTS "${AUTOTUNE_CORPUS_DIR}")
    file(GLOB AUTOTUNE_CORPUS_WAVS CONFIGURE_DEPENDS "${AUTOTUNE_CORPUS_DIR}/*.wav")
    list(FILTER AUTOTUNE_CORPUS_WAVS EXCLUDE REGEX "/\\._[^/]*\\.wav$")
    if (AUTOTUNE_CORPUS_WAVS)
        add_test(NAME CorpusRegression
            COMMAND AutotuneCorpusRegression
                    "${CMAKE_BINARY_DIR}/corpus_regression.csv"
                    ${AUTOTUNE_CORPUS_WAVS})
    endif()
endif()
