# Chapter 99 — A Reversible LiveOps Change Without Redeploying the Client

> Code: `baas/remote_config/config_service.{h,cc}` (`set_audited`, `remove_audited`) ·
> `baas/admin/admin_controller.cc` (the operator endpoints) ·
> `tests/test_baas_liveops.cc`

The Horizon 2 exit gate names three things the platform must do: survive a documented
failure drill (Chapter 98), *measure a release*, and *run a reversible LiveOps change without
client redeployment*. This chapter closes the third. It is a small change — the LiveOps
*mechanism* already existed — and the interesting part is what "reversible" actually demands.

## The mechanism was already there

Remote config is server-controlled key/value settings a game reads at runtime: feature flags,
tunables, a message of the day. The client fetches them from `/v1/config` (api-key gated); an
operator writes them through `PUT /v1/admin/config/{key}` (secret gated). That is already a
LiveOps change *without client redeployment*: flip `max_agents` from 50 to 80 on the server and
every client picks it up on its next fetch — no rebuild, no app-store round trip. Nothing new
is needed for the "without redeployment" half.

## What "reversible" actually requires

The old `set` just overwrote the value:

```cpp
void set(long pid, const std::string& key, const std::string& value);   // upsert, no memory
```

Once it ran, the previous value was gone. To revert, an operator had to *remember* what it used
to be. That is not a reversible change; it is a one-way change you hope you can undo from
memory. Reversibility needs two things the old path didn't provide: the prior value handed back,
and a durable record of the transition.

`set_audited` provides both by composing two things already built — `get` and the audit log
from Chapter 98:

```cpp
std::optional<std::string> set_audited(long pid, const std::string& key,
                                       const std::string& value, const std::string& actor) {
    const auto old = get(pid, key);                       // the revert target
    set(pid, key, value);
    audit::record(pid, actor, "config.set",
                  "key=" + key + " old=" + old.value_or("<unset>") + " new=" + value);
    return old;                                            // hand it back to the caller
}
```

The return value *is* the reversibility: to undo, the operator calls `set_audited` again with the
value it just returned. The audit row is the durable record — if the operator is a different
person, or it is a week later, `audit::recent` shows `key=max_agents old=50 new=80` and the undo
is unambiguous. `remove_audited` is the symmetric case: it records the deleted key's old value so
even a deletion is revertible via a subsequent set. The admin endpoints now return that prior
value as `"previous"` in their JSON, so the dashboard can offer a one-click revert.

This is the ponytail move in its purest form: no new table, no versioned-config history
subsystem, no rollback state machine. Reversibility falls out of composing `get` +
`audit::record` + returning the old value. A full config *version history* (every value a key has
ever held, point-in-time restore) is a real feature — and a speculative one until an operator
asks for more than "what was it just before." The audit log already answers the question people
actually have.

## The proof

`tests/test_baas_liveops.cc` runs the whole loop against the real config service, no HTTP server
in the way:

1. First set of a new key → `previous` is unset; the client read path (`cfg::get`, exactly what
   `/v1/config` serves) returns the new value.
2. Change it (50 → 80) → `get` returns 80 with **no client code change**; `set_audited` returns
   50, the revert target.
3. The audit trail's newest row is `config.set` with `old=50 new=80`.
4. Set the returned 50 back → `get` returns 50 again. The change was reversible.
5. `remove_audited` returns the old value and records `config.delete`; deleting a missing key is
   a quiet no-op.

Same `cfg::get` throughout — the client never learns a change happened except by reading the new
value. That is the exit-gate clause, demonstrated: a reversible, audited LiveOps change with the
client none the wiser and never rebuilt.
