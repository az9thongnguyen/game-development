// =============================================================================
//  games/iso/pathfind.hpp  —  A* shortest path on a tile grid
// =============================================================================
//  A* finds the cheapest route from start to goal by always expanding the open
//  cell with the smallest f = g + h, where g is the real cost so far and h is an
//  ADMISSIBLE estimate of the cost remaining (never an over-estimate). Admissible
//  h ⇒ the first time we pop the goal, its path is optimal.
//
//  Decoupling: the planner does not know what a "farm" is. It asks a caller
//  predicate walkable(x,y) whether a cell may be entered. That keeps A* reusable
//  (any grid, any blocking rule) and trivially testable on toy maps.
//
//  Movement model: 8-connected. Orthogonal step costs 1, diagonal costs √2, and
//  we forbid "corner cutting" — a diagonal move is allowed only when BOTH shared
//  orthogonal neighbors are walkable (you can't squeeze through a wall corner).
// =============================================================================
#pragma once

#include <functional>
#include <vector>

#include "engine/iso.hpp"   // iso::Vec2i

namespace iso {

// Returns the cell path from `start` to `goal` INCLUSIVE.
//   • empty           → unreachable, or invalid inputs, or start/goal blocked
//   • {start}         → start == goal
//   • {start,…,goal}  → an optimal route otherwise
std::vector<Vec2i> astar(int w, int h, Vec2i start, Vec2i goal,
                         const std::function<bool(int, int)>& walkable);

} // namespace iso
