cmake_minimum_required(VERSION 3.20)

# macOS settings must be chosen before project() so the toolchain picks them up.
# Deployment target 11.0 is the oldest macOS that runs arm64; it also covers
# Intel Macs on Big Sur and later. Override with -DCMAKE_OSX_DEPLOYMENT_TARGET=.
if(APPLE)
  if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
  endif()
  # Universal binary by default. Override with -DCMAKE_OSX_ARCHITECTURES=x86_64
  # (or arm64) for a single-architecture build.
  if(NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Build architectures")
  endif()
endif()

project(ShutterFX VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Debug or Release" FORCE)
endif()

option(SHUTTERFX_WERROR "Treat compiler warnings as errors" ON)
option(SHUTTERFX_BUILD_TESTS "Build the unit tests" ON)
option(SHUTTERFX_SANITIZE "Build with AddressSanitizer + UndefinedBehaviorSanitizer" OFF)
option(SHUTTERFX_ADHOC_SIGN "Ad-hoc codesign the bundle after building (macOS)" ON)

# ---------------------------------------------------------------------------
# OpenFX SDK (git submodule, pinned to OFX_Release_1_4_TAG)
# ---------------------------------------------------------------------------
set(OFX_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/external/openfx")
if(NOT EXISTS "${OFX_ROOT}/include/ofxCore.h")
  message(FATAL_ERROR
    "OpenFX headers not found at ${OFX_ROOT}/include.\n"
    "Fetch the SDK submodule:  git submodule update --init --recursive")
endif()

# ---------------------------------------------------------------------------
# Common compile options
# ---------------------------------------------------------------------------
function(shutterfx_apply_options target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 $<$<BOOL:${SHUTTERFX_WERROR}>:/WX>)
  else()
    target_compile_options(${target} PRIVATE
      -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-unknown-pragmas
      $<$<BOOL:${SHUTTERFX_WERROR}>:-Werror>)
    if(SHUTTERFX_SANITIZE)
      target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer -g)
      target_link_options(${target} PRIVATE -fsanitize=address,undefined)
    endif()
  endif()
endfunction()

# ---------------------------------------------------------------------------
# Host-independent core (no OpenFX dependency)
# ---------------------------------------------------------------------------
add_library(shutterfx_core STATIC src/utilities/ImageUtils.cpp)
target_include_directories(shutterfx_core PUBLIC src/utilities)
set_target_properties(shutterfx_core PROPERTIES POSITION_INDEPENDENT_CODE ON
  CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
shutterfx_apply_options(shutterfx_core)

# ---------------------------------------------------------------------------
# The OFX plugin binary: ShutterFX.ofx
# ---------------------------------------------------------------------------
add_library(ShutterFX MODULE
  src/plugin/ShutterFXFactory.cpp
  src/plugin/ShutterFXPlugin.cpp
  src/plugin/ShutterFXPlugin.h
  src/plugin/PluginParameters.h)
target_link_libraries(ShutterFX PRIVATE shutterfx_core)
target_include_directories(ShutterFX PRIVATE
  "${OFX_ROOT}/include" src/plugin src/utilities)
target_compile_definitions(ShutterFX PRIVATE
  SHUTTERFX_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
  SHUTTERFX_VERSION_MINOR=${PROJECT_VERSION_MINOR}
  SHUTTERFX_VERSION_PATCH=${PROJECT_VERSION_PATCH}
  "SHUTTERFX_VERSION_STRING=\"${PROJECT_VERSION}\"")
shutterfx_apply_options(ShutterFX)

# Layout (OFX spec, "ofxPackaging"): NAME.ofx.bundle/Contents/<ARCH>/NAME.ofx
# macOS universal binaries live in Contents/MacOS. The Linux layout exists only
# so the code can be compile-checked off-Mac; it is not a supported target.
if(APPLE)
  set(SHUTTERFX_ARCH_DIR "MacOS")
elseif(WIN32)
  set(SHUTTERFX_ARCH_DIR "Win64")
else()
  set(SHUTTERFX_ARCH_DIR "Linux-x86-64")
endif()

set(SHUTTERFX_BUNDLE "${CMAKE_BINARY_DIR}/$<CONFIG>/ShutterFX.ofx.bundle")
set_target_properties(ShutterFX PROPERTIES
  PREFIX ""
  OUTPUT_NAME "ShutterFX"
  SUFFIX ".ofx"
  LIBRARY_OUTPUT_DIRECTORY "${SHUTTERFX_BUNDLE}/Contents/${SHUTTERFX_ARCH_DIR}"
  CXX_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN ON)
if(APPLE)
  # Drop unreferenced code; exported symbols are governed by visibility.
  target_link_options(ShutterFX PRIVATE -Wl,-dead_strip)
endif()

# Info.plist (required on macOS; harmless elsewhere)
configure_file(resources/Info.plist.in "${CMAKE_BINARY_DIR}/Info.plist.generated" @ONLY)
file(GENERATE
  OUTPUT "${SHUTTERFX_BUNDLE}/Contents/Info.plist"
  INPUT "${CMAKE_BINARY_DIR}/Info.plist.generated")

if(APPLE AND SHUTTERFX_ADHOC_SIGN)
  # Apple Silicon refuses to load unsigned arm64 code. An ad-hoc signature is
  # sufficient for local use; distribution would need a Developer ID.
  add_custom_command(TARGET ShutterFX POST_BUILD
    COMMAND codesign --force --sign - --timestamp=none "${SHUTTERFX_BUNDLE}"
    COMMENT "Ad-hoc signing ShutterFX.ofx.bundle"
    VERBATIM)
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(SHUTTERFX_BUILD_TESTS)
  enable_testing()
  add_executable(test_image_utils tests/test_image_utils.cpp)
  target_link_libraries(test_image_utils PRIVATE shutterfx_core)
  shutterfx_apply_options(test_image_utils)
  add_test(NAME image_utils COMMAND test_image_utils)

  # Exercises the real OFX entry points against a mock host (no Resolve needed).
  add_executable(test_ofx_entry tests/test_ofx_entry.cpp
    src/plugin/ShutterFXFactory.cpp src/plugin/ShutterFXPlugin.cpp)
  target_include_directories(test_ofx_entry PRIVATE
    "${OFX_ROOT}/include" src/plugin src/utilities)
  target_compile_definitions(test_ofx_entry PRIVATE
    SHUTTERFX_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    SHUTTERFX_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    SHUTTERFX_VERSION_PATCH=${PROJECT_VERSION_PATCH}
    "SHUTTERFX_VERSION_STRING=\"${PROJECT_VERSION}\"")
  target_link_libraries(test_ofx_entry PRIVATE shutterfx_core)
  shutterfx_apply_options(test_ofx_entry)
  add_test(NAME ofx_entry COMMAND test_ofx_entry)
endif()
