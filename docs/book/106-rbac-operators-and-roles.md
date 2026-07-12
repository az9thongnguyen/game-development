# Chapter 106 — RBAC: Operators, Roles, and an Honest Boundary

> Code: `baas/db/db.cc` (migration 6) · `baas/rbac/rbac.{h,cc}` (`Role`, `authorize`,
> `create_operator`, `authenticate`, `list_operators`) · `baas/admin/admin_controller.cc`
> (`createOperator`, `listOperators`) · `tests/test_baas_rbac.cc`

Chapter 101 gave a project *one* rotatable secret. That is enough for a solo developer and wrong for
a two-person team: the single secret cannot say "my teammate may read the metrics but must not rotate
the secret or change prices." This chapter adds the model that can — named operators, each with their
own key and a role — and it is also a chapter about where to *stop*: the model and its authentication
are built and tested; wiring role enforcement across every endpoint is named as a separate slice, not
rushed in on top.

## Three roles, ordered

`Role` is `Viewer < Admin < Owner`, and the integer order *is* the privilege rank, so the entire
authorization decision is one comparison:

```cpp
bool authorize(Role have, Role need) { return static_cast<int>(have) >= static_cast<int>(need); }
```

An owner clears an admin gate; an admin clears a viewer gate; a viewer clears only viewer gates. There
is no permission matrix, no custom-role editor, no per-resource ACL — a `ponytail:` note says why: a
small team needs read / change / own, not an enterprise RBAC engine. That grows when a real
organization does; until then it would be three abstractions guarding one team.

## An operator is a name, a key, and a role

Migration 6 adds `operators(project_id, name, key_hash, role, UNIQUE(project_id, name))`. Each operator
has their *own* key — distinct from the project secret — hashed with argon2id exactly like a password,
so the store never holds a plaintext credential. `create_operator` mints the key, stores the hash and
role, audits `operator.create`, and returns the plaintext **once**; a duplicate name is refused rather
than silently overwritten.

Authentication identifies *which* operator and yields their role in one lookup:

```cpp
auto rows = /* SELECT key_hash, role WHERE project_id=? AND name=? */;
if (rows.empty()) return nullopt;                        // unknown operator
if (!pw::verify(key, rows[0]["key_hash"])) return nullopt;  // wrong key
return Operator{name, role_from_string(rows[0]["role"])};
```

Because the lookup is keyed by `(project_id, name)`, verifying the key is a single argon2id check
against the right operator's hash — not a scan over every operator. It is also project-scoped: an
operator's key authenticates only within their own project, which the test pins directly.

## The composed gate — and the honest boundary

Authorization in practice is the two primitives composed: authenticate, then `authorize` the returned
role against what the operation needs. The test builds exactly that gate and checks it both ways:

```cpp
auto may_change = [&](name, key) {
    auto op = authenticate(pid, name, key);
    return op && authorize(op->role, Role::Admin);
};
// owner passes, viewer fails, wrong-key fails (not authenticated at all)
```

That is the RBAC foundation, complete and tested: the model, per-operator credentials, authentication,
project scoping, and the authorize gate. What this chapter deliberately does **not** do is rewire the
existing endpoint filters (`ApiKeyFilter` / `SecretKeyFilter`) to demand a specific role per route. That
is a security-critical change to the whole request chain, and doing it on momentum — right after five
other slices — is exactly how an auth regression slips in. It gets its own focused pass: an
`OperatorFilter` that reads the operator credential, resolves the role, and maps each route to a
minimum role, with tests per route. The provisioning endpoints here are gated by the existing project
secret (the secret-holder is the de-facto owner who bootstraps the first operators), so the feature is
usable today; the per-route enforcement is the next, isolated step.

The proof (`tests/test_baas_rbac.cc`) covers the ordering, provisioning (including duplicate and
bad-name refusal), authentication (right key, wrong key, cross-operator key, unknown name, cross-project
key), and the composed gate. Naming what is *not* yet enforced is part of the deliverable: an RBAC model
you can authenticate against is real progress; claiming every endpoint is role-guarded when it is not
would be the kind of security overclaim the strategy's principle 7 ("production readiness is a
*measurable* claim") exists to prevent.
