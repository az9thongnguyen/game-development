// =============================================================================
//  games/iso/pathfind.cpp  —  A* implementation
// =============================================================================
#include "games/iso/pathfind.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>

namespace iso {
namespace {

constexpr float kSqrt2 = 1.41421356237f;

// Octile distance: the exact admissible/consistent heuristic for 8-connected
// grids with diagonal cost √2. Walk diagonally over the shorter axis, then
// straight for the rest:  h = (dx+dy) + (√2 - 2)·min(dx,dy).
float octile(Vec2i a, Vec2i b) {
    const float dx = std::fabs(static_cast<float>(a.x - b.x));
    const float dy = std::fabs(static_cast<float>(a.y - b.y));
    return (dx + dy) + (kSqrt2 - 2.0f) * std::min(dx, dy);
}

} // namespace

std::vector<Vec2i> astar(int w, int h, Vec2i start, Vec2i goal,
                         const std::function<bool(int, int)>& walkable) {
    std::vector<Vec2i> out;
    if (w <= 0 || h <= 0) return out;

    auto inb = [&](int x, int y) { return x >= 0 && y >= 0 && x < w && y < h; };
    if (!inb(start.x, start.y) || !inb(goal.x, goal.y)) return out;
    if (!walkable(start.x, start.y) || !walkable(goal.x, goal.y)) return out;
    if (start == goal) { out.push_back(start); return out; }

    const int  n   = w * h;
    auto       idx = [&](int x, int y) { return y * w + x; };
    const int  si  = idx(start.x, start.y);
    const int  gi  = idx(goal.x, goal.y);

    std::vector<float> g(static_cast<std::size_t>(n), std::numeric_limits<float>::infinity());
    std::vector<int>   came(static_cast<std::size_t>(n), -1);
    std::vector<char>  closed(static_cast<std::size_t>(n), 0);

    struct Node { float f; int i; };
    struct Cmp  { bool operator()(const Node& a, const Node& b) const { return a.f > b.f; } };
    std::priority_queue<Node, std::vector<Node>, Cmp> open;

    g[si] = 0.0f;
    open.push({octile(start, goal), si});

    // 4 orthogonal (cost 1) then 4 diagonal (cost √2).
    static const int dx8[8] = {+1, -1, 0, 0, +1, +1, -1, -1};
    static const int dy8[8] = {0, 0, +1, -1, +1, -1, +1, -1};

    bool found = false;
    while (!open.empty()) {
        const Node cur = open.top();
        open.pop();
        if (closed[cur.i]) continue;          // stale duplicate (lazy deletion)
        closed[cur.i] = 1;
        if (cur.i == gi) { found = true; break; }

        const int cx = cur.i % w;
        const int cy = cur.i / w;
        for (int k = 0; k < 8; ++k) {
            const int nx = cx + dx8[k];
            const int ny = cy + dy8[k];
            if (!inb(nx, ny) || !walkable(nx, ny)) continue;

            const bool diag = (k >= 4);
            if (diag && (!walkable(cx, ny) || !walkable(nx, cy))) {
                continue;                     // no corner cutting through a wall
            }

            const int ni = idx(nx, ny);
            if (closed[ni]) continue;
            const float ng = g[cur.i] + (diag ? kSqrt2 : 1.0f);
            if (ng < g[ni]) {
                g[ni]    = ng;
                came[ni] = cur.i;
                open.push({ng + octile({nx, ny}, goal), ni});
            }
        }
    }

    if (!found) return out;                   // unreachable → empty

    for (int i = gi; i != -1; i = came[i]) {  // walk parents back from the goal
        out.push_back(Vec2i{i % w, i / w});
    }
    std::reverse(out.begin(), out.end());
    return out;
}

} // namespace iso
