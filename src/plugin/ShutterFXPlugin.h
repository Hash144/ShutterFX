// ShutterFX - per-instance plugin logic (OFX actions that act on an instance).
#pragma once

#include "ofxCore.h"
#include "ofxImageEffect.h"
#include "ofxParam.h"
#include "ofxProperty.h"

namespace shutterfx {

// Suites fetched from the host in the Load action.
struct HostSuites {
    const OfxPropertySuiteV1* property = nullptr;
    const OfxImageEffectSuiteV1* effect = nullptr;
    const OfxParameterSuiteV1* parameter = nullptr;

    bool complete() const { return property && effect && parameter; }
};

// One ShutterFXInstance exists per effect instance in the host. It owns no
// OFX image resources between actions: images are acquired and released inside
// render().
class ShutterFXInstance {
public:
    ShutterFXInstance(const HostSuites& suites, OfxImageEffectHandle effect);

    // Resolves clip and parameter handles. Returns false if the host did not
    // provide what describeInContext declared.
    bool bind();

    OfxStatus render(OfxPropertySetHandle inArgs);
    OfxStatus isIdentity(OfxPropertySetHandle inArgs, OfxPropertySetHandle outArgs);

private:
    bool readParams(OfxTime time, bool& enable, double& mixPercent) const;

    HostSuites suites_;
    OfxImageEffectHandle effect_;
    OfxImageClipHandle sourceClip_ = nullptr;
    OfxImageClipHandle outputClip_ = nullptr;
    OfxParamHandle enableParam_ = nullptr;
    OfxParamHandle mixParam_ = nullptr;
};

// Describe the plugin / its context. Implemented in ShutterFXPlugin.cpp.
OfxStatus describe(const HostSuites& suites, OfxImageEffectHandle effect);
OfxStatus describeInContext(const HostSuites& suites, OfxImageEffectHandle effect,
                            OfxPropertySetHandle inArgs);

} // namespace shutterfx
