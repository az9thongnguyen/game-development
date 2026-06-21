// =============================================================================
//  games/colony/colony.cpp  —  Sim implementation (ECS + jobs)
// =============================================================================
#include "games/colony/colony.hpp"

#include <cmath>
#include <utility>

#include "games/iso/pathfind.hpp"

namespace colony {

Sim::Sim(int w, int h, int workers)
    : map_(w, h, iso::Terrain::Grass), jobs_(workers) {}

ecs::Entity Sim::spawn_agent(int x, int y, gfx::Color color) {
    const ecs::Entity e = reg_.create();
    reg_.add<GridPos>(e, {static_cast<float>(x), static_cast<float>(y)});
    reg_.add<Visual>(e, {color, true});
    reg_.add<Agent>(e, {});
    ++agent_count_;
    return e;
}

ecs::Entity Sim::spawn_prop(int x, int y, gfx::Color color) {
    const ecs::Entity e = reg_.create();
    reg_.add<GridPos>(e, {static_cast<float>(x), static_cast<float>(y)});
    reg_.add<Visual>(e, {color, false});
    return e;
}

bool Sim::walkable(int x, int y) const { return map_.terrain_walkable(x, y); }

void Sim::set_goal(int gx, int gy) {
    auto walk = [this](int x, int y) { return walkable(x, y); };
    reg_.view<Agent, GridPos>([&](ecs::Entity, Agent& a, GridPos& p) {
        const iso::Vec2i start{static_cast<int>(std::lround(p.x)), static_cast<int>(std::lround(p.y))};
        a.path = iso::astar(width(), height(), start, iso::Vec2i{gx, gy}, walk);
        a.idx  = (a.path.size() > 1) ? 1 : a.path.size();   // skip the current cell
    });
}

void Sim::update(double dt) {
    // Snapshot the agent entities, then advance them in PARALLEL. Each task touches
    // only its own entity's Agent/GridPos (disjoint dense-array slots) and never adds
    // or removes entities, so the pools are read concurrently and written at distinct
    // slots — data-race-free (verified by ThreadSanitizer).
    std::vector<ecs::Entity> ents;
    reg_.view<Agent, GridPos>([&](ecs::Entity e, Agent&, GridPos&) { ents.push_back(e); });

    const float h = static_cast<float>(dt);
    jobs_.parallel_for(ents.size(), [&](std::size_t i) {
        Agent*   a = reg_.get<Agent>(ents[i]);
        GridPos* p = reg_.get<GridPos>(ents[i]);
        if (!a || !p || !a->moving()) return;
        float remaining = a->speed * h;
        while (remaining > 0.0f && a->moving()) {
            const iso::Vec2i t = a->path[a->idx];
            const float dx = static_cast<float>(t.x) - p->x;
            const float dy = static_cast<float>(t.y) - p->y;
            const float dist = std::sqrt(dx * dx + dy * dy);
            if (dist <= remaining || dist < 1e-5f) {
                p->x = static_cast<float>(t.x);
                p->y = static_cast<float>(t.y);
                remaining -= dist;
                ++a->idx;
            } else {
                p->x += dx / dist * remaining;
                p->y += dy / dist * remaining;
                remaining = 0.0f;
            }
        }
    }, 64);
}

void Sim::reset_default() {
    reg_ = ecs::Registry{};            // wipe all entities (move-assign a fresh one)
    agent_count_ = 0;

    map_.fill(iso::Terrain::Grass);
    const int w = width(), h = height();
    for (int x = 0; x < w; ++x) map_.set(x, h / 2, iso::Terrain::Path);          // a road
    for (int y = h - 4; y < h - 1; ++y)
        for (int x = w - 5; x < w - 1; ++x) map_.set(x, y, iso::Terrain::Water); // a pond

    // A few static props…
    const gfx::Color prop = gfx::rgb(120, 100, 70);
    const iso::Vec2i props[] = {{2, 2}, {5, 3}, {3, 8}, {9, 6}, {7, 10}};
    for (const iso::Vec2i pr : props) spawn_prop(pr.x, pr.y, prop);

    // …and a handful of agents along the road.
    const gfx::Color agent = gfx::rgb(90, 180, 235);
    for (int i = 0; i < 6; ++i) spawn_agent(1 + i, h / 2, agent);
}

} // namespace colony
