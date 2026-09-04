// M37 smoke test: the real magic to-hit model -- the caster stat behind
// every status effect's magnitude, and the resistance gate built on it.
//
// Everything asserted here is decompiled ground truth:
//   * FUN_1004bc60 / Character-stats binding 3 ("GetSpellToHit"):
//     `spellcast + 2 * willpower`
//   * FUN_1004bbd0 / Character-stats binding 4 ("GetSpellResistance"):
//     `magicResistance + willpower / 5` (a 0x66666667 magic multiply)
//   * FUN_100458e4's gate: `chance = (power << 16) / ((power + resist) *
//     0x100)`, applied as `chance == 0x100 || rand(0, 0x100) < chance`
//   * the magnitude source: the caster's stats block `+0x34`, which
//     FUN_10048244's GetLevel/SetLevel bindings identify as the character
//     level, clamped by `if (0x18 < mag) mag = 0x19`; with the spell's own
//     SetLevel (`spell+0x1d0`) as the scroll fallback
//   * the stats-block layout the two formulas read, recovered from
//     FUN_1004ad40's index switch cross-checked against FUN_10048244's
//     getters: +0x04 spellcast, +0x06 magic resistance, +0x1a willpower,
//     +0x34 level
//
// The point of asserting against real shipped scripts rather than
// hand-made numbers is that the magnitude change is only meaningful if
// real spells behave differently because of it.
#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>

#include "assets/string_table.h"
#include "simkin_bindings/combat.h"
#include "simkin_bindings/item_executable.h"
#include "simkin_bindings/menu_stack.h"
#include "simkin_bindings/monster_executable.h"
#include "simkin_bindings/player_executable.h"
#include "skExecutableContext.h"
#include "skInterpreter.h"
#include "skParseException.h"
#include "skRValue.h"
#include "skRValueArray.h"
#include "skRuntimeException.h"

namespace {

int g_failures = 0;

void Check(bool ok, const std::string& what) {
    std::printf("%-74s %s\n", what.c_str(), ok ? "OK" : "FAILED");
    if (!ok) ++g_failures;
}

}  // namespace

int main() {
    using Item = sk_bindings::ItemExecutable;
    using Monster = sk_bindings::MonsterExecutable;

    const char* scriptRoot =
        "The-Elder-Scrolls-Travels-Shadowkey_N-Gage_EN-FR-DE-ES-IT_USA-Europe-EnFrDeEsIt-26102004/"
        "system/apps/6r51";

    sk::StringTable strings;
    strings.Load(std::string(scriptRoot) + "/stringtable.txt");

    skInterpreter interpreter;
    sk_bindings::MenuStack stack(scriptRoot, interpreter, &strings);

    // Generic on purpose: these three receivers (player, monster, item)
    // have no common base with a method() this test could name.
    auto callInt = [&](auto* obj, const char* name, int value) {
        skRValueArray a;
        a.append(skRValue(value));
        skRValue r;
        skExecutableContext c(&interpreter);
        obj->method(skString(name), a, r, c);
    };
    auto monsterInt = [&](Monster& m, const char* name, int value) {
        callInt(&m, name, value);
    };
    auto loadMonster = [&](const std::string& rel) -> std::unique_ptr<Monster> {
        skExecutableContext loadCtxt(&interpreter);
        try {
            auto m = std::make_unique<Monster>(
                skString((std::string(scriptRoot) + "/" + rel).c_str()), loadCtxt, &strings,
                stack.player(), stack);
            skRValueArray args;
            args.append(skRValue(0));
            skRValue ret;
            skExecutableContext callCtxt(&interpreter);
            m->method(skString("Init"), args, ret, callCtxt);
            return m;
        } catch (skParseException&) {
            return nullptr;
        } catch (skRuntimeException&) {
            return nullptr;
        }
    };
    auto loadSpell = [&](const std::string& rel) -> std::unique_ptr<Item> {
        skExecutableContext loadCtxt(&interpreter);
        auto spell =
            std::make_unique<Item>(skString((std::string(scriptRoot) + "/" + rel).c_str()),
                                    loadCtxt, stack);
        skRValueArray args;
        args.append(skRValue(0));
        skRValue ret;
        skExecutableContext callCtxt(&interpreter);
        spell->method(skString("Init"), args, ret, callCtxt);
        return spell;
    };
    auto hit = [&](Item& spell, Monster& victim) {
        skRValueArray args;
        args.append(skRValue(static_cast<skiExecutable*>(&victim), false));
        skRValue ret;
        skExecutableContext ctxt(&interpreter);
        spell.method(skString("DoAttackRoll"), args, ret, ctxt);
    };

    std::printf("=== M37: the real magic to-hit model ===\n\n");

    // ---- 1. The two stat formulas, as pure arithmetic ----
    {
        using namespace sk_bindings;
        Check(SpellToHit(30, 50) == 130, "GetSpellToHit is spellcast + 2*willpower (30, 50 -> 130)");
        Check(SpellResistance(12, 50) == 22,
              "GetSpellResistance is magicResistance + willpower/5 (12, 50 -> 22)");
        // The /5 is a magic multiply in the binary, so its truncation is
        // worth pinning at a non-multiple.
        Check(SpellResistance(0, 49) == 9 && SpellResistance(0, 4) == 0,
              "...and that division truncates (49/5 -> 9, 4/5 -> 0), matching the shift form");
    }

    // ---- 2. The gate's arithmetic, on real creature resistances ----
    {
        using namespace sk_bindings;
        auto rat = loadMonster("monsters/Azra_Rat.s");
        auto lakvan = loadMonster("monsters/lakvan.s");
        Check(rat && lakvan, "real Azra_Rat.s and lakvan.s load");
        if (rat && lakvan) {
            // No creature script in the whole corpus calls SetWillpower
            // (the only two call sites are on the player), so a creature's
            // resistance is exactly its SetMagicResistance().
            Check(rat->spellResistance() == rat->magicResistance() &&
                      lakvan->spellResistance() == lakvan->magicResistance(),
                  "a creature resists with its SetMagicResistance() alone -- no willpower term");

            int power = stack.player().spellToHit();
            Check(power == sk_bindings::SpellToHit(50, stack.player().willpower()),
                  "the player's own to-hit comes from the same formula, not a stored number");

            int vsRat = SpellHitChance(power, rat->spellResistance());
            int vsLakvan = SpellHitChance(power, lakvan->spellResistance());
            // M43: the rat's resistance is no longer the 3 its script
            // literally sets -- its Init() ends with SetMob(4), which
            // overwrites the whole stat block from a zone-scaled template
            // (monster_executable.h's ApplyMobTemplate). lakvan.s has no
            // SetMob, so its 45 stands. The point of the model survives the
            // change intact: the boss is still meaningfully harder.
            Check(vsRat == (power << 16) / ((power + rat->spellResistance()) * 0x100) &&
                      vsRat > vsLakvan && rat->spellResistance() < 45,
                  "chance = power*256/(power+resistance), and Lakvan (45) resists far more than a "
                  "rat");
            Check(SpellHitChance(power, 0) == 0x100,
                  "an unresisting target yields exactly 0x100 -- the engine's own certain-hit case");
            Check(sk_bindings::RollSpellHit(power, 0),
                  "...which RollSpellHit() special-cases, so it can never miss");
            Check(SpellHitChance(0, 10) == 0 && !sk_bindings::RollSpellHit(0, 10),
                  "a caster with no power at all can never land anything");
        }
    }

    // ---- 3. The magnitude source: the caster's level ----
    //
    // The old substitution was the spell's own SetRating(). Reading the
    // whole spells/ directory is what killed it: the values are a dense
    // alphabetical run with duplicates (absorb 1, blind 3, bodytomind 5,
    // disease 5, ... azrasustenance 28) and the scroll/unique variants
    // don't set it at all.
    {
        auto absorb = loadSpell("spells/Absorb.s");
        Check(absorb->rating() == 1 && absorb->spellLevel() == 0,
              "real spells/Absorb.s has SetRating(1) and no SetLevel at all");

        // Absorb is the branch with no variance (min == max == mag + 12),
        // which makes it the cleanest probe for what magnitude actually is.
        auto castAtLevel = [&](int level) -> int {
            callInt(&stack.player(), "SetLevel", level);
            auto v = loadMonster("monsters/lakvan.s");
            monsterInt(*v, "SetMagicResistance", 0);  // certain hit, see part 2
            int hp0 = v->currentHealth();
            hit(*absorb, *v);
            return hp0 - v->currentHealth();
        };
        int atOne = castAtLevel(1);
        int atTwelve = castAtLevel(12);
        Check(atOne == 13 && atTwelve == 24,
              "Absorb's damage is casterLevel + 12 -- it moves with the caster, not the spell");

        // The clamp: `if (0x18 < mag) mag = 0x19`.
        Check(castAtLevel(25) == 37 && castAtLevel(99) == 37,
              "...and the caster's level is clamped to 25, so a level-99 caster gains nothing");
    }

    // ---- 4. The scroll fallback ----
    //
    // `if (!scroll && (caster == null || casterIsCharacter)) mag =
    // casterLevel; else mag = spell->level`. spells\IgniteScroll.s is a
    // real three-line wrapper: SetLevel(8), SetSpellType(4024), then
    // RunScript into IgniteFoe.
    {
        auto scroll = loadSpell("spells/IgniteScroll.s");
        Check(scroll->spellLevel() == 8 && scroll->spellType() == 4024,
              "real spells/IgniteScroll.s carries SetLevel(8) and SetSpellType(4024)");
        Check(scroll->statusEffect() == Item::kEffectIgniteFoe,
              "...and its SetSpellType resolves it to IgniteFoe's own branch directly");

        callInt(&stack.player(), "SetLevel", 25);
        callInt(scroll.get(), "SetScroll", 1);
        auto v = loadMonster("monsters/lakvan.s");
        monsterInt(*v, "SetMagicResistance", 0);
        int hp0 = v->currentHealth();
        hit(*scroll, *v);
        int dealt = hp0 - v->currentHealth();
        // Magnitude 8 (the scroll's), not 25 (the caster's): the roll is
        // (8+1)*2 .. (8+1)*5 == 18..45, well clear of a level-25 caster's
        // (25+1)*2 .. (25+1)*5 == 52..130.
        Check(dealt >= 18 && dealt <= 45,
              "a scroll casts at the scroll's own level even for a capped caster");
    }

    // ---- 5. The four damage-only branches M37 adds ----
    {
        callInt(&stack.player(), "SetLevel", 10);

        auto blaze = loadSpell("blaze.s");
        Check(blaze->statusEffect() == Item::kEffectBlaze,
              "real blaze.s (entities.txt typeId 50) is a real dispatcher branch, not a fallback");
        {
            auto v = loadMonster("monsters/lakvan.s");
            monsterInt(*v, "SetMagicResistance", 0);
            int hp0 = v->currentHealth();
            hit(*blaze, *v);
            int dealt = hp0 - v->currentHealth();
            // `min = 3, max = level*3 + 3` == 3..33 at level 10.
            Check(dealt >= 3 && dealt <= 33,
                  "...rolling 3 .. casterLevel*3+3, the real branch's own pair");
        }

        auto deathHowl = loadSpell("spells/DeathHowl.s");
        Check(deathHowl->statusEffect() == Item::kEffectDeathHowl,
              "real spells/DeathHowl.s (4035) resolves to its own branch");
        {
            auto v = loadMonster("monsters/lakvan.s");
            monsterInt(*v, "SetMagicResistance", 0);
            int hp0 = v->currentHealth();
            hit(*deathHowl, *v);
            int dealt = hp0 - v->currentHealth();
            // `r = rand(1,10); dmg = r*8 + 1 + magnitude` == 19..91.
            Check(dealt >= 1 * 8 + 1 + 10 && dealt <= 10 * 8 + 1 + 10,
                  "...dealing r*8 + 1 + casterLevel for a 1..10 roll of r");
        }

        auto doomHammer = loadSpell("spells/DoomHammer.s");
        Check(doomHammer->statusEffect() == Item::kEffectDoomHammer,
              "real spells/DoomHammer.s (4012/4017) resolves to its own branch");
        {
            auto v = loadMonster("monsters/lakvan.s");
            monsterInt(*v, "SetMagicResistance", 0);
            int hp0 = v->currentHealth();
            hit(*doomHammer, *v);
            int dealt = hp0 - v->currentHealth();
            // `r = rand(1,10); dmg = magnitude*r + r` == 11..110.
            Check(dealt >= 10 * 1 + 1 && dealt <= 10 * 10 + 10,
                  "...dealing casterLevel*r + r, so its variance is in r, not a min..max roll");
        }

        // DeadToDust is the one branch with a target precondition: the
        // real code returns immediately unless the target is undead
        // (monster+0x2d0 == 2, the field SetUndead writes and IsUndead
        // reads).
        auto deadToDust = loadSpell("spells/DeadToDust.s");
        Check(deadToDust->statusEffect() == Item::kEffectDeadToDust,
              "real spells/DeadToDust.s (4002) resolves to its own branch");
        {
            auto living = loadMonster("monsters/lakvan.s");
            monsterInt(*living, "SetMagicResistance", 0);
            int hp0 = living->currentHealth();
            hit(*deadToDust, *living);
            bool spared = living->currentHealth() == hp0 && !living->undead();

            monsterInt(*living, "SetUndead", 1);
            hit(*deadToDust, *living);
            int dealt = hp0 - living->currentHealth();
            Check(spared && living->undead() && dealt >= (10 + 1) * 2 && dealt <= (10 + 1) * 5,
                  "DeadToDust does nothing to the living and (mag+1)*2..(mag+1)*5 to the undead");
        }
    }

    // ---- 6. The gate really does gate the *status* half too ----
    //
    // This is the behavioural difference from the flat-subtraction model
    // the port used before: a resisted Poison used to still poison (just
    // for less), where the real dispatcher does all its status work inside
    // the hit branch. Asserted statistically, since the gate is a roll:
    // a caster with 1 power against a 10,000-resistance target should land
    // essentially nothing.
    {
        callInt(&stack.player(), "SetLevel", 10);
        callInt(&stack.player(), "SetSpellcast", 1);
        callInt(&stack.player(), "SetWillpower", 0);
        auto poison = loadSpell("spells/Poison.s");
        int poisoned = 0;
        for (int i = 0; i < 200; ++i) {
            auto v = loadMonster("monsters/Azra_Rat.s");
            if (!v) break;
            monsterInt(*v, "SetMagicResistance", 10000);
            hit(*poison, *v);
            if (v->poisoned()) ++poisoned;
        }
        Check(poisoned == 0,
              "an overwhelmingly resisted Poison applies no poison at all, not a weakened one");
    }

    std::printf("\nm37_spellpower_smoke: %s%s\n", g_failures ? "FAILED" : "PASSED",
                g_failures ? "" : " (all checks)");
    return g_failures ? 1 : 0;
}
