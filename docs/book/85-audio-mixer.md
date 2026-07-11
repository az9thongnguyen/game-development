# 85 · Audio Mixer — making sounds play *together*

> Code: `src/engine/audio/mixer.{hpp,cpp}` · lib `audio_core` · test `tests/test_audio.cpp`
> Seen (and heard) live: `./build/demo --audio`

## Why this chapter exists

Every engine feature so far has been visual. But a game with no sound is half a
game — and it turns out ours couldn't really do sound at all. The platform seam
has had `play_sound` since the raycaster (M2), but look at what it does:

```cpp
void play_sound(const int16_t* samples, int count) {
    ...
    SDL_QueueAudio(g_audio_dev, samples, count * sizeof(int16_t));   // append to queue
}
```

It *appends* to SDL's device queue. Play two clips in the same frame and they come
out one after the other, never on top of each other. A chord, a footstep over
music, an explosion while the engine hums — all impossible. The code even admits
it: "no callback mixer in M2." This chapter writes that mixer.

## The one decision that shapes everything: mix above the seam

There are two places a mixer could live:

1. **Below the seam**, on SDL's audio callback thread — the textbook low-latency
   design. It means a lock-free ring buffer between the game thread and the audio
   thread, and it means editing `backend_sdl.cpp` and the platform interface.
2. **Above the seam**, in engine code — a pure mixer sums voices into a chunk, and
   a scene streams that chunk through the *existing* `play_sound` each frame.

We pick #2. The reason is the project's non-negotiable rule: the platform seam is
sacred and stays thin. #2 touches **zero** lines of `platform.hpp` or
`backend_sdl.cpp` — all the logic lands in a testable engine core. The cost is that
it's not sample-accurate low-latency; the benefit is it's simple, safe, and honest
about the constraint. The `// ponytail:` comment in the scene names the ceiling and
the upgrade path (#1) for the day a game needs it.

This works because of a timing coincidence: the game already runs a **fixed
timestep** (ch. on `App`) — `update()` is called at a steady 1/60 s. If each
`update()` produces exactly `rate/60` samples (735 at 44.1 kHz) and the device
consumes `rate` samples per second, production ≈ consumption. SDL's queue stays a
few frames deep and playback is continuous. Overproduce and `play_sound` self-limits
(it drops anything past ~1 s of backlog).

## The mixer

The whole thing is one `mix` call. A `Voice` is a non-owning view into some PCM the
caller keeps alive, plus a cursor and a volume:

```cpp
struct Voice { const int16_t* data; int len; int pos; float vol; };
```

`mix` sums every active voice's next slice into a scratch `int32` buffer — *wider*
than the output on purpose, so the sum can exceed the int16 range before we decide
what to do about it — then clips and writes:

```cpp
void Mixer::mix(int16_t* out, int n) {
    acc_.assign(n, 0);                                   // int32 scratch
    for (Voice& v : voices_) {
        const int take = std::min(n, v.len - v.pos);
        for (int i = 0; i < take; ++i)
            acc_[i] += int32_t(std::lround(v.data[v.pos + i] * v.vol));
        v.pos += take;
    }
    // drop voices that have played out
    voices_.erase(std::remove_if(voices_.begin(), voices_.end(),
                    [](const Voice& v){ return v.pos >= v.len; }), voices_.end());
    for (int i = 0; i < n; ++i) {
        int32_t s = acc_[i];
        if (s >  32767) s =  32767;                       // clip, don't wrap
        if (s < -32768) s = -32768;
        out[i] = int16_t(s);
    }
}
```

Two details matter:

- **The int32 accumulator.** If you summed directly into `int16`, two loud voices
  would overflow and *wrap* — +30000 + +30000 becomes a horrible negative screech.
  Accumulating wide and then clipping to ±32767 is the difference between "loud" and
  "broken". The test plays two +30000 voices and asserts the result is exactly
  `32767`, and two −30000 voices give exactly `−32768`.
- **Voices are non-owning.** The mixer never copies sample data — a `Voice` points
  into a sound bank the scene owns for its whole lifetime. Zero-copy is the lazy
  *and* fast choice; the price is a documented rule: don't play a temporary.

`tone` is the pure SFX source — a sine at `freq` for `ms` milliseconds, with a
linear decay envelope so it fades instead of clicking off:

```cpp
const float env = 1.0f - float(i) / n;                    // 1 → 0 over the clip
s[i] = int16_t(std::sin(2*pi*freq*t) * vol * env * 32767);
```

## Determinism and the headless test

There is no device, no thread, and no clock in `audio_core` — `mix` is pure integer
arithmetic. So `test_audio` runs with no speakers and no SDL: it plays constant-value
voices and checks the sums *exactly* (`1000 + 2000 == 3000`), checks the clip
limits, checks that a short voice stops and leaves silence, and checks `tone`'s
length and amplitude bound. That's the same "pure core, headless test" discipline as
the particle and lighting cores — audio is not a special case.

> A footnote from writing this: the first draft of `mixer.cpp` had a `kTwoPi`
> constant whose "T" was accidentally a Cyrillic "Т". It *looked* right and might
> even have compiled as a Unicode identifier — a genuinely invisible bug. Worth a
> glance whenever a constant "should obviously work" but the eye trusts the glyph.

## Seeing (and hearing) it: `--audio`

The demo has four tone buttons (C, E, G, C′), a **chord** button that plays all
four at once, a master-volume slider, and a **live oscilloscope** of the last mixed
chunk. The chord is the whole point made visible: press it and the waveform jumps
from a single sine to the lumpy sum of four — voices mixing, exactly what the queue
alone could never show. If no audio device opens, the label reads "(no device)" and
everything still runs; the mixer and the scope don't need speakers.

The `update()` glue is four lines:

```cpp
mixer_.mix(last_.data(), chunk_);            // one fixed step of audio
if (audio_ok_) platform::play_sound(last_.data(), chunk_);   // stream it
```

## What was deliberately left out

- **Audio-thread callback + lock-free ring** — the low-latency design; deferred
  until a game needs sample-accurate timing.
- **Looping voices, stereo pan, ADSR envelopes, WAV/asset loading** — all future.
- **Spatial SFX** — distance attenuation could reuse the 2D-lighting positions
  (ch.84); a natural later composition, not built now (YAGNI).

## Try it

```sh
ctest --test-dir build -R '^audio$' --output-on-failure   # the headless proof
./build/demo --audio                                      # press "chord" and watch the sum
```
