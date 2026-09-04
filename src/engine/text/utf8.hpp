// =============================================================================
//  engine/text/utf8.hpp  —  a UTF-8 decoder, because a byte is not a character
// =============================================================================
//  Every text path in the engine used to walk a string one `char` at a time and
//  coerce anything outside printable ASCII to '?'. A single "→" (U+2192) is three
//  bytes, so it printed as three question marks — and no accented Vietnamese
//  letter could ever appear. Decoding here, once, fixes every caller.
//
//  PURE: no allocation, no I/O, no dependencies. Header-only so the tests and the
//  renderer can share it without a link edge.
// =============================================================================
#pragma once

#include <cstddef>
#include <cstdint>

namespace text {

inline constexpr char32_t kReplacement = 0xFFFD;   // U+FFFD REPLACEMENT CHARACTER

// Decode the scalar value starting at `p` (which must be < `end`), advancing `p`
// past the bytes consumed.
//
// Malformed input — a stray continuation byte, a truncated sequence, a bad
// continuation, an overlong encoding, a UTF-16 surrogate half, or a value above
// U+10FFFF — yields U+FFFD and consumes exactly ONE byte. Consuming one byte (not
// the whole malformed run) matters twice: the caller's loop always makes progress,
// and one corrupt byte cannot swallow the valid text that follows it.
inline char32_t utf8_next(const char*& p, const char* end) {
    const auto bad = [&p]() -> char32_t { ++p; return kReplacement; };

    const auto b0 = static_cast<unsigned char>(*p);
    if (b0 < 0x80) { ++p; return b0; }              // ASCII: the overwhelmingly common case

    int      len;                                    // total bytes in this sequence
    char32_t cp;                                     // accumulating value
    char32_t lowest;                                 // smallest value legal at this length
    if      ((b0 & 0xE0) == 0xC0) { len = 2; cp = b0 & 0x1Fu; lowest = 0x80; }
    else if ((b0 & 0xF0) == 0xE0) { len = 3; cp = b0 & 0x0Fu; lowest = 0x800; }
    else if ((b0 & 0xF8) == 0xF0) { len = 4; cp = b0 & 0x07u; lowest = 0x10000; }
    else                          { return bad(); }  // 10xxxxxx (orphan) or 11111xxx (illegal)

    if (end - p < len) return bad();                 // truncated at the end of the buffer
    for (int i = 1; i < len; ++i) {
        const auto bi = static_cast<unsigned char>(p[i]);
        if ((bi & 0xC0) != 0x80) return bad();       // expected 10xxxxxx
        cp = (cp << 6) | (bi & 0x3Fu);
    }
    // Overlong forms are a security problem, not a curiosity: they let the same
    // character be spelled two ways, so a filter that checks one spelling misses
    // the other. Reject them.
    if (cp < lowest)                    return bad();
    if (cp > 0x10FFFF)                  return bad();
    if (cp >= 0xD800 && cp <= 0xDFFF)   return bad();   // surrogate halves are not scalars

    p += len;
    return cp;
}

// Number of scalar values in a NUL-terminated string (malformed bytes count as one
// U+FFFD each). Only used by the fixed-width fallback font, which advances per
// character rather than per glyph metric.
inline std::size_t utf8_count(const char* s) {
    if (!s) return 0;
    const char* end = s;
    while (*end) ++end;
    std::size_t n = 0;
    for (const char* p = s; p < end; ++n) utf8_next(p, end);
    return n;
}

} // namespace text
