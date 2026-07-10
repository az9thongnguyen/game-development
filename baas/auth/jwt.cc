// =============================================================================
//  baas/auth/jwt.cc  —  see jwt.h
// =============================================================================
#include "baas/auth/jwt.h"

#include <cstring>
#include <ctime>
#include <memory>
#include <vector>

#include <json/json.h>
#include <sodium.h>

namespace web::jwt {
namespace {

std::string b64url(const unsigned char* data, std::size_t len) {
    const std::size_t cap =
        sodium_base64_encoded_len(len, sodium_base64_VARIANT_URLSAFE_NO_PADDING);
    std::string out(cap, '\0');
    sodium_bin2base64(out.data(), cap, data, len,
                      sodium_base64_VARIANT_URLSAFE_NO_PADDING);
    out.resize(std::strlen(out.c_str()));   // cap includes the NUL terminator
    return out;
}
std::string b64url(const std::string& s) {
    return b64url(reinterpret_cast<const unsigned char*>(s.data()), s.size());
}

std::optional<std::vector<unsigned char>> b64url_decode(const std::string& s) {
    std::vector<unsigned char> out(s.size());   // decoded length <= input length
    std::size_t                out_len = 0;
    if (sodium_base642bin(out.data(), out.size(), s.data(), s.size(), nullptr,
                          &out_len, nullptr,
                          sodium_base64_VARIANT_URLSAFE_NO_PADDING) != 0)
        return std::nullopt;
    out.resize(out_len);
    return out;
}

void hmac(const std::string& key, const std::string& msg,
          unsigned char out[crypto_auth_hmacsha256_BYTES]) {
    crypto_auth_hmacsha256_state st;
    crypto_auth_hmacsha256_init(&st,
                                reinterpret_cast<const unsigned char*>(key.data()),
                                key.size());
    crypto_auth_hmacsha256_update(
        &st, reinterpret_cast<const unsigned char*>(msg.data()), msg.size());
    crypto_auth_hmacsha256_final(&st, out);
}

std::string compact_json(const Json::Value& v) {
    Json::StreamWriterBuilder w;
    w["indentation"] = "";
    return Json::writeString(w, v);
}

}  // namespace

std::string issue(long user_id, long project_id, const std::string& secret,
                  int ttl_seconds) {
    const std::time_t now = std::time(nullptr);

    Json::Value header;
    header["alg"] = "HS256";
    header["typ"] = "JWT";

    Json::Value payload;
    payload["sub"] = static_cast<Json::Int64>(user_id);
    payload["pid"] = static_cast<Json::Int64>(project_id);
    payload["iat"] = static_cast<Json::Int64>(now);
    payload["exp"] = static_cast<Json::Int64>(now + ttl_seconds);

    const std::string signing_input =
        b64url(compact_json(header)) + "." + b64url(compact_json(payload));

    unsigned char mac[crypto_auth_hmacsha256_BYTES];
    hmac(secret, signing_input, mac);
    return signing_input + "." + b64url(mac, sizeof(mac));
}

std::optional<Claims> verify(const std::string& token, const std::string& secret) {
    const auto d1 = token.find('.');
    if (d1 == std::string::npos) return std::nullopt;
    const auto d2 = token.find('.', d1 + 1);
    if (d2 == std::string::npos) return std::nullopt;
    if (token.find('.', d2 + 1) != std::string::npos) return std::nullopt;  // extra dot

    const std::string signing_input = token.substr(0, d2);
    const std::string sig_b64       = token.substr(d2 + 1);

    // Recompute the MAC and compare in constant time.
    unsigned char mac[crypto_auth_hmacsha256_BYTES];
    hmac(secret, signing_input, mac);
    const auto sig = b64url_decode(sig_b64);
    if (!sig || sig->size() != sizeof(mac)) return std::nullopt;
    if (sodium_memcmp(sig->data(), mac, sizeof(mac)) != 0) return std::nullopt;  // bad signature

    // Signature is valid → parse the payload for claims + expiry.
    const auto payload_bytes = b64url_decode(token.substr(d1 + 1, d2 - d1 - 1));
    if (!payload_bytes) return std::nullopt;
    const std::string payload(payload_bytes->begin(), payload_bytes->end());

    Json::Value                       j;
    Json::CharReaderBuilder           rb;
    std::string                       errs;
    const std::unique_ptr<Json::CharReader> reader(rb.newCharReader());
    if (!reader->parse(payload.data(), payload.data() + payload.size(), &j, &errs))
        return std::nullopt;

    const std::time_t now = std::time(nullptr);
    if (!j.isMember("exp") || static_cast<std::time_t>(j["exp"].asInt64()) < now)
        return std::nullopt;  // expired / missing exp

    return Claims{static_cast<long>(j["sub"].asInt64()),
                  static_cast<long>(j["pid"].asInt64())};
}

}  // namespace web::jwt
