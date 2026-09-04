// =============================================================================
//  engine/document/command_stack.cpp
// =============================================================================
#include "engine/document/command_stack.hpp"

namespace doc {

void CommandStack::push_apply(Command c) {
    if (!c.apply || !c.revert) return;    // an un-undoable command is not a command
    c.apply();

    // A new edit after an undo discards the redo branch. Keeping it would mean the
    // document has two possible futures and no way to say which one Ctrl+Y means.
    undone_.clear();

    // Merge into the previous command when they belong to the same gesture. Keep the
    // FIRST revert (so undo returns to before the whole gesture) and the LATEST
    // apply (so redo lands on the final value).
    if (c.merge_key != 0 && !done_.empty() && done_.back().merge_key == c.merge_key) {
        done_.back().apply = std::move(c.apply);
        done_.back().label = std::move(c.label);
        return;
    }

    done_.push_back(std::move(c));

    if (done_.size() > limit_) {
        const std::size_t drop = done_.size() - limit_;
        done_.erase(done_.begin(), done_.begin() + static_cast<long>(drop));
        if (saved_at_ >= 0) {
            saved_at_ -= static_cast<long long>(drop);
            // The save point fell off the front: there is no longer any history
            // position that can be proven identical to the file on disk.
            if (saved_at_ < 0) saved_at_ = kLost;
        }
    }
}

bool CommandStack::undo() {
    if (done_.empty()) return false;
    done_.back().revert();
    undone_.push_back(std::move(done_.back()));
    done_.pop_back();
    return true;
}

bool CommandStack::redo() {
    if (undone_.empty()) return false;
    undone_.back().apply();
    done_.push_back(std::move(undone_.back()));
    undone_.pop_back();
    return true;
}

std::string CommandStack::undo_label() const {
    return done_.empty() ? std::string{} : done_.back().label;
}

std::string CommandStack::redo_label() const {
    return undone_.empty() ? std::string{} : undone_.back().label;
}

bool CommandStack::dirty() const {
    if (saved_at_ == kLost) return true;
    return static_cast<long long>(done_.size()) != saved_at_;
}

void CommandStack::mark_saved() { saved_at_ = static_cast<long long>(done_.size()); }

void CommandStack::set_limit(std::size_t n) {
    limit_ = n < 1 ? 1 : n;
    if (done_.size() > limit_) {
        const std::size_t drop = done_.size() - limit_;
        done_.erase(done_.begin(), done_.begin() + static_cast<long>(drop));
        if (saved_at_ >= 0) {
            saved_at_ -= static_cast<long long>(drop);
            if (saved_at_ < 0) saved_at_ = kLost;
        }
    }
}

void CommandStack::clear() {
    done_.clear();
    undone_.clear();
    saved_at_ = 0;
}

} // namespace doc
