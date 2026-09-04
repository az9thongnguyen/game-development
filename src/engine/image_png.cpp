// =============================================================================
//  engine/image_png.cpp  —  see image_png.hpp
// =============================================================================
#include "engine/image_png.hpp"

#include <array>
#include <cstring>

#include "engine/compress/inflate.hpp"

namespace gfx {
namespace {

bool fail(std::string* why, const char* msg) {
    if (why) *why = msg;
    return false;
}

std::uint32_t be32(const std::uint8_t* p) {
    return (static_cast<std::uint32_t>(p[0]) << 24) | (static_cast<std::uint32_t>(p[1]) << 16) |
           (static_cast<std::uint32_t>(p[2]) << 8)  |  static_cast<std::uint32_t>(p[3]);
}

// CRC-32 as PNG defines it (the same polynomial zip uses). Every chunk carries one,
// and checking it is what turns "the image looked odd" into "the file is damaged".
std::uint32_t crc32(const std::uint8_t* data, std::size_t n) {
    static const std::array<std::uint32_t, 256> table = [] {
        std::array<std::uint32_t, 256> t{};
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            t[i] = c;
        }
        return t;
    }();
    std::uint32_t c = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < n; ++i) c = table[(c ^ data[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

int channels_for(int colour_type) {
    switch (colour_type) {
        case 0: return 1;   // grey
        case 2: return 3;   // RGB
        case 3: return 1;   // palette index
        case 4: return 2;   // grey + alpha
        case 6: return 4;   // RGBA
        default: return 0;
    }
}

// PNG's Paeth predictor (spec §9.4): pick whichever of left/above/upper-left the
// linear estimate a+b-c is nearest to. Written out rather than folded into the
// filter switch because it is the one line of the five that is not obvious.
int paeth(int a, int b, int c) {
    const int p  = a + b - c;
    const int pa = p > a ? p - a : a - p;
    const int pb = p > b ? p - b : b - p;
    const int pc = p > c ? p - c : c - p;
    if (pa <= pb && pa <= pc) return a;
    return (pb <= pc) ? b : c;
}

} // namespace

std::optional<Image> decode_png(const std::vector<std::uint8_t>& bytes, std::string* why) {
    if (why) why->clear();

    static const std::uint8_t kSig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    if (bytes.size() < 8 || std::memcmp(bytes.data(), kSig, 8) != 0) {
        fail(why, "not a PNG (bad signature)");
        return std::nullopt;
    }

    int w = 0, h = 0, depth = 0, ctype = 0, interlace = 0;
    bool                      have_header = false;
    std::vector<std::uint8_t> palette;      // RGB triples
    std::vector<std::uint8_t> trns;         // palette alpha, or a colour key
    std::vector<std::uint8_t> idat;

    std::size_t at = 8;
    for (;;) {
        if (at + 8 > bytes.size()) { fail(why, "truncated chunk header"); return std::nullopt; }
        const std::uint32_t len = be32(&bytes[at]);
        const char* type = reinterpret_cast<const char*>(&bytes[at + 4]);
        if (at + 12 + len > bytes.size()) { fail(why, "truncated chunk data"); return std::nullopt; }

        const std::uint8_t* data = &bytes[at + 8];
        const std::uint32_t want = be32(&bytes[at + 8 + len]);
        if (crc32(&bytes[at + 4], len + 4) != want) { fail(why, "chunk CRC mismatch"); return std::nullopt; }

        if (std::memcmp(type, "IHDR", 4) == 0) {
            if (len != 13) { fail(why, "bad IHDR length"); return std::nullopt; }
            w         = static_cast<int>(be32(data));
            h         = static_cast<int>(be32(data + 4));
            depth     = data[8];
            ctype     = data[9];
            interlace = data[12];
            have_header = true;
            if (w <= 0 || h <= 0)              { fail(why, "zero-sized image");            return std::nullopt; }
            if (depth != 8)                    { fail(why, "only 8-bit channels supported"); return std::nullopt; }
            if (channels_for(ctype) == 0)      { fail(why, "unknown colour type");         return std::nullopt; }
            if (interlace != 0)                { fail(why, "interlaced PNG not supported"); return std::nullopt; }
            // A row is 1 filter byte + w*channels, and h rows. Guard the product before
            // anything allocates from it.
            if (static_cast<long long>(w) * h > 64LL * 1024 * 1024) {
                fail(why, "image too large");
                return std::nullopt;
            }
        } else if (std::memcmp(type, "PLTE", 4) == 0) {
            if (len % 3 != 0) { fail(why, "PLTE length is not a multiple of 3"); return std::nullopt; }
            palette.assign(data, data + len);
        } else if (std::memcmp(type, "tRNS", 4) == 0) {
            trns.assign(data, data + len);
        } else if (std::memcmp(type, "IDAT", 4) == 0) {
            // Split across as many chunks as the encoder felt like; one stream.
            idat.insert(idat.end(), data, data + len);
        } else if (std::memcmp(type, "IEND", 4) == 0) {
            break;
        }
        at += 12 + len;
    }

    if (!have_header)            { fail(why, "no IHDR");                       return std::nullopt; }
    if (idat.empty())            { fail(why, "no image data");                 return std::nullopt; }
    if (ctype == 3 && palette.empty()) { fail(why, "palette image with no PLTE"); return std::nullopt; }

    const int         ch     = channels_for(ctype);
    const std::size_t stride = static_cast<std::size_t>(w) * static_cast<std::size_t>(ch);
    const std::size_t need   = (stride + 1) * static_cast<std::size_t>(h);

    std::string zwhy;
    const auto raw = zip::inflate_zlib(idat, need, &zwhy);
    if (!raw)              { if (why) *why = "IDAT: " + zwhy;   return std::nullopt; }
    if (raw->size() < need) { fail(why, "image data is short");  return std::nullopt; }

    // ---- unfilter, in place, row by row ------------------------------------
    // Each row names its own filter and refers to the row above, so this cannot be
    // parallelised and does not want to be: it is one pass over the pixels.
    std::vector<std::uint8_t> px(static_cast<std::size_t>(h) * stride);
    for (int y = 0; y < h; ++y) {
        const std::uint8_t  filter = (*raw)[static_cast<std::size_t>(y) * (stride + 1)];
        const std::uint8_t* src    = raw->data() + static_cast<std::size_t>(y) * (stride + 1) + 1;
        std::uint8_t*       dst    = px.data() + static_cast<std::size_t>(y) * stride;
        const std::uint8_t* up     = (y > 0) ? dst - stride : nullptr;

        for (std::size_t i = 0; i < stride; ++i) {
            const int a = (i >= static_cast<std::size_t>(ch)) ? dst[i - static_cast<std::size_t>(ch)] : 0;
            const int b = up ? up[i] : 0;
            const int c = (up && i >= static_cast<std::size_t>(ch))
                              ? up[i - static_cast<std::size_t>(ch)] : 0;
            int v = src[i];
            switch (filter) {
                case 0: break;                       // None
                case 1: v += a; break;               // Sub
                case 2: v += b; break;               // Up
                case 3: v += (a + b) / 2; break;     // Average
                case 4: v += paeth(a, b, c); break;  // Paeth
                default: fail(why, "unknown scanline filter"); return std::nullopt;
            }
            dst[i] = static_cast<std::uint8_t>(v & 0xFF);
        }
    }

    // ---- expand to ARGB8888 -------------------------------------------------
    Image img;
    img.w = w;
    img.h = h;
    img.pixels.resize(static_cast<std::size_t>(w) * static_cast<std::size_t>(h));
    for (int y = 0; y < h; ++y) {
        const std::uint8_t* row = px.data() + static_cast<std::size_t>(y) * stride;
        for (int x = 0; x < w; ++x) {
            const std::uint8_t* p = row + static_cast<std::size_t>(x) * static_cast<std::size_t>(ch);
            std::uint8_t r = 0, g = 0, b = 0, a = 255;
            switch (ctype) {
                case 0: r = g = b = p[0]; break;
                case 2: r = p[0]; g = p[1]; b = p[2]; break;
                case 3: {
                    const std::size_t i = p[0];
                    if (i * 3 + 2 >= palette.size()) { fail(why, "palette index out of range"); return std::nullopt; }
                    r = palette[i * 3];
                    g = palette[i * 3 + 1];
                    b = palette[i * 3 + 2];
                    // tRNS on a palette image is one alpha per entry, and it may be
                    // SHORTER than the palette — entries past its end are opaque.
                    a = (i < trns.size()) ? trns[i] : 255;
                    break;
                }
                case 4: r = g = b = p[0]; a = p[1]; break;
                case 6: r = p[0]; g = p[1]; b = p[2]; a = p[3]; break;
                default: break;
            }
            img.pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) +
                       static_cast<std::size_t>(x)] = rgba(r, g, b, a);
        }
    }
    return img;
}

} // namespace gfx
