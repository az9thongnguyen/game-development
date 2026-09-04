// =============================================================================
//  games/farm/dialogue.cpp
// =============================================================================
#include "games/farm/dialogue.hpp"

#include <sstream>

namespace farm {

const std::vector<Choice> DialogueRunner::kNoChoices{};

namespace {

// Trim both ends. Dialogue files are indented for readability, and an indented line
// whose text keeps its leading spaces draws wrong in every box.
std::string trim(const std::string& s) {
    const auto b = s.find_first_not_of(" \t\r");
    if (b == std::string::npos) return {};
    const auto e = s.find_last_not_of(" \t\r");
    return s.substr(b, e - b + 1);
}

} // namespace

const DialogueNode* Dialogue::node(const std::string& id) const {
    for (const DialogueNode& n : nodes)
        if (n.id == id) return &n;
    return nullptr;
}

std::optional<Dialogue> parse_dialogue(const std::string& text) {
    Dialogue d;
    std::istringstream in(text);
    std::string raw;
    if (!std::getline(in, raw)) return std::nullopt;
    {
        std::istringstream head(raw);
        std::string magic; int version = 0;
        if (!(head >> magic >> version) || magic != "dlg" || version > 1) return std::nullopt;
    }

    DialogueNode current;
    bool have = false;
    const auto flush = [&]() -> bool {
        if (!have) return true;
        // A node with no exit strands the player in the box.
        if (!current.ends && current.goto_node.empty() && current.choices.empty()) return false;
        d.nodes.push_back(current);
        current = DialogueNode{};
        have = false;
        return true;
    };

    while (std::getline(in, raw)) {
        const std::string line = trim(raw);
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ln(line);
        std::string tok;
        ln >> tok;

        if (tok == "node") {
            if (!flush()) return std::nullopt;
            if (!(ln >> current.id)) return std::nullopt;
            have = true;
        } else if (!have) {
            return std::nullopt;                       // a line outside any node
        } else if (tok == "say") {
            std::string speaker, rest;
            if (!(ln >> speaker)) return std::nullopt;
            std::getline(ln, rest);
            current.lines.emplace_back(speaker, trim(rest));
        } else if (tok == "choice") {
            std::string rest;
            std::getline(ln, rest);
            const auto arrow = rest.find("->");
            if (arrow == std::string::npos) return std::nullopt;
            Choice c{trim(rest.substr(0, arrow)), trim(rest.substr(arrow + 2))};
            if (c.text.empty() || c.target.empty()) return std::nullopt;
            current.choices.push_back(std::move(c));
        } else if (tok == "goto") {
            if (!(ln >> current.goto_node)) return std::nullopt;
        } else if (tok == "end") {
            current.ends = true;
        } else {
            return std::nullopt;                       // an unknown verb is a typo
        }
    }
    if (!flush()) return std::nullopt;
    if (d.nodes.empty()) return std::nullopt;

    // Every jump must land somewhere. Checking at PARSE time means a broken link is
    // found when the file is loaded, not when a player happens to pick that option.
    for (const DialogueNode& n : d.nodes) {
        if (!n.goto_node.empty() && d.node(n.goto_node) == nullptr) return std::nullopt;
        for (const Choice& c : n.choices)
            if (d.node(c.target) == nullptr) return std::nullopt;
    }
    return d;
}

// ---- runner --------------------------------------------------------------------

void DialogueRunner::enter(const std::string& id) {
    n_ = d_ ? d_->node(id) : nullptr;
    line_ = 0;
    finished_ = (n_ == nullptr);
    // A node with no lines still has to resolve its exit, or `advance` would be needed
    // to leave a box that was never shown.
    if (n_ && n_->lines.empty()) {
        if (!n_->goto_node.empty()) enter(n_->goto_node);
        else if (n_->choices.empty()) finished_ = true;
    }
}

bool DialogueRunner::begin(const Dialogue& d, const std::string& node) {
    d_ = &d;
    enter(node);
    return !finished_ || (d.node(node) != nullptr);
}

std::string DialogueRunner::speaker() const {
    if (!n_ || line_ >= n_->lines.size()) return {};
    return n_->lines[line_].first;
}

std::string DialogueRunner::text() const {
    if (!n_ || line_ >= n_->lines.size()) return {};
    return n_->lines[line_].second;
}

const std::vector<Choice>& DialogueRunner::choices() const {
    if (!n_ || n_->choices.empty()) return kNoChoices;
    // Only once the last line is on screen: showing options while there is still text
    // to read makes the player answer a question they have not finished hearing.
    if (line_ + 1 < n_->lines.size()) return kNoChoices;
    return n_->choices;
}

void DialogueRunner::advance() {
    if (finished_ || !n_) return;
    if (!choices().empty()) return;              // waiting on a decision
    if (line_ + 1 < n_->lines.size()) { ++line_; return; }
    if (!n_->goto_node.empty()) { enter(n_->goto_node); return; }
    finished_ = true;
}

void DialogueRunner::choose(std::size_t i) {
    const auto& c = choices();
    if (i >= c.size()) return;
    enter(c[i].target);
}

} // namespace farm
