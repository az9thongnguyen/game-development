// =============================================================================
//  games/studio_shell/sound_bank.hpp  —  the Studio's speaker (chapter 133)
// =============================================================================
//  The audio lab was a window whose whole job was to own an audio::Mixer, synthesise
//  four tones and stream them to platform::play_sound. Absorbing it means that job
//  moves here, next to the two frames that need it — the Studio's Edit tabs and a
//  full-screen lab — so a Sound authored on an actor is heard in both.
//
//  Everything above this file is deaf on purpose: a Workspace hands over
//  Workspace::SoundRequest values and never touches the device, which is what lets
//  the workspaces compile into headless tests. The device is opened LAZILY, on the
//  first sound, so a Studio nobody makes a noise in never asks for one.
//
//  And the device itself is a SEAM, the same shape as ui::Context::set_clipboard: a
//  desktop build wires it to platform::init_audio/play_sound in main.cpp, and a
//  headless test wires nothing, so the Studio compiles into a test binary that links
//  no SDL and every bank in it is silent. Without that, adding a speaker to the shell
//  would have broken the golden test that drives the whole shell without a window.
// =============================================================================
#pragma once

#include <cstdint>
#include <map>
#include <vector>

#include "engine/audio/mixer.hpp"
#include "games/studio_shell/workspace.hpp"

namespace studioshell {

// The three things a speaker is: open one, ask its rate, hand it samples.
struct AudioDevice {
    bool (*open)()                             = nullptr;   // true if a device opened
    int  (*rate)()                             = nullptr;   // Hz, <=0 = unknown
    void (*play)(const std::int16_t*, int)     = nullptr;   // queue n samples
};
// Install the process-wide device. Called once, from the layer that is allowed to
// know about SDL. Unset (the default) means silence, not a crash.
void set_audio_device(AudioDevice d);
[[nodiscard]] AudioDevice audio_device();

class SoundBank {
public:
    // Start one request. Its PCM is cached by (freq, ms) in a node-based map: the
    // mixer holds non-owning pointers into these buffers, so the container must never
    // move what it already handed out — a vector would, on its next growth.
    void play(const Workspace::SoundRequest& s);

    // Mix and stream one frame's worth. Silent when nothing is playing rather than
    // streaming zeros forever, so an idle Studio leaves the device alone.
    void pump();

    [[nodiscard]] std::size_t voices() const { return mixer_.active(); }
    [[nodiscard]] bool        device_ok() const { return ok_; }

private:
    void ensure_device();

    bool         opened_ = false, ok_ = false;
    int          rate_ = 44100, chunk_ = 735;
    audio::Mixer mixer_;
    std::map<std::pair<int, int>, std::vector<std::int16_t>> clips_;
    std::vector<std::int16_t> out_;
};

} // namespace studioshell
