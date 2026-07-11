# Audio Mixer — design

**Track A (engine depth). Date: 2026-07-11.**

## Goal

Give the engine a real **software voice mixer** so overlapping sound effects play
*together*, not one-after-another. Sound is the last obviously-missing engine
dimension after visuals — this is the audio half of "engine không chỉ 2d/3d".

## The gap this fills

The platform seam already exposes `init_audio` / `audio_rate` / `play_sound`, but
`play_sound` is just `SDL_QueueAudio` — it concatenates clips into the device
queue. Two clips played the same frame play back-to-back, never summed (the code
even says "no callback mixer"). So a chord, or a footstep over music, is impossible
today. We fix it *above* the seam so the platform boundary stays untouched.

## Approach (why above the seam)

A pure `Mixer` sums all active voices into one output chunk; a scene mixes one
fixed-step chunk each `update()` and streams it via the existing `play_sound`.
Because production (one `rate/60` chunk per 1/60 s step) ≈ real-time consumption,
SDL's queue stays shallow and playback is continuous. This needs **zero changes to
`platform.hpp` / `backend_sdl.cpp`** — the non-negotiable seam is left alone, and
the valuable logic lives in testable engine code.

**Not** an audio-thread callback with a lock-free ring (the "correct" low-latency
design). That's a bigger, concurrency-heavy slice; the streamed-chunk model is the
lean version and self-limits (`play_sound` drops past ~1 s backlog). Noted as a
deferral with the upgrade path.

## Design

### Pure core — `engine/audio/mixer.{hpp,cpp}` (`audio_core`)

```
struct Voice { const int16_t* data; int len; int pos; float vol; };   // non-owning
class Mixer {
  void        play(const int16_t* data, int len, float vol = 1);
  void        mix(int16_t* out, int n);   // sum active voices, clip int16, drop finished
  std::size_t active() const;
  void        stop_all();
};
std::vector<int16_t> tone(float freq, float ms, int rate, float vol = 0.5);  // sine + decay
```

- `mix` accumulates each voice's next slice into an `int32` scratch buffer, clips
  to `[-32768, 32767]`, advances cursors, and drops played-out voices. Silence
  where no voice reaches. Deterministic integer arithmetic — headless-testable.
- Voices are **non-owning** views into a caller-held sound bank (no copies); the
  buffer must outlive playback (documented).
- `tone` synthesizes a sine "blip" with a linear decay envelope (no end-click);
  the pure SFX source for the demo and tests.

### Scene glue — `--audio` (`games/audio/audio_scene.{hpp,cpp}`)

Four tone buttons + a **"chord"** button (plays all four at once — the visible
proof of summing), a master-volume slider, and a **live waveform** of the last
mixed chunk so the mixing is visible even with no speakers. `update()` mixes one
chunk and streams it; buttons add voices. Falls back gracefully to "(no device)"
if `init_audio` fails (the mixer + waveform still work).

## Testing

`tests/test_audio.cpp`, dependency-free (no audio device):
- two DC voices sum sample-for-sample; both drop after consumption.
- volume scales a voice; summed output **clips** at ±int16 limits.
- a short voice stops mid-chunk → silence after, `active()==0`; no-voice mix is
  all-silence.
- guards: null / zero-length play is a no-op; `stop_all` clears.
- `tone`: exact length (`rate*ms/1000`), amplitude bounded by `vol`, not all-zero,
  `rate<=0` → empty.

## Deferrals (ponytail)

Audio-thread callback + lock-free ring (low-latency, glitch-proof); looping voices;
per-voice pan (stereo); ADSR envelopes; WAV/asset loading; distance attenuation
(pair with 2D lighting positions for spatial SFX); sound as a sandbox component.
