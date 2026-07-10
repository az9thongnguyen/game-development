// =============================================================================
//  gbaas/result.h  —  the uniform result + error type for every SDK call
// =============================================================================
#pragma once

#include <optional>
#include <string>

namespace gbaas {

struct Error {
    std::string code;       // machine code from the server envelope (or "transport")
    std::string message;    // human-readable
    int         status = 0; // HTTP status, or -1 for a transport failure
};

// Either a value (success) or an error. `if (result) { *result … }`.
template <class T>
struct Result {
    std::optional<T>     value;
    std::optional<Error> error;

    explicit operator bool() const { return value.has_value(); }
    const T& operator*() const { return *value; }
    const T* operator->() const { return &*value; }

    static Result ok(T v) { return Result{std::move(v), std::nullopt}; }
    static Result err(Error e) { return Result{std::nullopt, std::move(e)}; }
};

}  // namespace gbaas
