#pragma once

// M58: the effects system -- `AddEffect` and the three named enumerations
// behind it.
//
// `AddEffect` is the single busiest unimplemented native in the shipped
// script corpus: 83 real call sites, 63 of them on `GetOwner()` (every
// potion, herb, skin and dust in items/) and 20 on `GetPlayer()` (the six
// attribute shrines in crypt1/crypt2/cryptsh3, the Umbra Keth blessing,
// two loot menus and a conversation reward). Everything it needs on the
// engine side already existed -- M43/M48 built the timed-modifier
// machinery on ActorStats -- and what was missing was this: the
// script-facing entry point, and the effect table that gives its arguments
// meaning.
//
// ## The three enumerations are engine constants, not script constants
//
// A shipped call reads
//
//     GetOwner().AddEffect(Timed, ArmorValue, Increment, 5, 120);
//
// and `Timed`, `ArmorValue` and `Increment` are defined in **no `.s` file
// anywhere in the corpus**. They are named integer constants the engine
// pushes into the SimKin interpreter at startup, via
// `SIMKIN_MakeIntAtom` + `SIMKIN_MakeStringAtom` + `SIMKIN_RegisterConstant`
// -- the same three-call shape `SIMKIN_BRIDGE.md` first found for the
// `IPT_*` item categories. Two functions register them:
//
//   * `GameEngine_FirstTickBootstrap` (0x10023ad0), into the one global
//     interpreter -- 45 constants;
//   * `FUN_10073d3c` (the menu/screen base-class constructor, which gives
//     every menu object its own interpreter), into that object's
//     interpreter -- 60 registrations, and the `AR_*`/`SR_*`/`WR_*`
//     families additionally into the global one.
//
// Recovering them is mechanical: pair each `MakeIntAtom(value)` with the
// `MakeStringAtom(DAT_x)` that follows it and resolve the literal. The
// table below is that recovery, verbatim. It is also the reason the
// three "stat" names the corpus uses that are *not* in it matter -- see
// kEffectStatCount's comment.
//
// ## The stat enumeration is the stats block's own field map
//
// `FUN_1004ad40` is a 28-case switch on the stat index that reads one
// field of the stats block, combines it with the magnitude per the op, and
// writes it back. Its case-to-offset map lines up exactly with the
// recovered constant values -- Attack=1 writes +0x00, Defense=2 writes
// +0x02, ArmorValue=7 writes +0x0c -- which independently confirms M43's
// `ActorStats::StatIndex` (kStatAttack=1, kStatDefense=2, kStatArmor=7),
// guessed then from three unrelated dispatcher cases.
//
// ## Three durations, and they are three different mechanisms
//
// `FUN_1004aa28(stats, duration, stat, op, magnitude, seconds, obj, name)`
// branches on the *duration* constant, and the three arms share almost
// nothing:
//
//   * **Permanent (3)** -- apply the stat change and return. No node, no
//     bookkeeping, nothing to expire. This is what the attribute shrines
//     and every "eat this and gain 5 Strength forever" herb use.
//   * **Timed (1)** -- look for an existing node on the same stat; if one
//     is there and the new magnitude is not *stronger* in absolute value,
//     give up entirely; otherwise unlink it, undo its stat change, free it,
//     and fall through into the node-creating arm. At most one Timed
//     effect per stat, strongest wins.
//   * **Equipped (2)** -- create a node in a *second* list and stack it.
//     Nothing in the shipped corpus ever passes it (all 83 sites are Timed
//     or Permanent), and the code that would remove one is dead: see
//     effects.cpp's note on FUN_1004b3c0/FUN_1004b4f0.
//
// ## What a node is
//
// 0x18 bytes: `{ stat, op, s16 stored, expiry, obj, char* name }`. `stored`
// is the magnitude for an Increment and the field's *previous value* for a
// Set or Decrement, which is what makes expiry able to undo either kind
// (`FUN_1004b3b4`, one instruction of prologue on top of the applier).
// `expiry` is absolute -- `registry+0x460 + seconds * 0x100` -- where this
// port keeps a countdown, the deviation ActorStats already documented.
//
// `name` is a strdup'd 8-bit string and is what `FindStringEffect` and
// `RemoveEffect` search on; a node created without one stores a null and is
// invisible to both.

#include <string>

class skiExecutable;
class skRValue;
class skRValueArray;
class skString;

namespace sk_bindings {

class ActorStats;
class SpellActor;
struct Effect;

// ---- the recovered constant table ----

// `InvalidDuration`/`Timed`/`Equipped`/`Permanent`, and FUN_1004aa28's own
// three arms in that order.
enum EffectDuration {
    kDurationInvalid = 0,
    kDurationTimed = 1,
    kDurationEquipped = 2,
    kDurationPermanent = 3,
};

// `InvalidType`/`Increment`/`Set`/`Decrement`. FUN_1004ad40 tests only 1 and
// 3; anything else (including the registered `Set`) falls through to a plain
// assignment, so `Set` works by being the default rather than by having a
// case of its own.
//
// Note `Decrement`'s arm computes `magnitude - previous`, not
// `previous - magnitude`, and FUN_1004b3b4 negates only for `Increment` --
// so a Decrement node is not undone by its own expiry. Nothing in the corpus
// passes it, and RemoveEnchantments (which strips every Decrement node) is
// the only thing that ever looks for one.
enum EffectOp {
    kOpInvalid = 0,
    kOpIncrement = 1,
    kOpSet = 2,
    kOpDecrement = 3,
};

// The stat enumeration. Values are the registered ones; the two holes (24
// and 29 in the global registration) are real -- `StatLevel` = 24 is
// registered only by the per-menu interpreter, and 29 is registered by
// neither although FUN_1004ad40 has no case for it either.
enum EffectStat {
    kEffectStatInvalid = 0,
    kEffectStatAttack = 1,
    kEffectStatDefense = 2,
    kEffectStatSpellcast = 3,
    kEffectStatMagicResistance = 4,
    kEffectStatMinDamage = 5,
    kEffectStatMaxDamage = 6,
    kEffectStatArmorValue = 7,
    kEffectStatExpWorth = 8,
    // Not the strength field. FUN_1004ad40 case 9 writes stats+0x10, which
    // GetStrengthBonus (dispatcher index 0) reads, while GetStrength (index
    // 0x1b) reads stats+0x14 -- so an `AddEffect(..., Strength, ...)` moves
    // the strength *bonus* and leaves strength itself alone. Every other
    // attribute in this enum writes the attribute proper. Deliberate in the
    // engine or not, it is what the six shrine scripts do.
    kEffectStatStrength = 9,
    kEffectStatIntelligence = 10,
    kEffectStatAgility = 11,
    kEffectStatWill = 12,
    kEffectStatSpeed = 13,
    kEffectStatEndurance = 14,
    kEffectStatPersonality = 15,
    kEffectStatLuck = 16,
    kEffectStatMaxHealth = 17,
    kEffectStatMaxFatigue = 18,
    kEffectStatMaxMagicka = 19,
    kEffectStatHealth = 20,
    kEffectStatFatigue = 21,
    kEffectStatMagicka = 22,
    kEffectStatExperience = 23,
    kEffectStatLevel = 24,  // registered as `StatLevel`
    kEffectStatGold = 25,
    // Registered, but FUN_1004ad40 has no case for any of 26..29: they fall
    // to its default, which does nothing. Regeneration is real, but it is
    // the *periodic channel*'s business (SetSpellEffect / ActorStats::
    // ArmPeriodic), not this table's.
    kEffectStatRegenHealth = 26,
    kEffectStatRegenMagicka = 27,
    kEffectStatRegenFatigue = 28,
    // Two cases that exist and deliberately do nothing -- a node with a name
    // and a timer, and no stat behind it. `ItemUsed` is a **cooldown
    // marker**: twi_crystals.s is the whole idiom, and it is the only
    // FindStringEffect call site in the corpus.
    //
    //     if (GetPlayer().FindStringEffect(ItemUsed, "crystals") = false) {
    //         ... the effect ...
    //         GetPlayer().AddEffect(Timed, ItemUsed, 0, 0, 60, "crystals");
    //     } else { OpenMenu("CantUse"); }
    //
    // Note these two are also the only stat values that clear FUN_1004ad40's
    // entry guard with an op of 0: the guard panics on `stat <= 28 && op ==
    // 0`, and ItemUsed is 30.
    kEffectStatItemUsed = 30,
    kEffectStatMonsterAttackBonus = 31,
    // The odd one out: not a field but a *flag*. Sets or clears the blind
    // bit of the effect-flag word (stats+0x44 bit 2) depending on whether
    // the magnitude is positive. Its clear path is written
    // `flags = ~(~flags & 4)` -- `mvn`/`and`/`mvn`, where `bic` was meant --
    // so it sets every bit *except* blind instead of clearing blind. See
    // ApplyEffectStat. Nothing in the corpus reaches it.
    kEffectStatBlindness = 32,
};

// The stats block's real strength field (stats+0x14, `GetStrength`). It has
// no entry in the effect table -- `Strength` there is the bonus at +0x10 --
// but the derived-stat recompute reads it, so it needs an index of its own
// here. Deliberately outside the registered range.
inline constexpr int kEffectStatStrengthProper = 64;
// The health bonus at stats+0x12 (`SetHealthBonus`/`ModHealthBonus`), read
// by the same recompute. Also unreachable from the effect table.
inline constexpr int kEffectStatHealthBonus = 65;

struct EffectConstant {
    const char* name;
    int value;
};
// Every named integer constant the engine registers, in registration order:
// the 5 `IPT_*`, the 3 armour and 2 size and 9 weapon ratings, and the three
// effect enumerations. `RegisterGameConstants` walks this.
extern const EffectConstant kEffectConstants[];
extern const int kEffectConstantCount;

// ---- the primitives ----

// FUN_1004ad40. Applies one stat change and returns the field's value
// *before* it -- which is what a Set or Decrement node stores so its expiry
// can put it back. Returns 0 for the cases that do nothing, and for the
// entry guard (`stat <= 28 && op == 0`), which in the real engine stalls
// 10ms in `User::After` and falls out.
int ApplyEffectStat(SpellActor& actor, int stat, int op, int magnitude);

// FUN_1004b3b4: negate the magnitude if the op was Increment, then run the
// applier. Three instructions in the original, and the whole of expiry.
void UndoEffectStat(SpellActor& actor, int stat, int op, int stored);

// FUN_10049698, the derived-stat recompute the applier tails into after
// Intelligence, Agility, Will, Endurance, Personality and Luck -- but not
// after Strength or Speed:
//
//     maxHealth  = healthBonus + (strength + endurance) / 2
//     maxFatigue = will + strength + endurance
//     maxMagicka = intelligence            (plus a per-class term, below)
//
// All three are assignments, so a `SetMaxHealth` from a script survives only
// until the next attribute change. `strength` here is the real one at +0x14,
// which the effect table cannot reach -- so an attribute potion recomputes
// max health from a strength no `AddEffect` can move.
void RecomputeDerivedStats(SpellActor& actor);

// FUN_1004aa28, the whole of `AddEffect`.
void AddEffect(SpellActor& actor, int duration, int stat, int op, int magnitude,
               int durationSeconds = 0, const std::string& name = std::string(),
               skiExecutable* obj = nullptr);

// FUN_1004abe4: the first node in either list whose stat matches, whose name
// is non-empty, and whose name matches case-insensitively. Null otherwise.
const Effect* FindStringEffect(const ActorStats& stats, int stat, const std::string& name);

// The dispatcher's `RemoveEffect` (case 0x53), faithfully -- including that
// it is broken. It finds the node and frees it, and does **neither** of the
// two things the engine's own internal remover (FUN_1004bd24) does either
// side of that free: it never unlinks the node from its list, and it never
// undoes the stat change. Reproduced as "drop the node, leave the stat
// where it is"; the dangling list entry has no port equivalent. No script in
// the corpus calls it, which is presumably why nobody noticed.
bool RemoveEffect(SpellActor& actor, int stat, const std::string& name);

// FUN_1004bd24, the engine's own remover, and the one that is correct:
// unlink, undo, free. Its real signature takes a stat *and* a name -- the
// Disease branch of the status dispatcher applies two modifiers named
// "DISEASE" and "DISEASE_DEF" and CureDisease removes exactly those.
bool RemoveNamedEffect(SpellActor& actor, int stat, const std::string& name);

// The nameless form, for this port's own callers that have only a stat.
bool RemoveStatEffect(SpellActor& actor, int stat);

// FUN_10048140 (`RemoveEnchantments`, dispatcher case 7): zero the shared
// effect timer and +0x70, set flag bit 1, then repeatedly scan the *timed*
// list for a node that is either negative or of op Decrement, and unlink /
// undo / free it -- restarting the scan each time. Two shipped call sites,
// both `GetPlayer().RemoveEnchantments()`. It does not clear blindness; the
// spell of the same name does that separately in its caller.
void RemoveEnchantments(SpellActor& actor);

// The eight Character-stats bindings this milestone adds, in one place --
// they belong to trie 0x14db0, which both `GetPlayer()` and `GetOwner()`
// resolve to, so both receivers get exactly the same handler rather than two
// copies that could drift. Returns false for anything else, leaving the
// caller's own chain (and its soft-fail tail) to deal with it.
//
//   AddEffect(duration, stat, op, magnitude [, seconds [, name [, obj]]])
//   FindStringEffect(stat, name) -> bool
//   RemoveEffect(stat, name)
//   RemoveEnchantments()
//   SetSpellEffect(kind, durationUnits)
//   SetBlindness(bool [, timerUnits])
//   SetPoisoned(bool, timerUnits)
//   SetDiseased(bool, timerUnits)
bool HandleEffectNative(SpellActor& actor, const skString& methodName, skRValueArray& args,
                        skRValue& returnValue);

// The stats-block internal names the real status dispatcher gives the two
// modifiers its Disease branch applies (resolved from the literals at
// FUN_100458e4's call sites). Kept so CureDisease can remove exactly those
// two, by name, the way the engine does.
inline constexpr const char* kEffectNameDisease = "DISEASE";
inline constexpr const char* kEffectNameDiseaseDefense = "DISEASE_DEF";

}  // namespace sk_bindings
