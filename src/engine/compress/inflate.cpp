// =============================================================================
//  engine/compress/inflate.cpp  —  see inflate.hpp
// =============================================================================
#include "engine/compress/inflate.hpp"

#include <array>

namespace zip {
namespace {

// ---- the bit reader -------------------------------------------------------------
// DEFLATE reads bits LSB-first within a byte, and Huffman codes are stored with
// their bits REVERSED relative to that. Getting those two conventions the wrong way
// round produces a stream that decodes for a while and then explodes, which is the
// least helpful failure mode available — so both live here, named, in one place.
class Bits {
public:
    Bits(const std::uint8_t* p, std::size_t n) : p_(p), n_(n) {}

    bool need(int count) const {
        return static_cast<std::size_t>(pos_) + static_cast<std::size_t>((held_ + count + 7) / 8) <= n_ + 1;
    }

    // `count` bits, LSB-first. Returns false when the stream runs out.
    bool get(int count, std::uint32_t& out) {
        while (held_ < count) {
            if (pos_ >= n_) return false;
            acc_ |= static_cast<std::uint32_t>(p_[pos_++]) << held_;
            held_ += 8;
        }
        out = acc_ & ((count == 32) ? 0xFFFFFFFFu : ((1u << count) - 1u));
        acc_ >>= count;
        held_ -= count;
        return true;
    }

    void align() {                    // discard to the next byte boundary
        const int drop = held_ % 8;
        acc_ >>= drop;
        held_ -= drop;
    }

    // Whole bytes, only valid straight after align().
    bool bytes(std::uint8_t* dst, std::size_t count) {
        while (count > 0 && held_ >= 8) {
            *dst++ = static_cast<std::uint8_t>(acc_ & 0xFF);
            acc_ >>= 8;
            held_ -= 8;
            --count;
        }
        if (pos_ + count > n_) return false;
        for (std::size_t i = 0; i < count; ++i) *dst++ = p_[pos_++];
        return true;
    }

private:
    const std::uint8_t* p_;
    std::size_t         n_;
    std::size_t         pos_  = 0;
    std::uint32_t       acc_  = 0;
    int                 held_ = 0;
};

// ---- canonical Huffman ----------------------------------------------------------
// Built from code LENGTHS alone, which is all DEFLATE transmits: sort by (length,
// symbol) and hand out consecutive codes. Decoding walks one bit at a time and
// compares against the first code of each length — slower than a lookup table, and
// small enough to be obviously correct, which is the trade this project wants.
struct Huffman {
    std::array<int, 16>   count{};    // how many codes of each length
    std::vector<int>      symbol;     // symbols ordered by (length, symbol)

    bool build(const std::vector<int>& lengths) {
        count.fill(0);
        for (int l : lengths) {
            if (l < 0 || l > 15) return false;
            ++count[static_cast<std::size_t>(l)];
        }
        count[0] = 0;                       // length 0 means "symbol not present"

        // Over-subscribed sets are the classic corrupt-stream signature; an
        // incomplete set is legal only in the one-symbol case, which `left` allows.
        int left = 1;
        for (int len = 1; len <= 15; ++len) {
            left <<= 1;
            left -= count[static_cast<std::size_t>(len)];
            if (left < 0) return false;
        }

        std::array<int, 16> offs{};
        for (int len = 1; len < 15; ++len)
            offs[static_cast<std::size_t>(len + 1)] =
                offs[static_cast<std::size_t>(len)] + count[static_cast<std::size_t>(len)];

        symbol.assign(lengths.size(), 0);
        for (std::size_t sym = 0; sym < lengths.size(); ++sym) {
            const int l = lengths[sym];
            if (l != 0) symbol[static_cast<std::size_t>(offs[static_cast<std::size_t>(l)]++)] =
                            static_cast<int>(sym);
        }
        return true;
    }

    // -1 on a truncated stream or a code that is not in the set.
    int decode(Bits& b) const {
        int code = 0, first = 0, index = 0;
        for (int len = 1; len <= 15; ++len) {
            std::uint32_t bit = 0;
            if (!b.get(1, bit)) return -1;
            code |= static_cast<int>(bit);
            const int n = count[static_cast<std::size_t>(len)];
            if (code - first < n) return symbol[static_cast<std::size_t>(index + (code - first))];
            index += n;
            first = (first + n) << 1;
            code <<= 1;
        }
        return -1;
    }
};

// RFC 1951 §3.2.5. Length codes 257..285 and distance codes 0..29.
constexpr int kLenBase[29]  = {3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,
                               67,83,99,115,131,163,195,227,258};
constexpr int kLenExtra[29] = {0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
constexpr int kDistBase[30] = {1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,
                               1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
constexpr int kDistExtra[30]= {0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};

bool fail(std::string* why, const char* msg) {
    if (why) *why = msg;
    return false;
}

bool block_codes(Bits& b, const Huffman& lit, const Huffman& dist,
                 std::vector<std::uint8_t>& out, std::string* why) {
    for (;;) {
        const int sym = lit.decode(b);
        if (sym < 0)   return fail(why, "truncated or invalid literal/length code");
        if (sym < 256) { out.push_back(static_cast<std::uint8_t>(sym)); continue; }
        if (sym == 256) return true;                      // end of block
        const int li = sym - 257;
        if (li >= 29) return fail(why, "invalid length symbol");

        std::uint32_t extra = 0;
        if (!b.get(kLenExtra[li], extra)) return fail(why, "truncated length extra bits");
        const int length = kLenBase[li] + static_cast<int>(extra);

        const int dsym = dist.decode(b);
        if (dsym < 0 || dsym >= 30) return fail(why, "invalid distance code");
        if (!b.get(kDistExtra[dsym], extra)) return fail(why, "truncated distance extra bits");
        const std::size_t distance = static_cast<std::size_t>(kDistBase[dsym]) + extra;

        // A distance reaching before the start of the output is the difference
        // between a corrupt file and reading someone else's memory.
        if (distance > out.size()) return fail(why, "distance points before the output");
        const std::size_t from = out.size() - distance;
        for (int i = 0; i < length; ++i) out.push_back(out[from + static_cast<std::size_t>(i)]);
    }
}

// The fixed tables of RFC 1951 §3.2.6, built once from their code lengths rather
// than written out — the spec defines them that way, and so a typo in a 288-entry
// literal table cannot exist.
const Huffman& fixed_lit() {
    static const Huffman h = [] {
        std::vector<int> l(288);
        for (int i = 0;   i < 144; ++i) l[static_cast<std::size_t>(i)] = 8;
        for (int i = 144; i < 256; ++i) l[static_cast<std::size_t>(i)] = 9;
        for (int i = 256; i < 280; ++i) l[static_cast<std::size_t>(i)] = 7;
        for (int i = 280; i < 288; ++i) l[static_cast<std::size_t>(i)] = 8;
        Huffman x;
        x.build(l);
        return x;
    }();
    return h;
}

const Huffman& fixed_dist() {
    static const Huffman h = [] {
        Huffman x;
        x.build(std::vector<int>(30, 5));
        return x;
    }();
    return h;
}

bool dynamic_tables(Bits& b, Huffman& lit, Huffman& dist, std::string* why) {
    std::uint32_t hlit = 0, hdist = 0, hclen = 0;
    if (!b.get(5, hlit) || !b.get(5, hdist) || !b.get(4, hclen))
        return fail(why, "truncated dynamic block header");
    const std::size_t nlit  = hlit + 257;
    const std::size_t ndist = hdist + 1;
    const std::size_t nclen = hclen + 4;

    // The order the code-length code lengths arrive in (RFC 1951 §3.2.7) — the most
    // useful ones first, so the tail can be omitted.
    static const int kOrder[19] = {16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
    std::vector<int> clen(19, 0);
    for (std::size_t i = 0; i < nclen; ++i) {
        std::uint32_t v = 0;
        if (!b.get(3, v)) return fail(why, "truncated code-length lengths");
        clen[static_cast<std::size_t>(kOrder[i])] = static_cast<int>(v);
    }
    Huffman cl;
    if (!cl.build(clen)) return fail(why, "invalid code-length code");

    std::vector<int> lengths;
    lengths.reserve(nlit + ndist);
    while (lengths.size() < nlit + ndist) {
        const int sym = cl.decode(b);
        if (sym < 0) return fail(why, "truncated code-length stream");
        if (sym < 16) { lengths.push_back(sym); continue; }

        int repeat = 0, value = 0;
        std::uint32_t extra = 0;
        if (sym == 16) {
            if (lengths.empty()) return fail(why, "repeat with no previous length");
            value = lengths.back();
            if (!b.get(2, extra)) return fail(why, "truncated repeat");
            repeat = 3 + static_cast<int>(extra);
        } else if (sym == 17) {
            if (!b.get(3, extra)) return fail(why, "truncated zero-run");
            repeat = 3 + static_cast<int>(extra);
        } else {
            if (!b.get(7, extra)) return fail(why, "truncated long zero-run");
            repeat = 11 + static_cast<int>(extra);
        }
        if (lengths.size() + static_cast<std::size_t>(repeat) > nlit + ndist)
            return fail(why, "code lengths overrun the table");
        for (int i = 0; i < repeat; ++i) lengths.push_back(value);
    }

    if (!lit.build(std::vector<int>(lengths.begin(), lengths.begin() + static_cast<long>(nlit))))
        return fail(why, "invalid literal/length code");
    if (!dist.build(std::vector<int>(lengths.begin() + static_cast<long>(nlit), lengths.end())))
        return fail(why, "invalid distance code");
    return true;
}

} // namespace

std::uint32_t adler32(const std::uint8_t* data, std::size_t n) {
    std::uint32_t a = 1, b = 0;
    for (std::size_t i = 0; i < n; ++i) {
        a = (a + data[i]) % 65521u;
        b = (b + a) % 65521u;
    }
    return (b << 16) | a;
}

std::optional<std::vector<std::uint8_t>> inflate_raw(const std::vector<std::uint8_t>& in,
                                                     std::size_t expected, std::string* why) {
    if (why) why->clear();
    Bits b(in.data(), in.size());
    std::vector<std::uint8_t> out;
    out.reserve(expected);

    for (;;) {
        std::uint32_t final_block = 0, type = 0;
        if (!b.get(1, final_block) || !b.get(2, type)) { fail(why, "truncated block header"); return std::nullopt; }

        if (type == 0) {                 // stored
            b.align();
            std::uint8_t hdr[4];
            if (!b.bytes(hdr, 4)) { fail(why, "truncated stored header"); return std::nullopt; }
            const std::size_t len  = static_cast<std::size_t>(hdr[0]) | (static_cast<std::size_t>(hdr[1]) << 8);
            const std::size_t nlen = static_cast<std::size_t>(hdr[2]) | (static_cast<std::size_t>(hdr[3]) << 8);
            if ((len ^ 0xFFFFu) != nlen) { fail(why, "stored block LEN/NLEN mismatch"); return std::nullopt; }
            const std::size_t at = out.size();
            out.resize(at + len);
            if (len > 0 && !b.bytes(out.data() + at, len)) { fail(why, "truncated stored block"); return std::nullopt; }
        } else if (type == 1) {
            if (!block_codes(b, fixed_lit(), fixed_dist(), out, why)) return std::nullopt;
        } else if (type == 2) {
            Huffman lit, dist;
            if (!dynamic_tables(b, lit, dist, why)) return std::nullopt;
            if (!block_codes(b, lit, dist, out, why)) return std::nullopt;
        } else {
            fail(why, "reserved block type 3");
            return std::nullopt;
        }

        if (final_block) break;
    }
    return out;
}

std::optional<std::vector<std::uint8_t>> inflate_zlib(const std::vector<std::uint8_t>& in,
                                                      std::size_t expected, std::string* why) {
    if (why) why->clear();
    if (in.size() < 6) { fail(why, "zlib stream too short"); return std::nullopt; }

    const int cmf = in[0], flg = in[1];
    if ((cmf & 0x0F) != 8)                { fail(why, "not deflate (CM != 8)"); return std::nullopt; }
    if (((cmf << 8) + flg) % 31 != 0)     { fail(why, "bad zlib header check"); return std::nullopt; }
    if (flg & 0x20)                       { fail(why, "preset dictionary not supported"); return std::nullopt; }

    const std::vector<std::uint8_t> body(in.begin() + 2, in.end() - 4);
    auto out = inflate_raw(body, expected, why);
    if (!out) return std::nullopt;

    const std::uint32_t want = (static_cast<std::uint32_t>(in[in.size() - 4]) << 24) |
                               (static_cast<std::uint32_t>(in[in.size() - 3]) << 16) |
                               (static_cast<std::uint32_t>(in[in.size() - 2]) << 8)  |
                                static_cast<std::uint32_t>(in[in.size() - 1]);
    if (adler32(out->data(), out->size()) != want) { fail(why, "adler-32 mismatch"); return std::nullopt; }
    return out;
}

} // namespace zip
