// ShutterFX - host-independent image helpers.
//
// Nothing in this header depends on OpenFX, so it can be unit tested on any
// platform. All coordinates are in OFX "pixel coordinates": the image buffer
// covers [x1,x2) x [y1,y2) and may have a non-zero origin. Row 0 of the buffer
// is the row at y1, and consecutive rows are `rowBytes` apart (which may be
// negative, may include padding, and need not match between images).
#pragma once

#include <cstddef>
#include <cstdint>

namespace shutterfx {

enum class PixelDepth { Byte, Short, Float };

// Number of channels in an RGBA pixel.
constexpr int kRGBAChannels = 4;

// Bytes per RGBA pixel for a depth.
std::size_t bytesPerPixel(PixelDepth depth);

// Integer rectangle, half-open: [x1,x2) x [y1,y2).
struct Rect {
    int x1 = 0, y1 = 0, x2 = 0, y2 = 0;
    int width() const { return x2 - x1; }
    int height() const { return y2 - y1; }
    bool empty() const { return x2 <= x1 || y2 <= y1; }
};

Rect intersect(const Rect& a, const Rect& b);

// Non-owning view of an RGBA image buffer.
struct ImageView {
    void* data = nullptr;       // address of the pixel at (bounds.x1, bounds.y1)
    Rect bounds;                // addressable pixels
    std::ptrdiff_t rowBytes = 0;// byte distance between rows (may be negative)
    PixelDepth depth = PixelDepth::Float;
};

// Returns true when the view can safely be addressed over `bounds`:
// non-null data, non-empty bounds, and |rowBytes| large enough for one row.
bool isValid(const ImageView& v);

// Address of the first byte of the pixel at (x, y). The pixel must be inside
// v.bounds.
const std::uint8_t* pixelAddress(const ImageView& v, int x, int y);
std::uint8_t* pixelAddress(ImageView& v, int x, int y);

enum class Result {
    Ok,
    InvalidImage,    // null pointer, empty bounds, bad row stride
    DepthMismatch,   // source and destination depths differ
    WindowOutside,   // render window not inside the destination bounds
};

// Copy `window` from src to dst, value-for-value. Pixels of the window that
// lie outside src.bounds are written as transparent black (all zeros) so the
// destination never holds uninitialised data. src and dst must not overlap
// unless they are the very same buffer with identical layout.
Result copyRegion(const ImageView& src, ImageView& dst, const Rect& window);

// dst = original * (1 - mix) + processed * mix, per channel, over `window`.
// mix is clamped to [0,1]. mix >= 1 is an exact copy of `processed`; mix <= 0
// is an exact copy of `original`. Alpha is blended like any other channel,
// which is exact for the identity engine and keeps premultiplication
// consistent between the two inputs.
Result mixRegion(const ImageView& original, const ImageView& processed,
                 ImageView& dst, const Rect& window, double mix);

} // namespace shutterfx
