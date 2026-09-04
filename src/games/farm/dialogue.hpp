// =============================================================================
//  games/farm/dialogue.hpp  —  conversations as data
// =============================================================================
//  A dialogue lives in a `.dlg` file, not in C++ string literals, for the same reason
//  the crops do: writing dialogue is content work, and content work should not need a
//  compiler. It is small on purpose — say, choose, jump, end — because that is what a
//  village NPC needs, and a scripting language is a project of its own.
//
//      dlg 1
//      node start
//        say Anna Morning! The parsnips like water.
//        choice Ask about the shop -> shop
//        choice Say goodbye        -> bye
//      node shop
//        say Anna Pierre opens at nine.
//        goto bye
//      node bye
//        say Anna See you around.
//        end
//
//  Runtime: a cursor over the current node's lines, with `advance()` for "the player
//  pressed A" and `choose(i)` for a branch. The typewriter effect is the caller's
//  business — it owns the clock — so this exposes the full line and the caller reveals
//  as much of it as it likes.
//
//  PURE: no renderer, no I/O.
// =============================================================================
#pragma once

#include <optional>
#include <string>
#include <vector>

namespace farm {

struct Choice { std::string text, target; };

struct DialogueNode {
    std::string                              id;
    std::vector<std::pair<std::string, std::string>> lines;   // speaker, text
    std::vector<Choice>                      choices;
    std::string                              goto_node;       // empty unless `goto`
    bool                                     ends = false;    // `end`
};

struct Dialogue {
    std::vector<DialogueNode> nodes;
    [[nodiscard]] const DialogueNode* node(const std::string& id) const;
};

// Parse a `.dlg`. A node that neither ends, jumps, nor offers a choice is an error:
// it would leave the player stuck in a box with no way out, and that is exactly the
// kind of bug that only shows up in front of someone.
std::optional<Dialogue> parse_dialogue(const std::string& text);

class DialogueRunner {
public:
    // Start at `node` (default "start"). False if that node does not exist.
    bool begin(const Dialogue& d, const std::string& node = "start");

    [[nodiscard]] bool finished() const { return finished_; }
    [[nodiscard]] std::string speaker() const;
    [[nodiscard]] std::string text() const;
    // Non-empty only when the current line is the LAST of its node and that node
    // offers choices — a choice list mid-node would be unreachable.
    [[nodiscard]] const std::vector<Choice>& choices() const;

    // "The player pressed A." Advances to the next line, follows a goto, or finishes.
    // Does nothing while a choice is pending: that needs choose().
    void advance();
    // Take branch `i`. Out of range is ignored rather than crashing on a stray click.
    void choose(std::size_t i);

private:
    const Dialogue* d_ = nullptr;
    const DialogueNode* n_ = nullptr;
    std::size_t line_ = 0;
    bool finished_ = true;
    static const std::vector<Choice> kNoChoices;

    void enter(const std::string& id);
};

} // namespace farm
