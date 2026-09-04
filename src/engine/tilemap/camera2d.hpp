// =============================================================================
//  engine/tilemap/camera2d.hpp  —  a 2D follow camera
// =============================================================================
//  engine/camera.hpp is 3D only (orbit/fly, view+projection matrices). The one 2D
//  "camera" that existed was two pan floats inside games/iso, with a comment saying
//  zoom was left as an exercise. So every 2D scene placed the world on screen by
//  hand, which is why none of them can scroll.
//
//  PURE: arithmetic on floats. No renderer, no I/O — a scene asks it where the world
//  goes and draws there.
//
//  Three behaviours, each of which exists because leaving it out is visible:
//    deadzone  — a box the target moves inside without the camera reacting, so small
//                idle movements do not make the whole screen swim
//    smoothing — exponential, framerate-independent, so a teleport does not snap the
//                eye and a walk does not stutter
//    snapping  — the final offset is rounded to whole pixels, because a half-pixel
//                camera makes every sprite edge shimmer as it moves
// =============================================================================
#pragma once

namespace tilemap {

struct Vec2f { float x = 0.0f, y = 0.0f; };

class Camera2D {
public:
    // Viewport size in world units (= screen pixels at zoom 1).
    void set_viewport(int w, int h) { vw_ = w; vh_ = h; }

    // Clamp the camera so the viewport never shows outside [0,w)x[0,h) world units.
    // A world smaller than the viewport is CENTRED rather than clamped to a corner.
    void set_bounds(float w, float h) { bw_ = w; bh_ = h; has_bounds_ = true; }
    void clear_bounds() { has_bounds_ = false; }

    // Half-size of the box around the camera centre that the target may move in
    // without pulling the camera. 0 = the camera tracks every pixel.
    void set_deadzone(float hx, float hy) { dz_x_ = hx; dz_y_ = hy; }

    // Fraction of the remaining distance covered per second, in (0,1]. 1 = rigid.
    // Applied framerate-independently, so the motion is the same at 30 and 144 fps.
    void set_smoothing(float per_second) { smooth_ = per_second; }

    void snap_to(Vec2f world);              // jump, no smoothing (level load, teleport)
    void follow(Vec2f target, float dt);    // step toward the target

    Vec2f centre() const { return pos_; }

    // Top-left of the viewport in world units, rounded to whole pixels.
    Vec2f origin() const;

    Vec2f world_to_screen(Vec2f w) const;
    Vec2f screen_to_world(Vec2f s) const;

    // The inclusive tile range the viewport covers, for culling. `pad` widens it so
    // sprites that straddle the edge are not popped out a frame early.
    void visible_tiles(int tile_px, int& x0, int& y0, int& x1, int& y1, int pad = 1) const;

private:
    Vec2f pos_{};                  // camera centre, world units
    int   vw_ = 0, vh_ = 0;
    float bw_ = 0.0f, bh_ = 0.0f;
    bool  has_bounds_ = false;
    float dz_x_ = 0.0f, dz_y_ = 0.0f;
    float smooth_ = 1.0f;

    void clamp();
};

} // namespace tilemap
