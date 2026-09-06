// =============================================================================
//  games/studio_shell/sound_bank.cpp
// =============================================================================
#include "games/studio_shell/sound_bank.hpp"

#include <algorithm>

namespace studioshell {

namespace {
AudioDevice g_device{};
}

void set_audio_device(AudioDevice d) { g_device = d; }
AudioDevice audio_device() { return g_device; }

void SoundBank::ensure_device() {
    if (opened_) return;
    opened_ = true;
    ok_     = g_device.open && g_device.open() && g_device.play;
    const int r = (ok_ && g_device.rate) ? g_device.rate() : 0;
    rate_   = r > 0 ? r : 44100;
    chunk_  = std::max(1, rate_ / 60);
    out_.assign(static_cast<std::size_t>(chunk_), 0);
}

void SoundBank::play(const Workspace::SoundRequest& s) {
    ensure_device();
    // Quantise the key so a slider dragged through 300.1, 300.2 … does not synthesise
    // a new clip per frame. One hertz and one millisecond are below what an ear
    // resolves here and well above what the float noise of a drag produces.
    const int hz = static_cast<int>(s.freq  + 0.5f);
    const int ms = static_cast<int>(s.ms    + 0.5f);
    if (hz <= 0 || ms <= 0) return;
    const std::pair<int, int> key{hz, ms};
    auto it = clips_.find(key);
    if (it == clips_.end())
        it = clips_.emplace(key, audio::tone(static_cast<float>(hz), static_cast<float>(ms),
                                             rate_, 0.6f)).first;
    if (it->second.empty()) return;
    mixer_.play(it->second.data(), static_cast<int>(it->second.size()),
                std::clamp(s.gain, 0.0f, 1.0f));
}

void SoundBank::pump() {
    if (!ok_ || mixer_.active() == 0) return;
    if (out_.size() != static_cast<std::size_t>(chunk_))
        out_.assign(static_cast<std::size_t>(chunk_), 0);
    mixer_.mix(out_.data(), chunk_);
    g_device.play(out_.data(), chunk_);
}

} // namespace studioshell
