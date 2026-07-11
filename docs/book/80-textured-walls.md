# Chapter 80 — Textured Walls: All Three Tools Meet

> Code: `src/games/fps/raycast_scene.cpp` (`load_wall_textures`),
> `src/games/fps/textures.hpp` (`WallTextures`); assets `assets/textures/wall_1..3.hrt`;
> run `./build/demo --fps` (walk the Lab level, now Lab-textured).

This is a short chapter because it is a small change — and that is the whole point. By now
the Mini-Studio has three tools: the **Texture Lab** makes `.hrt` images (ch.73–75), the
**Map / Level Lab** makes `.map` levels (ch.78), and the **Textured Sprites** join (ch.77)
put Lab images on sandbox actors. This chapter closes the triangle: a Lab-authored level,
drawn with Lab-authored *wall* textures, walked in the fps raycaster. Author a level in
`--maplab`, author textures in `--studio`, and `--fps` shows both — no engine change, because
the seams were already in the right places.

## The one thing that made it a 12-line change

The raycaster's wall loop (ch. on the FPS) samples a `gfx::Image` per wall id:

```cpp
const gfx::Image& tex = textures_.for_id(h.wall);
const int texW = tex.w, texH = tex.h;                 // ← reads the image's OWN size
...
const gfx::Color c = px[texY * texW + texX];
```

It never assumed a size. The procedural textures happen to be 64×64, but the sampler indexes
by `tex.w`/`tex.h`, so a 128×128 Lab texture — or any size — drops in unchanged. That single
existing property is what let the join be an *override at construction* rather than a rewrite:

```cpp
WallTextures load_wall_textures() {
    WallTextures wt = make_wall_textures();               // hand-made procedural baseline
    for (int id = 1; id <= 3; ++id) {
        char path[32]; std::snprintf(path, sizeof(path), "textures/wall_%d.hrt", id);
        if (auto img = gfx::load_image(path)) wt.tex[id] = std::move(*img);  // skin it, if present
    }
    return wt;
}
```

## The convention *is* the interface

There is no "assign texture to wall" UI. The **file name is the binding**: a texture saved as
`textures/wall_<id>.hrt` becomes the skin for wall id `<id>` — and those ids are exactly the
Map Lab's palette (`Wall`=1, `Room`=2, `Pillar`=3). So the three tools compose through the
asset folder and a naming rule, not through code that knows about all three. Each tool still
only knows its own job; the filesystem is the integration point.

This is the same lazy move as ch.77's "a texture is a name" and ch.78's "reuse `fps::Map`":
prefer a convention over a mechanism. A missing `wall_2.hrt` simply leaves wall id 2 on its
procedural texture — degrade to the hand-made default, never fail.

## Where this leaves the Mini-Studio

```
   Texture Lab  --.hrt-->  ┌─ sandbox actors  (ch.77 textured sprites)
                           └─ fps walls        (this chapter)
   Map/Level Lab --.map-->    fps levels       (ch.78)
```

Two producers, three consumers, all joined through `assets/` by name. The next joins are
natural and deferred: a `Save as wall N` button in the Texture Lab (so you never hand-name a
file), textured *particles* (ch.79 exercise 1), and an in-`--maplab` texture preview so you
author the level already seeing its skin.

## Pitfalls

- **Assuming a fixed texture size in the sampler.** It already reads `tex.w/tex.h`; keep it
  that way and any Lab texture fits.
- **Failing when a `wall_N.hrt` is absent.** Skinning is optional; fall back to the procedural
  texture, don't blank the wall.
- **Baking the id→file map into code.** The naming convention keeps the tools decoupled; a
  hard-coded table would couple the raycaster to the Lab.

## Exercises

1. **Save-as-wall.** Add a Texture Lab control that writes the current texture straight to
   `textures/wall_<id>.hrt`. What does it need to know that the Lab doesn't today?
2. **Floor/ceiling textures.** The raycaster draws flat gradients for floor/ceiling. Skin them
   from `floor.hrt`/`ceil.hrt` with per-pixel floor casting. Which loop changes?
3. **Per-map texture sets.** Let a `.map` name its texture set so different levels look
   different. Where does the name live — in `fpsmap1`, or a sidecar?
