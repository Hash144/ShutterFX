// ShutterFX - OFX plugin factory and main entry point.
//
// This is the only translation unit that exports symbols from the bundle:
// OfxGetNumberOfPlugins and OfxGetPlugin, as required by the OFX specification.
#include "PluginParameters.h"
#include "ShutterFXPlugin.h"

#include "ofxCore.h"
#include "ofxImageEffect.h"

#include <exception>
#include <new>
#include <cstring>

// OfxExport expands to a plain `extern` on non-Windows hosts, which would be
// hidden by -fvisibility=hidden, so export the two entry points explicitly.
#if defined(_WIN32)
#define SHUTTERFX_EXPORT __declspec(dllexport)
#else
#define SHUTTERFX_EXPORT __attribute__((visibility("default")))
#endif

namespace {

using shutterfx::HostSuites;
using shutterfx::ShutterFXInstance;

OfxHost* g_host = nullptr;
HostSuites g_suites;

void setHostFunc(OfxHost* host) { g_host = host; }

OfxStatus fetchSuites() {
    if (!g_host || !g_host->fetchSuite) return kOfxStatErrMissingHostFeature;
    g_suites.property = static_cast<const OfxPropertySuiteV1*>(
        g_host->fetchSuite(g_host->host, kOfxPropertySuite, 1));
    g_suites.effect = static_cast<const OfxImageEffectSuiteV1*>(
        g_host->fetchSuite(g_host->host, kOfxImageEffectSuite, 1));
    g_suites.parameter = static_cast<const OfxParameterSuiteV1*>(
        g_host->fetchSuite(g_host->host, kOfxParameterSuite, 1));
    return g_suites.complete() ? kOfxStatOK : kOfxStatErrMissingHostFeature;
}

ShutterFXInstance* instanceFrom(OfxImageEffectHandle effect) {
    OfxPropertySetHandle props = nullptr;
    if (g_suites.effect->getPropertySet(effect, &props) != kOfxStatOK) return nullptr;
    void* data = nullptr;
    if (g_suites.property->propGetPointer(props, kOfxPropInstanceData, 0, &data) != kOfxStatOK)
        return nullptr;
    return static_cast<ShutterFXInstance*>(data);
}

OfxStatus createInstance(OfxImageEffectHandle effect) {
    OfxPropertySetHandle props = nullptr;
    if (g_suites.effect->getPropertySet(effect, &props) != kOfxStatOK) return kOfxStatErrBadHandle;

    auto* inst = new (std::nothrow) ShutterFXInstance(g_suites, effect);
    if (!inst) return kOfxStatErrMemory;
    if (!inst->bind()) {
        delete inst;
        return kOfxStatFailed;
    }
    if (g_suites.property->propSetPointer(props, kOfxPropInstanceData, 0, inst) != kOfxStatOK) {
        delete inst;
        return kOfxStatFailed;
    }
    return kOfxStatOK;
}

OfxStatus destroyInstance(OfxImageEffectHandle effect) {
    OfxPropertySetHandle props = nullptr;
    if (g_suites.effect->getPropertySet(effect, &props) != kOfxStatOK) return kOfxStatErrBadHandle;
    delete instanceFrom(effect);
    g_suites.property->propSetPointer(props, kOfxPropInstanceData, 0, nullptr);
    return kOfxStatOK;
}

OfxStatus pluginMain(const char* action, const void* handle,
                     OfxPropertySetHandle inArgs, OfxPropertySetHandle outArgs) {
    // Exceptions must never cross the C boundary into the host.
    try {
        if (!action) return kOfxStatErrBadHandle;
        auto effect = reinterpret_cast<OfxImageEffectHandle>(const_cast<void*>(handle));

        if (std::strcmp(action, kOfxActionLoad) == 0) return fetchSuites();
        if (std::strcmp(action, kOfxActionUnload) == 0) {
            g_suites = HostSuites{};
            return kOfxStatOK;
        }

        // Everything below needs the suites; refuse safely if Load never ran.
        if (!g_suites.complete()) return kOfxStatErrMissingHostFeature;

        if (std::strcmp(action, kOfxActionDescribe) == 0)
            return shutterfx::describe(g_suites, effect);
        if (std::strcmp(action, kOfxImageEffectActionDescribeInContext) == 0)
            return shutterfx::describeInContext(g_suites, effect, inArgs);
        if (std::strcmp(action, kOfxActionCreateInstance) == 0) return createInstance(effect);
        if (std::strcmp(action, kOfxActionDestroyInstance) == 0) return destroyInstance(effect);

        if (std::strcmp(action, kOfxImageEffectActionRender) == 0) {
            ShutterFXInstance* inst = instanceFrom(effect);
            return inst ? inst->render(inArgs) : kOfxStatErrBadHandle;
        }
        if (std::strcmp(action, kOfxImageEffectActionIsIdentity) == 0) {
            ShutterFXInstance* inst = instanceFrom(effect);
            return inst ? inst->isIdentity(inArgs, outArgs) : kOfxStatReplyDefault;
        }
    } catch (const std::bad_alloc&) {
        return kOfxStatErrMemory;
    } catch (const std::exception&) {
        return kOfxStatFailed;
    } catch (...) {
        return kOfxStatFailed;
    }
    // Actions we do not implement get the host's default behaviour.
    return kOfxStatReplyDefault;
}

OfxPlugin g_plugin = {
    kOfxImageEffectPluginApi,
    1,                                   // image effect API version 1.x
    shutterfx::kPluginIdentifier,
    SHUTTERFX_VERSION_MAJOR,
    SHUTTERFX_VERSION_MINOR,
    setHostFunc,
    pluginMain,
};

} // namespace

extern "C" {

SHUTTERFX_EXPORT int OfxGetNumberOfPlugins(void) { return 1; }

SHUTTERFX_EXPORT OfxPlugin* OfxGetPlugin(int nth) { return nth == 0 ? &g_plugin : nullptr; }

} // extern "C"
