// ShutterFX - plugin identity and parameter definitions (single source of truth).
#pragma once

// Version comes from CMake (project(VERSION ...)) so the binary, Info.plist and
// the OFX descriptor can never disagree.
#if !defined(SHUTTERFX_VERSION_MAJOR) || !defined(SHUTTERFX_VERSION_MINOR) || \
    !defined(SHUTTERFX_VERSION_PATCH) || !defined(SHUTTERFX_VERSION_STRING)
#error "SHUTTERFX_VERSION_* must be defined by the build system (see CMakeLists.txt)"
#endif

namespace shutterfx {

constexpr const char* kPluginIdentifier = "com.shutterfx.motionblur";
constexpr const char* kPluginLabel = "ShutterFX";
constexpr const char* kPluginGrouping = "ShutterFX";
constexpr const char* kPluginDescription =
    "ShutterFX - cinematic slow-shutter motion blur. "
    "Milestone 1: identity pass-through foundation (no blur is applied yet).";
constexpr const char* kPluginVersionString = SHUTTERFX_VERSION_STRING;

// Parameter script names (stable; saved in projects - do not rename).
constexpr const char* kParamPageMain = "main";
constexpr const char* kParamEnable = "enable";
constexpr const char* kParamMix = "mix";
constexpr const char* kParamGroupInfo = "information";
constexpr const char* kParamInfoName = "infoName";
constexpr const char* kParamInfoVersion = "infoVersion";

constexpr bool kDefaultEnable = true;
constexpr double kDefaultMixPercent = 100.0;
constexpr double kMixMinPercent = 0.0;
constexpr double kMixMaxPercent = 100.0;

} // namespace shutterfx
