// =============================================================================
//  engine/ui/touch_input.hpp  —  platform contacts adapted to pure touch pointers
// =============================================================================
#pragma once

#include <cstddef>

#include "engine/ui/touch.hpp"
#include "platform/input.hpp"

namespace touch {

// A frame-local view used by game controls. Geometry and hit testing stay pure in
// touch.hpp; this small seam is the only place that knows how platform contacts are
// represented. Real contacts win over SDL's synthesized mouse, otherwise one finger
// can become two identical actions.
struct PointerSet {
    Pointer data[platform::InputState::kTouchMax]{};
    std::size_t count = 0;

    [[nodiscard]] const Pointer& primary() const { return data[0]; }
    [[nodiscard]] bool over(const Box& box) const {
        for (std::size_t i = 0; i < count; ++i)
            if (box.contains(data[i].x, data[i].y)) return true;
        return false;
    }
    [[nodiscard]] bool down_in(const Box& box) const {
        for (std::size_t i = 0; i < count; ++i)
            if (data[i].down && box.contains(data[i].x, data[i].y)) return true;
        return false;
    }
};

inline PointerSet pointers(const platform::InputState& input) {
    PointerSet out;
    if (input.has_touch()) {
        for (const platform::TouchContact& contact : input.touches) {
            if (!contact.used) continue;
            out.data[out.count++] = Pointer{contact.x, contact.y,
                                            contact.down, contact.pressed};
        }
        return out;
    }

    out.data[0] = Pointer{input.mouse_x, input.mouse_y,
                          input.down(platform::MouseButton::Left),
                          input.pressed(platform::MouseButton::Left)};
    out.count = 1;
    return out;
}

} // namespace touch
