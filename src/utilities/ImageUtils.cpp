#include "ImageUtils.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace shutterfx {

std::size_t bytesPerPixel(PixelDepth depth) {
    switch (depth) {
        case PixelDepth::Byte:  return kRGBAChannels * sizeof(std::uint8_t);
        case PixelDepth::Short: return kRGBAChannels * sizeof(std::uint16_t);
        case PixelDepth::Float: return kRGBAChannels * sizeof(float);
    }
    return 0;
}

Rect intersect(const Rect& a, const Rect& b) {
    Rect r;
    r.x1 = std::max(a.x1, b.x1);
    r.y1 = std::max(a.y1, b.y1);
    r.x2 = std::min(a.x2, b.x2);
    r.y2 = std::min(a.y2, b.y2);
    if (r.empty()) r = Rect{};
    return r;
}

bool isValid(const ImageView& v) {
    if (!v.data || v.bounds.empty()) return false;
    const std::size_t rowSize =
        static_cast<std::size_t>(v.bounds.width()) * bytesPerPixel(v.depth);
    const std::size_t stride = static_cast<std::size_t>(std::llabs(
        static_cast<long long>(v.rowBytes)));
    return rowSize > 0 && stride >= rowSize;
}

const std::uint8_t* pixelAddress(const ImageView& v, int x, int y) {
    const auto* base = static_cast<const std::uint8_t*>(v.data);
    return base + static_cast<std::ptrdiff_t>(y - v.bounds.y1) * v.rowBytes +
           static_cast<std::ptrdiff_t>(x - v.bounds.x1) *
               static_cast<std::ptrdiff_t>(bytesPerPixel(v.depth));
}

std::uint8_t* pixelAddress(ImageView& v, int x, int y) {
    return const_cast<std::uint8_t*>(
        pixelAddress(static_cast<const ImageView&>(v), x, y));
}

namespace {

bool windowInside(const Rect& window, const Rect& bounds) {
    return window.x1 >= bounds.x1 && window.y1 >= bounds.y1 &&
           window.x2 <= bounds.x2 && window.y2 <= bounds.y2;
}

// Row-wise lerp, specialised per channel type. For identical inputs the result
// equals the input exactly (a + (a - a) * t == a, and rounding of an integral
// value is a no-op).
template <typename T>
T lerpChannel(T a, T b, double t);

template <>
std::uint8_t lerpChannel<std::uint8_t>(std::uint8_t a, std::uint8_t b, double t) {
    const double v = a + (static_cast<double>(b) - a) * t;
    return static_cast<std::uint8_t>(std::clamp(v + 0.5, 0.0, 255.0));
}
template <>
std::uint16_t lerpChannel<std::uint16_t>(std::uint16_t a, std::uint16_t b, double t) {
    const double v = a + (static_cast<double>(b) - a) * t;
    return static_cast<std::uint16_t>(std::clamp(v + 0.5, 0.0, 65535.0));
}
template <>
float lerpChannel<float>(float a, float b, double t) {
    // No clamping: float images legitimately carry values outside [0,1].
    return static_cast<float>(a + (static_cast<double>(b) - a) * t);
}

template <typename T>
void mixRow(const std::uint8_t* a, const std::uint8_t* b, std::uint8_t* out,
            std::size_t channels, double t) {
    for (std::size_t i = 0; i < channels; ++i) {
        T va, vb;
        std::memcpy(&va, a + i * sizeof(T), sizeof(T));
        std::memcpy(&vb, b + i * sizeof(T), sizeof(T));
        const T r = lerpChannel<T>(va, vb, t);
        std::memcpy(out + i * sizeof(T), &r, sizeof(T));
    }
}

} // namespace

Result copyRegion(const ImageView& src, ImageView& dst, const Rect& window) {
    if (!isValid(src) || !isValid(dst)) return Result::InvalidImage;
    if (src.depth != dst.depth) return Result::DepthMismatch;
    if (window.empty()) return Result::Ok;
    if (!windowInside(window, dst.bounds)) return Result::WindowOutside;

    const std::size_t bpp = bytesPerPixel(dst.depth);
    const Rect covered = intersect(window, src.bounds);

    for (int y = window.y1; y < window.y2; ++y) {
        std::uint8_t* out = pixelAddress(dst, window.x1, y);
        const bool rowInSrc = !covered.empty() && y >= covered.y1 && y < covered.y2;
        if (!rowInSrc) {
            std::memset(out, 0, static_cast<std::size_t>(window.width()) * bpp);
            continue;
        }
        // Left margin outside the source, then the covered span, then right margin.
        const std::size_t left = static_cast<std::size_t>(covered.x1 - window.x1);
        const std::size_t mid = static_cast<std::size_t>(covered.width());
        const std::size_t right = static_cast<std::size_t>(window.x2 - covered.x2);
        if (left) std::memset(out, 0, left * bpp);
        // memmove: harmless for distinct buffers, and correct when src == dst.
        std::memmove(out + left * bpp, pixelAddress(src, covered.x1, y), mid * bpp);
        if (right) std::memset(out + (left + mid) * bpp, 0, right * bpp);
    }
    return Result::Ok;
}

Result mixRegion(const ImageView& original, const ImageView& processed,
                 ImageView& dst, const Rect& window, double mix) {
    if (!(mix == mix)) mix = 1.0;               // NaN -> full effect
    mix = std::clamp(mix, 0.0, 1.0);
    if (mix >= 1.0) return copyRegion(processed, dst, window);
    if (mix <= 0.0) return copyRegion(original, dst, window);

    if (!isValid(original) || !isValid(processed) || !isValid(dst))
        return Result::InvalidImage;
    if (original.depth != dst.depth || processed.depth != dst.depth)
        return Result::DepthMismatch;
    if (window.empty()) return Result::Ok;
    if (!windowInside(window, dst.bounds)) return Result::WindowOutside;

    // Start from the original, then blend where processed has coverage.
    // (Pixels outside either input are transparent black.)
    const Rect both = intersect(intersect(window, original.bounds), processed.bounds);
    Result r = copyRegion(original, dst, window);
    if (r != Result::Ok || both.empty()) return r;

    const std::size_t channelsPerRow =
        static_cast<std::size_t>(both.width()) * kRGBAChannels;
    for (int y = both.y1; y < both.y2; ++y) {
        const std::uint8_t* a = pixelAddress(original, both.x1, y);
        const std::uint8_t* b = pixelAddress(processed, both.x1, y);
        std::uint8_t* o = pixelAddress(dst, both.x1, y);
        switch (dst.depth) {
            case PixelDepth::Byte:  mixRow<std::uint8_t>(a, b, o, channelsPerRow, mix); break;
            case PixelDepth::Short: mixRow<std::uint16_t>(a, b, o, channelsPerRow, mix); break;
            case PixelDepth::Float: mixRow<float>(a, b, o, channelsPerRow, mix); break;
        }
    }
    return Result::Ok;
}

} // namespace shutterfx
