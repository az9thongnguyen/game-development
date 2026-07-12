// =============================================================================
//  baas/admin/admin_service.h  —  platform/project admin operations
// =============================================================================
//  Project provisioning (create/list) and per-project user listing for the
//  dashboard. Project creation mints a random public + secret key; the secret is
//  returned ONCE (stored hashed) and never again.
// =============================================================================
#pragma once

#include <string>
#include <vector>

namespace web::admin {

struct NewProject {   // returned by create_project (secret_key shown once)
    long        id;
    std::string name;
    std::string public_key;
    std::string secret_key;
};

struct ProjectInfo {
    long        id;
    std::string name;
    std::string public_key;
};

struct UserInfo {
    long        id;
    std::string email;   // "" for guests
    std::string display_name;
    bool        is_guest;
};

NewProject               create_project(const std::string& name);
std::vector<ProjectInfo> list_projects();
std::vector<UserInfo>    list_users(long project_id);

// Mint a fresh project secret, replace the stored hash, and audit the rotation. The
// new plaintext secret is returned ONCE (like create_project); the previous secret
// stops verifying immediately. Caller must have proven it holds the current secret.
std::string              rotate_secret(long project_id);

}  // namespace web::admin
