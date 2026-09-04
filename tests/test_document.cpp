// =============================================================================
//  tests/test_document.cpp  —  undo/redo history, and autosave + recovery
// =============================================================================
//  The command stack is pure, so all of it runs headless. The document half writes
//  through the assets:: seam into a scratch directory, which is deleted afterwards —
//  the same shape test_release_ops uses.
// =============================================================================
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "engine/assets.hpp"
#include "engine/document/command_stack.hpp"
#include "engine/document/document.hpp"

static int g_failures = 0;
#define CHECK(cond)                                                      \
    do {                                                                 \
        if (!(cond)) {                                                   \
            std::printf("FAIL %s:%d:  %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                \
        }                                                                \
    } while (0)

using namespace doc;

// A command that sets an int, remembering what it replaced.
static Command set_to(int& target, int value, const char* label, std::uint64_t key = 0) {
    const int before = target;
    return Command{label,
                   [&target, value] { target = value; },
                   [&target, before] { target = before; },
                   key};
}

static void test_undo_redo() {
    CommandStack st;
    int v = 0;

    CHECK(!st.can_undo());
    CHECK(!st.can_redo());
    CHECK(st.undo_label().empty());
    CHECK(!st.undo());                    // undoing nothing is a no-op, not a crash
    CHECK(!st.redo());

    st.push_apply(set_to(v, 1, "set 1"));
    CHECK(v == 1);                        // push APPLIES; a stack you must apply
    CHECK(st.can_undo());                 // around is a stack someone forgets to apply
    CHECK(st.undo_label() == "set 1");

    st.push_apply(set_to(v, 2, "set 2"));
    CHECK(v == 2);
    CHECK(st.undo_depth() == 2);

    CHECK(st.undo());
    CHECK(v == 1);
    CHECK(st.can_redo());
    CHECK(st.redo_label() == "set 2");
    CHECK(st.undo_label() == "set 1");

    CHECK(st.undo());
    CHECK(v == 0);
    CHECK(!st.can_undo());
    CHECK(st.redo_depth() == 2);

    CHECK(st.redo());
    CHECK(v == 1);
    CHECK(st.redo());
    CHECK(v == 2);
    CHECK(!st.can_redo());

    // A new edit after an undo discards the redo branch: keeping it would leave the
    // document with two possible futures and no way to say which one redo means.
    st.undo();
    CHECK(v == 1);
    st.push_apply(set_to(v, 99, "set 99"));
    CHECK(v == 99);
    CHECK(!st.can_redo());
    CHECK(st.redo_depth() == 0);

    // A command missing either half is rejected rather than recorded: an edit that
    // cannot be reverted breaks undo for everything BELOW it in the stack too.
    const std::size_t before = st.undo_depth();
    st.push_apply(Command{"broken", [] {}, nullptr, 0});
    st.push_apply(Command{"broken", nullptr, [] {}, 0});
    CHECK(st.undo_depth() == before);
}

static void test_merge() {
    CommandStack st;
    int v = 0;

    // A drag emits one command per frame. Without merging, undoing it means pressing
    // Ctrl+Z sixty times.
    for (int i = 1; i <= 5; ++i) st.push_apply(set_to(v, i, "drag", /*merge_key*/ 7));
    CHECK(v == 5);
    CHECK(st.undo_depth() == 1);          // five commands, one undo step

    CHECK(st.undo());
    CHECK(v == 0);                        // back to before the WHOLE gesture
    CHECK(st.redo());
    CHECK(v == 5);                        // ...and forward to its final value

    // A different key starts a new step, and so does key 0.
    st.push_apply(set_to(v, 10, "other drag", 8));
    CHECK(st.undo_depth() == 2);
    st.push_apply(set_to(v, 11, "single", 0));
    st.push_apply(set_to(v, 12, "single", 0));
    CHECK(st.undo_depth() == 4);

    // Merging is only for CONSECUTIVE commands: an unrelated edit between two
    // same-key gestures must not fuse them into one.
    CommandStack s2;
    int x = 0;
    s2.push_apply(set_to(x, 1, "a", 5));
    s2.push_apply(set_to(x, 2, "b", 0));
    s2.push_apply(set_to(x, 3, "c", 5));
    CHECK(s2.undo_depth() == 3);
}

static void test_dirty() {
    CommandStack st;
    int v = 0;

    CHECK(!st.dirty());                   // a freshly opened document is clean
    st.push_apply(set_to(v, 1, "edit"));
    CHECK(st.dirty());

    st.mark_saved();
    CHECK(!st.dirty());

    st.push_apply(set_to(v, 2, "edit"));
    CHECK(st.dirty());

    // Undoing back to the saved point makes it clean again. Warning about unsaved
    // changes that have all been undone trains people to ignore the warning.
    st.undo();
    CHECK(!st.dirty());

    st.redo();
    CHECK(st.dirty());

    // Undoing PAST the save point is dirty too — the file on disk has content the
    // document no longer has.
    st.undo();
    st.undo();
    CHECK(st.dirty());

    // Trimming the history can discard the save marker, and then cleanliness is
    // genuinely unknowable, so the document stays dirty rather than claiming to be
    // saved.
    CommandStack lim;
    int w = 0;
    lim.set_limit(3);
    lim.push_apply(set_to(w, 1, "1"));
    lim.mark_saved();
    CHECK(!lim.dirty());
    for (int i = 2; i <= 6; ++i) lim.push_apply(set_to(w, i, "n"));
    CHECK(lim.undo_depth() == 3);
    CHECK(lim.dirty());
    while (lim.undo()) {}
    CHECK(lim.dirty());                   // still dirty: the marker is gone for good

    // clear() resets to a clean, empty history.
    lim.clear();
    CHECK(!lim.dirty());
    CHECK(!lim.can_undo());
    CHECK(!lim.can_redo());
}

static void test_autosave_recovery() {
    const std::string base = "test_document_tmp";
    std::filesystem::remove_all(base);
    std::filesystem::create_directories(base);
    assets::set_base_path(base);

    const std::string path = "doc.txt";

    // A file that does not exist opens as Missing rather than as empty content.
    CHECK(open(path).state == OpenState::Missing);

    CHECK(save(path, "version one"));
    {
        const Opened o = open(path);
        CHECK(o.state == OpenState::Clean);
        CHECK(o.content == "version one");
        CHECK(o.recovered.empty());
    }

    // An autosave with DIFFERENT content offers recovery, and does not apply it —
    // silently applying is how someone loses the version they deliberately saved.
    CHECK(write_autosave(path, "unsaved work"));
    {
        const Opened o = open(path);
        CHECK(o.state == OpenState::RecoveryOffered);
        CHECK(o.content == "version one");     // the file is untouched
        CHECK(o.recovered == "unsaved work");
    }

    // An IDENTICAL autosave is a leftover, not a recovery. Prompting about it teaches
    // the user to dismiss the prompt, which is exactly when it matters that they read it.
    CHECK(write_autosave(path, "version one"));
    CHECK(open(path).state == OpenState::Clean);

    // Declining recovery drops the autosave.
    CHECK(write_autosave(path, "unsaved work"));
    CHECK(open(path).state == OpenState::RecoveryOffered);
    CHECK(discard_autosave(path));
    CHECK(open(path).state == OpenState::Clean);

    // Saving for real also drops it — otherwise the next open offers to recover
    // content the file already has.
    CHECK(write_autosave(path, "more unsaved work"));
    CHECK(save(path, "version two"));
    {
        const Opened o = open(path);
        CHECK(o.state == OpenState::Clean);
        CHECK(o.content == "version two");
    }

    // An EMPTY autosave counts as absent — that is how discard marks it, and it is
    // the ceiling of not having a delete in the assets seam: a document cannot be
    // recovered *to* emptiness.
    CHECK(write_autosave(path, ""));
    CHECK(open(path).state == OpenState::Clean);

    // The autosave sits beside the file, where a user can find it.
    CHECK(autosave_path("maps/level.map2") == "maps/level.map2.autosave");

    assets::set_base_path(".");
    std::filesystem::remove_all(base);
}

int main() {
    test_undo_redo();
    test_merge();
    test_dirty();
    test_autosave_recovery();
    if (g_failures == 0) std::printf("document: all tests passed\n");
    else                 std::printf("document: %d FAILURE(S)\n", g_failures);
    return g_failures;
}
