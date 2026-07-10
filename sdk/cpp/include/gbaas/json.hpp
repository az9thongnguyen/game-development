// =============================================================================
//  gbaas/json.hpp  —  a minimal JSON for the SDK (parse + string-escape)
// =============================================================================
//  The SDK is game-facing and must not drag Drogon/jsoncpp into a game or the WASM
//  build, so it carries its own tiny JSON — enough to parse the small backend
//  responses and escape strings when building request bodies. Not a general-purpose
//  library; deliberately small. Header-only.
// =============================================================================
#pragma once

#include <cstddef>
#include <cstdlib>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace gbaas::json {

struct Value {
    enum class T { Null, Bool, Int, Dbl, Str, Arr, Obj };
    T                                       t = T::Null;
    bool                                    b = false;
    long long                               i = 0;   // 64-bit: wasm32/Windows have 32-bit long
    double                                  d = 0.0;
    std::string                             s;
    std::vector<Value>                      arr;
    std::vector<std::pair<std::string, Value>> obj;

    static const Value& null() {
        static const Value v;
        return v;
    }

    bool        is_null() const { return t == T::Null; }
    std::size_t size() const {
        return t == T::Arr ? arr.size() : (t == T::Obj ? obj.size() : 0);
    }
    bool has(const std::string& k) const {
        for (const auto& kv : obj)
            if (kv.first == k) return true;
        return false;
    }
    const Value& operator[](const std::string& k) const {
        for (const auto& kv : obj)
            if (kv.first == k) return kv.second;
        return null();
    }
    const Value& operator[](std::size_t idx) const {
        return idx < arr.size() ? arr[idx] : null();
    }
    std::string as_string(const std::string& def = "") const { return t == T::Str ? s : def; }
    long long   as_int(long long def = 0) const {
        return t == T::Int ? i : (t == T::Dbl ? static_cast<long long>(d) : def);
    }
    bool as_bool(bool def = false) const { return t == T::Bool ? b : def; }
};

namespace detail {

inline void append_utf8(std::string& out, unsigned long cp) {
    if (cp <= 0x7F) {
        out += static_cast<char>(cp);
    } else if (cp <= 0x7FF) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp <= 0xFFFF) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

struct Parser {
    const char*          p;
    const char*          end;
    int                  depth = 0;         // guards against stack-overflow on nested input
    static constexpr int kMaxDepth = 64;

    void skip_ws() {
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) ++p;
    }
    bool parse_hex4(unsigned long& out) {
        if (end - p < 4) return false;
        out = 0;
        for (int k = 0; k < 4; ++k) {
            const char c = *p++;
            out <<= 4;
            if (c >= '0' && c <= '9') out |= static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f') out |= static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') out |= static_cast<unsigned>(c - 'A' + 10);
            else return false;
        }
        return true;
    }
    bool parse_string(std::string& out) {
        if (p >= end || *p != '"') return false;
        ++p;
        while (p < end && *p != '"') {
            char c = *p++;
            if (c != '\\') { out += c; continue; }
            if (p >= end) return false;
            const char e = *p++;
            switch (e) {
                case '"':  out += '"';  break;
                case '\\': out += '\\'; break;
                case '/':  out += '/';  break;
                case 'n':  out += '\n'; break;
                case 't':  out += '\t'; break;
                case 'r':  out += '\r'; break;
                case 'b':  out += '\b'; break;
                case 'f':  out += '\f'; break;
                case 'u': {
                    unsigned long cp = 0;
                    if (!parse_hex4(cp)) return false;
                    if (cp >= 0xD800 && cp <= 0xDBFF) {
                        // High surrogate: MUST be followed by a valid low surrogate.
                        if (end - p < 2 || p[0] != '\\' || p[1] != 'u') return false;
                        p += 2;
                        unsigned long lo = 0;
                        if (!parse_hex4(lo)) return false;
                        if (lo < 0xDC00 || lo > 0xDFFF) return false;   // not a low surrogate
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                        return false;   // lone low surrogate → malformed
                    }
                    append_utf8(out, cp);
                    break;
                }
                default: return false;
            }
        }
        if (p >= end) return false;   // unterminated
        ++p;                          // closing quote
        return true;
    }
    bool parse_value(Value& v) {
        skip_ws();
        if (p >= end) return false;
        switch (*p) {
            case '{': return parse_object(v);
            case '[': return parse_array(v);
            case '"': v.t = Value::T::Str; return parse_string(v.s);
            case 't':
                if (end - p >= 4 && std::string(p, p + 4) == "true") { p += 4; v.t = Value::T::Bool; v.b = true; return true; }
                return false;
            case 'f':
                if (end - p >= 5 && std::string(p, p + 5) == "false") { p += 5; v.t = Value::T::Bool; v.b = false; return true; }
                return false;
            case 'n':
                if (end - p >= 4 && std::string(p, p + 4) == "null") { p += 4; v.t = Value::T::Null; return true; }
                return false;
            default: return parse_number(v);
        }
    }
    bool parse_number(Value& v) {
        const char* start = p;
        bool        is_dbl = false;
        if (p < end && (*p == '-' || *p == '+')) ++p;
        while (p < end && ((*p >= '0' && *p <= '9') || *p == '.' || *p == 'e' || *p == 'E' ||
                           *p == '+' || *p == '-')) {
            if (*p == '.' || *p == 'e' || *p == 'E') is_dbl = true;
            ++p;
        }
        if (p == start) return false;
        const std::string tok(start, p);
        if (is_dbl) { v.t = Value::T::Dbl; v.d = std::strtod(tok.c_str(), nullptr); }
        else        { v.t = Value::T::Int; v.i = std::strtoll(tok.c_str(), nullptr, 10); }
        return true;
    }
    bool parse_array(Value& v) {
        if (++depth > kMaxDepth) return false;   // bound recursion (failure aborts the whole parse)
        v.t = Value::T::Arr;
        ++p;  // [
        skip_ws();
        if (p < end && *p == ']') { ++p; --depth; return true; }
        while (true) {
            Value item;
            if (!parse_value(item)) return false;
            v.arr.push_back(std::move(item));
            skip_ws();
            if (p >= end) return false;
            if (*p == ',') { ++p; continue; }
            if (*p == ']') { ++p; --depth; return true; }
            return false;
        }
    }
    bool parse_object(Value& v) {
        if (++depth > kMaxDepth) return false;   // bound recursion (failure aborts the whole parse)
        v.t = Value::T::Obj;
        ++p;  // {
        skip_ws();
        if (p < end && *p == '}') { ++p; --depth; return true; }
        while (true) {
            skip_ws();
            std::string key;
            if (!parse_string(key)) return false;
            skip_ws();
            if (p >= end || *p != ':') return false;
            ++p;
            Value val;
            if (!parse_value(val)) return false;
            v.obj.emplace_back(std::move(key), std::move(val));
            skip_ws();
            if (p >= end) return false;
            if (*p == ',') { ++p; continue; }
            if (*p == '}') { ++p; --depth; return true; }
            return false;
        }
    }
};

}  // namespace detail

inline std::optional<Value> parse(const std::string& text) {
    detail::Parser parser{text.data(), text.data() + text.size()};
    Value          v;
    if (!parser.parse_value(v)) return std::nullopt;
    parser.skip_ws();
    if (parser.p != parser.end) return std::nullopt;   // trailing garbage
    return v;
}

// Escape a string for embedding in a JSON body we build by hand.
inline std::string escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\t': out += "\\t";  break;
            case '\r': out += "\\r";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    static const char* hex = "0123456789abcdef";
                    out += "\\u00";
                    out += hex[(c >> 4) & 0xF];
                    out += hex[c & 0xF];
                } else {
                    out += c;
                }
        }
    }
    return out;
}

}  // namespace gbaas::json
