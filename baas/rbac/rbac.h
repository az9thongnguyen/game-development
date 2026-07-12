// =============================================================================
//  baas/rbac/rbac.h  —  project operators + roles (H2 RBAC foundation)
// =============================================================================
//  Multiple operators per project, each with their OWN hashed key (distinct from the
//  single project secret) and a role: viewer < admin < owner. This is the model + the
//  authentication + a pure authorize() primitive. Per-endpoint role ENFORCEMENT across
//  the API (an OperatorFilter mapping roles → allowed operations) is a deliberate next
//  slice — rewiring the filter chain is security-critical and gets its own focused pass;
//  it is not bolted on here on momentum.
//  ponytail: three fixed roles, no custom permissions/role hierarchy editor — a solo/
//  small team needs read/change/own, not an RBAC matrix. Grow it when a real org does.
// =============================================================================
#pragma once

#include <optional>
#include <string>
#include <vector>

namespace web::rbac {

// Ordered by privilege: Viewer < Admin < Owner. The integer order IS the rank, so
// authorize() is a comparison — do not reorder without updating that intent.
enum class Role { Viewer = 0, Admin = 1, Owner = 2 };

std::optional<Role> role_from_string(const std::string& s);   // "viewer"/"admin"/"owner"
std::string         role_name(Role r);

// True iff `have` is at least as privileged as `need` (an owner passes an admin gate).
bool authorize(Role have, Role need);

bool valid_name(const std::string& name);   // 1-64 chars of [A-Za-z0-9_.-]

struct Operator {
    std::string name;
    Role        role;
};

// Provision an operator: mint a key, store (name, hashed key, role), audit. Returns the
// plaintext key ONCE (like a project secret), or nullopt on a bad name or a duplicate name.
std::optional<std::string> create_operator(long project_id, const std::string& name, Role role,
                                           const std::string& actor);

// Authenticate an operator by (name, key): verify the key against that operator's stored
// hash and return their role. nullopt if the name is unknown or the key does not match.
std::optional<Operator> authenticate(long project_id, const std::string& name,
                                     const std::string& key);

std::vector<Operator> list_operators(long project_id);

}  // namespace web::rbac
