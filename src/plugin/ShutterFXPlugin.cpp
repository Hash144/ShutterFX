#include "ShutterFXPlugin.h"

#include "PluginParameters.h"
#include "../utilities/ImageUtils.h"

#include <cstring>

namespace shutterfx {

namespace {

// ---------------------------------------------------------------------------
// RAII wrapper: guarantees clipReleaseImage() runs on every exit path.
// ---------------------------------------------------------------------------
class ClipImage {
public:
    ClipImage(const HostSuites& s, OfxImageClipHandle clip, OfxTime time) : suites_(s) {
        if (clip && suites_.effect->clipGetImage(clip, time, nullptr, &props_) != kOfxStatOK)
            props_ = nullptr;
    }
    ~ClipImage() {
        if (props_) suites_.effect->clipReleaseImage(props_);
    }
    ClipImage(const ClipImage&) = delete;
    ClipImage& operator=(const ClipImage&) = delete;

    explicit operator bool() const { return props_ != nullptr; }
    OfxPropertySetHandle props() const { return props_; }

private:
    const HostSuites& suites_;
    OfxPropertySetHandle props_ = nullptr;
};

// Reads an OFX image's properties into an ImageView. Returns kOfxStatOK or an
// error status; on error `view` must not be used.
OfxStatus readImage(const HostSuites& s, OfxPropertySetHandle img, ImageView& view) {
    const OfxPropertySuiteV1* p = s.property;

    char* depthStr = nullptr;
    char* compStr = nullptr;
    int bounds[4] = {0, 0, 0, 0};
    int rowBytes = 0;
    void* data = nullptr;

    if (p->propGetString(img, kOfxImageEffectPropPixelDepth, 0, &depthStr) != kOfxStatOK ||
        p->propGetString(img, kOfxImageEffectPropComponents, 0, &compStr) != kOfxStatOK ||
        p->propGetIntN(img, kOfxImagePropBounds, 4, bounds) != kOfxStatOK ||
        p->propGetInt(img, kOfxImagePropRowBytes, 0, &rowBytes) != kOfxStatOK ||
        p->propGetPointer(img, kOfxImagePropData, 0, &data) != kOfxStatOK)
        return kOfxStatErrBadHandle;

    if (!depthStr || !compStr || !data) return kOfxStatErrImageFormat;
    if (std::strcmp(compStr, kOfxImageComponentRGBA) != 0) return kOfxStatErrImageFormat;

    if (std::strcmp(depthStr, kOfxBitDepthByte) == 0) view.depth = PixelDepth::Byte;
    else if (std::strcmp(depthStr, kOfxBitDepthShort) == 0) view.depth = PixelDepth::Short;
    else if (std::strcmp(depthStr, kOfxBitDepthFloat) == 0) view.depth = PixelDepth::Float;
    else return kOfxStatErrImageFormat;   // e.g. half float: not declared, refuse safely

    view.data = data;
    view.bounds = Rect{bounds[0], bounds[1], bounds[2], bounds[3]};
    view.rowBytes = static_cast<std::ptrdiff_t>(rowBytes);
    return isValid(view) ? kOfxStatOK : kOfxStatErrImageFormat;
}

} // namespace

// ---------------------------------------------------------------------------
// Instance
// ---------------------------------------------------------------------------
ShutterFXInstance::ShutterFXInstance(const HostSuites& suites, OfxImageEffectHandle effect)
    : suites_(suites), effect_(effect) {}

bool ShutterFXInstance::bind() {
    OfxParamSetHandle paramSet = nullptr;
    if (suites_.effect->getParamSet(effect_, &paramSet) != kOfxStatOK) return false;

    if (suites_.parameter->paramGetHandle(paramSet, kParamEnable, &enableParam_, nullptr) != kOfxStatOK ||
        suites_.parameter->paramGetHandle(paramSet, kParamMix, &mixParam_, nullptr) != kOfxStatOK)
        return false;

    if (suites_.effect->clipGetHandle(effect_, kOfxImageEffectSimpleSourceClipName, &sourceClip_, nullptr) != kOfxStatOK ||
        suites_.effect->clipGetHandle(effect_, kOfxImageEffectOutputClipName, &outputClip_, nullptr) != kOfxStatOK)
        return false;
    return true;
}

bool ShutterFXInstance::readParams(OfxTime time, bool& enable, double& mixPercent) const {
    int e = 1;
    double m = kDefaultMixPercent;
    if (suites_.parameter->paramGetValueAtTime(enableParam_, time, &e) != kOfxStatOK ||
        suites_.parameter->paramGetValueAtTime(mixParam_, time, &m) != kOfxStatOK)
        return false;
    enable = (e != 0);
    mixPercent = m;
    return true;
}

OfxStatus ShutterFXInstance::isIdentity(OfxPropertySetHandle inArgs, OfxPropertySetHandle outArgs) {
    OfxTime time = 0;
    if (suites_.property->propGetDouble(inArgs, kOfxPropTime, 0, &time) != kOfxStatOK)
        return kOfxStatReplyDefault;
    bool enable = true;
    double mix = kDefaultMixPercent;
    if (!readParams(time, enable, mix)) return kOfxStatReplyDefault;

    // Only report identity when the effect is bypassed. While enabled the
    // render action always runs, so the identity engine is genuinely exercised.
    if (!enable || mix <= 0.0) {
        suites_.property->propSetString(outArgs, kOfxPropName, 0, kOfxImageEffectSimpleSourceClipName);
        suites_.property->propSetDouble(outArgs, kOfxPropTime, 0, time);
        return kOfxStatOK;
    }
    return kOfxStatReplyDefault;
}

OfxStatus ShutterFXInstance::render(OfxPropertySetHandle inArgs) {
    OfxTime time = 0;
    int win[4] = {0, 0, 0, 0};
    if (suites_.property->propGetDouble(inArgs, kOfxPropTime, 0, &time) != kOfxStatOK ||
        suites_.property->propGetIntN(inArgs, kOfxImageEffectPropRenderWindow, 4, win) != kOfxStatOK)
        return kOfxStatErrBadHandle;

    bool enable = true;
    double mixPercent = kDefaultMixPercent;
    if (!readParams(time, enable, mixPercent)) return kOfxStatErrBadHandle;

    // Acquire both images; the RAII wrappers release them on every return path.
    ClipImage src(suites_, sourceClip_, time);
    ClipImage dst(suites_, outputClip_, time);
    if (!dst) return kOfxStatFailed;        // nowhere to write
    if (!src) return kOfxStatFailed;        // required input missing

    ImageView srcView, dstView;
    OfxStatus st = readImage(suites_, src.props(), srcView);
    if (st != kOfxStatOK) return st;
    st = readImage(suites_, dst.props(), dstView);
    if (st != kOfxStatOK) return st;
    if (srcView.depth != dstView.depth) return kOfxStatErrImageFormat;

    // Never write outside the destination buffer, whatever the host asks for.
    const Rect window = intersect(Rect{win[0], win[1], win[2], win[3]}, dstView.bounds);
    if (window.empty()) return kOfxStatOK;

    // Identity engine: the "processed" image is the source itself. Mix blends
    // original and processed, so the output equals the source for every Mix.
    Result r;
    if (!enable) r = copyRegion(srcView, dstView, window);
    else r = mixRegion(srcView, srcView, dstView, window, mixPercent / 100.0);

    switch (r) {
        case Result::Ok: return kOfxStatOK;
        case Result::DepthMismatch:
        case Result::InvalidImage: return kOfxStatErrImageFormat;
        case Result::WindowOutside: return kOfxStatFailed;
    }
    return kOfxStatFailed;
}

// ---------------------------------------------------------------------------
// Describe / DescribeInContext
// ---------------------------------------------------------------------------
OfxStatus describe(const HostSuites& s, OfxImageEffectHandle effect) {
    OfxPropertySetHandle props = nullptr;
    if (s.effect->getPropertySet(effect, &props) != kOfxStatOK) return kOfxStatErrBadHandle;
    const OfxPropertySuiteV1* p = s.property;

    p->propSetString(props, kOfxPropLabel, 0, kPluginLabel);
    p->propSetString(props, kOfxPropShortLabel, 0, kPluginLabel);
    p->propSetString(props, kOfxPropLongLabel, 0, kPluginLabel);
    p->propSetString(props, kOfxImageEffectPluginPropGrouping, 0, kPluginGrouping);
    p->propSetString(props, kOfxPropPluginDescription, 0, kPluginDescription);

    p->propSetString(props, kOfxImageEffectPropSupportedContexts, 0, kOfxImageEffectContextFilter);
    p->propSetString(props, kOfxImageEffectPropSupportedContexts, 1, kOfxImageEffectContextGeneral);

    p->propSetString(props, kOfxImageEffectPropSupportedPixelDepths, 0, kOfxBitDepthByte);
    p->propSetString(props, kOfxImageEffectPropSupportedPixelDepths, 1, kOfxBitDepthShort);
    p->propSetString(props, kOfxImageEffectPropSupportedPixelDepths, 2, kOfxBitDepthFloat);

    // Source and output must share a depth; this plugin never converts.
    p->propSetInt(props, kOfxImageEffectPropSupportsMultipleClipDepths, 0, 0);
    // The render code works on any sub-window and any image origin.
    p->propSetInt(props, kOfxImageEffectPropSupportsTiles, 0, 1);
    p->propSetInt(props, kOfxImageEffectPropSupportsMultiResolution, 0, 1);
    // No temporal access yet (reserved for later milestones).
    p->propSetInt(props, kOfxImageEffectPropTemporalClipAccess, 0, 0);
    // Stateless render: instances can render concurrently.
    p->propSetString(props, kOfxImageEffectPluginRenderThreadSafety, 0, kOfxImageEffectRenderFullySafe);
    return kOfxStatOK;
}

OfxStatus describeInContext(const HostSuites& s, OfxImageEffectHandle effect, OfxPropertySetHandle) {
    const OfxPropertySuiteV1* p = s.property;
    OfxPropertySetHandle clipProps = nullptr;

    // Output clip.
    if (s.effect->clipDefine(effect, kOfxImageEffectOutputClipName, &clipProps) != kOfxStatOK)
        return kOfxStatFailed;
    p->propSetString(clipProps, kOfxImageEffectPropSupportedComponents, 0, kOfxImageComponentRGBA);

    // Source clip (required input).
    if (s.effect->clipDefine(effect, kOfxImageEffectSimpleSourceClipName, &clipProps) != kOfxStatOK)
        return kOfxStatFailed;
    p->propSetString(clipProps, kOfxImageEffectPropSupportedComponents, 0, kOfxImageComponentRGBA);
    p->propSetInt(clipProps, kOfxImageClipPropOptional, 0, 0);

    OfxParamSetHandle paramSet = nullptr;
    if (s.effect->getParamSet(effect, &paramSet) != kOfxStatOK) return kOfxStatFailed;
    const OfxParameterSuiteV1* ps = s.parameter;
    OfxPropertySetHandle pp = nullptr;

    // Enable
    if (ps->paramDefine(paramSet, kOfxParamTypeBoolean, kParamEnable, &pp) != kOfxStatOK)
        return kOfxStatFailed;
    p->propSetString(pp, kOfxPropLabel, 0, "Enable");
    p->propSetString(pp, kOfxParamPropHint, 0, "Turn the effect on or off. Off passes the source through untouched.");
    p->propSetInt(pp, kOfxParamPropDefault, 0, kDefaultEnable ? 1 : 0);

    // Mix (percent)
    if (ps->paramDefine(paramSet, kOfxParamTypeDouble, kParamMix, &pp) != kOfxStatOK)
        return kOfxStatFailed;
    p->propSetString(pp, kOfxPropLabel, 0, "Mix (%)");
    p->propSetString(pp, kOfxParamPropHint, 0, "Blend between the original image (0) and the processed image (100).");
    p->propSetString(pp, kOfxParamPropDoubleType, 0, kOfxParamDoubleTypePlain);
    p->propSetDouble(pp, kOfxParamPropDefault, 0, kDefaultMixPercent);
    p->propSetDouble(pp, kOfxParamPropMin, 0, kMixMinPercent);
    p->propSetDouble(pp, kOfxParamPropMax, 0, kMixMaxPercent);
    p->propSetDouble(pp, kOfxParamPropDisplayMin, 0, kMixMinPercent);
    p->propSetDouble(pp, kOfxParamPropDisplayMax, 0, kMixMaxPercent);
    p->propSetDouble(pp, kOfxParamPropIncrement, 0, 1.0);
    p->propSetInt(pp, kOfxParamPropDigits, 0, 1);

    // Information group (read-only labels).
    if (ps->paramDefine(paramSet, kOfxParamTypeGroup, kParamGroupInfo, &pp) != kOfxStatOK)
        return kOfxStatFailed;
    p->propSetString(pp, kOfxPropLabel, 0, "Information");
    p->propSetInt(pp, kOfxParamPropGroupOpen, 0, 0);

    struct InfoRow { const char* name; const char* label; const char* value; };
    const InfoRow rows[] = {
        {kParamInfoName, "Plugin", kPluginLabel},
        {kParamInfoVersion, "Version", kPluginVersionString},
    };
    for (const InfoRow& row : rows) {
        if (ps->paramDefine(paramSet, kOfxParamTypeString, row.name, &pp) != kOfxStatOK)
            return kOfxStatFailed;
        p->propSetString(pp, kOfxPropLabel, 0, row.label);
        p->propSetString(pp, kOfxParamPropStringMode, 0, kOfxParamStringIsLabel);
        p->propSetString(pp, kOfxParamPropDefault, 0, row.value);
        p->propSetString(pp, kOfxParamPropParent, 0, kParamGroupInfo);
        p->propSetInt(pp, kOfxParamPropEnabled, 0, 0);
        p->propSetInt(pp, kOfxParamPropAnimates, 0, 0);
        p->propSetInt(pp, kOfxParamPropPersistant, 0, 0);
        p->propSetInt(pp, kOfxParamPropEvaluateOnChange, 0, 0);
        p->propSetInt(pp, kOfxParamPropCanUndo, 0, 0);
    }

    // Single page listing the controls in display order.
    if (ps->paramDefine(paramSet, kOfxParamTypePage, kParamPageMain, &pp) != kOfxStatOK)
        return kOfxStatFailed;
    p->propSetString(pp, kOfxPropLabel, 0, "Main");
    p->propSetString(pp, kOfxParamPropPageChild, 0, kParamEnable);
    p->propSetString(pp, kOfxParamPropPageChild, 1, kParamMix);
    p->propSetString(pp, kOfxParamPropPageChild, 2, kParamGroupInfo);
    return kOfxStatOK;
}

} // namespace shutterfx
