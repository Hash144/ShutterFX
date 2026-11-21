// Unit tests for the host-independent image helpers. No test framework: each
// CHECK prints a failure and the process exits non-zero if any failed.
#include "ImageUtils.h"

#include <cstdio>
#include <cstring>
#include <vector>

using namespace shutterfx;

static int g_failures = 0;
static int g_checks = 0;
#define CHECK(cond)                                                          \
    do {                                                                     \
        ++g_checks;                                                          \
        if (!(cond)) {                                                       \
            ++g_failures;                                                    \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);      \
        }                                                                    \
    } while (0)

// Buffer with padded rows, guard bytes before and after, and a pattern fill.
struct Buffer {
    std::vector<std::uint8_t> mem;
    ImageView view;
    std::size_t guardBytes = 64;
    std::ptrdiff_t padBytes;
    std::size_t total;

    Buffer(Rect bounds, PixelDepth depth, std::ptrdiff_t pad, std::uint8_t fill) {
        const std::size_t rowSize = static_cast<std::size_t>(bounds.width()) * bytesPerPixel(depth);
        padBytes = pad;
        const std::size_t stride = rowSize + static_cast<std::size_t>(pad);
        total = stride * static_cast<std::size_t>(bounds.height());
        mem.assign(total + 2 * guardBytes, 0xEE);
        std::fill(mem.begin() + static_cast<std::ptrdiff_t>(guardBytes), mem.begin() + static_cast<std::ptrdiff_t>(guardBytes + total), fill);
        view.data = mem.data() + guardBytes;
        view.bounds = bounds;
        view.rowBytes = static_cast<std::ptrdiff_t>(stride);
        view.depth = depth;
    }
    bool guardsIntact() const {
        for (std::size_t i = 0; i < guardBytes; ++i)
            if (mem[i] != 0xEE || mem[guardBytes + total + i] != 0xEE) return false;
        return true;
    }
};

static void seedPattern(Buffer& b) {
    // Unique value per byte derived from coordinates.
    for (int y = b.view.bounds.y1; y < b.view.bounds.y2; ++y)
        for (int x = b.view.bounds.x1; x < b.view.bounds.x2; ++x) {
            std::uint8_t* p = pixelAddress(b.view, x, y);
            for (std::size_t i = 0; i < bytesPerPixel(b.view.depth); ++i)
                p[i] = static_cast<std::uint8_t>(x * 7 + y * 13 + static_cast<int>(i) * 31 + 1);
        }
}

static bool windowEqual(const ImageView& a, const ImageView& b, const Rect& w) {
    const std::size_t bpp = bytesPerPixel(a.depth);
    for (int y = w.y1; y < w.y2; ++y)
        for (int x = w.x1; x < w.x2; ++x)
            if (std::memcmp(pixelAddress(a, x, y), pixelAddress(b, x, y), bpp) != 0)
                return false;
    return true;
}

static void testIdentityCopyAllDepths() {
    const PixelDepth depths[] = {PixelDepth::Byte, PixelDepth::Short, PixelDepth::Float};
    // Odd dimensions, non-zero origins, different (padded) strides.
    for (PixelDepth d : depths) {
        Buffer src({-5, 3, 12, 10}, d, 24, 0);       // 17 x 7
        Buffer dst({-5, 3, 12, 10}, d, 40, 0);
        seedPattern(src);
        CHECK(copyRegion(src.view, dst.view, dst.view.bounds) == Result::Ok);
        CHECK(windowEqual(src.view, dst.view, dst.view.bounds));
        CHECK(dst.guardsIntact());
        CHECK(src.view.rowBytes != dst.view.rowBytes);
    }
}

static void testSubWindow() {
    Buffer src({100, 200, 131, 215}, PixelDepth::Float, 16, 0); // 31 x 15
    Buffer dst({100, 200, 131, 215}, PixelDepth::Float, 0, 0xAB);
    seedPattern(src);
    const Rect w{105, 203, 120, 210};
    CHECK(copyRegion(src.view, dst.view, w) == Result::Ok);
    CHECK(windowEqual(src.view, dst.view, w));
    // Outside the window is untouched.
    CHECK(*pixelAddress(dst.view, 100, 200) == 0xAB);
    CHECK(*pixelAddress(dst.view, 120, 205) == 0xAB);
    CHECK(*pixelAddress(dst.view, 105, 210) == 0xAB);
    CHECK(dst.guardsIntact());
}

static void testSourceSmallerThanDest() {
    Buffer src({2, 2, 5, 5}, PixelDepth::Byte, 8, 0);
    Buffer dst({0, 0, 8, 8}, PixelDepth::Byte, 3, 0x77);
    seedPattern(src);
    CHECK(copyRegion(src.view, dst.view, dst.view.bounds) == Result::Ok);
    CHECK(windowEqual(src.view, dst.view, {2, 2, 5, 5}));
    for (int i = 0; i < 4; ++i) CHECK(*pixelAddress(dst.view, 0, 0) + i == 0 + i); // zero fill
    CHECK(pixelAddress(dst.view, 0, 0)[0] == 0 && pixelAddress(dst.view, 7, 7)[3] == 0);
    CHECK(pixelAddress(dst.view, 1, 3)[0] == 0 && pixelAddress(dst.view, 5, 3)[0] == 0);
    CHECK(dst.guardsIntact());
}

static void testNegativeRowBytes() {
    // Bottom-up buffer: data points at row y1, rows go backwards in memory.
    Buffer srcStore({0, 0, 9, 4}, PixelDepth::Short, 6, 0);
    seedPattern(srcStore);
    ImageView flipped = srcStore.view;
    flipped.data = pixelAddress(srcStore.view, 0, 3);      // last row in memory
    flipped.rowBytes = -srcStore.view.rowBytes;
    Buffer dst({0, 0, 9, 4}, PixelDepth::Short, 0, 0);
    CHECK(copyRegion(flipped, dst.view, dst.view.bounds) == Result::Ok);
    CHECK(windowEqual(flipped, dst.view, dst.view.bounds));
    CHECK(dst.guardsIntact());
}

static void testSpecialFloatValues() {
    Buffer src({0, 0, 3, 1}, PixelDepth::Float, 0, 0);
    Buffer dst({0, 0, 3, 1}, PixelDepth::Float, 0, 0);
    float* s = reinterpret_cast<float*>(src.view.data);
    const float vals[12] = {-1.5f, 65504.0f, 0.0f, 0.25f, 1e30f, -0.0f, 7.0f, 1.0f,
                            0.0f, 0.0f, 0.0f, 0.5f};
    std::memcpy(s, vals, sizeof vals);
    CHECK(copyRegion(src.view, dst.view, dst.view.bounds) == Result::Ok);
    CHECK(std::memcmp(src.view.data, dst.view.data, sizeof vals) == 0); // bit-exact
}

static void testInvalidInputs() {
    Buffer good({0, 0, 4, 4}, PixelDepth::Float, 0, 0);
    ImageView nullImg;
    CHECK(copyRegion(nullImg, good.view, good.view.bounds) == Result::InvalidImage);
    ImageView nullDst = nullImg;
    CHECK(copyRegion(good.view, nullDst, good.view.bounds) == Result::InvalidImage);

    ImageView tightStride = good.view;
    tightStride.rowBytes = 4; // smaller than a row
    CHECK(copyRegion(tightStride, good.view, good.view.bounds) == Result::InvalidImage);

    ImageView emptyBounds = good.view;
    emptyBounds.bounds = {0, 0, 0, 4};
    CHECK(copyRegion(emptyBounds, good.view, good.view.bounds) == Result::InvalidImage);

    Buffer other({0, 0, 4, 4}, PixelDepth::Byte, 0, 0);
    CHECK(copyRegion(good.view, other.view, other.view.bounds) == Result::DepthMismatch);

    CHECK(copyRegion(good.view, good.view, {2, 2, 9, 9}) == Result::WindowOutside);
    CHECK(copyRegion(good.view, good.view, {2, 2, 2, 2}) == Result::Ok); // empty window
}

static void testInPlaceCopy() {
    Buffer b({0, 0, 5, 5}, PixelDepth::Byte, 4, 0);
    seedPattern(b);
    Buffer ref({0, 0, 5, 5}, PixelDepth::Byte, 4, 0);
    seedPattern(ref);
    CHECK(copyRegion(b.view, b.view, b.view.bounds) == Result::Ok);
    CHECK(windowEqual(b.view, ref.view, b.view.bounds));
}

static void testMix() {
    const PixelDepth depths[] = {PixelDepth::Byte, PixelDepth::Short, PixelDepth::Float};
    for (PixelDepth d : depths) {
        Buffer a({0, 0, 7, 5}, d, 10, 0), b({0, 0, 7, 5}, d, 2, 0), out({0, 0, 7, 5}, d, 0, 0);
        seedPattern(a);
        // identical inputs -> exact copy for every mix value
        for (int y = 0; y < 5; ++y)
            for (int x = 0; x < 7; ++x)
                std::memcpy(pixelAddress(b.view, x, y), pixelAddress(a.view, x, y),
                            bytesPerPixel(d));
        const double mixes[] = {0.0, 0.25, 0.5, 0.999, 1.0};
        for (double m : mixes) {
            CHECK(mixRegion(a.view, b.view, out.view, out.view.bounds, m) == Result::Ok);
            CHECK(windowEqual(a.view, out.view, out.view.bounds));
        }
        // out-of-range and NaN mixes are tolerated
        CHECK(mixRegion(a.view, b.view, out.view, out.view.bounds, -3.0) == Result::Ok);
        CHECK(mixRegion(a.view, b.view, out.view, out.view.bounds, 42.0) == Result::Ok);
        CHECK(mixRegion(a.view, b.view, out.view, out.view.bounds, __builtin_nan("")) == Result::Ok);
        CHECK(out.guardsIntact());
    }
    // real blend of different inputs
    Buffer a({0, 0, 1, 1}, PixelDepth::Float, 0, 0), b({0, 0, 1, 1}, PixelDepth::Float, 0, 0),
        o({0, 0, 1, 1}, PixelDepth::Float, 0, 0);
    const float av[4] = {0.f, 1.f, 2.f, 1.f}, bv[4] = {1.f, 1.f, 0.f, 0.f};
    std::memcpy(a.view.data, av, sizeof av);
    std::memcpy(b.view.data, bv, sizeof bv);
    CHECK(mixRegion(a.view, b.view, o.view, o.view.bounds, 0.25) == Result::Ok);
    const float* r = reinterpret_cast<const float*>(o.view.data);
    CHECK(r[0] == 0.25f && r[1] == 1.f && r[2] == 1.5f && r[3] == 0.75f);
    Buffer a8({0, 0, 1, 1}, PixelDepth::Byte, 0, 0), b8({0, 0, 1, 1}, PixelDepth::Byte, 0, 0),
        o8({0, 0, 1, 1}, PixelDepth::Byte, 0, 0);
    const std::uint8_t a8v[4] = {0, 100, 255, 255}, b8v[4] = {255, 100, 0, 255};
    std::memcpy(a8.view.data, a8v, 4);
    std::memcpy(b8.view.data, b8v, 4);
    CHECK(mixRegion(a8.view, b8.view, o8.view, o8.view.bounds, 0.5) == Result::Ok);
    const std::uint8_t* r8 = static_cast<const std::uint8_t*>(o8.view.data);
    CHECK(r8[0] == 128 && r8[1] == 100 && r8[2] == 128 && r8[3] == 255);
}

int main() {
    testIdentityCopyAllDepths();
    testSubWindow();
    testSourceSmallerThanDest();
    testNegativeRowBytes();
    testSpecialFloatValues();
    testInvalidInputs();
    testInPlaceCopy();
    testMix();
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
