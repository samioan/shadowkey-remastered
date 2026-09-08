#include "simkin_bindings/effects.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

#include "simkin_bindings/actor_stats.h"
#include "simkin_bindings/spell_actor.h"
#include "skRValue.h"
#include "skRValueArray.h"
#include "skString.h"

namespace sk_bindings {

// The recovered table, in registration order. Values are the literals
// `SIMKIN_MakeIntAtom` is called with; names are the resolved
// `SIMKIN_MakeStringAtom` / `SIMKIN_ord101` literals.
//
// Two things about it are worth stating because they change what a script
// means, not just what a constant is:
//
//   * The `AR_*` / `SR_*` / `WR_*` families are **bit flags**, not the
//     0,1,2,... ordinals this port had been guessing (game_constants.cpp
//     carried them as "real identifiers, unconfirmed values"). Every weapon
//     rating is a distinct bit, which is what lets a single stored word say
//     "this scabbard takes long blades or daggers".
//   * `IPT_Consumable` is registered as **4** by the global bootstrap
//     (`GameEngine_FirstTickBootstrap`, the run at 0x10023fa8: 1, 2, 0, 3,
//     **4**) and as **0** by the per-menu constructor (`FUN_10073d3c`, the
//     run at 0x10074044) -- and 0 is `IPT_Misc`. Both are plain immediates
//     in their respective functions, and there are two copies of the name
//     strings to match (0x100ac798 and 0x100b2da8). A menu script asking
//     `if (GetItemType() = IPT_Consumable)` is therefore asking a different
//     question from the same line in an item script. This port has one
//     interpreter and takes the bootstrap's 4, which is the value the item
//     side (where the type is actually set) uses.
//
//     **M74 closes this as a non-issue rather than a deviation.** A census
//     of every `IPT_` reference in the shipped corpus: eleven scripts use
//     the constants by name and all eleven are world/creature merchants
//     (`dstar_e\blk_market.s`, `monsters\eranthos_merchant.s`, ...), which
//     run in the *game* interpreter, where the value is 4. The only two
//     menu-side scripts that mention `IPT_Consumable` at all --
//     `buysell.s:161` and `inventory.s:215` -- mention it **in a comment**
//     and compare against the literal `4` in the code. So the per-menu 0 is
//     read by nothing in the shipped game, and collapsing the two
//     interpreters onto the bootstrap's 4 is not just the safer choice, it
//     is observationally identical.
const EffectConstant kEffectConstants[] = {
    {"IPT_Weapon", 1},
    {"IPT_Spell", 2},
    {"IPT_Misc", 0},
    {"IPT_Armor", 3},
    {"IPT_Consumable", 4},

    {"AR_Light", 2},
    {"AR_Medium", 4},
    {"AR_Heavy", 8},

    {"SR_Small", 8},
    {"SR_Medium", 16},

    {"WR_Melee", 4},
    {"WR_Dagger", 512},
    {"WR_LightBow", 8},
    {"WR_MediumBow", 16},
    {"WR_ShortBlade", 32},
    {"WR_LongBlade", 64},
    // The one name the decompiler dropped the string atom for. Its value
    // (1024) sits between WR_LongBlade's 64 and WR_Blunt's 128 in the
    // registration order, and the registration order is the string block's
    // order exactly: ... LongBlade, EnchantedBlade, Blunt, Axe. The bit is
    // the next free one after Dagger's 512, which is what every other entry
    // in this family also does.
    {"WR_EnchantedBlade", 1024},
    {"WR_Blunt", 128},
    {"WR_Axe", 256},

    {"InvalidDuration", kDurationInvalid},
    {"Timed", kDurationTimed},
    {"Equipped", kDurationEquipped},
    {"Permanent", kDurationPermanent},

    {"InvalidStat", kEffectStatInvalid},
    {"Attack", kEffectStatAttack},
    {"Defense", kEffectStatDefense},
    {"Spellcast", kEffectStatSpellcast},
    {"MagicResistance", kEffectStatMagicResistance},
    {"MinDamage", kEffectStatMinDamage},
    {"MaxDamage", kEffectStatMaxDamage},
    {"ArmorValue", kEffectStatArmorValue},
    {"ExpWorth", kEffectStatExpWorth},
    {"Strength", kEffectStatStrength},
    {"Intelligence", kEffectStatIntelligence},
    {"Agility", kEffectStatAgility},
    {"Will", kEffectStatWill},
    {"Speed", kEffectStatSpeed},
    {"Endurance", kEffectStatEndurance},
    {"Personality", kEffectStatPersonality},
    {"Luck", kEffectStatLuck},
    {"MaxHealth", kEffectStatMaxHealth},
    {"MaxFatigue", kEffectStatMaxFatigue},
    {"MaxMagicka", kEffectStatMaxMagicka},
    {"Health", kEffectStatHealth},
    {"Fatigue", kEffectStatFatigue},
    {"Magicka", kEffectStatMagicka},
    {"Experience", kEffectStatExperience},
    // Registered only by FUN_10073d3c. The global bootstrap skips straight
    // from Experience to Gold, so in the real game a non-menu script naming
    // `StatLevel` gets an unresolved identifier -- and no script names it.
    {"StatLevel", kEffectStatLevel},
    {"Gold", kEffectStatGold},
    {"RegenHealth", kEffectStatRegenHealth},
    {"RegenMagicka", kEffectStatRegenMagicka},
    {"RegenFatigue", kEffectStatRegenFatigue},
    {"ItemUsed", kEffectStatItemUsed},
    {"MonsterAttackBonus", kEffectStatMonsterAttackBonus},
    // The only one of the 44 registered as an 8-bit literal rather than
    // UTF-16, which is why the first pass over the bootstrap resolved 43
    // names and one blank.
    {"Blindness", kEffectStatBlindness},

    {"InvalidType", kOpInvalid},
    {"Increment", kOpIncrement},
    {"Set", kOpSet},
    {"Decrement", kOpDecrement},
};
const int kEffectConstantCount =
    static_cast<int>(sizeof(kEffectConstants) / sizeof(kEffectConstants[0]));

namespace {

// FUN_1004ad40's arithmetic, shared by all 25 field cases. The engine reads
// the field as a signed 16-bit, combines, and stores 16 bits back; the two
// 32-bit fields (Experience, Gold) skip the truncation, which is the only
// difference between its `sVar2` and `param_4` arms.
int CombineStat(int previous, int op, int magnitude, bool sixteenBit) {
    int value = magnitude;
    if (op == kOpIncrement) {
        value = magnitude + previous;
    } else if (op == kOpDecrement) {
        // As written: magnitude minus the previous value, not the other way
        // round. Reproduced rather than corrected -- see effects.h.
        value = magnitude - previous;
    }
    return sixteenBit ? static_cast<int16_t>(value) : value;
}

bool IsSixteenBitStat(int stat) {
    return stat != kEffectStatExperience && stat != kEffectStatGold;
}

// The six attributes whose arm tails into the derived-stat recompute.
// Strength (which writes the *bonus*) and Speed deliberately do not.
bool RecomputesDerivedStats(int stat) {
    return stat == kEffectStatIntelligence || stat == kEffectStatAgility ||
           stat == kEffectStatWill || stat == kEffectStatEndurance ||
           stat == kEffectStatPersonality || stat == kEffectStatLuck;
}

bool EqualsIgnoreCase(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

}  // namespace

int ApplyEffectStat(SpellActor& actor, int stat, int op, int magnitude) {
    // The entry guard, exactly: `cmp r1, #0x1c / cmple lr, r4 / bne` --
    // bail when the stat is inside the switch's own range *and* the op is
    // InvalidType. The engine's bail is a 10ms `User::After` and a return of
    // zero; the stall has no meaning here, the zero does.
    if (stat <= kEffectStatRegenFatigue && op == kOpInvalid) return 0;

    // Blindness is a flag, not a field.
    if (stat == kEffectStatBlindness) {
        ActorStats& stats = actor.actorStats();
        if (magnitude > 0) {
            stats.SetEffectFlagsRaw(stats.effectFlags() | ActorStats::kEffectFlagBlind);
        } else {
            // `flags = ~(~flags & 4)`. Not a typo here either: the engine
            // emits mvn/and/mvn where `bic` was meant, so "cure blindness"
            // through this path sets every bit except blind -- poison and
            // disease included. Nothing in the corpus reaches it, and the
            // SetBlindness native (which does have a real `& 0xfffffffb`)
            // is what cure_blindness_balm.s actually calls.
            stats.SetEffectFlagsRaw(~(~stats.effectFlags() & ActorStats::kEffectFlagBlind));
        }
        return 0;
    }

    // The two marker stats: a real case in the switch, with an empty body.
    if (stat == kEffectStatItemUsed || stat == kEffectStatMonsterAttackBonus) {
        return 0;
    }

    // Health goes through the owner's clamping setter -- see
    // SpellActor::EffectStatSlot.
    if (stat == kEffectStatHealth) {
        const int previous = actor.actorHealth();
        actor.SetActorHealth(CombineStat(previous, op, magnitude, true));
        return previous;
    }

    int* slot = actor.EffectStatSlot(stat);
    if (!slot) return 0;  // no case, or no such field on this actor

    const int previous = *slot;
    int value = CombineStat(previous, op, magnitude, IsSixteenBitStat(stat));

    // Fatigue and Magicka carry the same two clamps health does, written
    // out in their own arms (against MaxFatigue / MaxMagicka, then zero).
    // A creature has no maximum for either, and the engine's answer there
    // would be to clamp against a zero field; skipping the max clamp when
    // the actor has no such field is the deliberate departure.
    if (stat == kEffectStatFatigue || stat == kEffectStatMagicka) {
        const int maxStat =
            stat == kEffectStatFatigue ? kEffectStatMaxFatigue : kEffectStatMaxMagicka;
        const int* maxSlot = actor.EffectStatSlot(maxStat);
        if (maxSlot && *maxSlot < value) value = *maxSlot;
        if (value < 0) value = 0;
    }

    *slot = value;

    if (RecomputesDerivedStats(stat)) RecomputeDerivedStats(actor);
    return previous;
}

void UndoEffectStat(SpellActor& actor, int stat, int op, int stored) {
    ApplyEffectStat(actor, stat, op, op == kOpIncrement ? -stored : stored);
}

void RecomputeDerivedStats(SpellActor& actor) {
    const int strength = actor.EffectStatValue(kEffectStatStrengthProper);
    const int endurance = actor.EffectStatValue(kEffectStatEndurance);
    const int healthBonus = actor.EffectStatValue(kEffectStatHealthBonus);

    if (int* maxHealth = actor.EffectStatSlot(kEffectStatMaxHealth)) {
        // `(int)(str + end) >> 1`, an arithmetic shift -- a negative sum
        // rounds away from zero, not toward it.
        *maxHealth = static_cast<int16_t>(healthBonus + ((strength + endurance) >> 1));
    }
    if (int* maxFatigue = actor.EffectStatSlot(kEffectStatMaxFatigue)) {
        *maxFatigue = static_cast<int16_t>(actor.EffectStatValue(kEffectStatWill) +
                                           strength + endurance);
    }
    if (int* maxMagicka = actor.EffectStatSlot(kEffectStatMaxMagicka)) {
        // M64 completes what M58 left as the non-player arm for both
        // actors. The real function branches on `vtable[0xcc]`, and the
        // player side does two extra things (see spell_actor.h): it bails
        // *without writing max magicka* when the class row has no magic --
        // note that max health and max fatigue above have already been
        // written by then, so this is a partial recompute, not a no-op --
        // and it adds the two bonus words on top of intelligence.
        const int intelligence = actor.EffectStatValue(kEffectStatIntelligence);
        if (!actor.isPlayerActor()) {
            *maxMagicka = static_cast<int16_t>(intelligence);
        } else if (actor.actorClassHasMagic()) {
            *maxMagicka = static_cast<int16_t>(actor.actorMagickaBonus() + intelligence);
        }
    }
}

void AddEffect(SpellActor& actor, int duration, int stat, int op, int magnitude,
               int durationSeconds, const std::string& name, skiExecutable* obj) {
    ActorStats& stats = actor.actorStats();

    // Permanent: apply and leave. No node, nothing to expire, nothing to
    // undo -- which is exactly why the six attribute shrines use it.
    if (duration == kDurationPermanent) {
        ApplyEffectStat(actor, stat, op, magnitude);
        return;
    }
    if (duration != kDurationTimed && duration != kDurationEquipped) {
        return;  // the engine stalls in User::After and falls out
    }

    if (duration == kDurationTimed) {
        // Strongest wins, by absolute value, and a tie loses. That tie is
        // what stops twi_crystals.s from refreshing its own cooldown: both
        // magnitudes are 0, `0 <= 0`, and the second AddEffect is dropped.
        // (It never gets there -- the script's own FindStringEffect guard
        // is checked first -- but it does mean only one named ItemUsed
        // cooldown can be live at a time, whatever item set it.)
        if (Effect* existing = stats.FindTimedEffectByStat(stat)) {
            if (std::abs(magnitude) <= std::abs(existing->stored)) return;
            const Effect replaced = *existing;
            std::vector<Effect>& list = stats.timedEffects();
            list.erase(list.begin() + (existing - list.data()));
            UndoEffectStat(actor, replaced.stat, replaced.op, replaced.stored);
        }
    }

    Effect node;
    node.stat = stat;
    node.op = op;
    node.remaining = durationSeconds * 256;  // the real `seconds * 0x100`
    node.obj = obj;
    node.name = name;

    // The applier runs *before* the node is stored, and its return -- the
    // field's previous value -- is what a non-Increment node keeps. An
    // Increment node keeps the magnitude instead, so that undoing it is a
    // subtraction rather than a restore. Both are 16-bit stores.
    const int previous = ApplyEffectStat(actor, stat, op, magnitude);
    node.stored = static_cast<int16_t>(op == kOpIncrement ? magnitude : previous);

    if (duration == kDurationTimed) {
        stats.AddTimedEffect(node);
    } else {
        stats.AddEquippedEffect(node);
    }
}

const Effect* FindStringEffect(const ActorStats& stats, int stat, const std::string& name) {
    // Both lists, timed first, and a node with no name is skipped outright
    // (`node[5] != 0` before the strcasecmp) -- so the nameless modifiers
    // every spell applies can never be found or removed this way.
    for (const Effect& e : stats.timedEffects()) {
        if (!e.name.empty() && e.stat == stat && EqualsIgnoreCase(e.name, name)) return &e;
    }
    for (const Effect& e : stats.equippedEffects()) {
        if (!e.name.empty() && e.stat == stat && EqualsIgnoreCase(e.name, name)) return &e;
    }
    return nullptr;
}

namespace {

// Erase one node from whichever of the two lists holds it. Returns a copy of
// what was there, so the caller can undo it afterwards the way the engine's
// unlink / undo / free ordering does.
bool EraseEffect(ActorStats& stats, const Effect* node, Effect* out) {
    for (std::vector<Effect>* list : {&stats.timedEffects(), &stats.equippedEffects()}) {
        if (node < list->data() || node >= list->data() + list->size()) continue;
        *out = *node;
        list->erase(list->begin() + (node - list->data()));
        return true;
    }
    return false;
}

}  // namespace

bool RemoveEffect(SpellActor& actor, int stat, const std::string& name) {
    ActorStats& stats = actor.actorStats();
    const Effect* found = FindStringEffect(stats, stat, name);
    if (!found) return false;
    // Faithful to the dispatcher: it calls only the *free*. The node stays
    // linked (a dangling pointer in the real engine) and the stat keeps
    // whatever the effect did to it. This port drops the node, because a
    // dangling entry is not a thing a std::vector can hold -- and leaves the
    // stat alone, which is the half that is actually observable.
    Effect erased;
    EraseEffect(stats, found, &erased);
    return true;
}

bool RemoveNamedEffect(SpellActor& actor, int stat, const std::string& name) {
    ActorStats& stats = actor.actorStats();
    const Effect* found = FindStringEffect(stats, stat, name);
    if (!found) return false;
    Effect erased;
    if (!EraseEffect(stats, found, &erased)) return false;
    UndoEffectStat(actor, erased.stat, erased.op, erased.stored);
    return true;
}

bool RemoveStatEffect(SpellActor& actor, int stat) {
    ActorStats& stats = actor.actorStats();
    Effect* found = stats.FindTimedEffectByStat(stat);
    if (!found) return false;
    Effect erased;
    if (!EraseEffect(stats, found, &erased)) return false;
    UndoEffectStat(actor, erased.stat, erased.op, erased.stored);
    return true;
}

void RemoveEnchantments(SpellActor& actor) {
    ActorStats& stats = actor.actorStats();
    stats.ClearEnchantmentTimers();

    // The engine's loop restarts from the head of the list after every
    // removal rather than continuing the scan; with the removal itself
    // correct that makes no difference, and this is the same set.
    for (bool removedOne = true; removedOne;) {
        removedOne = false;
        for (const Effect& e : stats.timedEffects()) {
            if (e.stored >= 0 && e.op != kOpDecrement) continue;
            Effect erased;
            EraseEffect(stats, &e, &erased);
            UndoEffectStat(actor, erased.stat, erased.op, erased.stored);
            removedOne = true;
            break;
        }
    }
}

// ---- the script-facing side ----

bool HandleEffectNative(SpellActor& actor, const skString& methodName, skRValueArray& args,
                        skRValue& returnValue) {
    ActorStats& stats = actor.actorStats();

    if (methodName == skString("AddEffect") && args.entries() >= 4) {
        // The dispatcher reads the four required arguments last, after
        // optionally picking up the name (arg 5) and the object (arg 6),
        // and takes the magnitude as a short. Order does not matter here;
        // the truncation does -- `Random(-2, -12)` in vermin_bomb.s is a
        // small negative and every shrine is a small positive, but the
        // 16-bit store is what the engine does with any of them.
        const int duration = args[0].intValue();
        const int stat = args[1].intValue();
        const int op = args[2].intValue();
        const int magnitude = static_cast<int16_t>(args[3].intValue());
        const int seconds = args.entries() > 4 ? args[4].intValue() : 0;
        std::string name;
        if (args.entries() > 5) {
            const skString text = args[5].str();
            name.assign(text.c_str(), text.c_str() + text.length());
            // The real buffer is a 32-byte descriptor: one length byte and
            // 31 characters.
            if (name.size() > 31) name.resize(31);
        }
        skiExecutable* obj = nullptr;
        if (args.entries() > 6 && args[6].type() == skRValue::T_Object) obj = args[6].obj();
        AddEffect(actor, duration, stat, op, magnitude, seconds, name, obj);
        return true;
    }

    if (methodName == skString("FindStringEffect") && args.entries() >= 2) {
        const skString text = args[1].str();
        const std::string name(text.c_str(), text.c_str() + text.length());
        returnValue = skRValue(FindStringEffect(stats, args[0].intValue(), name) != nullptr);
        return true;
    }

    if (methodName == skString("RemoveEffect") && args.entries() >= 2) {
        const skString text = args[1].str();
        const std::string name(text.c_str(), text.c_str() + text.length());
        RemoveEffect(actor, args[0].intValue(), name);
        return true;
    }

    if (methodName == skString("RemoveEnchantments") && args.entries() == 0) {
        RemoveEnchantments(actor);
        return true;
    }

    if (methodName == skString("SetSpellEffect") && args.entries() >= 2) {
        // Two stores and nothing else. The seven shipped call sites are all
        // potions (`GetOwner().SetSpellEffect(kind, units)`) and between them
        // use kinds 1, 2, 3, 5 and 9 -- none of which FUN_10049780, the
        // periodic tick, has a case for. They are pure durations, read
        // elsewhere: kind 4 makes the stats block's own DoDamage return
        // without applying anything at all (FUN_10049e78's first line) and
        // blocks weapon swaps, and kind 9 makes it skip damage whose source
        // is a creature's melee. Kinds 1/2/3/5 additionally each select a
        // line of HUD status text (FUN_1002c0e4's switch), and kind 3 adds
        // 2 to a radius two different range checks compute.
        stats.SetPeriodic(args[0].intValue(), args[1].intValue());
        return true;
    }

    // The three flag setters share one shape: a guard comparing the *whole*
    // flag word against the single bit (so "already exactly this and nothing
    // else" refuses the call outright), then either an OR plus a raw timer,
    // or a clear plus a zero timer.
    if (methodName == skString("SetBlindness") && args.entries() >= 1) {
        const bool on = args[0].boolValue();
        const int timer = args.entries() > 1 ? args[1].intValue() : 0;
        const int flags = stats.effectFlags();
        if (flags != ActorStats::kEffectFlagBlind || !on) {
            if (on) {
                stats.SetEffectFlagsRaw(flags | ActorStats::kEffectFlagBlind);
                stats.SetEffectTimerRaw(timer);
            } else {
                // The only one of the three whose clear is written
                // correctly -- a real `& 0xfffffffb`. cure_blindness_balm.s
                // is the sole caller, and it works.
                stats.SetEffectFlagsRaw(flags & ~ActorStats::kEffectFlagBlind);
                stats.SetEffectTimerRaw(0);
            }
        }
        return true;
    }
    if ((methodName == skString("SetPoisoned") || methodName == skString("SetDiseased")) &&
        args.entries() >= 1) {
        const int bit = methodName == skString("SetPoisoned") ? ActorStats::kEffectFlagPoison
                                                              : ActorStats::kEffectFlagDisease;
        if (stats.effectFlags() == bit) return true;  // the whole-word guard
        const bool on = args[0].boolValue();
        if (on) {
            stats.SetEffectFlagsRaw(stats.effectFlags() | bit);
            stats.SetEffectTimerRaw(args.entries() > 1 ? args[1].intValue() : 0);
        } else {
            stats.SetEffectTimerRaw(0);
            // The same mvn/and/mvn the AddEffect Blindness arm has: this
            // *sets* every bit but `bit` instead of clearing it. Neither
            // name has a call site in the corpus, so nothing has ever run
            // it; reproduced rather than corrected.
            stats.SetEffectFlagsRaw(~(~stats.effectFlags() & bit));
        }
        return true;
    }

    return false;
}

// FUN_1004b3c0 / FUN_1004b4f0, recorded because they are the other half of
// the `Equipped` duration and because neither is ever called:
//
//   FUN_1004b3c0(stats, obj)  -- the first *timed* node whose +0x10 is obj
//   FUN_1004b4f0(stats, obj)  -- find it that way, then unlink it from the
//                                *equipped* list, undo it, and free it
//
// So the intended "take the ring off, lose the bonus" path was written with
// its two halves reading different lists, and then never wired up: neither
// function has a single caller in the image, no shipped script passes
// `Equipped` to AddEffect, and nothing else ever pushes onto stats+0x64. The
// list, the object field on every node, and this pair are the complete
// remains of a feature that did not ship.

}  // namespace sk_bindings
