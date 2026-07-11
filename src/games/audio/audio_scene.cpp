// =============================================================================
//  games/audio/audio_scene.cpp
// =============================================================================
#include "games/audio/audio_scene.hpp"

#include <algorithm>
#include <cstdio>

#include "engine/ui/theme.hpp"
#include "platform/platform.hpp"

namespace audiodemo {

using platform::MouseButton;

namespace {
const char* kNames[4] = {"C  (262)", "E  (330)", "G  (392)", "C' (523)"};
const float kFreqs[4] = {262.0f, 330.0f, 392.0f, 523.0f};
} // namespace

AudioScene::AudioScene() {
    audio_ok_ = platform::init_audio();
    rate_     = platform::audio_rate() > 0 ? platform::audio_rate() : 44100;
    chunk_    = std::max(1, rate_ / 60);
    for (int i = 0; i < 4; ++i) bank_[i] = audio::tone(kFreqs[i], 260.0f, rate_, 0.6f);
    last_.assign(static_cast<std::size_t>(chunk_), 0);
}

void AudioScene::update(double, const platform::InputState&) {
    // Mix exactly one fixed step of audio and stream it. Running once per fixed
    // 1/60 step keeps production ≈ real-time consumption; SDL's queue absorbs the
    // jitter. ponytail: proper fix is an audio-thread callback pulling a lock-free
    // ring — deferred; the queue self-limits (play_sound drops past ~1s backlog).
    mixer_.mix(last_.data(), chunk_);
    if (audio_ok_) platform::play_sound(last_.data(), chunk_);
}

void AudioScene::render(const engine::Context& ctx) {
    gfx::Renderer2D& g = ctx.gfx;
    w_ = g.width(); h_ = g.height();
    g.set_font(ctx.font, ui::theme::sz_body);
    g.clear(gfx::rgb(14, 14, 20));

    // ---- waveform of the last mixed chunk ----
    const int mid = h_ / 2, amp = h_ / 3;
    g.draw_line(0, mid, w_, mid, gfx::rgb(40, 40, 56));
    int px = 0, py = mid;
    for (int x = 0; x < w_; ++x) {
        const int idx = chunk_ > 0 ? x * chunk_ / w_ : 0;
        const int s   = idx < int(last_.size()) ? last_[static_cast<std::size_t>(idx)] : 0;
        const int y   = mid - s * amp / 32768;
        if (x > 0) g.draw_line_aa(px, py, x, y, gfx::rgb(120, 230, 170));
        px = x; py = y;
    }

    // ---- UI ----
    ui::Input in;
    in.mx = ctx.input.mouse_x; in.my = ctx.input.mouse_y;
    in.down     = ctx.input.down(MouseButton::Left);
    in.pressed  = ctx.input.pressed(MouseButton::Left);
    in.released = ctx.input.released(MouseButton::Left);
    ui_.begin(&g, in);
    ui_.panel(ui::Rect{12, 12, 210, 260}, "AUDIO MIXER");
    for (int i = 0; i < 4; ++i)
        if (ui_.button(kNames[i]))
            mixer_.play(bank_[i].data(), int(bank_[i].size()), master_);
    if (ui_.button("chord (C+E+G+C')"))
        for (int i = 0; i < 4; ++i) mixer_.play(bank_[i].data(), int(bank_[i].size()), master_);
    ui_.slider("master", master_, 0.0f, 1.0f);
    char cnt[64];
    std::snprintf(cnt, sizeof(cnt), "voices: %zu%s", mixer_.active(), audio_ok_ ? "" : "  (no device)");
    ui_.label(cnt);
    ui_.end();

    g.set_font_size(ui::theme::sz_caption);
    g.draw_text(232, h_ - 20, "play tones — 'chord' sums 4 voices at once (that's the mixer)",
                ui::theme::text_muted);
}

} // namespace audiodemo
