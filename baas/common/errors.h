// =============================================================================
//  baas/common/errors.h  —  the one JSON error envelope for every endpoint
// =============================================================================
//  Every failure returns `{"error":{"code":"...","message":"..."}}` with a
//  matching HTTP status, so the SDK can parse errors uniformly regardless of
//  which service produced them.
// =============================================================================
#pragma once

#include <string>

#include <drogon/HttpResponse.h>

namespace web {

drogon::HttpResponsePtr make_error(int status, const std::string& code,
                                   const std::string& message);

}  // namespace web
