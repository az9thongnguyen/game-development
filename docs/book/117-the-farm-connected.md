# Chapter 117 — The farm, connected: prices from a dashboard, a save that argues back

> Code: `src/games/farm/cloud.{hpp,cpp}` (new) · `src/games/farm/defs.{hpp,cpp}` ·
> `src/games/farm/world.cpp` · `src/games/farm/farm_scene.{hpp,cpp}` ·
> `sdk/cpp/include/gbaas/{client.h,transport.h}` · `sdk/cpp/src/client.cc` ·
> `baas/db/db.cc` · `baas/auth/auth_{service,controller}.{h,cc}` ·
> `src/games/colony/colony_scene.{hpp,cpp}` ·
> Data: `assets/farm/items.def` ·
> Tests: `tests/test_farm.cpp`, `tests/test_farm_scene.cpp`,
> `tests/test_farm_live.cc` (new), `tests/test_baas_admin.cc`

## Tóm tắt (VI)

Farm là game thứ hai, và là game đầu tiên **phụ thuộc** vào backend chứ không phải
biểu diễn nó. Ba việc: giá đến từ remote config, một live event đè lên giá đó, và save
được **đối chiếu** với bản trên mây thay vì ghi đè.

**Một định dạng, ba nguồn.** Remote config và live event gửi đúng cái text mà
`assets/farm/crops.def` đang dùng: `crop parsnip sell=70`. Người vận hành đổi giá bằng
cách gõ đúng dòng họ sẽ gõ trong file. Không có parser mới, không có schema JSON riêng.

Nhưng **không dùng `merge_defs`** — hàm đó thay cả bản ghi. Gõ `crop parsnip sell=70`
vào dashboard sẽ âm thầm reset `days`/`stages`/`seed` về mặc định của struct: một lần
đổi giá kéo theo đổi luôn tốc độ lớn. `apply_overrides` chỉ gán những trường mà dòng
đó **có nêu tên**, không commit gì từ một dòng lỗi, và **từ chối** dòng làm cây không
thể trồng được (`days<1`, `stages<2`) — remote config không được phép làm hỏng game
đang chạy.

**`sell` từng được viết hai lần.** `crops.def` và `items.def` cùng ghi 35, và
`end_day` đọc bản của item. Nghĩa là giá của cây là con số duy nhất mà một đợt cân
bằng **không thể đổi** — từ file hay từ dashboard đều vô hiệu. Nay cây sở hữu giá của
cây, và `items.def` không lặp lại nữa.

**Cloud save đối chiếu, không ghi đè.** 404 là "ô trống"; 500 thì không, và "không
biết" **không bao giờ** được biến thành upload. Bản trên mây đọc không được thì để
nguyên — một bản build mới hơn có thể đọc tốt. Hai bên cùng đổi kể từ lần đồng ý cuối:
**hỏi người chơi** (F6/F7), đúng luật "đề nghị, đừng tự áp dụng" mà autosave recovery
của Studio đang theo.

**Farm giờ mới biết tiếp tục ván cũ.** Nó có file save từ chương trước nhưng lối vào
duy nhất là F9 — phím không ai bấm vì không ai biết. Việc này cũng làm sync trở nên
mạch lạc: thứ trên màn hình và thứ trên đĩa là **cùng một thế giới**.

**Hai lỗi thật do test end-to-end tìm ra**, không phải do đọc code:

1. **Guest là tài khoản mới mỗi lần chạy** — nên save đẩy lên được nhưng không bao giờ
   kéo về được. Upload chạy, download chạy, tính năng vẫn không hoạt động. Sửa bằng
   `device_id` (migration 7+8).
2. **Bấm F5 trong lúc sync đầu tiên còn đang bay** thì save được gửi **hai lần**, và
   quyết định sync được tính từ ảnh chụp mây **cũ hơn** cú upload. Cửa sổ đó rộng vài
   trăm mili-giây — vừa đủ để bấm một phím.

**20 mutation, giết hết.**

---

## One format, three sources

The farm's balance already lived in text, not in C++ literals — that was chapter 105's
whole point. A crop is a line:

```
crop parsnip season=spring days=4 stages=5 sell=35 seed=20
```

So when remote config needed to carry a price, the question was what shape it should
arrive in. A JSON object of prices. A `farm_prices` table with its own schema. A
key-per-crop namespace (`price.parsnip`, `price.turnip`, …).

The answer that survives is: **the same line**. Remote config's value for `farm_defs`
is a defs snippet. A live event's payload is a defs snippet. The file is a defs
snippet. Three sources, one format, one parser, and an operator who wants to raise the
parsnip price types exactly what they would have typed in the file.

The layering is what makes it a feature rather than three ways to say the same thing:

```
assets/farm/crops.def      →  sell=35    the shipped balance
remote config "farm_defs"  →  sell=40    this week's tuning
live event payload         →  sell=90    the Harvest Festival, while it runs
```

Each layer names only what it changes. Turn the festival off and the price falls back
to 40 — not to 35, and not to whatever the last event happened to leave behind.

## Why an override is not a merge

`merge_defs` already existed, and reaching for it here would have been the obvious
move. It is wrong, and quietly so:

```cpp
void merge_defs(Defs& into, const Defs& more) {
    for (const CropDef& c : more.crops) {
        const int at = into.crop_index(c.name);
        if (at >= 0) into.crops[at] = c;      // <- the WHOLE record
```

`parse_defs("crop parsnip sell=70")` produces a `CropDef` whose other fields are the
*struct defaults*: `days=4`, `stages=5`, `seed=20`. Merging that over a tuned
six-day crop resets it to four days. The operator changed a price and re-balanced
growth, and nothing anywhere says so.

So `apply_overrides` assigns field by field, and commits nothing from a line that
failed:

```cpp
CropDef c = into.crops[at];                    // a copy
while (why.empty() && (ln >> tok)) {
    if (!split_kv(tok, k, v)) { why = "'" + tok + "' is not key=value"; break; }
    why = assign_crop(c, k, v);
    if (why.empty()) ++applied;
}
if (why.empty() && (c.days < 1 || c.stages < 2))
    why = "days/stages would make it unplayable";
if (!why.empty()) { rep.problems.push_back(where + ": " + why); continue; }
into.crops[at] = c;                            // all of it, or none of it
```

Three decisions in there are worth naming.

**A line is atomic.** `crop parsnip sell=5 dayz=1` sets nothing — not even the `sell=5`
that parsed cleanly. A half-applied record is a balance nobody chose and nobody can
see.

**An unknown key is an error here, and is ignored in the file.** That looks
inconsistent until you ask who typed it. A file is additive and forward-compatible:
`parse_defs` skipping an unknown key is what lets a newer field ship without breaking
an older reader. A dashboard field was typed by a person thirty seconds ago, and the
only thing they want to know is whether it worked.

**An override cannot invent a record.** `crop turnip sell=10` against a defs table
with no turnip is a typo, not a new crop — creating it from a dashboard would ship
content that no client has a sprite, a season, or a seed item for.

**And it cannot brick the game.** `stages=1` divides by zero the moment a crop is
drawn; `days=0` makes it ripe the instant it is planted. Both are refused with the
rest of the line. Remote config is a lever an operator pulls without a build, which
means it is also the one input that can take a live game down without a deploy.

## The number that was written twice

While wiring the price through, the price turned out not to move. `end_day`:

```cpp
if (const ItemDef* it = defs.item(name); it && it->sell > 0) unit = it->sell;
else if (const CropDef* c = defs.crop(name)) unit = c->sell;
```

and `assets/farm/items.def`:

```
item parsnip type=crop sell=35
```

Both files carried 35. They agreed, so nothing was visibly wrong — and the crop's
`sell` was dead code the whole time. Change it in `crops.def` and nothing happens.
Change it from a dashboard and nothing happens.

This is the same failure this project keeps finding, in its fourth costume: **one idea
written down more than once**. Chapter 114 had four copies of "resolve a project";
chapter 115 had two lists of known entries; chapter 116 had two scene editors. Here it
is two files with the same number, and the tell is identical — they agree on the day
they are written and one of them silently stops being read.

The fix is both halves: the rule made explicit in code (a crop owns a crop's price),
and the duplicate data deleted so nothing can drift back.

```
# No `sell=` on a crop item: crops.def owns the price. Two files carrying the same
# number is how one of them stops being read.
item parsnip type=crop
```

A test pins the chain end to end — override → price → the gold a day's shipping pays:

```cpp
Defs d = *parse_defs("crop parsnip ... sell=35\nitem parsnip type=crop sell=999\n");
CHECK(earn(d) >= 33 && earn(d) <= 37);          // the crop's 35, not the item's 999
CHECK(apply_overrides(d, "crop parsnip sell=100\n").applied == 1);
CHECK(earn(d) >= 98 && earn(d) <= 102);
```

## The decision before the transfer

Cloud save is the first operation in this project that can **destroy work by
succeeding**. Everything else either does the thing or refuses: a publish with a
missing asset is refused, a promote to an unknown channel is refused. An overwrite
works perfectly and takes an evening with it.

So the decision is pure, and it is pinned before a byte moves:

```cpp
Sync decide_sync(const LocalSave& local, const RemoteSave& remote);
```

The trick is the **bookmark**. A save's hash says *what* it is; it cannot say whether
this machine has touched it since the cloud last agreed. Two hashes that differ tell
you nothing about who moved. So the local side remembers the version and hash it last
agreed on, and every question becomes *changed since we agreed?* — which each side can
answer on its own:

```cpp
if (local.hash == remote.hash) return Sync::InSync;       // content, not version numbers
const bool local_changed  = local.hash    != local.synced_hash;
const bool remote_changed = remote.version != local.synced_version;
if (local_changed && remote_changed) return Sync::Conflict;
if (local_changed)                   return Sync::Push;
if (remote_changed)                  return Sync::Pull;
return Sync::Conflict;
```

That last line is the interesting one. Neither side claims to have moved, yet the
bytes differ — which should be impossible. Returning `InSync` there would keep
whichever copy happened to be local, silently, on the basis of a bookmark that is
demonstrably lying. It is reported instead.

Content is compared before versions on purpose: two byte-identical saves *are* the
same run, and pushing one because another machine happened to save it second is churn
with a risk attached.

The bookmark lives beside the save (`saves/farm/slot1.sync`), not inside it. It is
metadata about the file, not part of the world — putting it in the save would change
the save format, and its hash, to record something the game itself never reads.

## What "we do not know" must never become

Three states look alike from inside a failed request, and only one of them is safe to
upload over:

| the cloud says | means | what happens |
|---|---|---|
| `404` | the slot is empty | a fact — push if we have something |
| `500` / transport failure | we could not ask | **do nothing** |
| 200, unreadable payload | something is there | **do nothing**, leave it alone |

```cpp
} else if (r.error && r.error->status != 404) {
    cloud_line_ = "cloud unavailable";
    return;
}
```

The third row is the one that is easy to get wrong. A save this build cannot parse is
still somebody's farm, and a newer build may read it perfectly well. Pushing over it
turns a temporary incompatibility into a permanent deletion.

## Offer, do not apply

When both sides moved, the game does not choose:

```cpp
case Sync::Conflict:
    conflict_   = true;
    cloud_line_ = "two saves differ";
    say("cloud save differs - F6 keeps yours, F7 takes the cloud's", 10.0);
```

Same rule as the Studio's autosave recovery (ch. 111): the machine can see that two
things differ, and cannot see which evening mattered. The chip stops reporting and
starts asking — and names both keys, because *"two saves differ"* on its own leaves
someone staring at a farm they cannot save.

Nothing is written until a key is pressed. The local world stays on screen; the cloud
copy is held in memory, unapplied.

## The farm learns to continue

Wiring sync up exposed something embarrassing: the farm had a save file and no way
back into it except F9 — a key nobody presses because nobody knows about it. Every
launch started day 1.

```cpp
if (const std::string text = read_text(save_path()); !text.empty()) {
    if (const auto w = world_from_text(text, &why)) adopt_world(*w);
    else { assets::write_file(save_path() + ".broken", *raw); ... }
}
```

Beyond being what a game with a save file should do, it makes sync **coherent**: what
is on screen and what is on disk are the same world, so uploading one cannot destroy
the other. Before this, "push the local save" and "push the world being played" were
two different things, and only one of them was on screen.

Bytes that cannot be read are copied aside first. They are still someone's farm.

## Two bugs the end-to-end test found

`test_farm_scene` drives the game through a scripted transport: it proves the game's
*reasoning* about a backend. `test_farm_live` boots a real Drogon server, changes a
price through the real admin route, and runs a real `FarmScene` against it. It proves
the chain **exists** — and both times it ran, it found something reading the code did
not.

### A guest is a new account every launch

The first run pushed a save, cleared the local file, started a second scene, and got
`cloud empty`. Upload worked. Download worked. The feature did not.

`auth().guest()` creates a *new* user, every time. Cloud saves are per-user. So the
save went into an account that the next launch would never sign into again. Colony has
had this since chapter 57 and never noticed, because colony never reads a save back
and compares it to anything.

The fix is the standard one, and small: an opaque id for the installation, kept by the
client, handed over at sign-in.

```sql
-- migration 7
ALTER TABLE users ADD COLUMN device_id TEXT;
-- migration 8
CREATE UNIQUE INDEX IF NOT EXISTS idx_users_device ON users(project_id, device_id);
```

Two migrations rather than one because `ALTER ... ADD COLUMN` is not idempotent, and
`db.cc` states its own invariant: a migration is a single statement or every statement
is idempotent. One statement each keeps it, with no transaction machinery.

Server side, a known device gets its **stored** name back, not the requested one — the
account belongs to the player, and a later launch should not silently rename them.

The device id is generated once and written to `saves/device.id` — beside the saves
rather than under `farm/`, because it identifies the machine, not the game. It stays
in the farm until a second game wants it; moving it then is a rename.

### Saving inside the sync window

With that fixed, the test still failed — by one version. The scene pushed `v1`; the
next scene pulled `v2`.

The sequence:

1. sign in → remote config → live events → **GET the cloud save** (in flight)
2. the player presses F5 → the file is written and **PUT** (in flight, concurrently)
3. the GET — issued *before* the PUT — comes back `404`
4. the verdict is computed from that stale `404`: local present, remote absent → **push**

Two uploads, and the second one was decided from a picture of the cloud taken before
the first. The harmless version is a wasted round trip and a version bump. The harmful
version is the mirror image: a `Pull` decided from a stale snapshot, overwriting the
save the player just made.

The fix is not a lock or a retry, it is noticing that the window exists:

```cpp
if (syncing_) { cloud_line_ = "saved - syncing"; return; }
```

Nothing is lost. `save_game()` has already written the file, and the verdict about to
arrive calls `local_stamp()`, which **reads the file** — so the decision sees this
world and pushes it if that is the right answer. One upload, taken by the decision
that knew about it.

`test_farm_scene` now pins it precisely, by holding the response open on purpose:

```cpp
t->hold = "GET /v1/saves/farm";            // the window, held open
...
sc.update(1.0 / 60.0, key(platform::Key::F5));
CHECK(t->count("PUT /v1/saves/farm") == 0);
CHECK(sc.cloud_line() == "saved - syncing");
t->release();
CHECK(t->count("PUT /v1/saves/farm") == 1);
```

## Layers are chained, not raced

Remote config and live events are two edits to the same table. Fired together, whichever
response lands second wins, and whether the festival applies depends on the network:

```cpp
client_.config().get(kConfigKey, [this](auto r) {
    ...
    client_.events().active([this](auto er) {     // only now
        ...
        sync_saves();
    });
});
```

Serial round trips are the cost, and they are the right cost: this is startup, once,
and the alternative is a price that is right most of the time.

Testing an ordering claim needs a test that can get the order wrong, so the fake
transport **drains its queue in reverse**:

```cpp
for (auto it = batch.rbegin(); it != batch.rend(); ++it) it->first(...);
```

Correct code here never has two of these in flight at once, so reversing costs it
nothing — and the parallel version fails immediately.

## `OfflineTransport`, and where the base URL lives

Two small moves into the SDK, both for the same reason.

`gbaas::default_base_url()` was a `#ifdef __EMSCRIPTEN__` inside colony. The farm
needed the same one. Two games each carrying their own copy is how a web build ends up
calling `127.0.0.1` from someone's browser — it is a fact about how this backend is
served, not about any one game.

`gbaas::OfflineTransport` answers every request with a transport failure, immediately
but still **asynchronously** — from `poll()`, like every other transport, so nothing
can accidentally depend on being called back inside `send()`. It makes "no backend"
something you *construct* rather than something that happens to you.

The immediate payoff was a test that had been quietly lying. `test_farm_scene` used the
default constructor, which talks to whatever is on port 8080 — nothing in CI, and, on
the machine this chapter was written on, a completely unrelated web server. A unit test
whose behaviour depends on what else is running is not a unit test.

## What the HUD gained

A tool used to be a word in the corner. Four slots say the same thing and one more:
what you are *not* holding, which is the half a word cannot show. The seed slot carries
what would be planted and how many are left — the one number a player checks before
walking to the far field.

The cloud chip is one line, always present, and `offline` is written in the muted
colour on purpose: the farm is a complete game with no backend at all.

A remote-config problem gets its own strip, above the hotbar, on its own filled chip —
warn-coloured text straight onto a green field is a colour nobody can read, and this is
the one line an operator needs to read from across the room. It is also the one the
test counts, by its exact fill colour, to prove the message is on screen at all.

## Testing notes

**20 mutations, all killed.** The ones worth naming:

| mutation | what it broke |
|---|---|
| override starts from struct defaults (= `merge_defs`) | `days`/`stages`/`seed` reset by a price change |
| a failed line is committed anyway | half-applied record |
| no playability guard | `stages=1` accepted from a dashboard |
| both-moved silently pushes | the cloud save destroyed without asking |
| version compared before content | identical saves re-uploaded forever |
| the impossible case read as in-sync | a lying bookmark resolved in local's favour |
| any failure treated as an empty slot | a `500` turns into an upload |
| push over an unreadable cloud save | a save a newer build could read, deleted |
| config + events fired together | the festival applies or not, by luck |
| no resume | the save file is unreachable again |
| a new device id every launch | the guest cannot come back |
| the server ignores the device id | same, from the other end |
| a save inside the sync window uploads | two uploads, decided from a stale picture |

Two test-quality lessons, both of which had already bitten:

**A screenshot is of whatever scene you passed it.** The render lambda closed over the
first scene, so every screenshot taken further down — of *other* scenes — was a picture
of that one. The conflict screenshot showed a farm with no conflict, and looked fine.

**Count, do not probe.** The fourth chapter in a row where a single-coordinate check
measured the wrong thing. `ink(x, y, w, h)` — pixels in a rect that are not the modal
colour there — is the honest form of nearly every visual claim.

## Ceilings

- **`test_farm_live` does not run in CI.** CI installs SDL2 and nothing else, so every
  Drogon-guarded test — this one included — silently vanishes there. It runs locally,
  and it is the only thing that proves the chain end to end.
- **The device id is per installation, not per player.** Two people sharing a machine
  share a farm; one person on two machines has two farms until they register a real
  account. That is what `auth().login()` is for, and the game has no UI for it.
- **Conflict resolution is all-or-nothing.** F6 or F7 — there is no merge, and for a
  farm there probably should not be. A game with a shared world would need one.
- **No leaderboard.** The farm submits no score, so none is seeded: a seeded row that
  nothing reads is a row someone later mistakes for a feature.
- **Analytics are fire-and-forget.** `day_end`, `sale` and `harvest` go out with no
  retry and no queue. Play offline and that day is not in the numbers.
- **Remote config is fetched once, at startup.** Changing a price mid-session does not
  reach a running game until it is restarted.
- **A number this book had been reporting was wrong.** Every chapter since the Studio
  bench existed quoted "Release ss=2 1.1–1.6 ms". That is the **ss=1** column. Measured
  again here, and cross-checked by building the previous commit in Release and running
  the same bench: ss=1 is 1.0–1.8 ms and **ss=2 is 6–10 ms, over the 8 ms budget in two
  runs of three**. Nothing regressed — the label had been wrong since the figure was
  first taken, and the ratio the brief already stated (ss=2 ≈ 4× ss=1, fill-bound) was
  the thing that should have made it obvious. Corrected in `PROJECT-BRIEF.md`.
