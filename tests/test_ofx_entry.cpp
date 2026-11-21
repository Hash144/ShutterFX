// Drives the real plugin entry points (OfxGetPlugin -> mainEntry) against a
// minimal in-process mock OFX host. This validates our use of the OFX API
// (property names, suite calls, image acquire/release pairing, status codes).
// It is NOT a substitute for testing in DaVinci Resolve.
#include "ofxCore.h"
#include "ofxImageEffect.h"
#include "ofxParam.h"
#include "ofxProperty.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

extern "C" {
int OfxGetNumberOfPlugins(void);
OfxPlugin* OfxGetPlugin(int nth);
}

static int g_checks = 0, g_failures = 0;
#define CHECK(cond)                                                       \
    do {                                                                  \
        ++g_checks;                                                       \
        if (!(cond)) {                                                    \
            ++g_failures;                                                 \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);   \
        }                                                                 \
    } while (0)

namespace {

// ---- property sets --------------------------------------------------------
struct Value { int i = 0; double d = 0; std::string s; void* p = nullptr; };
struct PropSet { std::map<std::string, std::vector<Value>> props; };

PropSet* ps(OfxPropertySetHandle h) { return reinterpret_cast<PropSet*>(h); }
Value* slot(OfxPropertySetHandle h, const char* n, int idx, bool create) {
    if (!h || idx < 0) return nullptr;
    auto it = ps(h)->props.find(n);
    if (it == ps(h)->props.end()) {
        if (!create) return nullptr;
        it = ps(h)->props.emplace(n, std::vector<Value>()).first;
    }
    if (static_cast<int>(it->second.size()) <= idx) {
        if (!create) return nullptr;
        it->second.resize(static_cast<std::size_t>(idx) + 1);
    }
    return &it->second[static_cast<std::size_t>(idx)];
}

OfxStatus setP(OfxPropertySetHandle h, const char* n, int i, void* v) { auto* s = slot(h, n, i, true); if (!s) return kOfxStatErrBadHandle; s->p = v; return kOfxStatOK; }
OfxStatus setS(OfxPropertySetHandle h, const char* n, int i, const char* v) { auto* s = slot(h, n, i, true); if (!s) return kOfxStatErrBadHandle; s->s = v; return kOfxStatOK; }
OfxStatus setD(OfxPropertySetHandle h, const char* n, int i, double v) { auto* s = slot(h, n, i, true); if (!s) return kOfxStatErrBadHandle; s->d = v; return kOfxStatOK; }
OfxStatus setI(OfxPropertySetHandle h, const char* n, int i, int v) { auto* s = slot(h, n, i, true); if (!s) return kOfxStatErrBadHandle; s->i = v; return kOfxStatOK; }
OfxStatus getP(OfxPropertySetHandle h, const char* n, int i, void** v) { auto* s = slot(h, n, i, false); if (!s) return kOfxStatErrUnknown; *v = s->p; return kOfxStatOK; }
OfxStatus getS(OfxPropertySetHandle h, const char* n, int i, char** v) { auto* s = slot(h, n, i, false); if (!s) return kOfxStatErrUnknown; *v = const_cast<char*>(s->s.c_str()); return kOfxStatOK; }
OfxStatus getD(OfxPropertySetHandle h, const char* n, int i, double* v) { auto* s = slot(h, n, i, false); if (!s) return kOfxStatErrUnknown; *v = s->d; return kOfxStatOK; }
OfxStatus getI(OfxPropertySetHandle h, const char* n, int i, int* v) { auto* s = slot(h, n, i, false); if (!s) return kOfxStatErrUnknown; *v = s->i; return kOfxStatOK; }
OfxStatus getIN(OfxPropertySetHandle h, const char* n, int c, int* v) { for (int k = 0; k < c; ++k) { auto* s = slot(h, n, k, false); if (!s) return kOfxStatErrUnknown; v[k] = s->i; } return kOfxStatOK; }
OfxStatus setIN(OfxPropertySetHandle h, const char* n, int c, const int* v) { for (int k = 0; k < c; ++k) setI(h, n, k, v[k]); return kOfxStatOK; }
OfxStatus dimension(OfxPropertySetHandle h, const char* n, int* c) { auto it = ps(h)->props.find(n); *c = it == ps(h)->props.end() ? 0 : static_cast<int>(it->second.size()); return kOfxStatOK; }
OfxStatus reset(OfxPropertySetHandle h, const char* n) { ps(h)->props.erase(n); return kOfxStatOK; }

OfxPropertySuiteV1 g_propSuite;

// ---- effect / params / clips ---------------------------------------------
struct Param { std::string type; PropSet props; bool valueSet = false; int iv = 0; double dv = 0; };
struct Clip { PropSet props; };
struct Effect {
    PropSet props;
    std::map<std::string, std::unique_ptr<Param>> params;
    std::map<std::string, std::unique_ptr<Clip>> clips;
    std::vector<std::string> paramOrder;
    // Images the host will hand out for clipGetImage (by clip name).
    std::map<std::string, PropSet*> images;
    bool failImageFetch = false;
};
Effect* g_effect = nullptr;
std::set<PropSet*> g_outstanding;  // images acquired but not yet released
int g_acquired = 0;

OfxStatus fxGetProps(OfxImageEffectHandle e, OfxPropertySetHandle* p) { *p = reinterpret_cast<OfxPropertySetHandle>(&reinterpret_cast<Effect*>(e)->props); return kOfxStatOK; }
OfxStatus fxGetParamSet(OfxImageEffectHandle e, OfxParamSetHandle* p) { *p = reinterpret_cast<OfxParamSetHandle>(e); return kOfxStatOK; }
OfxStatus fxClipDefine(OfxImageEffectHandle e, const char* name, OfxPropertySetHandle* p) {
    auto& c = reinterpret_cast<Effect*>(e)->clips[name]; c = std::make_unique<Clip>();
    *p = reinterpret_cast<OfxPropertySetHandle>(&c->props); return kOfxStatOK; }
OfxStatus fxClipGetHandle(OfxImageEffectHandle e, const char* name, OfxImageClipHandle* c, OfxPropertySetHandle* p) {
    auto* fx = reinterpret_cast<Effect*>(e); auto it = fx->clips.find(name);
    if (it == fx->clips.end()) return kOfxStatErrUnknown;
    *c = reinterpret_cast<OfxImageClipHandle>(it->second.get());
    if (p) *p = reinterpret_cast<OfxPropertySetHandle>(&it->second->props);
    return kOfxStatOK; }
OfxStatus fxClipGetProps(OfxImageClipHandle c, OfxPropertySetHandle* p) { *p = reinterpret_cast<OfxPropertySetHandle>(&reinterpret_cast<Clip*>(c)->props); return kOfxStatOK; }
OfxStatus fxClipGetImage(OfxImageClipHandle c, OfxTime, const OfxRectD*, OfxPropertySetHandle* out) {
    if (g_effect->failImageFetch) return kOfxStatFailed;
    for (auto& kv : g_effect->clips) {
        if (reinterpret_cast<OfxImageClipHandle>(kv.second.get()) != c) continue;
        auto it = g_effect->images.find(kv.first);
        if (it == g_effect->images.end() || !it->second) return kOfxStatFailed;
        // Hand out a fresh copy of the descriptor per call, like a real host.
        auto* img = new PropSet(*it->second);
        g_outstanding.insert(img); ++g_acquired;
        *out = reinterpret_cast<OfxPropertySetHandle>(img);
        return kOfxStatOK;
    }
    return kOfxStatErrBadHandle; }
OfxStatus fxClipRelease(OfxPropertySetHandle h) {
    auto* img = ps(h);
    if (!g_outstanding.erase(img)) return kOfxStatErrBadHandle;  // double release / bogus
    delete img; return kOfxStatOK; }
int fxAbort(OfxImageEffectHandle) { return 0; }
OfxImageEffectSuiteV1 g_fxSuite;

OfxStatus pDefine(OfxParamSetHandle s, const char* type, const char* name, OfxPropertySetHandle* p) {
    auto* fx = reinterpret_cast<Effect*>(s); auto prm = std::make_unique<Param>(); prm->type = type;
    *p = reinterpret_cast<OfxPropertySetHandle>(&prm->props);
    fx->paramOrder.push_back(name); fx->params[name] = std::move(prm); return kOfxStatOK; }
OfxStatus pGetHandle(OfxParamSetHandle s, const char* name, OfxParamHandle* h, OfxPropertySetHandle* p) {
    auto* fx = reinterpret_cast<Effect*>(s); auto it = fx->params.find(name);
    if (it == fx->params.end()) return kOfxStatErrUnknown;
    *h = reinterpret_cast<OfxParamHandle>(it->second.get());
    if (p) *p = reinterpret_cast<OfxPropertySetHandle>(&it->second->props);
    return kOfxStatOK; }

// The real API is variadic after `time`; implement with a proper va_list.
OfxStatus pGetValueAtTimeV(OfxParamHandle h, OfxTime time, ...) {
    (void)time;
    auto* prm = reinterpret_cast<Param*>(h);
    va_list ap; va_start(ap, time);
    OfxStatus st = kOfxStatOK;
    if (prm->type == kOfxParamTypeBoolean) {
        int* out = va_arg(ap, int*);
        *out = prm->valueSet ? prm->iv : (prm->props.props.count(kOfxParamPropDefault) ? prm->props.props[kOfxParamPropDefault][0].i : 0);
    } else if (prm->type == kOfxParamTypeDouble) {
        double* out = va_arg(ap, double*);
        *out = prm->valueSet ? prm->dv : (prm->props.props.count(kOfxParamPropDefault) ? prm->props.props[kOfxParamPropDefault][0].d : 0.0);
    } else st = kOfxStatErrBadHandle;
    va_end(ap); return st; }
OfxParameterSuiteV1 g_paramSuite;

const void* fetchSuite(OfxPropertySetHandle, const char* name, int version) {
    if (version != 1) return nullptr;
    if (!std::strcmp(name, kOfxPropertySuite)) return &g_propSuite;
    if (!std::strcmp(name, kOfxImageEffectSuite)) return &g_fxSuite;
    if (!std::strcmp(name, kOfxParameterSuite)) return &g_paramSuite;
    return nullptr; }

OfxHost g_host;

void initHost() {
    std::memset(&g_propSuite, 0, sizeof g_propSuite);
    g_propSuite.propSetPointer = setP; g_propSuite.propSetString = setS;
    g_propSuite.propSetDouble = setD; g_propSuite.propSetInt = setI;
    g_propSuite.propSetIntN = setIN;
    g_propSuite.propGetPointer = getP; g_propSuite.propGetString = getS;
    g_propSuite.propGetDouble = getD; g_propSuite.propGetInt = getI;
    g_propSuite.propGetIntN = getIN;
    g_propSuite.propReset = reset; g_propSuite.propGetDimension = dimension;
    std::memset(&g_fxSuite, 0, sizeof g_fxSuite);
    g_fxSuite.getPropertySet = fxGetProps; g_fxSuite.getParamSet = fxGetParamSet;
    g_fxSuite.clipDefine = fxClipDefine; g_fxSuite.clipGetHandle = fxClipGetHandle;
    g_fxSuite.clipGetPropertySet = fxClipGetProps; g_fxSuite.clipGetImage = fxClipGetImage;
    g_fxSuite.clipReleaseImage = fxClipRelease; g_fxSuite.abort = fxAbort;
    std::memset(&g_paramSuite, 0, sizeof g_paramSuite);
    g_paramSuite.paramDefine = pDefine; g_paramSuite.paramGetHandle = pGetHandle;
    g_paramSuite.paramGetValueAtTime = pGetValueAtTimeV;
    g_host.host = nullptr; g_host.fetchSuite = fetchSuite;
}

// ---- test images ----------------------------------------------------------
struct TestImage {
    std::vector<unsigned char> mem; PropSet desc;
    int bounds[4]; std::size_t bpp; int rowBytes; unsigned char* data;
    static constexpr std::size_t kGuard = 128;
    TestImage(const char* depth, std::size_t bytesPerPixel, int x1, int y1, int x2, int y2,
              int pad, unsigned char seed, const char* components = kOfxImageComponentRGBA)
        : bpp(bytesPerPixel) {
        bounds[0] = x1; bounds[1] = y1; bounds[2] = x2; bounds[3] = y2;
        rowBytes = static_cast<int>(static_cast<std::size_t>(x2 - x1) * bpp) + pad;
        const std::size_t total = static_cast<std::size_t>(rowBytes) * static_cast<std::size_t>(y2 - y1);
        mem.assign(total + 2 * kGuard, 0xEE);
        data = mem.data() + kGuard;
        for (std::size_t i = 0; i < total; ++i) data[i] = static_cast<unsigned char>(seed ? (i * 31 + seed) : 0x55);
        auto* h = reinterpret_cast<OfxPropertySetHandle>(&desc);
        setS(h, kOfxImageEffectPropPixelDepth, 0, depth);
        setS(h, kOfxImageEffectPropComponents, 0, components);
        setIN(h, kOfxImagePropBounds, 4, bounds);
        setI(h, kOfxImagePropRowBytes, 0, rowBytes);
        setP(h, kOfxImagePropData, 0, data);
        total_ = total;
    }
    bool guardsOk() const {
        for (std::size_t i = 0; i < kGuard; ++i)
            if (mem[i] != 0xEE || mem[kGuard + total_ + i] != 0xEE) return false;
        return true; }
    bool pixelsEqual(const TestImage& o, int x1, int y1, int x2, int y2) const {
        for (int y = y1; y < y2; ++y)
            for (int x = x1; x < x2; ++x)
                if (std::memcmp(data + (y - bounds[1]) * rowBytes + (x - bounds[0]) * static_cast<int>(bpp),
                                o.data + (y - o.bounds[1]) * o.rowBytes + (x - o.bounds[0]) * static_cast<int>(o.bpp), bpp)) return false;
        return true; }
    std::size_t total_ = 0;
};

OfxStatus call(OfxPlugin* p, const char* action, const void* handle, PropSet* in = nullptr, PropSet* out = nullptr) {
    return p->mainEntry(action, handle, reinterpret_cast<OfxPropertySetHandle>(in), reinterpret_cast<OfxPropertySetHandle>(out)); }

PropSet makeRenderArgs(int x1, int y1, int x2, int y2, double time = 0) {
    PropSet a; auto* h = reinterpret_cast<OfxPropertySetHandle>(&a);
    setD(h, kOfxPropTime, 0, time);
    const int w[4] = {x1, y1, x2, y2}; setIN(h, kOfxImageEffectPropRenderWindow, 4, w);
    return a; }

void setParams(Effect& fx, bool enable, double mix) {
    auto& e = *fx.params[ "enable" ]; e.valueSet = true; e.iv = enable ? 1 : 0;
    auto& m = *fx.params[ "mix" ]; m.valueSet = true; m.dv = mix; }

std::string str(PropSet& p, const char* n, int i = 0) { auto* s = slot(reinterpret_cast<OfxPropertySetHandle>(&p), n, i, false); return s ? s->s : "<missing>"; }

} // namespace

int main() {
    initHost();
    CHECK(OfxGetNumberOfPlugins() == 1);
    OfxPlugin* plugin = OfxGetPlugin(0);
    CHECK(plugin != nullptr);
    CHECK(OfxGetPlugin(1) == nullptr);
    CHECK(OfxGetPlugin(-1) == nullptr);
    if (!plugin) return 1;
    CHECK(std::strcmp(plugin->pluginApi, kOfxImageEffectPluginApi) == 0);
    CHECK(std::strcmp(plugin->pluginIdentifier, "com.shutterfx.motionblur") == 0);
    CHECK(plugin->pluginVersionMajor == 0 && plugin->pluginVersionMinor == 1);
    plugin->setHost(&g_host);

    Effect fx; g_effect = &fx;
    const void* handle = &fx;

    // Actions before Load must not crash.
    CHECK(call(plugin, kOfxActionDescribe, handle) != kOfxStatOK);
    // Unknown action -> default reply.
    CHECK(call(plugin, kOfxActionLoad, nullptr) == kOfxStatOK);
    CHECK(call(plugin, "OfxActionSomethingNew", handle) == kOfxStatReplyDefault);

    // ---- Describe ----
    CHECK(call(plugin, kOfxActionDescribe, handle) == kOfxStatOK);
    CHECK(str(fx.props, kOfxPropLabel) == "ShutterFX");
    CHECK(str(fx.props, kOfxImageEffectPluginPropGrouping) == "ShutterFX");
    CHECK(str(fx.props, kOfxImageEffectPropSupportedPixelDepths, 0) == kOfxBitDepthByte);
    CHECK(str(fx.props, kOfxImageEffectPropSupportedPixelDepths, 1) == kOfxBitDepthShort);
    CHECK(str(fx.props, kOfxImageEffectPropSupportedPixelDepths, 2) == kOfxBitDepthFloat);
    CHECK(str(fx.props, kOfxImageEffectPropSupportedContexts, 0) == kOfxImageEffectContextFilter);
    CHECK(str(fx.props, kOfxImageEffectPluginRenderThreadSafety) == kOfxImageEffectRenderFullySafe);

    // ---- DescribeInContext ----
    PropSet ctxArgs; setS(reinterpret_cast<OfxPropertySetHandle>(&ctxArgs), kOfxImageEffectPropContext, 0, kOfxImageEffectContextFilter);
    CHECK(call(plugin, kOfxImageEffectActionDescribeInContext, handle, &ctxArgs) == kOfxStatOK);
    CHECK(fx.clips.count("Output") == 1 && fx.clips.count("Source") == 1);
    CHECK(fx.params.count("enable") == 1 && fx.params["enable"]->type == kOfxParamTypeBoolean);
    CHECK(fx.params.count("mix") == 1 && fx.params["mix"]->type == kOfxParamTypeDouble);
    CHECK(fx.params["enable"]->props.props[kOfxParamPropDefault][0].i == 1);
    CHECK(fx.params["mix"]->props.props[kOfxParamPropDefault][0].d == 100.0);
    CHECK(fx.params["infoName"]->props.props[kOfxParamPropDefault][0].s == "ShutterFX");
    CHECK(fx.params["infoVersion"]->props.props[kOfxParamPropDefault][0].s == "0.1.0");
    CHECK(str(fx.params["infoVersion"]->props, kOfxParamPropParent) == "information");

    // ---- CreateInstance ----
    CHECK(call(plugin, kOfxActionCreateInstance, handle) == kOfxStatOK);

    struct Fmt { const char* depth; std::size_t bpp; };
    const Fmt fmts[] = {{kOfxBitDepthByte, 4}, {kOfxBitDepthShort, 8}, {kOfxBitDepthFloat, 16}};

    // ---- Render: all depths, odd sizes, non-zero origins, differing strides ----
    for (const Fmt& f : fmts) {
        TestImage src(f.depth, f.bpp, -7, 11, 10, 18, 24, 3);     // 17 x 7, origin (-7,11)
        TestImage dst(f.depth, f.bpp, -7, 11, 10, 18, 56, 0);
        fx.images["Source"] = &src.desc; fx.images["Output"] = &dst.desc;
        setParams(fx, true, 100.0);
        PropSet args = makeRenderArgs(-7, 11, 10, 18);
        CHECK(call(plugin, kOfxImageEffectActionRender, handle, &args) == kOfxStatOK);
        CHECK(src.pixelsEqual(dst, -7, 11, 10, 18));
        CHECK(dst.guardsOk());
        CHECK(g_outstanding.empty());

        // Partial render window only touches that window.
        TestImage dst2(f.depth, f.bpp, -7, 11, 10, 18, 8, 0);
        fx.images["Output"] = &dst2.desc;
        PropSet part = makeRenderArgs(-3, 12, 5, 16);
        CHECK(call(plugin, kOfxImageEffectActionRender, handle, &part) == kOfxStatOK);
        CHECK(src.pixelsEqual(dst2, -3, 12, 5, 16));
        CHECK(dst2.data[0] == 0x55);                       // outside window untouched
        CHECK(dst2.guardsOk());

        // Window larger than the output is clamped, never overruns.
        TestImage dst3(f.depth, f.bpp, -7, 11, 10, 18, 0, 0);
        fx.images["Output"] = &dst3.desc;
        PropSet big = makeRenderArgs(-100, -100, 100, 100);
        CHECK(call(plugin, kOfxImageEffectActionRender, handle, &big) == kOfxStatOK);
        CHECK(src.pixelsEqual(dst3, -7, 11, 10, 18));
        CHECK(dst3.guardsOk());

        // Enable off and partial Mix are still exact identity.
        for (auto cfg : {std::pair<bool, double>{false, 100.0}, {true, 0.0}, {true, 37.5}, {true, 250.0}, {true, -5.0}}) {
            TestImage d(f.depth, f.bpp, -7, 11, 10, 18, 16, 0);
            fx.images["Output"] = &d.desc;
            setParams(fx, cfg.first, cfg.second);
            PropSet a = makeRenderArgs(-7, 11, 10, 18);
            CHECK(call(plugin, kOfxImageEffectActionRender, handle, &a) == kOfxStatOK);
            CHECK(src.pixelsEqual(d, -7, 11, 10, 18));
            CHECK(g_outstanding.empty());
        }
    }
    setParams(fx, true, 100.0);

    // ---- Render: failure paths must be safe and must not leak images ----
    {
        TestImage src(kOfxBitDepthFloat, 16, 0, 0, 5, 5, 0, 9);
        TestImage half(kOfxBitDepthHalf, 8, 0, 0, 5, 5, 0, 0);
        TestImage rgb(kOfxBitDepthFloat, 12, 0, 0, 5, 5, 0, 0, kOfxImageComponentRGB);
        TestImage byteDst(kOfxBitDepthByte, 4, 0, 0, 5, 5, 0, 0);
        TestImage nullData(kOfxBitDepthFloat, 16, 0, 0, 5, 5, 0, 0);
        setP(reinterpret_cast<OfxPropertySetHandle>(&nullData.desc), kOfxImagePropData, 0, nullptr);
        TestImage badStride(kOfxBitDepthFloat, 16, 0, 0, 5, 5, 0, 0);
        setI(reinterpret_cast<OfxPropertySetHandle>(&badStride.desc), kOfxImagePropRowBytes, 0, 8);
        PropSet args = makeRenderArgs(0, 0, 5, 5);

        struct Case { const char* name; PropSet* srcDesc; PropSet* dstDesc; } cases[] = {
            {"half-float output", &src.desc, &half.desc},
            {"RGB output", &src.desc, &rgb.desc},
            {"depth mismatch", &src.desc, &byteDst.desc},
            {"null output data", &src.desc, &nullData.desc},
            {"null source data", &nullData.desc, &src.desc},
            {"bad row bytes", &src.desc, &badStride.desc},
            {"missing source", nullptr, &src.desc},
            {"missing output", &src.desc, nullptr},
        };
        for (auto& c : cases) {
            fx.images["Source"] = c.srcDesc; fx.images["Output"] = c.dstDesc;
            OfxStatus st = call(plugin, kOfxImageEffectActionRender, handle, &args);
            if (st == kOfxStatOK) std::printf("  (unexpected OK for case: %s)\n", c.name);
            CHECK(st != kOfxStatOK);
            CHECK(g_outstanding.empty());
        }
        fx.failImageFetch = true;
        CHECK(call(plugin, kOfxImageEffectActionRender, handle, &args) != kOfxStatOK);
        CHECK(g_outstanding.empty());
        fx.failImageFetch = false;

        // Missing render window property.
        PropSet noWin; setD(reinterpret_cast<OfxPropertySetHandle>(&noWin), kOfxPropTime, 0, 0);
        fx.images["Source"] = &src.desc; fx.images["Output"] = &src.desc;
        CHECK(call(plugin, kOfxImageEffectActionRender, handle, &noWin) != kOfxStatOK);
        CHECK(g_outstanding.empty());
    }
    CHECK(g_acquired > 0);

    // ---- IsIdentity ----
    {
        PropSet in; setD(reinterpret_cast<OfxPropertySetHandle>(&in), kOfxPropTime, 0, 4.0);
        PropSet out;
        setParams(fx, true, 100.0);
        CHECK(call(plugin, kOfxImageEffectActionIsIdentity, handle, &in, &out) == kOfxStatReplyDefault);
        setParams(fx, false, 100.0);
        CHECK(call(plugin, kOfxImageEffectActionIsIdentity, handle, &in, &out) == kOfxStatOK);
        CHECK(str(out, kOfxPropName) == "Source");
        setParams(fx, true, 0.0);
        PropSet out2;
        CHECK(call(plugin, kOfxImageEffectActionIsIdentity, handle, &in, &out2) == kOfxStatOK);
    }

    // ---- DestroyInstance / Unload ----
    CHECK(call(plugin, kOfxActionDestroyInstance, handle) == kOfxStatOK);
    CHECK(call(plugin, kOfxImageEffectActionRender, handle, nullptr) != kOfxStatOK); // no instance data
    CHECK(call(plugin, kOfxActionUnload, nullptr) == kOfxStatOK);
    CHECK(call(plugin, kOfxActionDescribe, handle) != kOfxStatOK);  // suites gone

    // Host missing a required suite -> Load refuses.
    OfxHost badHost = g_host;
    badHost.fetchSuite = [](OfxPropertySetHandle, const char*, int) -> const void* { return nullptr; };
    plugin->setHost(&badHost);
    CHECK(call(plugin, kOfxActionLoad, nullptr) == kOfxStatErrMissingHostFeature);
    plugin->setHost(nullptr);
    CHECK(call(plugin, kOfxActionLoad, nullptr) == kOfxStatErrMissingHostFeature);

    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
