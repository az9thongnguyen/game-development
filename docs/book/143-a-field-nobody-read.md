# 143 — A field nobody read

`assets/farm/crops.def` has looked like this since chapter 113:

```
crop parsnip season=spring days=4 stages=5 sell=35 seed=20
crop turnip  season=spring days=2 stages=3 sell=15 seed=8
crop pumpkin season=autumn days=6 stages=4 sell=90 seed=50
```

`defs.cpp` parsed `season` into `CropDef::season`. `world.cpp` never looked at it. Not
once, in eighteen chapters — not when planting, not at the day boundary, not in the
save file, not on screen. A pumpkin could be planted in spring and a parsnip in winter,
and the only thing the word did was sit in a struct.

**A field that is written and never read is not data, it is a claim.** This one claimed
the game had seasons.

---

## What it means now

A year is four seasons of `kDaysPerSeason` days. **Seven**, not twenty-eight: a day here
is twelve real minutes, so a Stardew-length season is five and a half hours and a player
would never once see one turn. Seven makes a season about ninety minutes — long enough
to plan inside, short enough to meet.

Two moments, one rule:

```cpp
// planting
if (!grows_in(want, now))
    return fail(want.name + " does not grow in " + season_name(now));

// the day boundary
if (r.season_changed)
    for (auto& [key, s] : w.soil)
        if (s.crop >= 0 && !grows_in(defs.crops[s.crop], now)) { … ++r.crops_withered; }
```

Both go through `grows_in`, deliberately. They are the same question asked at two
different times, and two copies of it is exactly how a crop becomes plantable and then
immediately dies.

Withering is not un-tilling. The plot survives; making the player hoe the whole field
again every seventh day would be a chore, not a decision. And it takes ripe crops too,
which is the pressure the rule exists to create: a six-day pumpkin planted on the fifth
day of autumn is a loss.

`season=all` is a legal word and is **not** a season — which took two functions to say
properly. `season_from_string` answers with one of the four or nothing;
`valid_season_word` answers whether a `.def` file may write it. Collapsing them into one
is how `season=any` becomes loadable and unplantable.

---

## The duplication that let it rot

The first attempt at the validity rule went into `assign_crop`, and the test that a file
with `season=sprnig` is refused **failed**. `assign_crop` is the override path. The FILE
path — `parse_defs` — carried its own copy of the same dispatch:

```cpp
if      (k == "season") c.season = v;
else if (k == "days")   { if (!to_int(v, c.days))   return std::nullopt; }
…
```

Two functions deciding which keys a crop has and what their values may be. They agreed
for thirty chapters and disagreed the moment one of the fields grew a rule.

They are one function now, with the single flag that is the *real* difference between
the two callers:

```cpp
std::string assign_crop(CropDef& c, const std::string& k, const std::string& v,
                        bool unknown_is_error);
```

A `.def` **file** ignores a key it has never heard of, so a newer file still loads in an
older build. An **override** arriving from remote config refuses one, or an operator who
types `sel=40` silently changes nothing and believes they changed a price. That
difference is worth a parameter. The rest was never worth a second copy.

The forward-compatibility half of that promise had no test at all until this chapter —
`parse_defs("crop ok days=2 stages=2 water=3\n")` is one line, and without it the flag
had nothing holding it in place.

---

## Visible, not merely enforced

A rule with teeth that the screen does not mention is a rule the player experiences as
a bug. Three places say it:

- The HUD clock reads `Day 8  summer 1/7  06:00`. The day *inside* the season is there
  because the rule kills crops: a player who cannot count the days left is being
  punished for something the screen never told them.
- The morning report says `3 withered` when the night took something. A field that
  empties overnight in silence reads as a crash.
- The seed chip is drawn **dim** when the selected crop cannot go in the ground today.

That last one is a decision expressed as a colour, and it is the mutation that survived:

```
18/18  SURVIVED   S18  the seed chip never says out of season
```

Every other test in `test_farm_scene` still passed with the chip permanently bright.
Nothing in the file could see a colour choice — `ink()` counts pixels that are *not*
background, which is the wrong question for "is THIS drawn here". So `count_colour` now
exists, the Seed slot is selected first (so its own label is `text`, never `text_muted`,
and any muted pixel in the rect must be the sub-label), and the test cycles Q until it
has seen the chip both dim and bright. Both directions, because a chip that is always
dim passes a test that only looks for dim.

Eight crops now, so every season has something to plant and `season=all` has an example
— a clover that pays badly, because a crop you can plant on any day of the year should
not also be the best one.

---

## And a place to write decisions down

The other half of this slice: `docs/adr/`.

It is an **index**, not an archive. Forty-nine rows, each naming a decision in one line
and pointing at the chapter where the argument already lives. There is no new prose in
it on purpose — a second copy of an argument is a second thing to forget, which is the
lesson of the two crop parsers above, one file up.

What it adds that the book does not is **reversals**. Eight rows say `Superseded by N`,
and those are the ones worth having:

| | |
|---|---|
| 21 → 22 | `.hrt` has three offline doors → a fourth, because it answers a question the others cannot |
| 23 → 24 | attribution is a line you must remember → provenance is derived |
| 25 → 26 | `fpsmap1` is the map format → `map2` is the only one anything writes |
| 48 → 49 | twelve one-per-scene CLI flags → one door per kind |
| 81 → 82 | a read-then-write is atomic because the pool is one connection → it is a transaction with a locking read |
| 84 → 85 | Postgres is a documented deploy-time build → one dialect in the source, two on the wire |
| 88 → 89 | `--seed` seeds and exits → `--seed` serves; `--seed-only` stops |
| 103 → 104 | a crop's `season` is a label → `season` is a rule |

The last row is this chapter putting its own reversal in the log.

An index of forty-nine chapter pointers rots in three mechanical ways — a renamed
chapter, an id used twice, a `Superseded by` naming a row that is not there — so
`test_adr_index` checks all three. It found one immediately: the markdown-escaped pipes
in `` `HRT1\|w\|h\|RGBA8` `` made row 20 read as nine columns of nonsense.

---

## Files

- `src/games/farm/defs.{hpp,cc}` — `Season`, `season_of`, `day_of_season`,
  `season_from_string`, `valid_season_word`, `grows_in`; one `assign_crop`
- `src/games/farm/world.{hpp,cpp}` — the planting refusal, the withering, `DayReport`
- `src/games/farm/farm_scene.cpp` — the HUD calendar, the morning report, the dim chip
- `assets/farm/crops.def` — eight crops, four seasons, one `all`
- `docs/adr/README.md`, `tests/test_adr_index.cpp`
- `tests/test_farm.cpp`, `tests/test_farm_scene.cpp`
