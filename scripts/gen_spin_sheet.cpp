// Generate a vertical 8-frame "spinner" sprite sheet as .hrt (48 x 384).
// Each frame: 8 dots on a ring; a bright head rotates with a fading trail.
// Reproduce the asset (run from the repo root):
//   c++ -std=c++20 -Isrc scripts/gen_spin_sheet.cpp src/engine/image.cpp \
//       src/engine/assets.cpp -o /tmp/gen && /tmp/gen
// Output: assets/sprites/spin_8.hrt  (consumed by demo --anim, ch.86).
#include <cmath>
#include <cstdint>
#include <fstream>
#include <vector>

#include "engine/color.hpp"
#include "engine/image.hpp"

int main() {
    const int fw = 48, fh = 48, frames = 8;
    gfx::Image img;
    img.w = fw;
    img.h = fh * frames;
    img.pixels.assign(std::size_t(img.w) * img.h, gfx::rgba(0, 0, 0, 0));  // transparent

    const float PI = 3.14159265358979323846f;
    for (int f = 0; f < frames; ++f) {
        for (int d = 0; d < 8; ++d) {
            const float th  = d * (PI / 4.0f);
            const int   dcx = int(24 + std::cos(th) * 16.0f + 0.5f);
            const int   dcy = int(24 + std::sin(th) * 16.0f + 0.5f);
            const int   dist = (f - d + 8) % 8;                 // 0 = head
            const int   a = dist == 0 ? 255 : std::max(30, 255 - dist * 40);
            const int   R = 6;
            for (int yy = -R; yy <= R; ++yy)
                for (int xx = -R; xx <= R; ++xx) {
                    if (xx * xx + yy * yy > R * R) continue;
                    const int lx = dcx + xx, ly = dcy + yy;
                    if (lx < 0 || lx >= fw || ly < 0 || ly >= fh) continue;
                    const int gy = f * fh + ly;
                    img.pixels[std::size_t(gy) * img.w + lx] =
                        gfx::rgba(90, 210, 255, std::uint8_t(a));
                }
        }
    }

    const std::vector<std::uint8_t> bytes = gfx::encode_hrt(img);
    std::ofstream out("assets/sprites/spin_8.hrt", std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
    return out ? 0 : 1;
}
