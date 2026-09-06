# The developer debug suite (M68)

An in-game console, a stat overlay and an instrumentation layer, for
hunting bugs in the PC port by playing it.

It exists because of M67. That bug — every door in the game impassable —
sat undetected through twenty-three milestones of data-driven verification
and was found in an afternoon by someone playing the game and walking into
a door. The RE was all on the page; nobody had asked the right question of
it. Smoke tests verify what the port *computes*; they cannot see a feature
that was never wired at all. Only playing can, and playing is much faster
with tools.

---

## Quick start

| Key | |
|---|---|
| **F1** or **`** | open / close the console |
| **Shift+F1** | toggle the one-line fps/position readout |
| **F2** / **Shift+F2** | next / previous overlay page |
| **F5–F12** | free — `bind` your own commands to them |
| **Esc** (console open) | close the console |
| **Ctrl+↑ / Ctrl+↓**, **PgUp / PgDn** | scroll the scrollback |
| **↑ / ↓** | command history |
| **Tab** | complete a command, or its first argument |

**The suite owns F1, F2 and the backtick, and nothing else.** Everything
the M69 control scheme gives the game — WASD, the arrow keys, Space, E, Q,
M, C, G, Tab, Return, Esc, the left mouse button, and **F3/F4** (the
green/red softkeys) — passes straight through. That is checked in the smoke
test, key by key, rather than assumed; `bind` refuses F1–F4 for the same
reason, because binding a game key would silently swallow it.

While the console is open it consumes *every* key, so typing `E` into a
command never also opens a door.

In the console: `help` lists every command grouped by area, `help <command>`
gives its usage, `find <word>` searches names and help text.

Two things worth knowing on day one:

```
> zone azra
```
travels straight to a zone. From the main menu it starts the game there,
skipping character creation entirely.

```
> mark
… do the thing you are investigating …
> diff
```
`mark` snapshots every counter; `diff` prints only the ones that moved.
That is the loop this whole thing is built around — press a key, run a
command, walk into a door, then ask what actually ran.

---

## What it will not do

It draws **over** the presented frame, at the window's real resolution,
after the game's 176×208 `Backbuffer` has already been scaled and blitted
(`src/graphics/overlay_surface.h`). It never writes a pixel into the
backbuffer, so it cannot perturb the software rasterizer, and it cannot
move the tracked `.ppm` render dumps that are this port's regression
evidence. That is structural, not a promise.

With the console closed and no overlay page selected, the per-frame cost
is a metrics frame roll, one null check in `Present`, and (if the mini bar
is on) one line of text.

---

## The commands

### world

| | |
|---|---|
| `where` | the full position readout: zone, world position, tile, raw heading, the cell's flags, floor and ceiling |
| `tp <x> <y>` | teleport, world units |
| `tpt <tileX> <tileY>` | teleport to a tile centre |
| `tpe <index>` | teleport next to a live entity (indices from `ents`) |
| `zone [name]` | current zone, or travel to another |
| `zones [filter]` | every zone the game ships |
| `face <degrees>` | turn to an absolute heading |
| `tile [tx ty]` | one tile's flags, block bit, light, heights, and whether it blocks a player-sized circle |

`tp` goes through the real `SetPosition` native, so the destination gets
the engine's own ground snap — poking the camera directly would leave you
standing in the air or inside the floor.

### entities

| | |
|---|---|
| `ents [filter]` | live entities, nearest first, with health, AI state and door passability |
| `entinfo <index>` | everything about one, including its model's collision box and whether it is tile-stamped |
| `spawn <script\|typeId> [count] [distance]` | spawn creatures in front of you |
| `killall [filter]` | kill every creature, or those matching |

`spawn arat.s 3` and `spawn 202 3` do the same thing: `entities.txt`'s
fourth column *is* the script path, so a script name is resolved back to
its type id and both go through `Level.CreateEntity` — the same native
`crypt1.s` uses to put Umbra Keth in the room. A spawned creature is built
by exactly the code that builds a placed one.

`CreateEntity` parks its result in a **single** pending slot that the tick
drains, on the documented grounds that nothing in the shipped scripts calls
it twice without an `AddObject` between. True of the scripts; not true of a
console. So `spawn <n> <count>` queues and issues one call per tick rather
than looping — `count` ticks, 40ms each — and says so in its reply.

`killall` goes through `ApplyDamage`, not by zeroing health, so death runs
the real path — loot drops, kill counters, zone triggers. Those are the
things worth debugging.

### items

| | |
|---|---|
| `give <script\|typeId> [count]` | add an item to the inventory |
| `inv [filter]` | the inventory, with equip state |
| `equip <name> [l\|r]` / `unequip <name>` | equip by name substring |
| `items [filter]` | every item script the game ships |
| `catalog <kind> [filter]` | search any static table: zones, entities, scripts, monsters, items, weapons, armor, spells, races, classes |

### character

| | |
|---|---|
| `stats` | attributes, vitals, derived stats, race, class, equipped hands |
| `stat <name> [value]` | read or set one of `str int wil agi spd end per luck` |
| `race [id]` / `class [id]` | read or set, via `ChooseRace` / `ChooseCharacter` |
| `level [n]`, `xp [amount]`, `gold [amount]`, `heal` | the obvious ones |
| `quests` | every quest id the player has touched, with its state |
| `quest <id> [assigned\|solved\|completed] [0\|1]` | read or set one quest's three real flags |

`stat` is the one place a curated command earns its keep over the raw
native bridge, for two reasons. The Get/Set pairs are not mechanical
(`GetWil` against `SetWillpower`), and — more importantly — the engine
recomputes derived stats (max health, magicka, attack, defense) in
`UpdateAttributes`, not in the eight setters.

But `UpdateAttributes` has **two** behaviours selected by the character's
current level, and at level 1 it *reseeds all eight attributes from race
and sex* — so calling it after a setter silently undoes what you just did.
This was found by running the command: `stat str 90` on a fresh character
reported `50 -> 40`. `stat` now recomputes only above level 1, and at level
1 says exactly what it skipped. `race` and `class` do call it
unconditionally, because reseeding from race is precisely what they mean —
it is what `chooseportraitmenu.s` does.

Every setter command reads the value back through its getter afterwards
rather than trusting the write: several real setters clamp (`SetHealth`
cannot overheal), and a silently clamped value is exactly what a debug tool
must not hide.

### toggles

| | |
|---|---|
| `flags` | every toggle and its value |
| `set <flag> [value]` | set one; with no value, flip it |

`noclip` — walk through walls, doors and props. The single most useful
toggle for a bug of M67's shape: being able to walk through the thing that
is wrongly solid is how you tell "the grid says blocked" from "the geometry
is wrong".
`god` — incoming damage is dropped, but the roll still happens and is still
traceable.
`freezeai` — creatures stop thinking entirely, so a fight can be paused and
inspected mid-swing.
`tilegrid` — puts a 9×9 ASCII window of the block bit around the player on
the `world` page.

### the native bridge

| | |
|---|---|
| `recv` | the receivers `call` and `sk` accept |
| `call <receiver> <Method> [args...]` | invoke any real native binding |
| `sk <receiver> <simkin code>` | run real Simkin on the game's own interpreter |

This is why the command list above is short rather than exhaustive. The
port already implements the real natives for stats, quests, inventory,
gold, experience, equipment and entity creation
(`docs/SIMKIN_NATIVE_API.md` enumerates all 702), so:

```
> call player SetGold 5000
> call player SetQuestAssigned 12 1
> call level CreateEntity 1013
> sk player SetGold(GetGold() + 1000);
> sk ent:0 OnUse()
```

all go through the *same* dispatch the shipped scripts go through. A
curated command that poked a C++ field directly would be testing something
the game never does — which is the wrong thing to do when the whole project
is a behavioural port.

Receivers: `player`, `level`, `menu` (whatever is on top of the menu
stack), `zone` (the zone-root script object — `azra.s` and friends), and
`ent:<n>` for any entity by its `ents` index.

Arguments are converted by shape: a decimal integer becomes an int (which
is what nearly every real native wants), `true`/`false` become bools,
anything else stays a string. If you need a string that looks like a
number, use `sk`.

### diagnostics — "what is running"

| | |
|---|---|
| `mark` / `diff` | snapshot every counter, then report what moved |
| `metrics [filter]` | every counter and timing sample |
| `events [count] [category]` | the event ring, newest first |
| `categories` | how many events each category has produced |
| `softfails [count]` | recent soft-failed natives — the ones this port has not implemented |
| `trace [off\|statements\|methods] [filter]` | script tracing |
| `page [name]` / `inspect <page>` | the on-screen panel / the same data in the console |
| `watch <command>` / `unwatch [n]` | re-run a command every frame, show its first line |

**`trace methods` is the sharp instrument.** The vendored Simkin
interpreter carries two hooks this port had never used —
`setTraceCallback` and `setStatementStepper` — and turning them on logs
every script method call as it executes, with its source location and line
number, plus a per-frame statement count. It is extremely loud, so it takes
a filter: `trace methods door` follows one script.

Both stepper callbacks always return `true`, deliberately and with a
comment saying so: returning `false` from `statementExecuted` *halts the
running method*, and returning `false` from `exceptionEncountered`
*swallows the exception*. An instrument that can change control flow is not
an instrument. With tracing off, both interpreter hooks are set back to
null, so a normal session runs the identical code path it ran before M68.

### meta

`help`, `find`, `clear`, `echo`, `alias`, `bind`, `exec`, `quit`.

`bind f7 "set noclip"` puts a toggle on a function key. `exec <file>` runs
`port/debug/<file>.cfg`, one command per line — **and this is the feature
that makes a repro reproducible.** Instead of "load azra, walk to the
second corridor, open the door, note that…", write the setup down once:

```
# port/debug/doorbug.cfg
zone azra
set tilegrid 1
page world
watch tile
bind f8 "diff"
```

`autoexec.cfg`, if present, runs at startup. It is deliberately
`.gitignore`d — it is your own working state, and a half-finished repro
belonging to one person should not run in everyone else's session.

---

## The overlay pages

`F2`/`F3` cycle, or `page <name>`. From the host: `world`, `player`,
`zone`, `entities`, `tile`, `inventory`, `quests`, `render`, `script`.
From the suite itself: `metrics`, `events`, `watch`, `binds`.

`zone` includes a live census of the block bit — total cells, walls,
blocked, and blocked-but-not-wall — which is the exact number M67 left
open (roughly two thirds of that bit's on-disk population is authored
blocking that no entity placement explains).

`script` shows per-frame and total statement counts, method calls,
soft-fails and exceptions, plus the top soft-failed natives by name. That
last table is a live to-do list for the port.

---

## Architecture

```
sk_debug (static library, links simkin + sk_bindings, NOT sk_world)
├── debug_console      generic: registry, parsing, history, completion, scrollback
├── debug_commands     the command table, written against DebugHost
├── debug_overlay      stat panels; pages come from the host
├── debug_metrics      counters, mark/diff, event ring, timing samples
├── script_tracer      skTraceCallback + skStatementStepper on the interpreter
├── debug_suite        the façade main.cpp talks to
└── debug_host.h       the abstract seam ─── implemented by LiveDebugHost in main.cpp
```

Two seams keep the game and the tool apart:

- **`sk_debug::DebugHost`** — an abstract interface main.cpp implements
  over its own locals. `sk_debug` cannot see `MonsterInstance`,
  `DoorInstance` and friends (they are file-local to main.cpp) and does not
  link `sk_world`. Same layering as `LevelExecutable::ZoneRegions` (M44)
  and `DoorExecutable::TileStamp` (M67).
- **`sk::OverlaySurface`** — a text/rect drawing surface with no Windows
  type in it, implemented over GDI in `window.cpp` and over a plain
  character grid in the smoke test. That is why the whole suite is testable
  with no window, no zone and no interpreter.

The interface stays small because of two decisions. Every read-only view is
a generic `StatGroup` list keyed by a page name, so one virtual serves every
panel and adding a panel is a main.cpp-only change. And the native bridge
means "change a stat" needs no interface method at all.

**Commands are queued, not executed, when Enter is pressed.**
`DebugSuite::BeginTick` drains the queue from inside the game tick. A
command can run real script code, and running script from a window
procedure would execute it outside the tick, on a half-updated world, in a
place no exception handler covers. A throwing command is caught and printed
into the console rather than taking the process down.

---

## Removing it

The suite is a development tool, not a shipping feature.

```
cmake -DSK_DEBUG_SUITE=OFF
```

is enough: the library is not built, the `SK_DEBUG_SUITE` macro is not
defined, and every touchpoint in `main.cpp` and `window.cpp` compiles out.
That configuration is checked, not assumed — it builds clean.

To delete it outright:

1. `rm -r port/src/debug port/debug port/src/tests/m68_debug_suite_smoke.cpp port/src/graphics/overlay_surface.h`
2. Remove the `SK_DEBUG_SUITE` blocks from `port/CMakeLists.txt` (the
   library, the `target_link_libraries`, `msimg32`, the smoke test).
3. `grep -rn SK_DEBUG_SUITE port/src` and delete every block it reports:
   twelve `#if SK_DEBUG_SUITE` blocks in `main.cpp`, plus two touchpoints
   in `window.cpp` and `native_binding_common.cpp` that carry the same
   comment marker but are not conditional — they are inert without the
   suite (a null `std::function` and a null function pointer) and were left
   unconditional so the headers stay stable.

Four small additions to game files are left behind by that, all inert and
all harmless to keep:

- `SetSoftFailObserver` in `simkin_bindings/native_binding_common.h` — a
  null function pointer, tested once per soft-failed call.
- `Window::SetOverlayCallback` and its GDI surface — one null check per
  present. This is why `shadowkey_port` links `msimg32` (`AlphaBlend`,
  for the panels' translucency) even with the suite off.
- `EntityTypeTable::all()` — a const accessor over the map it already owns.
- `PlayerExecutable`'s seven attribute getters and `race()` — const
  accessors mirroring the `willpower()` that was already there.
