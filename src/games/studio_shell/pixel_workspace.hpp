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
//  Chapter 127 closed the ceiling this file shipped with: every colour it could
//  select was a colour the image ALREADY held, because both doors — the sampled
//  palette and the eyedropper — read the file. An editor that cannot introduce one
//  new hue is a retouching tool, not a drawing one. `engine/paint/colour.hpp` is the
//  arithmetic; the MIX section of the inspector is the two triggers.
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
#include "engine/paint/colour.hpp"
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
    // `project_path` is the manifest a NEW sheet gets declared in. Empty is allowed
    // and is not silently ignored: new_sheet() says the sheet exists and the project
    // cannot see it, which is a state worth naming rather than one worth hiding.
    explicit PixelWorkspace(std::vector<std::string> texture_paths,
                            std::string project_path = "");
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

    // Where the mixer's sliders are. NOT derived from colour(): see paint::Hsv — a
    // colour cannot say where its sliders are, and every colour with v=0 is black.
    [[nodiscard]] paint::Hsv         mix() const { return mix_; }
    [[nodiscard]] const std::string& hex_field() const { return hex_field_; }

    // True when the inspector ran out of room and its last control came back short.
    // `slot()` CLAMPS rather than overflowing, so a panel one control too tall does
    // not spill — it silently hands back a zero-height rect, which draws nothing and
    // cannot be clicked. That is the chapter-126 bug from the other side: not drawn
    // somewhere it cannot be pressed, but not drawn at all. Reported in status().
    [[nodiscard]] bool inspector_clipped() const { return inspector_clipped_; }

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

    // Where the mix controls landed, recorded by draw for the same reason canvas_ is:
    // a caller that recomputed them would stop testing the layout and start testing
    // its own copy of it (ch. 126). Empty before the first draw.
    [[nodiscard]] ui::Rect mix_slider(int i) const {
        return (i >= 0 && i < 3) ? mix_rect_[i] : ui::Rect{};
    }
    [[nodiscard]] ui::Rect hex_rect() const { return hex_rect_; }
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

    // Create `textures/<name>.hrt` from a new blank `.pix`, declare it in the project,
    // re-bake the ledger, and open it. Goes through the `asset.new` command rather
    // than doing any of that here: the operation lives in a core and a trigger only
    // calls it, so the Studio button and the CLI cannot become two implementations
    // that agree today.
    //
    // Refused while dirty, for the reason open_index is: opening the new sheet would
    // drop unsaved pixels with no undo across it.
    engine::OpResult new_sheet(const std::string& name);

    [[nodiscard]] const std::string& new_name() const { return new_name_; }
    // Where the create controls landed, recorded by draw for the same reason the mix
    // sliders are: a caller that recomputed them would test its own copy of the
    // layout instead of the layout (ch. 126).
    [[nodiscard]] ui::Rect new_name_rect() const { return new_name_rect_; }
    [[nodiscard]] ui::Rect new_button_rect() const { return new_button_rect_; }
    [[nodiscard]] ui::Rect save_rect() const { return save_rect_; }

private:
    void load();
    void build_palette();
    void note(bool ok, std::string msg);
    // Select `c` AND move the mixer onto it — one function, because a selection that
    // left the sliders behind would make the next drag jump to an unrelated colour.
    void adopt(gfx::Color c);

    std::vector<std::string> paths_;
    std::string              project_;
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
    // you that pack's own colours, which is what a pixel artist reaches for first —
    // and it is why the mixer below is an ADDITION rather than a replacement: the
    // palette answers "the same colour as that", the mixer answers "a colour this
    // sheet does not have yet", and each is the wrong tool for the other question.
    std::vector<gfx::Color> palette_;

    // The colour being MIXED, in the coordinates the sliders move. Alpha rides
    // alongside rather than inside: dragging hue must not change how transparent a
    // picked pixel was.
    paint::Hsv   mix_{};
    std::uint8_t mix_a_ = 255;
    // The hex field's text, which is NOT `to_hex(colour_)`: while it has focus it is
    // whatever the user has typed so far, including halves of a code that do not
    // parse yet.
    std::string hex_field_ = "#FFFFFFFF";
    bool        hex_focused_ = false;
    bool        inspector_clipped_ = false;
    ui::Rect    mix_rect_[3]{};
    ui::Rect    hex_rect_{};

    // The name being typed for a new sheet, and where its two controls are.
    std::string new_name_;
    bool        new_focused_ = false;
    ui::Rect    new_name_rect_{};
    ui::Rect    new_button_rect_{};
    ui::Rect    save_rect_{};

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
    std::optional<paint::Hsv>  want_mix_;      // a slider moved
    std::optional<gfx::Color>  want_colour_;   // a code was typed
    bool want_undo_ = false, want_redo_ = false, want_save_ = false;
    bool want_new_  = false;
};

} // namespace studioshell
