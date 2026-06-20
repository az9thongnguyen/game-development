# Chapter 29 — A\* Pathfinding

> **What this is.** The farmer's brain. Right-click a tile and he walks the *shortest*
> route there, routing around trees, rocks, and water. We hand-write **A\*** in
> `src/games/iso/pathfind.cpp` — the most important graph-search algorithm in games —
> plus the smooth movement that follows the path in `Farm::update`. You will learn
> the open/closed sets, why the heuristic must be *admissible*, the **octile** metric
> for 8-direction grids, and the "no corner cutting" rule.

---

## 1. The problem

A grid of cells, some walkable, some not. Given a start and a goal, find the cheapest
sequence of steps from one to the other — or report that none exists. "Cheapest" means
fewest tiles for orthogonal moves, with diagonal moves costing a little more (you
travel `√2` times as far).

We could brute-force it (breadth-first search explores everything outward), but that
wastes time fanning out in the wrong direction. **A\*** is BFS that *aims*: it prefers
cells that look closer to the goal, while never being fooled into a suboptimal path.

## 2. The core idea: f = g + h

A\* keeps, for each cell it considers, two numbers:

- **`g`** — the *real* cost of the best route found *so far* from start to this cell.
- **`h`** — a *heuristic estimate* of the cost remaining from this cell to the goal.

It always expands the open cell with the smallest **`f = g + h`** — the most promising
total-cost guess. Two guarantees make it work:

1. If `h` is **admissible** (never *over*-estimates the true remaining cost), the first
   time A\* pops the goal, that path is **optimal**.
2. If `h` is also **consistent** (the estimate drops by at most the step cost each
   move — the triangle inequality), each cell is finalized exactly once, so we can
   close a cell on pop and never revisit it.

Set `h = 0` and A\* degenerates into Dijkstra's algorithm (correct but unguided). A
good `h` keeps the search pointed at the goal.

## 3. The octile heuristic

Our farmer moves in **8 directions**. Orthogonal step = `1`, diagonal step = `√2`. The
exact admissible/consistent heuristic for that movement is the **octile distance**:
walk diagonally across the shorter axis, then straight the rest of the way.

```
dx = |ax - bx|,  dy = |ay - by|
h  = (dx + dy) + (√2 - 2) · min(dx, dy)
```

```cpp
float octile(Vec2i a, Vec2i b) {
    const float dx = std::fabs(float(a.x - b.x));
    const float dy = std::fabs(float(a.y - b.y));
    return (dx + dy) + (kSqrt2 - 2.0f) * std::min(dx, dy);
}
```

Sanity check the formula: `(dx+dy)` would be the cost if you could only go straight;
the correction `(√2 − 2)·min(dx,dy)` is *negative* (√2−2 ≈ −0.586), shaving off the
saving you get by replacing `min(dx,dy)` pairs of straight steps with cheaper
diagonals. It never exceeds the true cost → admissible. Using plain Euclidean distance
would *also* be admissible but a looser estimate (slower search); using Manhattan
`dx+dy` would *over*-estimate when diagonals are allowed and could return non-optimal
paths. Octile is the right tool for an 8-grid.

## 4. The algorithm, line by line

```cpp
std::vector<Vec2i> astar(int w, int h, Vec2i start, Vec2i goal,
                         const std::function<bool(int,int)>& walkable);
```

Note the **decoupling**: A\* doesn't know what a farm is. It asks a caller-supplied
`walkable(x, y)` predicate. That keeps the planner reusable on any grid and trivially
testable on toy maps (the unit tests pass lambdas like `[](int x,int){return x!=3;}`).

**Guards first** — reject impossible inputs cheaply:

```cpp
if (w <= 0 || h <= 0) return {};
if (!inb(start) || !inb(goal)) return {};
if (!walkable(start) || !walkable(goal)) return {};   // can't start/end in a wall
if (start == goal) return { start };                  // already there
```

**The open set** is a min-heap on `f`. We use lazy deletion: instead of decreasing a
key in place (which `std::priority_queue` can't do), we push a new, better entry and
ignore stale ones when they pop:

```cpp
std::vector<float> g(n, INF);      // best known cost to each cell
std::vector<int>   came(n, -1);    // predecessor, for path reconstruction
std::vector<char>  closed(n, 0);   // finalized cells

g[si] = 0;
open.push({ octile(start, goal), si });
```

**The main loop**: pop the cheapest, skip if already finalized (a stale duplicate),
stop if it's the goal, otherwise relax its 8 neighbors:

```cpp
while (!open.empty()) {
    Node cur = open.top(); open.pop();
    if (closed[cur.i]) continue;          // stale; a better entry already won
    closed[cur.i] = 1;
    if (cur.i == gi) { found = true; break; }

    for (int k = 0; k < 8; ++k) {
        int nx = cx + dx8[k], ny = cy + dy8[k];
        if (!inb(nx,ny) || !walkable(nx,ny)) continue;
        const bool diag = (k >= 4);
        if (diag && (!walkable(cx,ny) || !walkable(nx,cy))) continue;  // no corner cut
        int ni = idx(nx,ny);
        if (closed[ni]) continue;
        float ng = g[cur.i] + (diag ? kSqrt2 : 1.0f);
        if (ng < g[ni]) {                 // found a cheaper way to ni
            g[ni] = ng;
            came[ni] = cur.i;
            open.push({ ng + octile({nx,ny}, goal), ni });
        }
    }
}
```

**Reconstruction**: if we reached the goal, walk the `came` predecessors back and
reverse:

```cpp
for (int i = gi; i != -1; i = came[i]) out.push_back({ i % w, i / w });
std::reverse(out.begin(), out.end());     // start … goal
```

If `found` stayed false, the open set drained without reaching the goal → return empty
= "unreachable".

## 5. No corner cutting

A diagonal move from `(cx,cy)` to `(nx,ny)` slips between two cells. If *both* of
those shared orthogonal neighbors are walls, the diagonal would visually clip through
a wall corner:

```
   # = wall,  . = open,  the diagonal . → . would cut the corner:

     . #          allowed? NO — both # block the gap
     # .          require walkable(cx,ny) AND walkable(nx,cy)
```

The single line `if (diag && (!walkable(cx,ny) || !walkable(nx,cy))) continue;`
enforces it. Without this rule, agents shave wall corners and look like they're
phasing through geometry. `test_astar` includes a case where corner-cutting is the
*only* way through and asserts the path is correctly reported as **impossible**.

## 6. Following the path: smooth movement

A\* returns whole cells; the farmer should *glide*, not teleport. `Farm::command_farmer`
stores the path in the `Mover` component and sets `idx = 1` (skip the cell he's already
on). `Farm::update` then advances his fractional `Position` toward the next cell each
tick, at `speed` tiles per second:

```cpp
float remaining = mv->speed * dt;
while (remaining > 0 && mv->moving()) {
    Vec2i tgt = mv->path[mv->idx];
    float dx = tgt.x - pos->x, dy = tgt.y - pos->y;
    float dist = std::sqrt(dx*dx + dy*dy);
    if (dist <= remaining) { pos->x = tgt.x; pos->y = tgt.y; remaining -= dist; ++mv->idx; }
    else { pos->x += dx/dist*remaining; pos->y += dy/dist*remaining; remaining = 0; }
}
```

The `while` loop lets a fast farmer cross several short cells in one tick without
overshooting. Because `Position` is fractional, the depth sort (Chapter 27) places him
correctly *between* tiles as he moves — that's why the floats in `iso.hpp` mattered.

## 7. Run & observe

In `--iso`, right-click a clear tile across the map: the farmer beelines there, cutting
diagonals where it's cheaper. Now wall him in — surround a target with trees/water and
right-click inside: the HUD flashes **"no path"** (the empty-path return). Drop a fence
across his route mid-walk and re-issue the command: he re-plans around it. Every one of
those behaviors is the code above.

## 8. Pitfalls

- **Inadmissible heuristic.** Scaling `h` up "to go faster" (greedy/weighted A\*) makes
  the search faster but the path possibly *suboptimal*. Fine for some games; know the
  trade-off. Manhattan distance on an 8-grid is inadmissible — don't use it here.
- **Decrease-key.** `std::priority_queue` has none; the lazy-deletion `if
  (closed[i]) continue;` is the standard workaround. Forgetting it makes the heap
  process stale entries and can corrupt results.
- **Float keys.** Mixing `1.0` and `√2` means `g`/`f` are floats; comparisons are
  exact enough here, but don't test them with `==`.
- **Re-planning cost.** We re-run A\* from scratch on every command. For one agent on a
  16×16 grid that's microseconds. Thousands of agents would want a shared flow field
  or hierarchical paths (exercise 4).

## 9. Glossary

- **A\*** — best-first graph search expanding the smallest `f = g + h`.
- **g / h / f** — cost-so-far / heuristic-remaining / their sum.
- **Admissible / consistent** — `h` never over-estimates / obeys the triangle
  inequality; together they guarantee optimal, single-pop search.
- **Octile distance** — exact heuristic for 8-connected grids with `√2` diagonals.
- **Open / closed set** — frontier to expand / cells already finalized.
- **Corner cutting** — illegal diagonal slipping between two wall cells.

## 10. Exercises

1. **4-connected mode.** Restrict to orthogonal moves (Manhattan `h`) and compare the
   paths — blockier, and now Manhattan *is* admissible.
2. **Path smoothing.** Post-process the cell path: if there's line-of-sight from
   `path[i]` to `path[i+2]`, drop `path[i+1]`. The farmer walks straighter.
3. **Weighted terrain.** Make soil cost 1.5 and path cost 0.8; change the step cost so
   the farmer prefers the stone path. (`g` becomes terrain-dependent.)
4. **Many agents.** Add 20 farmers. Where does per-agent A\* hurt, and what would a
   flow field buy you?
