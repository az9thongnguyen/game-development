// =============================================================================
//  games/studio_shell/pixel_workspace.hpp  —  the Studio's pixel editor
// =============================================================================
//  The third workspace, and the one the art decision has been waiting for.
//
//  Chapter 121 imported a CC0 pack; chapter 122 generated the tile that pack did not
//  have. Between them they proved "support both sources" for TEXTURE — noise with a
//  ramp, which is why the Texture Lab can draw water and cannot draw a bucket. Every
//  pixel it makes is a function of twelve numbers.
//
//  Chapter 122 also found the ceiling the honest way: autotiling the farm's dirt path
//  is not blocked by the autotile rule (`autotile_index` has existed since chapter
//  110), it is blocked by ART. The path is one tile wide and no pack in hand ships a
//  narrow-strip piece for it. No amount of noise makes one. A human has to move a
//  cursor, and until this file there was nowhere in this project to do that.
//
//  It edits `.hrt` because that is the one raster format the engine reads, so a tile
//  drawn here and a tile imported from a pack are the same file by the time anything
//  downstream sees them — the property chapters 121 and 122 exist to protect.
//
//  No SDL: it reads a platform::InputState (a plain struct) and draws through
//  Renderer2D, so the whole thing runs in the headless golden test.
// =============================================================================
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "engine/document/command_stack.hpp"
#include "engine/image.hpp"
#include "engine/paint/paint.hpp"
#include "engine/release/ops.hpp"
#include "engine/ui/ui.hpp"
#include "games/studio_shell/workspace.hpp"
#include "platform/input.hpp"

namespace gfx { class Renderer2D; }

namespace studioshell {

class PixelWorkspace : public Workspace {
public:
    enum class Tool { Pencil, Rect, Fill, Pick };

    // Every texture the project declares, in manifest order; the first is opened.
    // A LIST rather than one path because a project has several sheets and the whole
    // point of this workspace is moving between them — and because the one thing a
    // user must be able to do is edit the sheet that is NOT the imported one.
    explicit PixelWorkspace(std::vector<std::string> texture_paths);
    ~PixelWorkspace() override;

    void register_commands() override;

    [[nodiscard]] const char*        name() const override { return "Pixels"; }
    [[nodiscard]] bool               loaded() const override { return loaded_; }
    [[nodiscard]] const std::string& path() const override { return path_; }
    [[nodiscard]] const std::string& problem() const override { return problem_; }
    [[nodiscard]] bool               dirty() const override { return stack_.dirty(); }

    [[nodiscard]] const gfx::Image& image() const { return img_; }
    [[nodiscard]] gfx::Color        colour() const { return colour_; }
    [[nodiscard]] const std::vector<gfx::Color>& palette() const { return palette_; }
    [[nodiscard]] Tool                           tool() const { return tool_; }

    [[nodiscard]] std::string status() const override;
    [[nodiscard]] const char* hint() const override;
    [[nodiscard]] int         inspector_width() const override { return 280; }

    [[nodiscard]] bool recovery_pending() const override { return recovery_pending_; }
    void take_recovery() override;
    void dismiss_recovery() override;

    void update(double dt, const platform::InputState& in, bool interactive) override;
    void draw_canvas(ui::Context& ui, gfx::Renderer2D& g, ui::Rect area) override;
    void draw_inspector(ui::Context& ui, gfx::Renderer2D& g, ui::Rect area) override;

    // Where one image pixel lands on the canvas, in logical pixels — the one place
    // the pan and zoom arithmetic lives, so the status bar, a test and the renderer
    // cannot disagree. Zero-width before the first draw, since only draw knows the
    // canvas.
    [[nodiscard]] ui::Rect pixel_rect(int px, int py) const;
    [[nodiscard]] int      hover_x() const { return hover_x_; }
    [[nodiscard]] int      hover_y() const { return hover_y_; }

    // Which texture is open, and how many there are to move between.
    [[nodiscard]] int         index() const { return index_; }
    [[nodiscard]] std::size_t count() const { return paths_.size(); }

    std::optional<engine::OpResult> take_message() override;

    engine::OpResult save() override;
    engine::OpResult reload() override;

    // Open texture `i`. REFUSED while dirty — reloading over unsaved pixels is a
    // silent loss, and there is no undo across a reload. Public because the inspector
    // list and the `pixel.next` command must be the same operation, not two that
    // agree today (D-rule: an operation cannot exist in only one trigger).
    engine::OpResult open_index(int i);

private:
    void load();
    void build_palette();
    void note(bool ok, std::string msg);

    std::vector<std::string> paths_;
    int                      index_ = 0;
    std::string              path_;
    std::string              problem_;
    bool                     loaded_ = false;
    gfx::Image               img_{};
    doc::CommandStack        stack_;
    paint::Stroke            stroke_;

    bool        recovery_pending_ = false;
    std::string recovery_text_;

    Tool       tool_   = Tool::Pencil;
    gfx::Color colour_ = 0xFFFFFFFFu;
    // Sampled from the image on load, not a fixed ramp. Editing a pack's sheet hands
    // you that pack's own colours, which is what a pixel artist would reach for first
    // — and it is less code than a colour picker, not more.
    std::vector<gfx::Color> palette_;

    int  zoom_ = 8;                        // pixel art is unusable at 1:1
    int  pan_x_ = 0, pan_y_ = 0;
    bool centred_ = false;

    ui::Rect canvas_{};
    int      hover_x_ = -1, hover_y_ = -1;
    bool     rect_active_ = false;
    int      rect_x0_ = 0, rect_y0_ = 0;
    // The previous frame's hovered pixel, so a fast drag paints a line instead of a
    // dotted trail. -1 = the stroke has not touched anything yet.
    int      last_x_ = -1, last_y_ = -1;

    bool panning_ = false;
    int  pan_from_x_ = 0, pan_from_y_ = 0;
    int  pan_origin_x_ = 0, pan_origin_y_ = 0;

    double autosave_timer_ = 0.0;
    bool   commands_registered_ = false;

    std::optional<engine::OpResult> message_;

    // Clicks resolve during draw (that is where the layout is known) and are acted on
    // in the next update — the same one-way street the Hub buttons and the map
    // workspace use.
    int  want_tool_ = -1;
    int  want_swatch_ = -1;
    int  want_index_ = -1;
    bool want_undo_ = false, want_redo_ = false, want_save_ = false;
};

} // namespace studioshell
