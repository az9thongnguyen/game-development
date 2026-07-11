// =============================================================================
//  games/sandbox/world.cpp  —  spawn + the deterministic tick systems
// =============================================================================
#include "games/sandbox/world.hpp"

#include <algorithm>
#include <cmath>

namespace sandbox {

ecs::Entity World::spawn(const Archetype& a, float x, float y) {
    ecs::Entity e = reg.create();
    reg.add<Transform2D>(e, {x, y, 0, 1});
    reg.add<Body>(e, {a.w, a.h});
    reg.add<Sprite>(e, {a.color, a.round, a.texture, a.frames, a.fps});
    if (a.mover)    reg.add<Mover>(e, {a.vx, a.vy});
    if (a.spinner)  reg.add<Spinner>(e, {a.omega});
    if (a.bouncer)  reg.add<Bouncer>(e, {});
    if (a.lifetime) reg.add<Lifetime>(e, {a.ttl});
    if (a.tag)      reg.add<Tag>(e, {a.tag});
    return e;
}

namespace {
// Two centered AABBs overlap iff they overlap on both axes.
bool aabb_overlap(const Transform2D& ta, const Body& ba,
                  const Transform2D& tb, const Body& bb) {
    return std::fabs(ta.x - tb.x) * 2 < (ba.w + bb.w) &&
           std::fabs(ta.y - tb.y) * 2 < (ba.h + bb.h);
}
} // namespace

void World::tick(float dt) {
    struct SpawnCmd { Archetype proto; float x, y; };
    std::vector<SpawnCmd>    spawns;
    std::vector<ecs::Entity> destroys;

    // 1. spawners: emit one proto per elapsed interval (catch up if dt is large).
    reg.view<Spawner, Transform2D>([&](ecs::Entity, Spawner& s, Transform2D& t) {
        s.timer += dt;
        while (s.interval > 0 && s.timer >= s.interval) {
            s.timer -= s.interval;
            spawns.push_back({s.proto, t.x, t.y});
        }
    });
    // 2. movers
    reg.view<Mover, Transform2D>([&](ecs::Entity, Mover& m, Transform2D& t) {
        t.x += m.vx * dt;
        t.y += m.vy * dt;
    });
    // 3. spinners
    reg.view<Spinner, Transform2D>([&](ecs::Entity, Spinner& s, Transform2D& t) {
        t.rot += s.omega * dt;
    });
    // 4. bouncers: clamp inside the world and flip the offending velocity axis.
    reg.view<Bouncer, Transform2D>([&](ecs::Entity e, Bouncer&, Transform2D& t) {
        Mover* m = reg.get<Mover>(e);
        Body*  b = reg.get<Body>(e);
        if (!m || !b) return;
        const float hw = b->w * 0.5f, hh = b->h * 0.5f;
        if (t.x - hw < 0)        { t.x = hw;              m->vx =  std::fabs(m->vx); }
        if (t.x + hw > bounds_w) { t.x = bounds_w - hw;   m->vx = -std::fabs(m->vx); }
        if (t.y - hh < 0)        { t.y = hh;              m->vy =  std::fabs(m->vy); }
        if (t.y + hh > bounds_h) { t.y = bounds_h - hh;   m->vy = -std::fabs(m->vy); }
    });
    // 5. lifetime
    reg.view<Lifetime>([&](ecs::Entity e, Lifetime& l) {
        l.ttl -= dt;
        if (l.ttl <= 0) destroys.push_back(e);
    });
    // 6. overlaps: first hit wins its action (read-only; edits deferred to reap).
    // ponytail: O(n^2) all-pairs overlap, fine for a sandbox; grid-hash if counts grow.
    reg.view<OnOverlap, Transform2D, Body>(
        [&](ecs::Entity e, OnOverlap& o, Transform2D& t, Body& b) {
            bool fired = false;
            reg.view<Tag, Transform2D, Body>(
                [&](ecs::Entity ot, Tag& tag, Transform2D& t2, Body& b2) {
                    if (fired || tag.id != o.other_tag || ot.index == e.index) return;
                    if (!aabb_overlap(t, b, t2, b2)) return;
                    fired = true;
                    switch (o.action) {
                        case Action::DestroySelf:  destroys.push_back(e);  break;
                        case Action::DestroyOther: destroys.push_back(ot); break;
                        case Action::SpawnProto:   spawns.push_back({o.proto, t.x, t.y}); break;
                    }
                });
        });

    // 7. reap: destroys (deduped by index) first, then spawns.
    std::sort(destroys.begin(), destroys.end(),
              [](ecs::Entity a, ecs::Entity c) { return a.index < c.index; });
    destroys.erase(std::unique(destroys.begin(), destroys.end(),
                   [](ecs::Entity a, ecs::Entity c) { return a.index == c.index; }),
                   destroys.end());
    for (ecs::Entity e : destroys) reg.destroy(e);
    for (auto& s : spawns)         spawn(s.proto, s.x, s.y);
}

} // namespace sandbox
