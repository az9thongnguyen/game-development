// =============================================================================
//  tests/test_sandbox.cpp  —  sandbox pure-core tests (dependency-free, CTest)
// =============================================================================
//  Covers the declarative sandbox core: spawn wiring, the deterministic tick
//  systems (mover/spinner/bouncer/lifetime/spawner/overlap), and the text
//  serializer used for both save/load and the Play/Stop snapshot. No SDL/window.
// =============================================================================
#include "games/sandbox/world.hpp"
#include "games/sandbox/serialize.hpp"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace sandbox;

static int g_failures = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::printf("FAIL %s:%d:  %s\n", __FILE__, __LINE__, #cond);  \
            ++g_failures;                                                 \
        }                                                                 \
    } while (0)

static bool approx(double a, double b, double eps = 1e-4) {
    return std::fabs(a - b) <= eps * (1.0 + std::fabs(a) + std::fabs(b));
}

// ---- Task 1: spawn ---------------------------------------------------------
static void test_spawn_attaches() {
    World w;
    Archetype a; a.mover = true; a.vx = 5; a.bouncer = true; a.tag = 3;
    ecs::Entity e = w.spawn(a, 10, 20);
    CHECK(w.alive() == 1);
    CHECK(w.reg.has<Transform2D>(e) && w.reg.has<Body>(e) && w.reg.has<Sprite>(e));
    CHECK(w.reg.has<Mover>(e) && w.reg.has<Bouncer>(e) && w.reg.has<Tag>(e));
    CHECK(!w.reg.has<Spinner>(e) && !w.reg.has<Lifetime>(e));
    CHECK(w.reg.get<Transform2D>(e)->x == 10 && w.reg.get<Mover>(e)->vx == 5);
    CHECK(w.reg.get<Tag>(e)->id == 3);
}

// ---- Task 2: tick systems --------------------------------------------------
static void test_mover_integrates_and_deterministic() {
    World w; Archetype a; a.mover = true; a.vx = 10; a.vy = -4;
    ecs::Entity e = w.spawn(a, 100, 100);
    w.tick(0.5f);
    CHECK(approx(w.reg.get<Transform2D>(e)->x, 105.0));
    CHECK(approx(w.reg.get<Transform2D>(e)->y, 98.0));
    // determinism: two worlds with an IDENTICAL history end bit-identical.
    World d1; ecs::Entity h1 = d1.spawn(a, 100, 100);
    World d2; ecs::Entity h2 = d2.spawn(a, 100, 100);
    for (int i = 0; i < 10; ++i) { d1.tick(0.016f); d2.tick(0.016f); }
    CHECK(d1.reg.get<Transform2D>(h1)->x == d2.reg.get<Transform2D>(h2)->x);
}
static void test_spinner() {
    World w; Archetype a; a.spinner = true; a.omega = 2;
    ecs::Entity e = w.spawn(a, 0, 0); w.tick(0.5f);
    CHECK(approx(w.reg.get<Transform2D>(e)->rot, 1.0));
}
static void test_bouncer_reflects() {
    World w; w.bounds_w = 200; w.bounds_h = 200;
    Archetype a; a.mover = true; a.vx = 100; a.vy = 0; a.bouncer = true; a.w = 20; a.h = 20;
    ecs::Entity e = w.spawn(a, 195, 100);              // 195+10 > 200 -> hits right wall
    w.tick(0.1f);
    CHECK(w.reg.get<Mover>(e)->vx < 0);                // x-vel flipped
    CHECK(w.reg.get<Mover>(e)->vy == 0);              // y untouched
    CHECK(w.reg.get<Transform2D>(e)->x <= 190);       // clamped inside
}
static void test_lifetime_despawns() {
    World w; Archetype a; a.lifetime = true; a.ttl = 0.05f;
    w.spawn(a, 0, 0); CHECK(w.alive() == 1);
    w.tick(0.1f);     CHECK(w.alive() == 0);
}
static void test_spawner() {
    World w; Archetype ball; ball.w = 4; ball.h = 4;
    Archetype emitter; Spawner sp; sp.interval = 1.0f; sp.proto = ball;
    ecs::Entity e = w.spawn(emitter, 50, 60);
    w.reg.add<Spawner>(e, sp);
    for (int i = 0; i < 10; ++i) w.tick(0.34f);        // 3.4s -> 3 spawns
    CHECK(w.alive() == 4);                             // emitter + 3
}
static void test_overlap_trigger() {
    World w;
    Archetype coin; coin.tag = 1; w.spawn(coin, 100, 100);
    Archetype sweeper; OnOverlap o; o.other_tag = 1; o.action = Action::DestroyOther;
    ecs::Entity s = w.spawn(sweeper, 100, 100); w.reg.add<OnOverlap>(s, o);
    w.tick(0.016f);
    CHECK(w.alive() == 1);                             // coin gone, sweeper stays
    CHECK(w.reg.valid(s));
    // no overlap -> nothing dies
    World w2; w2.spawn(coin, 0, 0);
    ecs::Entity s2 = w2.spawn(sweeper, 400, 400); w2.reg.add<OnOverlap>(s2, o);
    w2.tick(0.016f);
    CHECK(w2.alive() == 2);
}

// ---- Task 3: serializer ----------------------------------------------------
static void test_archetype_codec() {
    Archetype a; a.color = gfx::rgb(0x40, 0xcc, 0xff); a.round = true;
    a.mover = true; a.vx = 12; a.vy = -3; a.bouncer = true; a.tag = 2; a.w = 18;
    Archetype b = parse_archetype(archetype_tokens(a));
    CHECK(archetype_tokens(a) == archetype_tokens(b));  // stable round-trip
    CHECK(b.mover && b.vx == 12 && b.bouncer && b.tag == 2 && b.round);
}
static void test_scene_roundtrip() {
    World w; w.bounds_w = 800; w.bounds_h = 600;
    Archetype ball; ball.mover = true; ball.vx = 40; ball.vy = 30; ball.bouncer = true; ball.round = true;
    w.spawn(ball, 100, 120);
    Archetype coin; coin.tag = 1; w.spawn(coin, 300, 200);
    const std::string once = to_scene(w);
    World reparsed = from_scene(once);
    CHECK(to_scene(reparsed) == once);                  // text round-trip
    // trajectory fidelity: two restores tick identically
    World a = from_scene(once), b = from_scene(once);
    for (int i = 0; i < 50; ++i) { a.tick(0.016f); b.tick(0.016f); }
    CHECK(to_scene(a) == to_scene(b));
}
static void test_scene_with_behaviors_roundtrip() {
    World w;
    Archetype emitter; ecs::Entity e = w.spawn(emitter, 60, 60);
    Archetype small; small.w = 6; small.h = 6; small.mover = true; small.vy = 60; small.lifetime = true; small.ttl = 3;
    Spawner sp; sp.interval = 1.5f; sp.proto = small;
    w.reg.add<Spawner>(e, sp);
    Archetype sweeper; ecs::Entity s = w.spawn(sweeper, 100, 100);
    OnOverlap o; o.other_tag = 1; o.action = Action::SpawnProto; o.proto = small;
    w.reg.add<OnOverlap>(s, o);
    const std::string once = to_scene(w);
    CHECK(to_scene(from_scene(once)) == once);          // proto sub-records round-trip
}
// ---- textured sprites: a texture is a name that round-trips through the codec ----
static void test_spawn_copies_texture() {
    World w; Archetype a; a.texture = "studio_03";
    ecs::Entity e = w.spawn(a, 0, 0);
    CHECK(w.reg.get<Sprite>(e)->texture == "studio_03");
    Archetype b; ecs::Entity e2 = w.spawn(b, 0, 0);      // default is untextured
    CHECK(w.reg.get<Sprite>(e2)->texture.empty());
}
static void test_archetype_codec_texture() {
    Archetype a; a.texture = "studio_07"; a.mover = true; a.vx = 5;
    Archetype b = parse_archetype(archetype_tokens(a));
    CHECK(b.texture == "studio_07" && b.mover && b.vx == 5);
}
static void test_scene_roundtrip_texture() {
    World w; Archetype a; a.texture = "studio_02"; w.spawn(a, 10, 20);
    const std::string s = to_scene(w);
    CHECK(s.find("tex=studio_02") != std::string::npos);  // token present
    CHECK(to_scene(from_scene(s)) == s);                   // round-trips
    World u; Archetype b; u.spawn(b, 0, 0);                // untextured emits no token
    CHECK(to_scene(u).find("tex=") == std::string::npos);
}
// ---- animated sprites: frames/fps ride the archetype and round-trip ----
static void test_animated_sprite_roundtrip() {
    World w; Archetype a; a.texture = "spin_8"; a.frames = 8; a.fps = 12;
    ecs::Entity e = w.spawn(a, 30, 40);
    CHECK(w.reg.get<Sprite>(e)->frames == 8 && w.reg.get<Sprite>(e)->fps == 12);
    const std::string s = to_scene(w);
    CHECK(s.find("frames=8") != std::string::npos && s.find("fps=12") != std::string::npos);
    CHECK(to_scene(from_scene(s)) == s);                    // round-trips
    // A static sprite (frames==1) emits no anim tokens.
    World u; Archetype b; b.texture = "studio_00"; u.spawn(b, 0, 0);
    CHECK(to_scene(u).find("frames=") == std::string::npos);
}
static void test_snapshot_restore() {
    World w; Archetype ball; ball.mover = true; ball.vx = 40; ball.bouncer = true;
    w.spawn(ball, 50, 50);
    const std::string snap = to_scene(w);
    for (int i = 0; i < 100; ++i) w.tick(0.016f);
    World restored = from_scene(snap);
    CHECK(to_scene(restored) == snap);                  // Stop restores the placed state
}

// ---- chapter 133: the four effect labs, as components ----------------------

// An Emitter's config is scene data, so it must survive a save/load byte for byte —
// including `dir`, the only one with no slider (it is set by the gizmo, not typed).
static void test_emitter_roundtrip() {
    World w;
    Archetype a; a.name = "fountain";
    ecs::Entity e = w.spawn(a, 100, 200);
    Emitter em;
    em.cfg.rate = 240; em.cfg.speed = 180; em.cfg.spread = 0.4f;
    em.cfg.gravity = 320; em.cfg.life = 1.7f; em.cfg.dir = -1.2f;
    w.reg.add<Emitter>(e, em);

    const std::string text = to_scene(w);
    CHECK(text.find("emitter=240,180,0.4,320,1.7,-1.2") != std::string::npos);
    World back = from_scene(text);
    CHECK(to_scene(back) == text);
    ecs::Entity be{};
    back.reg.view<Emitter>([&](ecs::Entity h, Emitter&) { be = h; });
    Emitter* got = back.reg.get<Emitter>(be);
    CHECK(got != nullptr);
    if (got) CHECK(approx(got->cfg.gravity, 320.0));
}

// The emitter has to actually emit — a config that round-trips and produces nothing
// is a slider wired to a number nobody reads (chapters 123, 130 and 132, three times).
static void test_emitter_emits_at_the_actor() {
    World w;
    Archetype a;
    ecs::Entity e = w.spawn(a, 50, 60);
    Emitter em; em.cfg.rate = 100; em.cfg.gravity = 0; em.cfg.speed = 0; em.cfg.speed_var = 0;
    w.reg.add<Emitter>(e, em);
    CHECK(w.reg.get<Emitter>(e)->sys.alive() == 0);
    w.tick(0.1f);                                    // 100/s for 0.1 s = 10 particles
    const Emitter* live = w.reg.get<Emitter>(e);
    CHECK(live->sys.alive() == 10);
    // At the ACTOR, not the origin: the emitter rides whatever it is attached to.
    if (live->sys.alive() > 0) {
        CHECK(approx(live->sys.particles()[0].x, 50.0, 1e-2));
        CHECK(approx(live->sys.particles()[0].y, 60.0, 1e-2));
    }
    // Two emitters must not share a stream. Seeded by load order, so they differ.
    World two = from_scene("sandbox1\nbounds 640 360\n"
                           "e x=10 y=10 color=ffffff w=8 h=8 emitter=100,90,0.5,0,1.2,-1.5708\n"
                           "e x=10 y=10 color=ffffff w=8 h=8 emitter=100,90,0.5,0,1.2,-1.5708\n");
    two.tick(0.1f);
    std::vector<float> vx;
    two.reg.view<Emitter>([&](ecs::Entity, Emitter& m) {
        if (!m.sys.particles().empty()) vx.push_back(m.sys.particles()[0].vx);
    });
    CHECK(vx.size() == 2);
    if (vx.size() == 2) CHECK(vx[0] != vx[1]);
}

// A light rides its actor: that is the one thing the light lab could not do.
static void test_light_roundtrip_and_follows() {
    World w;
    Archetype a; a.mover = true; a.vx = 100; a.vy = 0;
    ecs::Entity e = w.spawn(a, 10, 20);
    w.reg.add<Light>(e, Light{200.0f, gfx::rgb(0xff, 0xaa, 0x5a), 1.5f});
    const std::string text = to_scene(w);
    CHECK(text.find("light=200,1.5,ffaa5a") != std::string::npos);
    CHECK(to_scene(from_scene(text)) == text);
    w.tick(1.0f);
    CHECK(approx(w.reg.get<Transform2D>(e)->x, 110.0));   // the light's position IS this

    // A ONE-field light keeps both remaining defaults. Zeroing what the text does not
    // mention would make `light=90` an invisible light with intensity 0, which reads
    // as "the component does nothing" rather than "the file said less than it could".
    World w3 = from_scene("sandbox1\nbounds 640 360\ne x=1 y=1 color=ffffff w=8 h=8 light=90\n");
    ecs::Entity le3{};
    w3.reg.view<Light>([&](ecs::Entity h, Light&) { le3 = h; });
    const Light* L3 = w3.reg.get<Light>(le3);
    CHECK(L3 != nullptr);
    if (L3) { CHECK(approx(L3->radius, 90.0)); CHECK(approx(L3->intensity, Light{}.intensity)); }

    // A colour-less light keeps its default; "the text after the last comma" would
    // read the intensity as a colour and dye it black.
    World w2 = from_scene("sandbox1\nbounds 640 360\ne x=1 y=1 color=ffffff w=8 h=8 light=90,2\n");
    ecs::Entity le{};
    w2.reg.view<Light>([&](ecs::Entity h, Light&) { le = h; });
    const Light* L = w2.reg.get<Light>(le);
    CHECK(L != nullptr);
    if (L) { CHECK(approx(L->radius, 90.0)); CHECK(approx(L->intensity, 2.0));
             CHECK(L->color == Light{}.color); }
}

// The model has no audio device and must not have one: a destroyed actor RECORDS
// what should be heard, and the host with the device drains it.
static void test_sound_recorded_on_destroy() {
    World w;
    Archetype coin; coin.tag = 1; coin.w = coin.h = 16;
    ecs::Entity c = w.spawn(coin, 100, 100);
    w.reg.add<Sound>(c, Sound{523.0f, 200.0f, 0.5f});
    Archetype eater; eater.w = eater.h = 20; eater.mover = true;
    ecs::Entity ev = w.spawn(eater, 100, 100);
    w.reg.add<OnOverlap>(ev, OnOverlap{1, Action::DestroyOther, {}});

    CHECK(w.sounds.empty());
    w.tick(0.016f);
    CHECK(w.sounds.size() == 1);
    if (w.sounds.size() == 1) CHECK(approx(w.sounds[0].freq, 523.0));
    w.tick(0.016f);
    CHECK(w.sounds.empty());          // one tick's worth, then cleared

    const std::string text = to_scene(from_scene(
        "sandbox1\nbounds 640 360\ne x=1 y=1 color=ffffff w=8 h=8 sound=523,200,0.5\n"));
    CHECK(text.find("sound=523,200,0.5") != std::string::npos);
}

// The sprite-animation lab, as a component: one clock, advanced by animate(), that
// loops or holds — and a stopped editor still animates, which is why it is not tick().
static void test_flipbook_clock() {
    World w;
    Archetype a; a.frames = 4; a.fps = 8.0f;
    ecs::Entity e = w.spawn(a, 0, 0);
    CHECK(sprite_frame(*w.reg.get<Sprite>(e)) == 0);
    w.animate(0.25f);                                     // 2 frames at 8 fps
    CHECK(sprite_frame(*w.reg.get<Sprite>(e)) == 2);
    w.animate(0.5f);                                      // wraps: 4 more frames
    CHECK(sprite_frame(*w.reg.get<Sprite>(e)) == 2);
    // tick() must NOT advance it — two clocks for one number is the drift this split
    // exists to prevent.
    const float before = w.reg.get<Sprite>(e)->t;
    w.tick(0.25f);
    CHECK(approx(w.reg.get<Sprite>(e)->t, before));

    // A STILL sprite never writes `noloop`, whatever its loop flag says: looping is
    // meaningless without frames, and writing it would move the bytes of every scene
    // file that has a plain coloured square in it.
    Archetype still; still.frames = 1; still.loop = false;
    World w1; w1.spawn(still, 0, 0);
    CHECK(to_scene(w1).find("noloop") == std::string::npos);

    Archetype once; once.frames = 4; once.fps = 8.0f; once.loop = false;
    ecs::Entity o = w.spawn(once, 0, 0);
    w.animate(10.0f);
    CHECK(sprite_frame(*w.reg.get<Sprite>(o)) == 3);      // one-shot holds the last frame
    const std::string text = to_scene(w);
    CHECK(text.find("noloop") != std::string::npos);
    CHECK(to_scene(from_scene(text)) == text);
    // Looping is the default, so it is never written — a scene file that says nothing
    // about looping means "loop", the way every scene written before today does.
    World old = from_scene("sandbox1\nbounds 9 9\ne x=0 y=0 color=ffffff w=8 h=8 frames=4 fps=8\n");
    int looping = 0;
    old.reg.view<Sprite>([&](ecs::Entity, Sprite& s) { if (s.loop) ++looping; });
    CHECK(looping == 1);
}

int main() {
    test_spawn_attaches();
    test_mover_integrates_and_deterministic();
    test_spinner();
    test_bouncer_reflects();
    test_lifetime_despawns();
    test_spawner();
    test_overlap_trigger();
    test_archetype_codec();
    test_scene_roundtrip();
    test_scene_with_behaviors_roundtrip();
    test_spawn_copies_texture();
    test_archetype_codec_texture();
    test_scene_roundtrip_texture();
    test_animated_sprite_roundtrip();
    test_snapshot_restore();
    test_emitter_roundtrip();
    test_emitter_emits_at_the_actor();
    test_light_roundtrip_and_follows();
    test_sound_recorded_on_destroy();
    test_flipbook_clock();
    if (g_failures == 0) std::printf("sandbox: all tests passed\n");
    else                 std::printf("sandbox: %d FAILURE(S)\n", g_failures);
    return g_failures;
}
