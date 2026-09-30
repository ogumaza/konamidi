# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.20)
project(konamidi VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Link MSVC's runtime statically, so the released konamidi.exe runs without the runtime DLLs installed.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(KONAMIDI_BUILD_TESTS "Build and register the tests" ON)

add_library(konamidi_core STATIC
    src/convert.cpp
    src/driver.cpp
    src/inflate.cpp
    src/midi.cpp
    src/pitch.cpp
    src/rom.cpp
    src/seqformat.cpp
    src/sequencer.cpp
    src/sf2.cpp
    src/soundfont.cpp
)
target_include_directories(konamidi_core PUBLIC src)

# Disable fused multiply-add so output stays byte-identical across platforms. GCC and Clang can fuse a * b + c on
# CPUs such as ARM64, changing the rounding. MSVC's default /fp:precise disables this in Visual Studio 2022 and later;
# clang-cl needs an explicit option.
if(MSVC)
    target_compile_options(konamidi_core PUBLIC /W4 /utf-8)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        target_compile_options(konamidi_core PUBLIC /clang:-ffp-contract=off)
    endif()
else()
    target_compile_options(konamidi_core PUBLIC -Wall -Wextra -Wpedantic -ffp-contract=off)
endif()

add_executable(konamidi src/main.cpp)
target_link_libraries(konamidi PRIVATE konamidi_core)
if(WIN32)
    target_link_libraries(konamidi PRIVATE shell32)  # CommandLineToArgvW
    # Embed the version and description shown in Explorer's file properties.
    configure_file(platform/windows/version.rc.in version.rc @ONLY)
    target_sources(konamidi PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/version.rc")
endif()
target_compile_definitions(konamidi PRIVATE KONAMIDI_VERSION="${PROJECT_VERSION}")
include(GNUInstallDirs)
install(TARGETS konamidi RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(FILES LICENSE THIRD_PARTY_NOTICES DESTINATION "${CMAKE_INSTALL_DATADIR}/konamidi")

# macOS: a droplet app, so files can be dropped on konamidi in the Finder.
if(APPLE)
    find_program(OSACOMPILE osacompile)
    find_program(OSASCRIPT osascript)
    find_program(CODESIGN codesign)
    if(OSACOMPILE AND CODESIGN)
        set(KONAMIDI_APP "${CMAKE_CURRENT_BINARY_DIR}/Konamidi.app")
        set(KONAMIDI_APP_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/platform/macos/Konamidi.applescript")
        add_custom_command(
            OUTPUT "${KONAMIDI_APP}/Contents/Resources/konamidi"
            COMMAND "${CMAKE_COMMAND}" -E rm -rf "${KONAMIDI_APP}"
            COMMAND "${OSACOMPILE}" -o "${KONAMIDI_APP}" "${KONAMIDI_APP_SCRIPT}"
            COMMAND "${CMAKE_COMMAND}" -E copy "$<TARGET_FILE:konamidi>"
                    "${KONAMIDI_APP}/Contents/Resources/konamidi"
            COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
                    "${CMAKE_CURRENT_SOURCE_DIR}/THIRD_PARTY_NOTICES" "${KONAMIDI_APP}/Contents/Resources/"
            COMMAND "${CODESIGN}" --force --deep --sign - "${KONAMIDI_APP}"
            DEPENDS konamidi "${KONAMIDI_APP_SCRIPT}" LICENSE THIRD_PARTY_NOTICES
            COMMENT "Building Konamidi.app"
            VERBATIM)
        add_custom_target(konamidi_app ALL DEPENDS "${KONAMIDI_APP}/Contents/Resources/konamidi")
    endif()
endif()

if(KONAMIDI_BUILD_TESTS)
    enable_testing()
    add_executable(konamidi_tests tests/test_main.cpp)
    target_link_libraries(konamidi_tests PRIVATE konamidi_core)
    add_test(NAME konamidi_tests COMMAND konamidi_tests)

    # Test Konamidi.app's conversion report using a stand-in for konamidi.
    if(TARGET konamidi_app AND OSASCRIPT)
        add_test(NAME droplet
                 COMMAND "${OSASCRIPT}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/droplet_test.applescript"
                         "${KONAMIDI_APP}/Contents/Resources/Scripts/main.scpt"
                         "${CMAKE_CURRENT_SOURCE_DIR}/tests/fake_konamidi.sh")
    endif()

    # Load local_tests.cmake from each immediate subdirectory to register tests that use locally supplied files.
    file(GLOB local_tests CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*/local_tests.cmake")
    foreach(file IN LISTS local_tests)
        include("${file}")
    endforeach()
endif()
