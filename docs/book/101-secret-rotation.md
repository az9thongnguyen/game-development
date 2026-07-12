# Chapter 101 — Secret Rotation: Prove You Hold It, Get a New One

> Code: `baas/admin/admin_service.{h,cc}` (`rotate_secret`) ·
> `baas/admin/admin_controller.cc` (`rotateSecret`) · `baas/gateway/admin_filters.cc`
> (`SecretKeyFilter`) · `tests/test_baas_secret_rotation.cc`

Horizon 2's RBAC/audit item lists "project roles, short-lived credentials, secret rotation,
audit logs, and separate operator/server/client trust domains." Chapter 98 built the audit log;
the trust domains already exist as filters. This chapter adds **secret rotation** — the ability
to replace a leaked or aging project secret without recreating the project.

## The trust the project secret carries

A project has one secret key, minted once at creation, stored only as an argon2id hash. Operator
endpoints (`/v1/admin/config`, `/v1/admin/events`, …) sit behind `SecretKeyFilter`, which
verifies the `X-Secret-Key` header against that hash on every request. The secret is the operator
trust domain: holding it means you can change a running game's config and events.

The gap: minted once, it lived forever. A secret that leaks — a screenshot, a log line, an
ex-teammate — could never be revoked short of destroying and rebuilding the project. That is not
an acceptable posture for "operate a real small game."

## Rotation is a hash swap, gated by proof of possession

`rotate_secret` is deliberately tiny — the security is in *where it sits*, not in the code:

```cpp
std::string rotate_secret(long project_id) {
    const std::string sec = rand_token("sk_", 16);          // fresh, libsodium-random
    db::client()->execSqlSync("UPDATE projects SET secret_key_hash=? WHERE id=?",
                              pw::hash(sec), project_id);
    audit::record(project_id, "admin", "secret.rotate", "project secret rotated");
    return sec;                                              // shown once
}
```

The endpoint `POST /v1/admin/secret/rotate` is behind `ApiKeyFilter` **and** `SecretKeyFilter` —
so it is reachable only by a caller who already proved it holds the *current* secret. That is the
right gate: rotation is a self-service "I have it, give me a new one," not an admin override.
The moment the `UPDATE` lands, the old secret stops verifying (the stored hash no longer matches
it), so a leaked secret is dead the instant its holder rotates. The new plaintext is returned
once, exactly like creation, and the rotation is on the audit trail.

## The proof

`tests/test_baas_secret_rotation.cc` runs the real service against a temp DB:

1. Create a project; its returned secret verifies against the stored hash (the exact check
   `SecretKeyFilter` runs).
2. Rotate; the new secret differs and the stored hash changes.
3. The new secret verifies; **the old secret no longer does** — immediate invalidation.
4. `audit::recent` contains a `secret.rotate` entry.

Step 3 is the whole point: rotation that left the old secret working would be theatre. What is
*not* here yet, and is named honestly as later RBAC slices: multiple operators per project with
distinct **roles** (owner/admin/viewer), and **short-lived credentials** (issue a time-boxed token
rather than a long-lived secret). Those need an operators table and a token-issuance design; a
single rotatable project secret is the right first rung, and it composes with the audit log
already in place.
