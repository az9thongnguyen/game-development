// =============================================================================
//  tests/test_audio.cpp  —  software voice mixer (dependency-free, CTest)
// =============================================================================
//  Voices sum, volume scales, the sum clips to int16, finished voices drop, and
//  the tone synth has the right length + bounded amplitude. No SDL, no device.
// =============================================================================
#include "engine/audio/mixer.hpp"

#include <cstdio>
#include <vector>

static int g_failures = 0;
#define CHECK(c)                                                                 \
    do {                                                                         \
        if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++g_failures; } \
    } while (0)

int main() {
    // Two constant (DC) voices sum sample-for-sample.
    {
        std::vector<std::int16_t> a(8, 1000), b(8, 2000);
        audio::Mixer m;
        m.play(a.data(), int(a.size()), 1.0f);
        m.play(b.data(), int(b.size()), 1.0f);
        CHECK(m.active() == 2);
        std::int16_t out[8] = {};
        m.mix(out, 8);
        for (int i = 0; i < 8; ++i) CHECK(out[i] == 3000);
        CHECK(m.active() == 0);                       // both voices consumed
    }

    // Volume scales a voice.
    {
        std::vector<std::int16_t> a(4, 1000);
        audio::Mixer m;
        m.play(a.data(), int(a.size()), 0.5f);
        std::int16_t out[4] = {};
        m.mix(out, 4);
        for (int i = 0; i < 4; ++i) CHECK(out[i] == 500);
    }

    // The summed output clips to the int16 range instead of overflowing.
    {
        std::vector<std::int16_t> hi(4, 30000);
        audio::Mixer m;
        m.play(hi.data(), int(hi.size()), 1.0f);
        m.play(hi.data(), int(hi.size()), 1.0f);      // 60000 > 32767
        std::int16_t out[4] = {};
        m.mix(out, 4);
        for (int i = 0; i < 4; ++i) CHECK(out[i] == 32767);

        std::vector<std::int16_t> lo(4, -30000);
        audio::Mixer m2;
        m2.play(lo.data(), int(lo.size()), 1.0f);
        m2.play(lo.data(), int(lo.size()), 1.0f);
        m2.mix(out, 4);
        for (int i = 0; i < 4; ++i) CHECK(out[i] == -32768);
    }

    // A short voice stops; mixing past its end yields silence, not garbage.
    {
        std::vector<std::int16_t> a(2, 5000);
        audio::Mixer m;
        m.play(a.data(), int(a.size()), 1.0f);
        std::int16_t out[4] = {};
        m.mix(out, 4);                                // voice covers first 2 only
        CHECK(out[0] == 5000 && out[1] == 5000);
        CHECK(out[2] == 0 && out[3] == 0);
        CHECK(m.active() == 0);
        std::int16_t out2[4] = {7, 7, 7, 7};
        m.mix(out2, 4);                               // no voices → all silence
        for (int i = 0; i < 4; ++i) CHECK(out2[i] == 0);
    }

    // Guards: null/empty play is a no-op; stop_all clears.
    {
        audio::Mixer m;
        m.play(nullptr, 10, 1.0f);
        std::vector<std::int16_t> a(4, 100);
        m.play(a.data(), 0, 1.0f);
        CHECK(m.active() == 0);
        m.play(a.data(), 4, 1.0f);
        CHECK(m.active() == 1);
        m.stop_all();
        CHECK(m.active() == 0);
    }

    // tone(): correct length, amplitude bounded by vol, not all-zero.
    {
        const std::vector<std::int16_t> s = audio::tone(440.0f, 100.0f, 44100, 0.5f);
        CHECK(int(s.size()) == int(44100 * 100.0f / 1000.0f));   // 4410
        const int bound = int(0.5f * 32767.0f) + 1;
        bool any_nonzero = false;
        for (std::int16_t v : s) {
            CHECK(v <= bound && v >= -bound);
            if (v != 0) any_nonzero = true;
        }
        CHECK(any_nonzero);
        CHECK(audio::tone(440.0f, 100.0f, 0, 0.5f).empty());     // rate<=0 guard
    }

    if (g_failures == 0) std::printf("audio: all tests passed\n");
    else                 std::printf("audio: %d FAILURE(S)\n", g_failures);
    return g_failures;
}
