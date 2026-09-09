#pragma once

// M76: the other half of talking to people -- an NPC that notices *you*.
//
// M75 (use_prompt.h) restored the prompt you walk up to and press Use on.
// This is the mechanism that runs without you pressing anything: a
// non-aggressive creature that has asked to be told when the player comes
// into range, whose script then greets you, opens a conversation, or turns
// hostile. 31 shipped scripts define an `OnDetect` handler.
//
// ---------------------------------------------------------------------
// Where it lives
//
// Entirely inside the creature AI tick, `FUN_10082224`. That function
// branches on the AI package (`monster+0x2a8`); the arm that matters is
//
//     else if (package == 2 /*idle*/ ||
//              (package == 3 /*pursue*/ && monster->+0x20c /*target*/ == 0))
//
// i.e. "I am looking for something". Everything below happens in that arm,
// in this order (singleplayer; the multiplayer clauses are noted where they
// change the answer):
//
//   1. Pick a candidate. In singleplayer there is exactly one -- the
//      player, `engine+0x618` -- and `dist` is `vtable[0x5c](self, player)`
//      == `FUN_100683d4`, which is
//
//          ((dy*dy) >> 8) + ((dx*dx) >> 8)          i.e. (dx^2 + dy^2)/256
//
//      the same **scaled squared** form `SetChaseRadius` stores, so the two
//      are directly comparable (monster_executable.h's chaseRadius()).
//
//   2. `if (target && package == 2) package = 3.` A creature in the idle
//      package flips to pursue the moment a candidate exists at all -- even
//      a non-aggressive one, which then never sets a target and so keeps
//      falling into this same arm every tick. That is what makes OnDetect a
//      *repeating* test rather than a one-shot: nothing in the engine
//      latches "already detected". The scripts do the latching themselves,
//      with `AiSleep()` (package -1, which this arm no longer matches) or
//      `SetAggressive(true)` (which sends them down the attack branch).
//
//   3. The perception roll -- see DetectMissPercent() below.
//
//   4. `if ((dist < chaseRadius && noticed) || multiplayerTeam) {`
//          `if (monster->+0x2ac /*aggressive*/ == 0) {`  ... OnDetect ...
//          `} else {`                                    ... acquire and attack ...
//
//      So **OnDetect is what a non-aggressive creature does instead of
//      attacking**, out of the same perception test, at the same distance,
//      on the same tick. The two are one branch, which is why an NPC that
//      calls `SetAggressive(true)` in its own Init -- `delfhide/
//      dh_guard_talk.s` does exactly that -- has a dead OnDetect handler.
//
//   5. The OnDetect guard itself (0x10083094):
//
//          if (target == engine->player &&
//              self->vtable[0x21c](self, target, self->+0x2dc >> 8, 1) &&
//              (self->+0x306 || !multiplayer || isHost))
//              if (self->+0x120 /*script*/)
//                  self->vtable[0xa8]("OnDetect", { target }, ...);
//
//      `vtable[0x21c]` is `FUN_10004d70` -- a real line-of-sight ray march,
//      see SightRangeTiles(). `+0x306` is `SetAlwaysOnDetect`, and `+0x120`
//      is the entity's script object.
//
// The handler is called with **one argument: the entity that was detected**
// (the player). The engine boxes `target + 0x14` -- the skiExecutable base
// sub-object -- into an skRValue and appends it to the argument array
// (0x100831ac..0x10083208). Every shipped handler declares the parameter
// (`OnDetect[ (s) ...]`) and none of them reads it; they all call
// `GetPlayer()` instead.
//
// ---------------------------------------------------------------------
// The sightline
//
// `vtable[0x21c]` == `FUN_10004d70(self, other, budgetTiles, 1)` is a real
// ray march, and it is worth spelling out because this port already had a
// hand-built equivalent (`Zone::HasLineOfSight`) documented as guesswork:
//
//   * start at the caller's *eye* -- `(+0x94, +0x9c, +0xa4 + height())`,
//     where height() is `vtable[0x108]` -- and aim at the target's eye;
//   * normalise the direction to 0x100 (one tile) and then halve it, so the
//     march steps half a tile at a time;
//   * step through `FUN_100182d0` with flag mask 0xb, which reports a wall
//     (`.zmp` blocking bit 2, exactly the flag this port's DDA already
//     tests) and also tracks floor/ceiling height against the ray's own z;
//   * a wall ends it -- `if (hit & 2) return 0` -- while an *entity* in the
//     way does not: the caller walks that tile's entity list, returns 1 if
//     the target is in it, and otherwise resumes the march past it.
//
// So sight-gating creature perception was the right model; only the range
// cap and the 3D-ness of it were missing.
//
// ---------------------------------------------------------------------
// The perception roll
//
// The one piece of randomness in the whole path, and it is shared with the
// attack branch -- a creature that fails it does not aggro either.
//
//     detect = monster->+0x258;  if (detect == 0) detect = 1;
//     agi    = playerStats->vtable[0x40]();          // Agility / 5
//     r      = (detect << 16) / ((detect + agi) << 8);
//     if (rand(0, 100) < (r * 100 >> 8))  noticed = false;
//
// `monster+0x258` is set to 1 by the creature constructor (FUN_100815e0)
// and **no binding, script or engine path ever writes it again** -- so the
// numerator is always 1 and the whole thing collapses to "the chance of
// going unnoticed this tick is 100/(1 + Agility/5) percent".
//
// `vtable[0x40]` on the stats block is `FUN_1004b848` for a creature and
// `FUN_10044b8c` for the player; both return the 16-bit field at
// `stats+0x18` divided by 5, which for the player (`player+0x3ac+0x18` ==
// `player+0x3c4`) is **Agility** (assets/save_records.h's own offset map).
// The player version then adds two equipped-item bonuses this port has no
// model for (`+0xf38 == 8` adds `+0xfb0`, `+0xf3c == 2` adds `+0xfb4` --
// the same equipped-effect array zone_script_executable.h's trap comment
// already records as not reproduced), so a real charm would only make the
// player *easier* to notice, never harder.
//
// It runs every tick, which is what makes the numbers behave: a starting
// Agility of 40 gives an 11% miss chance per tick, so detection happens
// within a frame or two. Even Agility 0 -- 100/101 miss -- resolves in
// about four seconds rather than never.
//
// ---------------------------------------------------------------------
// Two bindings that come with it, and one that is dead
//
// `SetAlwaysOnDetect(b)` -- Monster(AI) binding index 4, dispatcher case 4,
// `monster+0x306` -- has exactly one reader in the whole binary: the third
// clause of the guard in step 5. In singleplayer `!multiplayer` already
// satisfies that clause, so the flag changes nothing; it exists so a
// multiplayer *client* still runs OnDetect locally. All ten scripts that
// set it are `raiders/*.s`, the multiplayer arena creatures.
//
// `DetectOnKilled(b)` -- binding index 3, `monster+0x307` -- is read once,
// inside the death path (FUN_10083c04), and only under `engine+0x5c0 != 0`.
// Multiplayer-only in the same way.
//
// `MenuClosed` is the third piece, and it does not work in the shipped
// game. The notifier exists -- `FUN_10064c60(entity)` is
//
//     if (entity != engine->player && entity->+0x120 && !entity->+0x48)
//         entity->vtable[0xa8]("MenuClosed", {}, ...);
//
// and it sits at `vtable+0x3c` in all 31 entity vtables (checked: every one
// of the 31 words holding 0x10064c60 is exactly 20 slots below that same
// vtable's `SetUseText`). But **nothing calls it.** The only references to
// its address anywhere in the image are those 31 vtable words, and of the
// 18 sites in the binary that dispatch through slot 0x3c, not one has an
// entity receiver -- they are the menu manager's and the app object's own
// slot 0x3c, and they pass arguments FUN_10064c60 does not take. So
// `monsters/bbrawler_talk.s`'s "talk to him, then kill him" never completes
// on the device either: its `MenuClosed` handler, the thing that re-arms
// the brawler, is unreachable. Reproduced by not implementing it.

namespace sk_bindings {

// `monster+0x258`, written to 1 by FUN_100815e0 and never again.
constexpr int kDetectStrength = 1;

// The perception roll's threshold, transcribed with the real integer
// arithmetic (two truncating divides and a shift, in the engine's order --
// the result differs from the algebraic 100*d/(d+a) by a point or two).
// Returns a percentage in 0..100: the chance the creature does *not*
// notice, this tick.
inline int DetectMissPercent(int detectStrength, int playerAgility) {
    int detect = detectStrength == 0 ? 1 : detectStrength;
    // FUN_1004b848 / FUN_10044b8c: the 16-bit stat at stats+0x18, / 5.
    int agilityTerm = playerAgility > 0 ? playerAgility / 5 : 0;
    int denominator = (detect + agilityTerm) << 8;
    if (denominator <= 0) return 100;
    int ratio = (detect << 16) / denominator;
    int percent = (ratio * 100) >> 8;
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    return percent;
}

// `rand(0, 100)` is inclusive at both ends (FUN_100730c8 is
// `lo + rand % (hi - lo + 1)`), and the engine's test is
// `if (roll < missPercent) noticed = false`.
inline bool DetectionNoticed(int missPercent, int roll) { return roll >= missPercent; }

// How far the OnDetect sightline is allowed to march. The engine passes
// `monster->+0x2dc >> 8` -- the raw, *scaled-squared* SetAttackRange value
// shifted right by 8 -- as FUN_10004d70's distance budget, and that budget
// is in **tiles**: FUN_100182d0 loops `while (travelled < budget * 0x100)`
// adding 0x80 per half-tile step. The constructor default 0x6a4 therefore
// buys 6 tiles of sight, which is what every creature in the game uses --
// no shipped script calls SetAttackRange.
//
// Mixing a squared unit with a tile count is the engine's own arithmetic,
// not a transcription slip: `+0x2dc` is compared as a squared distance
// everywhere else in the same function.
inline int SightRangeTiles(int attackRangeScaledSquared) {
    return attackRangeScaledSquared >> 8;
}

}  // namespace sk_bindings
