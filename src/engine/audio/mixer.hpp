// =============================================================================
//  engine/audio/mixer.hpp  —  a small software voice mixer (pure, no SDL)
// =============================================================================
//  The platform seam can only *queue* PCM (SDL_QueueAudio), so two clips played
//  at once play back-to-back, not together. This mixer fixes that above the seam:
//  it SUMS all active voices into one output chunk (with clipping), which a scene
//  streams to platform::play_sound each frame. Pure and deterministic — mixing is
//  plain integer arithmetic — so it unit-tests headless with no audio device.
//  See docs/book/85-audio-mixer.md.
// =============================================================================
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace audio {

// One playing clip: a non-owning view into PCM the caller keeps alive, a play
// cursor, and a volume. (Non-owning by design — sounds live in a long-lived bank;
// the mixer never copies them. Don't play a temporary.)
struct Voice {
    const int16_t* data = nullptr;
    int            len  = 0;
    int            pos  = 0;
    float          vol  = 1.0f;
};

class Mixer {
public:
    // Start a clip. len is a sample count; the buffer must outlive playback.
    void play(const int16_t* data, int len, float vol = 1.0f);

    // Fill `out` with the next `n` mixed samples: sum of every active voice's next
    // slice, clipped to int16. Advances cursors and drops finished voices. Writes
    // silence where no voice reaches. `out` must have room for n samples.
    void mix(int16_t* out, int n);

    std::size_t active() const { return voices_.size(); }
    void        stop_all()     { voices_.clear(); }

private:
    std::vector<Voice>   voices_;
    std::vector<int32_t> acc_;   // scratch accumulator (reused to avoid per-call alloc)
};

// ---- Tiny tone synthesis (pure helpers for SFX / the demo) ------------------
// A sine "blip": `freq` Hz for `ms` milliseconds at `rate` Hz, peak `vol` (0..1),
// with a linear decay envelope so it doesn't click at the end. Sample count is
// rate*ms/1000.
std::vector<int16_t> tone(float freq, float ms, int rate, float vol = 0.5f);

} // namespace audio
