// =============================================================================
//  engine/compress/inflate.hpp  —  DEFLATE decompression, hand-written
// =============================================================================
//  The project's one hard rule is that SDL2 is the only runtime dependency, so
//  reading a PNG means reading the DEFLATE stream inside it ourselves. This is
//  that: RFC 1951 (deflate) and the RFC 1950 (zlib) wrapper around it.
//
//  It decompresses and does not compress, on purpose. Nothing here needs to WRITE
//  a PNG — `.hrt` is the format this engine ships — and a compressor is a
//  different, larger problem (match finding, Huffman construction) with no
//  consumer today. Half of a codec, where the half is the one that is needed.
//
//  PURE: bytes in, bytes out. No allocation strategy, no I/O, no engine types, so
//  the whole of it is exercised by a unit test against streams a real zlib produced.
// =============================================================================
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace zip {

// Raw DEFLATE (RFC 1951): no header, no checksum. `expected` is a size HINT used to
// reserve, not a limit — a stream that decodes to more is not an error.
//
// Returns nullopt on any malformed input: a bad block type, a distance pointing
// before the start of the output, a truncated stream. `why` (optional) says which,
// because "the PNG did not load" is not a debuggable sentence.
std::optional<std::vector<std::uint8_t>> inflate_raw(const std::vector<std::uint8_t>& in,
                                                     std::size_t  expected = 0,
                                                     std::string* why      = nullptr);

// zlib wrapper (RFC 1950): 2-byte header, deflate data, 4-byte Adler-32.
//
// The checksum IS verified. A PNG whose pixels decompressed to something other than
// what was compressed is a corrupt file, and finding that out at the checksum is
// cheaper than finding it out as wrong colours nobody can explain.
std::optional<std::vector<std::uint8_t>> inflate_zlib(const std::vector<std::uint8_t>& in,
                                                      std::size_t  expected = 0,
                                                      std::string* why      = nullptr);

// Adler-32 over a byte range (RFC 1950 §9). Exposed because the PNG reader has no
// other reason to know how zlib framing works, and a test can pin it directly.
std::uint32_t adler32(const std::uint8_t* data, std::size_t n);

} // namespace zip
