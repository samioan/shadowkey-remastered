#pragma once

// M63: `Level.CreateEffect(...)` -- the Zone/Level dispatcher's case 0x23
// (`FUN_1006dbec`), which is an inlined copy of the engine's own
// `FUN_10073528`, and the animated-sprite entity it builds.
//
// Thirteen shipped call sites, all ten-argument, eight in `crypt1.s` and
// five in `twilite.s`, plus a fourteenth commented out in `blaze.s`. Every
// one of the thirteen is in its zone script's `Init()` -- not `EnterZone`,
// not a trigger -- and every one is permanent, so this is the game's
// **static scenery animation** primitive: a decorative emitter placed by
// hand at a world coordinate when the zone loads, not a combat effect.
//
// ---- The ten arguments ----
//
//     Level.CreateEffect(firstSprite, lastSprite, drawFlags,
//                        x, y, z, animRate, lifetime, sizeX, sizeY)
//
// The real case reads them as ten `SIMKIN_ord29` fetches at
// `argArray + k*0x14` and, crucially, **returns immediately if there are
// fewer than ten** (`if (argc < 10) return 1;`) -- the nine per-argument
// `Leave` guards that follow it are dead code. So there is no shorter
// form: this native is all-or-nothing, unlike `SetPosition`'s optional z.
//
// ---- `firstSprite`/`lastSprite` are `global.spr` slot indices ----
//
// This is what M48 could not pin down. Its writeup recorded the spell
// projectile's `+0x134` as "not a models.txt index (2 is lantern.bin and 5
// is sbarrel.bin), so it selects from something else that has not been
// identified". It selects from **`global.spr`**, the same 384-slot image
// cache the menus and the HUD draw out of (docs/GRAPHICS_FORMAT.md), and
// `+0x134` is a slot index straight into the engine's loaded-sprite
// pointer table at `engine+0x4460`:
//
//     sprite = *(u16 **)(engine + 0x4460 + entity->0x134 * 4);
//
// -- `FUN_1008b25c`, the sprite entity's draw. Confirmed against the real
// archive, which settles it beyond the pointer arithmetic: slots 181-192
// are twelve consecutive **16x64** frames averaging RGB (204, 159, 102), a
// tall narrow flickering orange column -- fire, which is what `crypt1.s`
// spawns; slots 193-204 are twelve more 16x64 frames averaging
// (120, 121, 117), the same shape in grey -- steam, which is what
// `twilite.s` spawns, in the one zone that also ships a `steamsound.s`;
// slots 12-19 are eight **32x32** orange (123, 57, 13) frames whose opaque
// pixel count falls away from 337 to 59 -- a dissipating blast, which is
// `blaze.s`'s impact; and slot 71, the one `FUN_10081e5c` spawns with
// random velocity under gravity, is 32x32 and averages (133, 4, 7) --
// blood. The runs are exactly bounded, too: slot 180 is 57x46 and slot 205
// is 79x9, so 181-192 and 193-204 are not arbitrary windows into a longer
// sequence of same-sized art.
//
// ---- The animation ----
//
// `+0x140`/`+0x144` hold first and last slot in 8.8, `+0x14c` is the
// cursor and `+0x134` the slot actually drawn. `FUN_100606d0`:
//
//     cursor += (frameDelta * rate) >> 8;
//     if (cursor >= last + 0x100) {
//         if (!looping) { engine.Destroy(this); return; }
//         cursor = first;
//     }
//     sprite = cursor >> 8;
//
// so each frame is held for 1.0 and the range is inclusive. `rate` is
// `animRate * 15`, which at this port's `kAiFrameDeltaUnits` works out to
// **`animRate * 15 / 256` frames per second** -- every shipped call passes
// 128, i.e. 7.5 fps, and a twelve-frame flame loops in 1.6 s.
//
// Nothing clears the loop flag (`+0x151`, which the constructor sets), so
// a scripted effect always loops.
//
// ---- The lifetime, and why every shipped effect is permanent ----
//
// `lifetime * 15` seeds both `+0x13c` (remaining) and `+0x15c` (initial),
// and `FUN_1005ef94` counts the first down by the frame delta and destroys
// the entity at zero. But `lifetime == 0` sets `+0x152` instead, which
// skips the countdown entirely -- and **all thirteen shipped calls pass
// 0**. Only `blaze.s`'s commented-out line passes a real one (16, i.e.
// 240 units, 24 ticks, 0.94 s), so `lifetime` is in sixteenths of a
// second, near enough.
//
// `+0x152` also gates the ramp, `+0x138 = start + progress * (end -
// start)`. The constructor leaves both ends at 0x80, so a scripted effect
// holds one value the whole way and only an engine-spawned one (the blood
// spurt, 0x100 -> 0x40) ever changes.
//
// **CORRECTED (M79): `+0x138` is an opacity, not a size scale.** M63 read
// it as one because Ghidra mis-typed `FUN_1004f91c`'s parameter list by one
// -- the draw passes it fifteen arguments against fourteen declared, so
// `+0x58` and `+0x138` both land a slot early. Following the *call* instead,
// `FUN_1004f218`'s last two arguments are `+0x58` (0 = straight copy, 1 =
// blend) and `+0x138` (the blend level, quantised `(v + 0x1f) >> 6` into
// 0 = draw nothing, 1 = 25% source, 2 = 50%, 3 = 75%, 4 = fully opaque).
// Nothing about the drawn *size* passes through it at all. So the blood
// spurt fades from solid to a quarter rather than shrinking, and a scripted
// effect draws at a flat 50% blend. See render3d/zone_renderer.h.
//
// ---- `sizeX`/`sizeY` are not a radius ----
//
// PORT_ROADMAP.md's bullet read the ten arguments as "a position, a radius
// and a duration". The duration is argument 8; arguments 9 and 10 are two
// independent size scalars, and they are not in world units -- the draw
// multiplies each by its axis of the sprite's own pixel size, twice:
//
//     entity->0x5a = (i16)(sizeX * spriteWidth);         // FUN_1008b25c
//     halfWidth    = ((i16)entity->0x5a * spriteWidth) >> 8;
//
// and the same for `sizeY`/`spriteHeight`/`+0x5c`. The quad is then laid
// out in **camera space** (`+0xbc`/`+0xbe`/`+0xc0`, which `FUN_1001610c`
// fills by rotating the entity's offset from the player through the camera
// matrix and shifting down 8) as x from `-halfWidth` to `+halfWidth` and y
// from the entity's own height up by `2 * halfHeight` -- so a billboard is
// **bottom-anchored** at the effect's z, not centred on it.
//
// Because the sprite dimension enters twice, the scalars run inversely to
// the sprite: crypt1's flames pass sizeX 512 against a 16-pixel-wide
// sprite and sizeY 40 against a 64-pixel-tall one. The constructor's own
// default of 8 against a 32x32 sprite gives a half-width of 32 units, an
// eighth of a tile.
//
// One thing the formula is *not* checked against is how big the result
// looks. Taken literally it makes crypt1's first flame 1024 x 1280 world
// units -- four tiles by five -- and the blaze impact a square 1024
// across. Those are the numbers the instructions produce (the doubled
// multiply is in the disassembly, not an artefact of the decompiler), and
// they are reproduced here as-is; but nothing in this port draws a
// billboard yet, so they have not been seen. Treat the magnitudes as
// unverified and the mapping as exact.
//
// ---- What this port does and does not do ----
//
// **Drawn since M79.** `render3d/zone_renderer.h`'s SpriteBillboard is the
// billboard pass this file's note used to say was owed, and main.cpp feeds
// it every live effect and every spell projectile each frame: the current
// `global.spr` slot, the world position, the half-extents through
// EffectHalfWidth()/EffectHalfHeight(), and the blend. main.cpp already
// loads the active zone's `<zone>_sprites.txt` category, so the art is in
// memory when a zone's effects exist.
//
// Also not reproduced: the multiplayer mirror (`FUN_1003c124` replays a
// remote `CreateEffect` from a 0x28-byte packet, one field per argument),
// and the engine's own two spawners -- `FUN_10081e5c`'s blood spurt and
// `FUN_10067f84`'s attached effect -- which are separate call paths rather
// than script-visible bindings.

#include <cstdint>
#include <vector>

namespace sk_bindings {

// Both `animRate` and `lifetime` are stored multiplied by this. It is 15,
// not 16, in the real code -- the reason the "one second" of `blaze.s`'s
// lifetime 16 is really 0.94 s.
constexpr int kEffectTimeScale = 15;

// `+0x154`/`+0x158`, and so `+0x138`, as the constructor leaves them.
// 0x100 is 1:1, so a scripted effect draws at half size.
constexpr int kEffectDefaultScale = 0x80;

// One frame of the animation cursor's 8.8 fixed point.
constexpr int kEffectFrameStep = 0x100;

// The 0x160-byte animated-sprite entity built by `FUN_10060744` +
// `FUN_1008b420`, with only the fields anything actually reads. Field
// offsets are the real ones, for anyone reading this next to the
// decompile.
struct EffectEntity {
    int firstSprite = 0;  // +0x140 >> 8
    int lastSprite = 0;   // +0x144 >> 8, inclusive
    int sprite = 0;       // +0x134 -- the global.spr slot drawn this frame
    int animCursor = 0;   // +0x14c, 8.8 across the slot range
    int animRate = 0;     // +0x148, already scaled by kEffectTimeScale

    int x = 0, y = 0, z = 0;     // +0x94 / +0x9c / +0xa4 (z is 16-bit)
    int vx = 0, vy = 0, vz = 0;  // +0x98 / +0xa0 / +0xa6 (vz is 16-bit)

    int sizeX = 8, sizeY = 8;  // +0x12c / +0x130, the constructor's defaults

    int life = 0;     // +0x13c, counts down by the frame delta
    int lifeMax = 0;  // +0x15c, what it started at

    int scale = kEffectDefaultScale;       // +0x138
    int scaleStart = kEffectDefaultScale;  // +0x154
    int scaleEnd = kEffectDefaultScale;    // +0x158

    bool immortal = false;  // +0x152 -- set when lifetime is 0
    bool looping = true;    // +0x151 -- the constructor's default, never cleared
    bool gravity = false;   // +0x150 -- only the engine's blood spurt sets it

    // +0x58, the constructor's fifth argument. `CreateEffect`'s third
    // script argument lands here and every shipped call passes 1, so every
    // scripted effect is **blended**; a spell projectile passes 0 and is
    // drawn opaque. M79 feeds it to the billboard pass as its blend mode.
    int drawFlags = 0;

    bool alive = true;  // FUN_1001b484's deferred destroy
};

// The case body, verbatim: the constructor's defaults with the ten script
// arguments written over them.
EffectEntity MakeEffect(int firstSprite, int lastSprite, int drawFlags, int x, int y, int z,
                        int animRate, int lifetime, int sizeX, int sizeY);

// `FUN_1005ef94` (lifetime, motion, scale ramp) around `FUN_100606d0`
// (the frame advance). `frameDelta` is the engine's own per-frame delta,
// i.e. this port's kAiFrameDeltaUnits; `gravityUnits` is the engine field
// at `engine+0x14` that only a `gravity` effect reads -- no scripted
// effect ever does, so its exact value is not pinned down here.
void TickEffect(EffectEntity& effect, int frameDelta, int gravityUnits = 0);

// `FUN_1008b25c`'s two half-extents, in camera-space world units, given
// the drawn sprite's own pixel dimensions. The intermediate is truncated
// to a signed 16-bit field in the real code and is reproduced that way.
// M79: the shared form, because a spell projectile is the same entity
// class and reaches the same two instructions with its own `+0x12c`/
// `+0x130` (both 8, from `FUN_1008b420`).
int SpriteHalfExtent(int sizeScalar, int spritePixels);
int EffectHalfWidth(const EffectEntity& effect, int spriteWidth);
int EffectHalfHeight(const EffectEntity& effect, int spriteHeight);

// Drops every effect whose `alive` went false, preserving order.
void PruneEffects(std::vector<EffectEntity>& effects);

}  // namespace sk_bindings
