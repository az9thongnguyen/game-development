# 133 — Four labs, one actor

`--fx`, `--light`, `--audio`, `--anim`. Four windows. Each opened on one engine
core — a particle system, a radial light, a voice mixer, a flipbook — with a
panel of sliders down the left and a caption along the bottom explaining what to
drag. Each was, by every measure a demo is judged by, a good demo.

And each was a **dead end**. The particles in `--fx` could not be saved. The
lights in `--light` were three hard-coded structs in a constructor. The tones in
`--audio` were four notes in an array. Nothing you did in any of those windows
could be written to a file, shipped in a release, or looked at by anything else
in the repo. They demonstrated that the engine *had* the feature. They were also
the reason nothing else did.

This chapter turns all four into components on an actor, and deletes the labs.
13 doors become 9.

---

## What "absorbed" has to mean

Chapter 120 folded twelve flags into `--lab`, and wrote down the rule that made
it safe: **only delete a door when another door reaches the same room**. It also
listed, honestly, what was not yet true — Map Lab was still the only place an
entity could be authored (chapter 132 fixed that), and seven labs were still the
only runtime consumers of seven `*_core` libraries.

That second half is the interesting one. The audio lab was the **only** caller of
`audio::Mixer` anywhere in the repo. Delete it and `audio_core` becomes a library
with tests and no users — dead code with a green test suite, which is the most
expensive kind. So absorbing had to come first and deleting second, in that
order, and the deletion is only allowed because the absorption is real.

Real means: the thing is scene data now.

```
e x=140 y=90 rot=0 color=f0c846 round w=26 h=26
  emitter=240,180,0.4,320,1.7,-1.2
  light=200,1.5,ffaa5a
  sound=523,220,0.6
```

That line saves, loads, hashes into a release id, and rides through
`--project-package` like every other byte of content. The sliders that used to
live in a window now edit a document.

---

## Four components, one rule each

**Emitter** is `fx::EmitterConfig` plus a `fx::ParticleSystem`. One system per
emitter, not one shared pool — a pool would have to carry a config, and two
emitters with different gravity would fight over it. The cost is a particle
vector per emitter, which is nothing at the counts an editor produces.

**Light** is radius, colour and intensity; its *position* comes from the actor's
`Transform2D`. That is the whole upgrade. The light lab's lights were free-floating
`{x, y}` structs and one of them was glued to the mouse; as a component a light
rides whatever it is attached to, which is the thing a lab could not express.

**Flipbook** was almost already there — `Sprite` had `frames` and `fps` from
chapter 88 — but the clock was a single `anim_time_` double on the workspace, so
every animated sprite in the scene was on the same phase and neither `loop` nor
"restart" existed. Now the clock lives on the sprite and `anim::Flipbook` — the
lab's own class, unchanged — advances it.

**Sound** is the one that needed a decision.

## A pure core cannot open a speaker

`sandbox_core` compiles into `test_sandbox`, `test_runner` and
`test_scene_workspace`, none of which links SDL. A `Sound` component that played
itself would drag an audio device into all three, and "the test suite needs a
sound card" is not a trade anybody should take.

So the model does not play. It **records**:

```cpp
// What the last tick asked to be heard. The model has no device and must not have
// one, so it records instead of playing; the host drains this. Cleared per tick.
std::vector<Sound> sounds;
```

`World::tick` pushes a `Sound` when it destroys an actor that carries one — the
only event the sandbox already had, so no new trigger concept was needed, and it
is exactly the noise a coin makes when something eats it. A `Workspace` hands
those up as `SoundRequest` values through a virtual that defaults to empty, and
`studioshell::SoundBank` — which owns an `audio::Mixer` and synthesises tones on
demand — turns them into samples.

And the device under *that* is a seam:

```cpp
struct AudioDevice {
    bool (*open)()                         = nullptr;
    int  (*rate)()                         = nullptr;
    void (*play)(const std::int16_t*, int) = nullptr;
};
void set_audio_device(AudioDevice d);
```

`main.cpp` wires it to `platform::init_audio`/`play_sound`. A headless test wires
nothing and every bank in it is silent. This is the same shape as
`ui::Context::set_clipboard`, for the same reason, and without it giving the
Studio a speaker would have broken `test_shell_golden` — the test that drives the
entire Studio shell with no window.

---

## Then the screenshot, again

Fifth chapter running. Three bugs, none of which any assertion had noticed.

**The light ate the editor.** A 160 px radius on an actor near the edge of a
320 px scene bled clear across the panel background. Every particle and every
light is now clipped to the *world* rect, not the canvas rect. The world is where
the world stops, and a glow that keeps going past it does not just look wrong —
it hides the boundary the editor exists to show you.

**Every slider label sat on the control above it.** `ui::Context::slider` draws
its `"label: value"` at `r.y - sz_caption - 2` — *above* the rect it is handed.
The rect is the groove, not the control. So a row is the label **plus** the
groove, and advancing by the groove alone stacks each label onto its neighbour.
The two transform sliders that predate this chapter had been doing it all along.

**An empty panel does not explain itself.** While the scene is running the body
is not drawn, which used to be a small blank gap and is now a large one. It says
`running — Stop to edit`.

## And one the screenshot could not have found

Three effect sections make the inspector taller than the panel it lives in, so
the body scrolls. `ui::Context::begin_scroll` clips the *drawing* — and nothing
else. `point_in`, the predicate every widget's hit test runs through, knew
nothing about it.

So a control scrolled above the viewport keeps a live rect wherever the offset
put it. Invisible, because the renderer clips it. Clickable, because the hit test
does not. Scroll the inspector to the bottom and the Play button in the header is
sitting under a ghost that eats the click.

This is the inverse of the bug this project keeps finding. Chapter 127 and
chapter 132 were both *drawn but not clickable*. This one is *clickable but not
drawn*, and it is worse, because there is nothing on screen to be suspicious
about. The fix is six lines in `point_in`, where every caller gets it — the
Studio's Project asset list scrolls too, and had the same hole.

```cpp
const int n = scroll_depth_ < 4 ? scroll_depth_ : 4;
for (int i = 0; i < n; ++i) {
    const Rect& c = scroll_clip_[i];
    if (in_.mx < c.x || in_.mx >= c.x + c.w || in_.my < c.y || in_.my >= c.y + c.h)
        return false;
}
```

Both directions are tested, in `test_ui` (a row scrolled above the viewport is
dead; the row in view still works) and in `test_scene_workspace` (with the body
scrolled to the bottom, the Play button still plays).

---

## The test that did not exist

The scene workspace was the only one of the three Studio editors with **no test
at all**. Map and Pixels each got one the chapter they grew; Scene never did.

It has one now, and its questions are deliberately about *reach*:

```cpp
// Where an inspector control landed in the last draw, BY NAME — empty when it was
// not drawn at all, and empty when it was scrolled out of the viewport.
[[nodiscard]] ui::Rect control_rect(const char* id) const;
```

Named rather than indexed, because an index moves the instant a section is
inserted above it. Empty-when-clipped, because a test that asks where a control
is must be told the truth about whether a finger could land on it. And the helper
that uses it scrolls until the control is reachable and **fails if it never is** —
below the fold is "scroll to it", not "cannot be used", and a helper that
silently skipped an unreachable control would pass every assertion in the file
while the panel was unusable.

Writing it turned up the usual thing: my first draft of `select_first` fed a
`ui::Input` and selected nothing, because the canvas reads `platform::InputState`
and the widgets read `ui::Input`. Two input paths through one frame. A test that
only fed the widget one would have "proved" the inspector empty.

## Mutations

21 single-token mutations; six lived the first pass, and every one of them named
a question no test was asking.

| survivor | what it exposed |
|---|---|
| `light=90` zeroes the rest | no test used a short field list, so "keeps the default" and "zeroes it" both looked right — and a light that reads back at intensity 0 does nothing |
| `noloop` on a still sprite | nothing pinned the choice, so nothing objected to writing a token that would move the bytes of every scene with a plain square in it |
| slider row height | only the screenshot had ever seen it — and it is arithmetic, so it belongs in an assertion |
| Restart does nothing | the test compared the clock before and after, but every frame animates and the cycle is 0.5 s, so both readings were arbitrary points on a wrap |
| `pump` streams silence | `SoundBank` had no test at all |
| `animate` divides by zero fps | **equivalent** — `anim::Flipbook` guards `fps > 0` in both `update()` and `frame()` |

20/21 after. The one that lives is genuinely equivalent, and saying so is part of
the result rather than an excuse for it.

## And the harness ate a fix

Chapter 132 recorded that the mutation harness owns the working tree while it
runs. This chapter found the next edge of that: I restored a mutated file with
`git checkout`, and git restored it to **HEAD** — silently deleting the
uncommitted fix that was in it. The mutation went away and so did the code it was
testing, and the suite went green on both counts.

Restore by file copy. `git checkout` is not a restore, it is a different file.

---

## What this cost and what it bought

| | before | after |
|---|---|---|
| labs | 13 | 9 |
| lines in `src/games/{fx,light,audio,anim}` | 331 | 0 |
| runtime consumers of `audio_core` | 1 (a lab) | 1 (the Studio) |
| an effect you can save | no | yes |
| test suites | 77 | 78 |

Net **−492 lines**, and `particles_core`, `light_core`, `audio_core` and
`tween_core` all kept their tests, kept their callers, and became things you can
put in a game rather than things you can look at.

## Not verified

- **`dir` and `spread` have a gizmo, not a handle.** You set the emitter's aim
  with a slider and read it off three lines on the canvas. Dragging the gizmo
  would be the obvious next thing.
- **An effect cannot be part of an Archetype**, so a `Spawner` cannot spawn a
  glowing particle-trailing thing — deliberately, since a proto that could carry
  an emitter would nest a particle system inside a template that spawns copies of
  itself, but it is a real ceiling.
- **The Texture button is the only way to get `frames > 1`**, and it cycles a
  fixed list probed by filename (`studio_NN`, `spin_8`, `sheet_NN`). That path —
  pick a sheet, get a flipbook — is **not covered by a test**; the test sets
  `frames` in the fixture instead, because its scratch asset root has no art in
  it.
- **A Sound is heard on destroy and nowhere else.** No "on spawn", no "on
  overlap", no looping ambience.
- **Particles only fly during Play.** Stopped, you get the gizmo.
- **One light is fine and ten are not** — the deposit is the light lab's naive
  O(radius²) per light, per frame, in canvas space.
- The em dash in the new `running — Stop to edit` caption renders as a box in the
  headless screenshots, which have no Inter to load. In the Studio it is an em
  dash. This is a property of the test harness, not of the panel.
