// =============================================================================
//  games/maplab/maplab_scene.hpp  —  the Map/Level Lab (--maplab)
// =============================================================================
//  A tile-grid editor: pick a cell type from the palette, paint or flood-fill it
//  onto the grid, and Save a .map asset the fps raycaster loads. Edits go through
//  the pure maplab:: ops; only this file touches UI / renderer / assets. No sim,
//  no Play — a pure authoring tool (contrast the sandbox, which runs behavior).
// =============================================================================
#pragma once
#include <string>
#include <vector>

#include "engine/scene.hpp"
#include "engine/ui/ui.hpp"
#include "games/fps/map.hpp"

namespace maplab {

class MaplabScene : public engine::Scene {
public:
    MaplabScene();
    void render(const engine::Context& ctx) override;

private:
    void   save();                                  // write maps/level_NN.map
    void   load(const std::string& name);           // read maps/<name>.map
    void   scan_collection();                        // probe existing saved maps
    bool   cell_at(int mx, int my, int& cx, int& cy) const;  // screen -> cell

    fps::Map    map_;
    ui::Context ui_;

    uint8_t     brush_    = 1;                       // current cell id to paint
    bool        fill_mode_ = false;                  // false = Paint, true = Flood
    int         save_counter_ = 0;
    std::vector<std::string> collection_;            // saved level names

    // grid placement (recomputed each frame from the canvas size)
    int origin_x_ = 0, origin_y_ = 0, cell_px_ = 16;
    int w_ = 0, h_ = 0;
};

} // namespace maplab
