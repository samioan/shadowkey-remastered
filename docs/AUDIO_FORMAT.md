# Audio: real formats, the real sound-slot manifest convention, and what's wired up

Previously entirely unaddressed (`docs/PORT_ROADMAP.md`'s "Audio: entirely
unaddressed so far, format not RE'd" -- now resolved, M27). Unlike every
other asset this port has had to reverse-engineer (sprites, models, zone
geometry), **the audio data itself needed no RE at all** -- every real
sound file under `system/apps/6r51/` is a standard, off-the-shelf format.
The actual RE-worthy question was the *native call surface* (what does a
real script's `PlaySound(id)`/`PlayAmbient(id, volume)` argument actually
mean?), and that was answered entirely by real corpus cross-reference, no
Ghidra needed.

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
any real script -- these are native-only, triggered directly by the
original engine's own movement/combat code, not through SimKin at all.
Not wired up in this port (see "Not attempted" below).

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
  music, `volume` 0-100 mapped linearly to XAudio2 gain -- no real
  formula was RE'd for this mapping, a documented simple default).
- **Verified end to end against real data**, `audio_smoke`
  (`src/tests/m27_audio_smoke.cpp`): real `.wav`/`.ogg` decode, the real
  `azra_sounds.txt` manifest (including its real `NULL.wav` sentinel),
  and a real `door.s`/`azra.s` script call genuinely reaching the audio
  engine without throwing.

## Not attempted (documented gaps, not silently skipped)

- **Native-only player-action sounds** (attack swing / jump / death /
  footsteps, ids 79-82/88-96 above) -- real assets exist and *which*
  asset matches *which* action is inferable from filenames plus
  already-known per-weapon/per-sex data this port has (weapon type,
  player sex), but *when exactly* the original native code triggers each
  one wasn't decompiled -- left unwired rather than guessing a trigger
  point, keeping this milestone's implemented behavior 100%
  real-script-verified.
- **Main-menu background music** -- `menu_sounds.txt`'s own `70 battle3.
  ogg` is almost certainly meant to play on the main menu (same pattern
  every zone's own manifest follows for its ambient track), but no real
  script calls anything to start it (`menus/mainmenu.s` has no
  `PlayAmbient`/`PlaySound` call at all) -- native-triggered, not
  decompiled, left unwired.
- **`crypt2/controller.s`'s bare `PlaySound(83)`** and **`twilite/
  steamsound.s`'s 4-argument `PlaySound(65, 75, 1, 255)`** -- both real,
  but neither script's own loading/placement mechanism is established in
  this port yet (not one of the entity categories `main.cpp`'s zone-load
  block currently resolves into a live object) -- the extra 2 arguments
  on `steamsound.s`'s call (beyond `id`, presumably volume) were not
  decompiled either.
- **Positional/3D audio** -- every SFX plays at a flat gain regardless of
  the real world-space source's distance from the player; no RE was done
  to check whether the original engine does distance attenuation or
  stereo panning.
- **Volume/pan mixing formula** -- `PlayAmbient`'s `volume` (0-100 in the
  one real value seen, 100) is mapped linearly to XAudio2 gain; no real
  formula was decompiled to compare against.
