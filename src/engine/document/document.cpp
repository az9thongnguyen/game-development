// =============================================================================
//  engine/document/document.cpp
// =============================================================================
#include "engine/document/document.hpp"

#include "engine/assets.hpp"

namespace doc {
namespace {

std::optional<std::string> read_text(const std::string& path) {
    auto bytes = assets::load_file(path);
    if (!bytes) return std::nullopt;
    return std::string(bytes->begin(), bytes->end());
}

bool write_text(const std::string& path, const std::string& text) {
    return assets::write_file(path, std::vector<unsigned char>(text.begin(), text.end()));
}

} // namespace

std::string autosave_path(const std::string& path) { return path + ".autosave"; }

Opened open(const std::string& path) {
    Opened out;
    auto content = read_text(path);
    if (!content) return out;                       // Missing

    out.content = std::move(*content);
    out.state   = OpenState::Clean;

    // Offer recovery only when the autosave actually DIFFERS and is non-empty.
    //
    // Identical: a leftover. Prompting about it teaches the user to dismiss the
    // prompt, which is precisely when it matters that they read it.
    //
    // Empty: how discard_autosave marks "no autosave here", because the assets seam
    // has no delete and adding one — a path that can remove files — is a trust
    // boundary decision too big for this convenience. The ceiling that buys: you
    // cannot recover a document *to* emptiness. Deliberate, and cheap.
    if (auto auto_text = read_text(autosave_path(path));
        auto_text && !auto_text->empty() && *auto_text != out.content) {
        out.recovered = std::move(*auto_text);
        out.state     = OpenState::RecoveryOffered;
    }
    return out;
}

bool write_autosave(const std::string& path, const std::string& content) {
    return write_text(autosave_path(path), content);
}

bool save(const std::string& path, const std::string& content) {
    if (!write_text(path, content)) return false;
    discard_autosave(path);
    return true;
}

bool discard_autosave(const std::string& path) {
    // Truncate rather than unlink: the assets seam has no delete, and adding one —
    // a path that can remove files — is a trust-boundary decision far larger than
    // this convenience. open() treats an empty autosave as absent, so truncation and
    // deletion are indistinguishable to every reader.
    // ponytail: a real unlink belongs with whatever else first needs to delete.
    return write_text(autosave_path(path), std::string{});
}

} // namespace doc
