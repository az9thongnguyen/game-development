// =============================================================================
//  baas/common/context_keys.h  —  request-attribute keys shared across the gateway
// =============================================================================
//  Filters resolve identity onto the request; controllers read it back. Keeping
//  the keys (and their stored types) in one place prevents typos and type
//  mismatches: attributes are typed, so insert<T> and get<T> must agree.
//    kProjectId → long  (set by ApiKeyFilter from X-Api-Key)
//    kUserId    → long  (set by AuthFilter from the Bearer JWT, S1.3)
// =============================================================================
#pragma once

namespace web {

inline constexpr const char* kProjectId = "project_id";
inline constexpr const char* kUserId    = "user_id";

}  // namespace web
