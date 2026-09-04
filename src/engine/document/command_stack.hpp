// =============================================================================
//  engine/document/command_stack.hpp  —  undo, redo, and knowing you are dirty
// =============================================================================
//  The Studio has no undo, which is the difference between a tool you can explore in
//  and one you have to be careful in. This is the piece that fixes that, and it is
//  deliberately about EDITS rather than about the thing being edited: a command is a
//  pair of closures, so a map workspace, a scene workspace and a pixel editor all
//  share one history implementation.
//
//  (The name is `document`, not `studio`: `studio_core` already exists and is the
//  Texture Lab.)
//
//  PURE: no I/O, no renderer. It calls the closures it was given.
// =============================================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace doc {

struct Command {
    std::string           label;      // shown as "Undo <label>"
    std::function<void()> apply;
    std::function<void()> revert;
    // Consecutive commands sharing a non-zero key collapse into ONE undo step. A
    // slider drag emits a command per frame; without this, undoing a drag means
    // pressing Ctrl+Z sixty times. 0 = never merge.
    std::uint64_t         merge_key = 0;
};

class CommandStack {
public:
    // Apply the command and record it. This is the only way to change a document —
    // an edit made outside the stack is an edit that cannot be undone, and the bug
    // it causes appears later, as an undo that leaves the document inconsistent.
    void push_apply(Command c);

    bool undo();
    bool redo();

    [[nodiscard]] std::size_t undo_depth() const { return done_.size(); }
    [[nodiscard]] std::size_t redo_depth() const { return undone_.size(); }
    [[nodiscard]] bool can_undo() const { return !done_.empty(); }
    [[nodiscard]] bool can_redo() const { return !undone_.empty(); }

    // Labels for the menu, empty when there is nothing to undo/redo.
    [[nodiscard]] std::string undo_label() const;
    [[nodiscard]] std::string redo_label() const;

    // Dirty means "differs from the last save". Undoing back to the saved point makes
    // the document clean again — telling someone they have unsaved changes when they
    // have undone all of them trains them to ignore the warning.
    [[nodiscard]] bool dirty() const;
    void mark_saved();

    // Bound the history. Trimming from the front can discard the saved marker; when
    // that happens the document can no longer be proven clean, so it stays dirty.
    void set_limit(std::size_t n);

    void clear();

private:
    std::vector<Command> done_;      // applied, newest last
    std::vector<Command> undone_;    // reverted, newest last (the redo stack)
    std::size_t          limit_ = 200;
    // Index into done_ at the moment of the last save. -1 = never saved;
    // kLost = the marker was trimmed away and cleanliness is unknowable.
    long long            saved_at_ = 0;
    static constexpr long long kLost = -2;
};

} // namespace doc
