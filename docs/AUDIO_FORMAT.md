# Audio: real formats, the real sound-slot manifest convention, and the mixer

The audio system, in two passes. **M27** decoded the *data* and the
*script* call surface: unlike every other asset this port has had to
reverse-engineer, the sound files themselves needed no RE at all -- they
are standard formats -- and the RE-worthy question was what a real
script's `PlaySound(id)` argument means, which real corpus
cross-reference answered without Ghidra. **M51** decoded the *engine*
side: the mixer, the two attenuation models, the fade, and the native
triggers no script ever calls.

## The real files

- **38 `.wav` files** (`system/apps/6r51/*.wav`) -- ordinary uncompressed
  PCM `RIFF`/`WAVE` (confirmed by inspecting real headers: `RIFF....
  WAVEfmt ` then a 16-byte PCM `fmt ` chunk with `AudioFormat=1`, then a
  `data` chunk of raw samples). Every file checked is mono, 8000Hz,
  16-bit -- short one-shot sound effects (doors, chests, locks, coins,
  combat hits, casting, footsteps/jump/death voice lines, UI clicks).
- **6 `.ogg` files** (`system/apps/6r51/*.ogg`) -- genuine Ogg Vorbis
  (confirmed by header: `OggS` magic, `vorbis` codec string) --
  `battle2/3/4.ogg`, `explore2/3/4.ogg`, longer ambient/battle background
  music loops.
- **22 `<category>_sounds.txt` manifests** (one per real zone, e.g.
  `azra_sounds.txt`, plus a non-zone `menu_sounds.txt`) -- the exact same
  "numbered slot -> filename, `NULL.<ext>` sentinel for an unused slot"
  convention already established for `<zone>_models.txt`
  (`world/model_archive.h`) and `<category>_sprites.txt`
  (`assets/sprite_archive.h`), e.g. `azra_sounds.txt`:
  ```
  0 barch_dies.wav
  1 barch_firebow.wav
  2 NULL.wav
  ...
  62 door_close.wav
  63 door_open.wav
  ...
  73 explore3.ogg
  ...
  ```
  Each zone's manifest assigns slot numbers *independently* -- the same
  slot index can (and does) name a different real file in a different
  zone (e.g. slot 69 is `NULL.wav` in `azra_sounds.txt` but
  `battle2.ogg`, that zone's own battle track, in `broken1_sounds.txt`).

## The real native call surface -- confirmed by corpus cross-reference, no Ghidra needed

A real script's `PlaySound(id)` (called on `GetPlayer()`, `GetOwner()` --
an item's real owner once picked up, `item_executable.cpp`'s own
`GetOwner()` handler -- or occasionally bare/`self`) and
`Level.PlayAmbient(id, volume)` both take `id` as **the currently-loaded
zone's own `<zone>_sounds.txt` slot index, directly** -- proven by
grepping every real `.s` script's call site against its own zone's real
manifest:

- `door.s`'s real `OnUse()`: `GetPlayer().PlaySound(63)` when opening,
  `GetPlayer().PlaySound(62)` when closing -- matches `azra_sounds.txt`'s
  own `63 door_open.wav` / `62 door_close.wav` exactly, and every other
  real zone's manifest has the same two files at the same two slots.
- `azra.s`'s real `Init()`: `Level.PlayAmbient(73,100)` -- matches
  `azra_sounds.txt`'s own `73 explore3.ogg`. Every other real zone-root
  script's own `PlayAmbient(id,100)` call matches that zone's own
  manifest the same way (`broken1.s`/`broken2.s`: `PlayAmbient(69,100)` ->
  `broken1_sounds.txt`'s `69 battle2.ogg`; `crypt1.s`/`crypt2.s`/
  `crypt3.s`: `PlayAmbient(74,100)` -> each zone's own `74 explore4.ogg`
  or `74 NULL.wav`, silently skipped where absent).
- `menus/lockpickdoor.s`/`pickfailed.s`/`pickfaileddamage.s`:
  `GetPlayer().PlaySound(67)`/`PlaySound(66)` -- these menu screens open
  *during gameplay*, so they reuse whichever zone's manifest is already
  loaded (no separate `menu_sounds.txt` load needed for them) -- matches
  `azra_sounds.txt`'s own `66 lock_click_bad.wav` / `67 lock_click_good.
  wav`.
- **Also confirmed natively**: `docs/INPUT_HANDLING.md`'s hidden Konami-
  code Easter egg (`SecretSequence_OnComplete`, `FUN_1001b204`) plays
  "a sound effect (id `0x57`)" -- `0x57` = 87 decimal, and slot 87 is
  `pl_cast_powerup.wav` in every real zone's manifest that has it mapped
  -- confirming the native engine code itself uses the exact same
  slot-index convention as every scripted `PlaySound()` call, not a
  separate numbering scheme.

A full corpus survey of every real `PlaySound(id)`/`Level.PlayAmbient(id,
...)` call site found ids **{57, 58, 60, 62, 63, 65, 66, 67, 69, 73, 74,
83, 84, 85, 87, 101}** actually exercised by real scripts -- every one a
real, mapped, non-`NULL` slot in whichever zone calls it. A separate
cluster of ids (79-82: attack sounds; 88-96: jump/die/walk/run/wading
voice lines) exists in every zone's manifest but is **never** called from
any real script.

> **Correction (M51).** This paragraph used to finish "these are
> native-only, triggered directly by the original engine's own
> movement/combat code". Half of that holds: 80, 81, 87, 91 and 92 do
> have native triggers, now enumerated under "The native trigger table"
> below, and all five are wired up. The rest — 79, 90, 93, 94, 95 and
> 96 — are triggered by nothing at all, native or scripted. **The
> shipped game has no footstep sounds and no death voice lines.**

## What's implemented in the port

- **`port/src/audio/wav_file.h`/`.cpp`** -- a plain RIFF chunk walker
  (8/16-bit PCM), no library needed.
- **`port/src/audio/vorbis_decoder.h`/`.cpp`** -- wraps the vendored
  `third_party/stb_vorbis` (single-file, public domain/MIT, `PROVENANCE.
  md`) for real `.ogg` decoding.
- **`port/src/assets/sound_archive.h`/`.cpp`** -- the real per-zone
  manifest, loaded fresh on every zone change (`main.cpp`'s zone-load
  block, alongside the existing per-zone icon-set load); decoded PCM is
  cached by resolved file path (case-insensitive) rather than by slot, so
  a real sound shared across many zones' manifests (e.g. `door_open.wav`)
  is only ever decoded once.
- **`port/src/audio/audio_engine.h`/`.cpp`** -- XAudio2 playback (bundled
  with Windows, no redistributable): one transient source voice per
  one-shot SFX (auto-reaped once finished, `Update()`, called once per
  game tick), one persistent looping source voice for ambient/battle
  music (`PlayMusic()` always replaces whatever was playing, matching
  every real zone-root script's own single `PlayAmbient()` call in
  `Init()`).
- **Real native handlers**: `PlayerExecutable::PlaySound(id)` (covers the
  large majority of real corpus calls -- `GetPlayer()`/`GetOwner()`),
  `MonsterExecutable::PlaySound(id)` (the real bare/self calls that
  aren't `PlayerExecutable`, e.g. `monsters/umbra_keth.s`), and
  `LevelExecutable::PlayAmbient(id, volume)` (real looping background
  music; `volume` is 0-100, and M51 replaced this milestone's linear
  guess with the engine's own 101-entry volume curve).
- **Verified end to end against real data**, `audio_smoke`
  (`src/tests/m27_audio_smoke.cpp`): real `.wav`/`.ogg` decode, the real
  `azra_sounds.txt` manifest (including its real `NULL.wav` sentinel),
  and a real `door.s`/`azra.s` script call genuinely reaching the audio
  engine without throwing.

## The mixer (M51)

M27's five open gaps — the native-only player sounds, the main-menu
music, `steamsound.s`'s four-argument `PlaySound`, positional audio and
the mixing formula — turned out to be one system, and it decompiles
cleanly.

### The sound manager

One object at `app->+0x3a8`:

```
+0x048 + slot*4   256 sound objects, one per manifest slot
+0x448            the 16-bit output buffer
+0x648            the 32-bit mixing accumulator
+0xa54  bit 0     "the command queue has work"
+0xa58 + i*4      8 pending slot ids
+0xa78 + i*4        their volumes
+0xa98 + i*4        their repeat counts
+0xab8            master music volume
+0xabc            master SFX volume
+0xac0            which slot counts as "the music"
+0xac4            mute everything
+0xac5 / +0xac6   music fading out / fading in
+0xac8            the volume to fade back up to
```

and a sound object is `{u8 volume; u8 repeats; u8 flags; ...}`, flags
bit 0 = "playing".

Requests are queued, not applied: `FUN_100095d4` writes a volume and
`FUN_10009530` a repeat count into the 8-entry queue and set the dirty
bit. `FUN_1000881c`, the audio tick, drains it — for each entry it copies
the volume into the object, sets its repeat count, calls the object's
start method and sets the playing bit — then mixes.

### The mix — `FUN_10008b34(mgr, 256)`

Per 256-sample buffer: advance the music fade, zero the accumulator, walk
all 256 slots mixing every playing one, then clamp to 16 bits (saturating
at ±0x7fff, with `0x8001` rather than `0x8000` on the negative side).

Two structural facts:

- **At most eight voices sound at once.** The loop counts playing slots
  and `break`s at eight. It walks in *slot order*, so the cap is not
  "oldest wins" or "loudest wins" — it is **lowest manifest slot number
  wins**.
- **The master volume is chosen per voice, not per bus.** A slot gets
  `+0xab8` if it is `+0xac0` (the designated music slot) and `+0xabc`
  otherwise. So "music volume" is literally "the volume of whichever one
  slot `PlayAmbient` last started".

**There is no panning anywhere.** One accumulator, one output channel.

### The per-voice formula — `FUN_100080a0`

```c
if (masterVolume == 0 || voice->volume == 0) {
    voice->pos += count;             // still advances: a silenced sound
    if (voice->pos > voice->length)  //  finishes on schedule
        Finish(voice);
    return;
}
a = curve[masterVolume];             // FUN_10009324
b = curve[voice->volume];
for (each sample)
    *accum++ += ((a * b * 0x100 >> 16) * (sample >> 1)) >> 8;
```

`(a*b*256) >> 16` is `(a*b) >> 8`, and the sample is **halved** — that
halving is the headroom that lets eight voices fit in 16 bits, and it
means a single voice at full volume plays at half amplitude.

`FUN_10009324` is a **101-entry `int16` table at `0x100a5e9e`**, indexed
by a 0..100 volume and saturating at index 100 (`if (v < 0x65)`). It is a
2.56-per-unit ramp — 98 of its 100 steps are 2 or 3 — with exactly two
exceptions: a single **5-unit jump at index 13** (30 → 35 where the ramp
gives 33) and a flat step at the top, where 99 and 100 are both 256. The
one 5 is why the table is transcribed verbatim rather than recomputed.

### Positional audio — `FUN_10027980`

There are two ways into the mixer, and the difference between them is the
whole of "positional audio":

| wrapper | source position | used by |
|---|---|---|
| `FUN_1001b198` | a real world x/y | everything in the world |
| `FUN_1001b204` | the *listener's* own x/y | every UI and player-action sound |

The second passes the player's position as the source, so its distance
term is always zero. Both then run:

```c
dx = player->x - srcX;  dy = player->y - srcY;
d2 = (dx*dx >> 8) + (dy*dy >> 8);
if      ((d2 >> 8) == 0)      out = volume;   // inside one tile
else if ((d2 >> 8) >= 512)    out = 0;
else out = ((volume << 16) / 0x20000) * (0x20000 - d2) >> 16;
```

**Linear in squared distance.** The 512 is a limit on `d2 >> 8`, so the
audible radius is `sqrt(512)` ≈ **22.6 tiles**, not 512. Full volume
inside the first tile, then a smooth fall: at 4 tiles 96, at 8 tiles 87,
at 16 tiles 50, at 22 tiles 5.

`FUN_10064e00` is the same formula with a **per-entity range** instead of
the hardcoded 512 — the per-tick update for a looping ambient attached to
an entity (`FUN_100681f0` arms one, storing the slot at `entity+0x124`
and the range at `+0x126`, and re-evaluates every third tick). No shipped
script uses it: `SetAmbient` and `SetAmbientSound` have **zero call
sites**, and the latter's binding (GameEngine index 103) reads its
argument and then does nothing with it at all.

### The direction term, and why it does nothing

The third `PlaySound` argument asks for an extra attenuation:

```c
bearing = Atan2(dy, dx);                  // FUN_1001c20c
diff    = shortest signed angle between bearing and the *source's* yaw;
diff    = |diff >> 8| clamped to 32;
volume -= diff * ((volume >> 3) / 32);
```

Two things about it. First, it compares against the **emitter's own
heading**, so it models a directional source, not a listener with two
ears — consistent with there being no pan.

Second, and decisively: `(volume >> 3) / 32` is an **integer divide**,
and it is zero for every volume below 256. Volumes are 0..100. So the
entire directional branch subtracts **exactly zero** in the shipped game,
at every angle. The ceiling it would impose if it could reach — an eighth
— is real arithmetic, but nothing can reach it.

### The music fade

`FadeMusic` (`FUN_100090bc`) and `UnFadeMusic` (`FUN_1000915c`),
GameEngine bindings 11 and 12, with four real call sites:
`multiplayermenu.s` fades out on entry and back in on exit, and
`mainmenu.s` and `menus/bluetooth.s` each un-fade on their way back.

The tick moves the music volume by **5 per buffer** — at 8 kHz and 256
samples that is 32 ms a step, so a full 100 → 0 fade is 20 steps, about
**0.64 s**.

One asymmetry is worth keeping: `FadeMusic` only records a restore target
when the music is *currently audible*. Fading out from silence stores
nothing, and `UnFadeMusic` then does nothing at all.

### The native trigger table

`FUN_1001b198` (world) and `FUN_1001b204` (listener) are the engine's
**only** two ways to start a sound — nothing else reaches the queue
primitives. Between their 27 call sites, this is every id the engine
plays on its own:

| slot | file | what triggers it |
|---|---|---|
| 1 | `barch_firebow.wav` | the player fires a ranged weapon (`FUN_100425bc`, gated on the weapon's `+0x1a7` — M49's `SetRange > 0x400` flag) |
| 59 | `chest_open.wav` | a container of typeId 301/311/314 opens (`FUN_100646a8`) |
| 61 | *(`NULL.wav` everywhere)* | `FUN_1002e024` / `FUN_1008ff14` — mapped in no manifest, so silent |
| 63 | `door_open.wav` | the native door/teleport path (`FUN_1003908c`) |
| **80** | `pl_attack_impale.wav` | a melee swing that **finds a target**; the player **taking damage** (`FUN_10044814`); the player **dying** (`FUN_10042cb0`) |
| **81** | `pl_attack_sword.wav` | a melee swing that finds **nothing in range** |
| **87** | `pl_cast_powerup.wav` | **level up** (`FUN_10044618`, which also increments `player+0xf44` — M50's level-up points); and the twelve-step cheat sequence (`FUN_10028174`) |
| **91 / 92** | `pl_jump_female.wav` / `pl_jump_male.wav` | **jump**, chosen on `player+0xfac` — the field `SetSex` writes (M50). So sex 0 is female |
| 99 | `menuNEWxbx.wav` | UI navigation |
| 100 | `menu1.wav` | UI select |
| 176 | — | `FUN_1006b3fc`; past the end of every manifest, so silent |
| — | the creature's `+0x2b0`/`+0x2b2`/`+0x2b4` | attack / hit / death noise, already wired |

So **80 and 81 are hit and miss**, not two weapon classes: `FUN_100425bc`
picks between them on whether the target search produced anything at all,
not on whether the damage roll landed. And a jump is not free — the real
one refuses unless fatigue is above 5 and spends 5.

**The main-menu music** is `FUN_1002707c`, the native "return to the
front end" path (it resets the level, clears the multiplayer session and
drops the player). It loads the sound bank named `"menu"` and calls
`FUN_1001b180(engine, 0x46, 100, 0xff)` — `menu_sounds.txt` slot 70,
which is `battle3.ogg`, at volume 100, repeating 255 times. No script
triggers it because returning to the menu is not a scripted event.

### `PlaySound`'s real signature

`FUN_10061a60` case 0x25 builds three optional `skRValue`s with visible
defaults before dispatching:

```
PlaySound(id, volume = 100, directional = false, repeats = 1)
```

**`repeats` is a count, not a loop flag** — the queue writes it straight
into the sound object's own repeat byte. 255 is simply the largest a byte
holds, which is what both the music path and an entity ambient arm
themselves with.

The corpus uses one argument **120 times** and all four **exactly once**:
`twilite/steamsound.s`'s `PlaySound(65, 75, 1, 255)` — a quieter,
directional, endlessly repeating steam hiss. That one call is what pinned
the meaning of each argument.

`StopSound` (`FUN_1001b190`, clears the object's playing bit) and
`CreateSound` (`FUN_1001b188`, clones a loaded sound into a free slot)
are real bindings with **zero call sites**.

### The two Options sliders

`FUN_1007f8f8` compares the slider's own name against two wide strings
and writes `mgr+0xabc` for **`SoundFXSlider`** and `mgr+0xab8` for
**`MusicSlider`** — and `options.s` really does declare
`AddMenuSlider(4062,"SoundFXSlider",100,10)` and
`AddMenuSlider(4063,"MusicSlider",100,10)`. The native handler clamps to
0..256 while the slider only offers 0..100 in steps of 10, so the top of
its range is unreachable.

### What is wired to nothing

M27 recorded ids 79-82 and 88-96 as "native-only, triggered by the
engine's own movement/combat code". Half of that is right and half is
not. 80, 81, 87, 91 and 92 are native triggers, above. The rest are
triggered by **nothing at all** — no native call site and no script:

| slot | file |
|---|---|
| 79 | `pl_attack_blunt.wav` |
| 90 / 93 | `pl_female_die.wav` / `pl_male_die.wav` |
| 94 / 95 / 96 | `pl_run.wav` / `pl_walk.wav` / `pl_walk_wading.wav` |

**The shipped game has no footstep sounds and no death voice lines.** The
samples are real, they are mapped in 14 of the 22 manifests, and nothing
plays them. That is not a gap in this decode — it is the answer.

## What's implemented in the port

- **`port/src/audio/sound_mixing.h`/`.cpp`** — the volume curve verbatim,
  the per-voice gain, both attenuation terms, the fade state machine, the
  eight-voice cap, and the native slot constants. Pure arithmetic, no
  backend, so it is testable on its own.
- **`audio_engine.h`/`.cpp`** — `PlaySfx(sound, volume, repeats)` now
  takes the engine's own units and puts them through the curve; the
  eight-voice cap is enforced; `FadeMusic`/`UnFadeMusic`/`TickMusicFade`
  drive the real fade by wall-clock so it takes the same 0.64 s.
- **The native triggers**, in `main.cpp`: hit/miss on a melee swing, the
  impact sound on taking damage, the jump's fatigue cost and its
  sex-selected sample, and the front-end music started the way
  `FUN_1002707c` starts it.
- **Positional attenuation**, for the sounds this port knows a world
  position for (a creature's attack noise); the player's own sounds go
  through the listener-position path, which is what the engine does.
- **The four-argument `PlaySound`** on the Player, Monster and Level
  bindings, and `FadeMusic`/`UnFadeMusic` on the menu binding.
- **`m51_audio_mixing_smoke`** (42 checks): the arithmetic against the
  decompiled mixer, every native slot resolved against all 22 shipped
  manifests, the two sliders and the one four-argument call against the
  scripts, and the six orphan samples shown to be orphans.

## Still open

- **The bearing function** (`FUN_1001c20c`) is a normalised-vector lookup
  quantised to a 32×32 grid; this port uses a real `atan2` where it needs
  one. Since the term it feeds is arithmetically zero, the difference is
  unobservable.
- **`crypt2/controller.s`'s bare `PlaySound(83)`** — the call is
  understood now (slot 83 is `NULL.wav` in every manifest, so it is
  silent), but neither that script nor `twilite/steamsound.s` is loaded
  by this port yet: their placement mechanism is still not one of the
  entity categories the zone-load block resolves.
- **The sound object's own start/decode path** (`vtable+0x10`, and the
  Symbian media server behind it) was not decompiled — the port uses
  XAudio2 and applies the recovered *gain*, rather than reproducing the
  original's saturating integer accumulator sample by sample.
