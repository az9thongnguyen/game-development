// =============================================================================
//  engine/tilemap/map_edit.hpp  —  tile edits, expressed as undoable commands
// =============================================================================
//  S3 gave the project one map format; S4a gave it an undo stack. This is the joint:
//  the edit operations a map editor performs, each returning a `doc::Command` rather
//  than mutating and hoping someone remembers to record it.
//
//  Why a command per EDIT and not per CELL: the stack merges consecutive commands
//  that share a merge_key by keeping the FIRST revert and the LATEST apply, which is
//  right for a gesture whose latest state subsumes every earlier one (dragging an
//  object) and WRONG for one that accumulates (painting a stroke): the first revert
//  would only restore the first cell. So a stroke accumulates here, in `Stroke`, and
//  reaches the stack once, as a single command carrying every cell it touched.
//
//  PURE: no renderer, no I/O. The `Map&` a command captures must outlive the stack
//  it is pushed onto — in practice both live in the workspace, which is neither
//  copied nor moved.
// =============================================================================
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "engine/document/command_stack.hpp"
#include "engine/tilemap/map2.hpp"

namespace mapedit {

// One cell's before/after. Storing `before` per cell is what makes undo exact for a
// stroke that crossed several different tiles.
struct CellEdit {
    int          x = 0, y = 0;
    std::int32_t before = 0, after = 0;
};

// Cells a filled rectangle covers, clamped to the map, corners in any order.
std::vector<CellEdit> rect_cells(const tilemap::Map& m, const std::string& layer,
                                 int x0, int y0, int x1, int y1, std::int32_t id);

// The 4-connected region of cells matching the value under (x,y), repainted to `id`.
// Empty when the region already holds `id` (every cell would be a no-op).
std::vector<CellEdit> flood_cells(const tilemap::Map& m, const std::string& layer,
                                  int x, int y, std::int32_t id);

// Drop cells that change nothing. An "edit" whose before == after would still become
// an undo step, and Ctrl+Z would then appear not to work.
void drop_noops(std::vector<CellEdit>& cells);

// A command that writes `cells` into `m`. Returns nullopt when there is nothing to
// do, so the caller cannot accidentally push an empty step.
std::optional<doc::Command> make_command(tilemap::Map& m, std::string layer,
                                         std::vector<CellEdit> cells, std::string label);

// A drag. Cells are written through as the mouse moves — you have to see the paint
// under the cursor — and the whole gesture reaches the stack as ONE command when the
// button comes up. Re-applying it is therefore idempotent, which is exactly what
// redo needs.
class Stroke {
public:
    // The map is bound once, at the start of the gesture, rather than passed to every
    // touch: the command finish() returns has to write to the same map the stroke
    // painted, and a per-call parameter is a chance to pass a different one.
    void begin(tilemap::Map& m, std::string layer, std::int32_t id, std::string label);
    [[nodiscard]] bool active() const { return active_; }
    [[nodiscard]] std::size_t touched() const { return cells_.size(); }

    // Paint one cell. Out of bounds, an unknown layer, a cell already at `id`, and a
    // cell already in this stroke are all ignored.
    void touch(int x, int y);

    // End the gesture. nullopt when it changed nothing (a click that missed, or a
    // stroke over tiles that already held the brush value).
    std::optional<doc::Command> finish();

private:
    bool                  active_ = false;
    tilemap::Map*         map_ = nullptr;
    std::string           layer_, label_;
    std::int32_t          id_ = 0;
    std::vector<CellEdit> cells_;
};

} // namespace mapedit
