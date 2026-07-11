// =============================================================================
//  games/audio/audio_scene.hpp  —  the audio mixer playground (--audio)
// =============================================================================
//  Buttons trigger tones; a "chord" plays four at once to show the mixer SUMMING
//  overlapping voices (the thing the queue-only seam couldn't do). Each fixed step
//  the scene mixes one chunk and streams it to platform::play_sound, and draws the
//  mixed waveform so the mixing is visible even with no speakers.
//  See docs/book/85-audio-mixer.md.
// =============================================================================
#pragma once
#include <cstdint>
#include <vector>

#include "engine/audio/mixer.hpp"
#include "engine/scene.hpp"
#include "engine/ui/ui.hpp"

namespace audiodemo {

class AudioScene : public engine::Scene {
public:
    AudioScene();
    void update(double dt, const platform::InputState& input) override;
    void render(const engine::Context& ctx) override;

private:
    audio::Mixer                          mixer_;
    ui::Context                           ui_;
    int                                   rate_  = 44100;
    int                                   chunk_ = 735;      // samples per fixed step (rate/60)
    std::vector<std::int16_t>             bank_[4];          // C E G C' tones (kept alive for voices)
    std::vector<std::int16_t>             last_;             // last mixed chunk, for the waveform
    bool                                  audio_ok_ = false;
    float                                 master_ = 0.7f;
    int                                   w_ = 960, h_ = 600;
};

} // namespace audiodemo
