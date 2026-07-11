// =============================================================================
//  engine/audio/mixer.cpp
// =============================================================================
#include "engine/audio/mixer.hpp"

#include <algorithm>
#include <cmath>

namespace audio {

void Mixer::play(const int16_t* data, int len, float vol) {
    if (data == nullptr || len <= 0) return;
    voices_.push_back(Voice{data, len, 0, vol});
}

void Mixer::mix(int16_t* out, int n) {
    if (n <= 0) return;
    acc_.assign(static_cast<std::size_t>(n), 0);

    for (Voice& v : voices_) {
        const int take = std::min(n, v.len - v.pos);
        for (int i = 0; i < take; ++i)
            acc_[static_cast<std::size_t>(i)] +=
                static_cast<std::int32_t>(std::lround(v.data[v.pos + i] * v.vol));
        v.pos += take;
    }
    // Drop voices that have played out.
    voices_.erase(std::remove_if(voices_.begin(), voices_.end(),
                                 [](const Voice& v) { return v.pos >= v.len; }),
                  voices_.end());

    for (int i = 0; i < n; ++i) {
        std::int32_t s = acc_[static_cast<std::size_t>(i)];
        if (s >  32767) s =  32767;
        if (s < -32768) s = -32768;
        out[i] = static_cast<std::int16_t>(s);
    }
}

std::vector<std::int16_t> tone(float freq, float ms, int rate, float vol) {
    const int n = rate > 0 ? static_cast<int>(rate * ms / 1000.0f) : 0;
    std::vector<std::int16_t> s(static_cast<std::size_t>(std::max(0, n)));
    constexpr float kTwoPi = 6.28318530717958647692f;
    for (int i = 0; i < n; ++i) {
        const float t   = static_cast<float>(i) / static_cast<float>(rate);
        const float env = 1.0f - static_cast<float>(i) / static_cast<float>(n);  // linear decay
        s[static_cast<std::size_t>(i)] =
            static_cast<std::int16_t>(std::sin(kTwoPi * freq * t) * vol * env * 32767.0f);
    }
    return s;
}

} // namespace audio
