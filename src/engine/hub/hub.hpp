// =============================================================================
//  engine/hub/hub.hpp  —  the project hub view model (Horizon 1 Hub shell)
// =============================================================================
//  The roadmap's "first Hub shell" is a view/controller over the project + release
//  domain: it shows, for one reference project, whether it is shippable, what each
//  channel points at, and — the part that makes it a *controller* and not just a
//  report — the single next recommended action. That recommendation is pure decision
//  logic over aggregate state, so it lives here, headless and unit-tested, entirely
//  separate from any I/O (main.cpp assembles the view) or rendering (a Scene, later).
//  ponytail: one reference project's status, computed — not a directory-scanning
//  project browser (the "collection database" smell the strategy warns against).
// =============================================================================
#pragma once
#include <string>
#include <vector>  // HubView.channels/problems and hub_lines() return vectors

namespace engine {

// One channel's state relative to the project's current source.
struct HubChannel {
    std::string name;                 // development / preview / production
    std::string release;              // release id it points at ("" if unset)
    bool        present = false;      // is that release actually in the store?
    bool        matches_local = false;// does it equal the current source's package hash?
};

// Everything the hub knows about one project, aggregated from the domain commands.
struct HubView {
    std::string name;
    std::string entry;
    int         schema = 0;
    bool        shippable = false;             // parse + validate + dependency closure all ok
    std::vector<std::string> problems;         // why not shippable (empty when shippable)
    std::string local_package;                 // current source's package hash ("" if not shippable)
    std::vector<HubChannel> channels;          // development, preview, production (in that order)
};

// The single next recommended action for a project, derived from its aggregate state.
// Pure — the decision brain of the hub, tested independently of I/O and rendering.
std::string recommend(const HubView& v);

// The hub's display as a list of plain text lines (title, status, problems, package,
// each channel, and the "next:" recommendation). Pure, so the CLI dashboard and the
// graphical Hub Scene render the *same* content — one source of truth for what the hub
// says, tested without a window.
std::vector<std::string> hub_lines(const HubView& v);

} // namespace engine
