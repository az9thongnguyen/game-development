# Chapter 53 — Authentication: Passwords, JWT, Guests

> **What this is.** How players get identity: password accounts, anonymous
> **guest** accounts, and the **JSON Web Token** that proves who they are on every
> later request. You'll learn why we hash with **argon2id** (and never store
> plaintext), how a **HS256 JWT** is just a signed envelope we assemble over
> libsodium's HMAC (no JWT library needed), the **threat model** that shapes small
> decisions (no user enumeration), and the `AuthFilter` that guards logged-in
> routes. Code: `baas/auth/{password,jwt,auth_service,auth_controller}.*`,
> `baas/gateway/auth_filter.*`.

---

## 1. Passwords: argon2id, never plaintext

We never store or compare a plaintext password. libsodium's `crypto_pwhash_str`
produces a self-describing encoded string (algorithm + parameters + salt + digest);
verification is constant-time. This is a place where **not** hand-rolling is the
correct, safe choice — crypto belongs in audited code.

```cpp
// baas/auth/password.cc
std::string hash(const std::string& pw) {
    char out[crypto_pwhash_STRBYTES];
    if (crypto_pwhash_str(out, pw.c_str(), pw.size(),
            crypto_pwhash_OPSLIMIT_INTERACTIVE, crypto_pwhash_MEMLIMIT_INTERACTIVE) != 0)
        throw std::runtime_error("password hashing failed");
    return std::string(out);                        // e.g. "$argon2id$v=19$m=…"
}
bool verify(const std::string& pw, const std::string& h) {
    return crypto_pwhash_str_verify(h.c_str(), pw.c_str(), pw.size()) == 0;
}
```

argon2id is *memory-hard*: it deliberately costs RAM as well as CPU, which blunts
GPU/ASIC cracking. Two hashes of the same password differ (random salt) — a property
our unit test checks. `sodium_init()` must succeed once at startup before any of this.

## 2. A JWT is an envelope, not magic

After you authenticate, the server hands back an **access token** you attach to later
requests. We use a **JWT with HS256**, which is exactly:

```
base64url(header) "." base64url(payload) "." base64url( HMAC-SHA256(secret, header"."payload) )
```

The header says `{"alg":"HS256","typ":"JWT"}`; the payload carries our **claims**:
`sub` = user id, `pid` = project id, `iat`/`exp` = issued-at / expiry. The signature
is a keyed hash: anyone can *read* a JWT (it's just base64), but only the holder of
the secret can *forge* one, and any edit invalidates the signature.

There is no brew formula for `jwt-cpp`, and it would drag an OpenSSL backend — so we
assemble the envelope ourselves over libsodium's **`crypto_auth_hmacsha256`** (which
*is* standard HMAC-SHA256; note libsodium's plain `crypto_auth` is a different MAC).
We are not implementing crypto — just base64url + a call to an audited primitive.

```cpp
// baas/auth/jwt.cc  (issue, abridged)
const std::string signing_input = b64url(compact_json(header)) + "." + b64url(compact_json(payload));
unsigned char mac[crypto_auth_hmacsha256_BYTES];
hmac(secret, signing_input, mac);                    // crypto_auth_hmacsha256_{init,update,final}
return signing_input + "." + b64url(mac, sizeof(mac));
```

**Verification** recomputes the MAC over `header"."payload` and compares it to the
token's signature with **`sodium_memcmp`** (constant-time — a byte-by-byte `==` leaks
timing), then checks `exp`. Any failure — malformed, wrong key, tampered, expired —
returns `nullopt`:

```cpp
if (sig->size() != sizeof(mac) || sodium_memcmp(sig->data(), mac, sizeof(mac)) != 0)
    return std::nullopt;                              // bad signature
if (!j.isMember("exp") || (time_t)j["exp"].asInt64() < std::time(nullptr))
    return std::nullopt;                              // expired
```

The signing secret comes from `--jwt-secret` / `BAAS_JWT_SECRET` (`app_config`), never
the source tree; a missing secret falls back to a dev value **with a warning**.

## 3. The three ways in

`auth_service.cc` is pure-ish logic over the DB, always scoped by `project_id`:

- **register** — validate (email + display name present, password ≥ 6), reject a
  duplicate email in this project with **409**, hash, insert, return the user.
- **login** — look up by (project, email), verify the hash.
- **guest** — insert an `is_guest` user with no email/password.

**Guest login is not an afterthought** — it's table stakes for games. Players want to
tap "play" and *start*, registering later. So `POST /v1/auth/guest` mints a real
account and a real token immediately; upgrading a guest to a full account is a later
slice.

The controller turns a service result into the shared success shape and issues the
JWT:

```cpp
// {user:{user_id,display_name,is_guest}, access_token:"<jwt>"}
const std::string token = jwt::issue(u.id, project_id, config().jwt_secret, config().jwt_ttl_seconds);
```

All three routes sit behind `ApiKeyFilter` (you need a valid *project* to create or
authenticate a user) but **not** behind `AuthFilter` — you can't require a login in
order to log in. (Also: `register` is a C++ keyword, so the method is named `reg`.)

## 4. Threat model — small decisions, on purpose

- **No user enumeration.** Login returns the *identical* 401 whether the email is
  unknown or the password is wrong. If the two differed, an attacker could probe which
  emails have accounts. Our integration test asserts both responses match.
- **Passwords never leave in a response** — the user object omits `password_hash`.
- **Constant-time everywhere it matters** — password verify and MAC compare.
- **Short-lived tokens** — `exp` bounds the blast radius of a stolen token; refresh
  tokens are a later slice.

## 5. The "must be logged in" gate: `AuthFilter`

Routes that act on *a specific user* (submitting a score, reading "my rank") run
`AuthFilter` **after** `ApiKeyFilter`. It verifies the Bearer token, checks the
token's project matches the request's project, and leaves the user id behind:

```cpp
// baas/gateway/auth_filter.cc
const auto claims = jwt::verify(header.substr(7 /*"Bearer "*/), config().jwt_secret);
if (!claims) { fcb(make_error(401,"unauthorized","invalid or expired token")); return; }
if (claims->pid != req->attributes()->get<long>(kProjectId)) {   // token ≠ this project
    fcb(make_error(401,"unauthorized","token does not match project")); return;
}
req->attributes()->insert(kUserId, claims->sub);   // controllers trust THIS, not the body
fccb();
```

That last line is the whole anti-spoof story (Chapter 55): the user id comes from the
*verified token*, so a client cannot submit a score as someone else no matter what it
puts in the request body.

## 6. Checkpoints

- Why HMAC compare with `sodium_memcmp` instead of `==`? What does a timing leak give
  an attacker here?
- Login for an unknown email and for a wrong password return the same thing — why?
- A client sends a valid token from *another* project along with this project's API
  key. What happens, and which check stops it?
