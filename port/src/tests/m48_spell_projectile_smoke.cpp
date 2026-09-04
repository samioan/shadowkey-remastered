// M48 smoke test: the rest of a real cast, and the projectile it launches.
//
// docs/PORT_ROADMAP.md's open bullet: "the real cast deducts magicka,
// applies the self-targeted spells inline ... and for offensive spells
// spawns a 0x198-byte projectile whose *impact* is what calls the status
// dispatcher. This port calls HitTarget() directly on both sides instead,
// so spells are hitscan and the self-targeted half of the spell list does
// nothing."
//
// Four decompiled functions close it:
//
//   * FUN_10046764 -- the cast. A 200-line branch tree over the spell's
//     entities.txt typeId producing a magicka cost, a sound slot, a
//     projectile decision and, for the spells that have no projectile, a
//     self-targeted effect applied inline.
//   * FUN_1005f0b4 / FUN_1005f928 / FUN_1005f3c8 -- the projectile's
//     spawn, its twelve-tick flight and its impact.
//   * FUN_10049780's kinds 6 and 7 -- the two periodic-channel arms
//     WORLD_MODEL.md recorded as "set somewhere else, not yet found". The
//     somewhere else is the cast.
//   * FUN_10046680 -- the cast cooldown, which turns out to be a
//     Sanctuary-only rule.
//
// Part 1 is the important one: it reads the shipped scripts and checks the
// whole cast table against them, rather than restating the numbers this
// port already contains.
#include <cstdio>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "assets/string_table.h"
#include "simkin_bindings/actor_stats.h"
#include "simkin_bindings/game_constants.h"
#include "simkin_bindings/item_executable.h"
#include "simkin_bindings/level_executable.h"
#include "simkin_bindings/menu_stack.h"
#include "simkin_bindings/monster_executable.h"
#include "simkin_bindings/player_executable.h"
#include "simkin_bindings/spell_cast.h"
#include "simkin_bindings/spell_projectile.h"
#include "skExecutableContext.h"
#include "skInterpreter.h"
#include "skParseException.h"
#include "skRValue.h"
#include "skRValueArray.h"
#include "skRuntimeException.h"
#include "world/entity_types.h"

namespace {

int g_failures = 0;

void Check(bool ok, const std::string& what) {
    std::printf("%-76s %s\n", what.c_str(), ok ? "OK" : "FAILED");
    if (!ok) ++g_failures;
}

std::string ReadFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::string();
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

}  // namespace

int main(int argc, char** argv) {
    using Item = sk_bindings::ItemExecutable;
    using Monster = sk_bindings::MonsterExecutable;
    using Stats = sk_bindings::ActorStats;

    const char* scriptRoot =
        argc > 1 ? argv[1]
                  : "The-Elder-Scrolls-Travels-Shadowkey_N-Gage_EN-FR-DE-ES-IT_USA-Europe-"
                    "EnFrDeEsIt-26102004/system/apps/6r51";

    sk::StringTable strings;
    strings.Load(std::string(scriptRoot) + "/stringtable.eng");
    sk::EntityTypeTable entityTypes;
    if (!entityTypes.Load(scriptRoot)) {
        std::printf("m48_spell_projectile_smoke: FAILED to load entities.txt\n");
        return 1;
    }

    skInterpreter interpreter;
    sk_bindings::MenuStack stack(scriptRoot, interpreter, &strings);
    stack.level().SetEntityTypes(&entityTypes);

    auto loadItem = [&](const std::string& rel) -> std::unique_ptr<Item> {
        skExecutableContext loadCtxt(&interpreter);
        try {
            auto item = std::make_unique<Item>(
                skString((std::string(scriptRoot) + "/" + rel).c_str()), loadCtxt, stack);
            skRValueArray args;
            args.append(skRValue(0));
            skRValue ret;
            skExecutableContext callCtxt(&interpreter);
            item->method(skString("Init"), args, ret, callCtxt);
            return item;
        } catch (skParseException& e) {
            std::printf("  (parse error loading %s: %s)\n", rel.c_str(), e.toString().ptr());
            return nullptr;
        } catch (skRuntimeException& e) {
            std::printf("  (runtime error loading %s: %s)\n", rel.c_str(), e.toString().ptr());
            return nullptr;
        }
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
    auto callInt = [&](auto* obj, const char* name, int value) {
        skRValueArray a;
        a.append(skRValue(value));
        skRValue r;
        skExecutableContext c(&interpreter);
        obj->method(skString(name), a, r, c);
    };

    std::printf("=== M48: the rest of a real cast, and its projectile ===\n\n");

    // ---------------------------------------------------------------
    // Part 1. The cast table against the shipped scripts.
    // ---------------------------------------------------------------
    std::printf("-- 1. the branch tree, checked against the shipped corpus --\n");
    {
        // Every spell in the game, by the path the port resolves typeIds
        // from. `hasHitTarget` is read out of the file itself.
        struct SpellFile {
            const char* path;
            int typeId;
        };
        static const SpellFile kSpells[] = {
            {"blaze.s", 50},
            {"HealWound.s", 51},
            {"spells/DeadToDust.s", 4002},
            {"spells/Weakness.s", 4008},
            {"spells/Absorb.s", 4009},
            {"spells/Blind.s", 4010},
            {"spells/RaiseStrength.s", 4011},
            {"spells/DoomHammer.s", 4012},
            {"spells/BodyToMind.s", 4013},
            {"spells/CureDisease.s", 4014},
            {"spells/CurePoison.s", 4015},
            {"spells/DaedricWeapon.s", 4016},
            {"spells/Drain.s", 4018},
            {"spells/Energize.s", 4019},
            {"spells/Fear.s", 4020},
            {"spells/FeebleBlade.s", 4021},
            {"spells/Frenzy.s", 4022},
            {"spells/HarmArmor.s", 4023},
            {"spells/IgniteFoe.s", 4024},
            {"spells/Paralyze.s", 4025},
            {"spells/RemoveEnchantment.s", 4026},
            {"spells/Righteousness.s", 4027},
            {"spells/Sanctuary.s", 4028},
            {"spells/Shield.s", 4029},
            {"spells/Disease.s", 4033},
            {"spells/Poison.s", 4034},
            {"spells/DeathHowl.s", 4035},
            {"spells/AzraWrath.s", 4038},
            {"spells/AzraSustenance.s", 4039},
        };
        int checked = 0, projectileSpells = 0, selfSpells = 0, mismatches = 0;
        for (const SpellFile& row : kSpells) {
            std::string text = ReadFile(std::string(scriptRoot) + "/" + row.path);
            if (text.empty()) continue;
            ++checked;
            const bool hasHitTarget = text.find("HitTarget[") != std::string::npos;
            const sk_bindings::SpellCastRule rule = sk_bindings::SpellCastRuleFor(row.typeId);
            const bool hasProjectile = rule.projectileSprite != 0;
            if (hasProjectile) {
                ++projectileSpells;
            } else {
                ++selfSpells;
            }
            // AzraWrath is the one documented exception: it has a
            // HitTarget handler but its damage comes from the native
            // area-of-effect branch, so the cast gives it no projectile.
            const bool expected = (row.typeId == 4038) ? !hasProjectile : (hasProjectile ==
                                                                            hasHitTarget);
            if (!expected) {
                std::printf("  MISMATCH %s: HitTarget=%d sprite=%d\n", row.path,
                            hasHitTarget ? 1 : 0, rule.projectileSprite);
                ++mismatches;
            }
        }
        Check(checked == 29, "all 29 shipped spell scripts read off disk");
        Check(mismatches == 0,
              "every spell with a projectile has a HitTarget handler, and vice versa");
        Check(projectileSpells == 15 && selfSpells == 14,
              "...15 projectile spells against 14 purely self-targeted ones");
        // The greater blaze (4006) shares blaze.s and is the sixteenth
        // projectile row; it has no file of its own to count above.
        Check(sk_bindings::SpellCastRuleFor(4006).projectileSprite == 2 &&
                  sk_bindings::SpellCastRuleFor(4017).projectileSprite == 2,
              "...plus 4006 and 4017, the two typeIds that share a script with another");
    }
    {
        // The three typeIds a script states outright, read back through the
        // port's own resolution.
        auto heal = loadItem("spells/U_Heal_Wound_Lvl10.s");
        auto frenzy = loadItem("spells/U_Frenzy_lvl10.s");
        auto blaze = loadItem("spells/U_Blaze_lvl5.s");
        Check(heal && heal->spellTypeId() == 51,
              "U_Heal_Wound_Lvl10.s's own SetSpellType(51) confirms HealWound's row");
        Check(frenzy && frenzy->spellTypeId() == 4022,
              "U_Frenzy_lvl10.s's SetSpellType(4022) confirms Frenzy's");
        Check(blaze && blaze->spellTypeId() == 50,
              "U_Blaze_lvl5.s's SetSpellType(50) confirms blaze's");
        Check(heal && heal->spellLevel() == 10 && blaze && blaze->spellLevel() == 5,
              "...and both carry the SetLevel their filename advertises");
    }
    {
        // The sound split. 0x54 is pl_cast_fire.wav and 0x57 is
        // pl_cast_powerup.wav in every shipped zone manifest, and the table
        // hands them out accordingly.
        int fireWithoutProjectile = 0, powerUpWithProjectile = 0;
        static const int kAll[] = {50,   51,   4002, 4006, 4008, 4009, 4010, 4011,
                                    4012, 4013, 4014, 4015, 4016, 4017, 4018, 4019,
                                    4020, 4021, 4022, 4023, 4024, 4025, 4026, 4027,
                                    4028, 4029, 4033, 4034, 4035, 4038, 4039};
        for (int typeId : kAll) {
            const sk_bindings::SpellCastRule rule = sk_bindings::SpellCastRuleFor(typeId);
            if (rule.soundId == sk_bindings::kCastSoundPowerUp && rule.projectileSprite != 0) {
                ++powerUpWithProjectile;
            }
            if (rule.soundId == sk_bindings::kCastSoundFire && rule.projectileSprite == 0) {
                ++fireWithoutProjectile;
            }
        }
        Check(powerUpWithProjectile == 0,
              "no spell plays pl_cast_powerup.wav and also launches a projectile");
        Check(fireWithoutProjectile == 2,
              "...and only DaedricWeapon and AzraWrath play pl_cast_fire.wav without one");
        Check(sk_bindings::SpellCastRuleFor(51).soundId == sk_bindings::kCastSoundHeal,
              "HealWound alone uses slot 0x55, which is NULL.wav in all 21 zones");
    }
    {
        // A handful of specific rows, and the default arm.
        const sk_bindings::SpellCastRule doom = sk_bindings::SpellCastRuleFor(4012);
        const sk_bindings::SpellCastRule greater = sk_bindings::SpellCastRuleFor(4006);
        const sk_bindings::SpellCastRule remove = sk_bindings::SpellCastRuleFor(4026);
        const sk_bindings::SpellCastRule none = sk_bindings::SpellCastRuleFor(4030);
        Check(doom.costBonus == 30 && !doom.costIsFixed,
              "DoomHammer is the most expensive level-scaled spell at level+30");
        Check(greater.costIsFixed && greater.costBonus == 0 && greater.impactDamage == 50,
              "the greater blaze is free and carries a flat 50 damage on its projectile");
        Check(remove.costIsFixed && remove.costBonus == 12,
              "RemoveEnchantment is the only flat non-zero cost, 12");
        Check(none.unknown && none.costBonus == 6 && none.projectileSprite == 0,
              "a typeId with no case takes the default arm: level+6, no projectile");
    }
    {
        auto sanctuary = loadItem("spells/Sanctuary.s");
        Check(sanctuary && sanctuary->refireRate() == 768,
              "Sanctuary.s's own SetRefireRate(768) -- '//3 secs', i.e. 3 * 0x100");
        auto blaze = loadItem("blaze.s");
        Check(blaze && blaze->refireRate() == 0,
              "...and every other spell leaves it 0, which makes the cooldown inert");
    }

    // ---------------------------------------------------------------
    // Part 2. The cast itself.
    // ---------------------------------------------------------------
    std::printf("\n-- 2. FUN_10046764: cost, gate and magnitude --\n");
    sk_bindings::PlayerExecutable& player = stack.player();
    callInt(&player, "SetLevel", 5);
    callInt(&player, "SetMagicka", 50);
    callInt(&player, "SetFatigue", 100);
    callInt(&player, "SetHealth", 100);
    {
        auto blaze = loadItem("blaze.s");
        sk_bindings::SpellCastResult cast = sk_bindings::CastSpell(*blaze, player);
        Check(cast.cast && cast.magickaSpent == 11 && player.actorMagicka() == 39,
              "a level-5 player's blaze costs 11 magicka (level + 6) and it is deducted");
        Check(cast.projectileSprite == 2 && cast.soundId == sk_bindings::kCastSoundFire,
              "...and it asks for sprite-2 art and the fire sound");
        Check(cast.magnitude == 5,
              "...at the caster's own level, which is the magnitude every branch scales with");
    }
    {
        auto doom = loadItem("spells/DoomHammer.s");
        player.SetActorMagicka(10);
        sk_bindings::SpellCastResult cast = sk_bindings::CastSpell(*doom, player);
        Check(!cast.cast && player.actorMagicka() == 10,
              "DoomHammer at 35 magicka refuses at 10 and takes nothing");
        player.SetActorMagicka(50);
    }
    {
        // A scroll: free, and its power is its own.
        auto scroll = loadItem("spells/DeadToDust.s");
        callInt(scroll.get(), "SetLevel", 9);
        skRValueArray a;
        a.append(skRValue(true));
        skRValue r;
        skExecutableContext c(&interpreter);
        scroll->method(skString("SetScroll"), a, r, c);
        player.SetActorMagicka(0);
        sk_bindings::SpellCastResult cast = sk_bindings::CastSpell(*scroll, player);
        Check(cast.cast && cast.magickaSpent == 0,
              "a scroll costs nothing and casts with an empty magicka pool");
        Check(cast.magnitude == 9,
              "...at the scroll's own SetLevel, not the reader's level");
        Check(cast.consumeScroll, "...and is destroyed by the reading");
        player.SetActorMagicka(50);
    }
    {
        // A creature: no gate, and the magnitude is the spell's SetLevel.
        auto floater = loadMonster("monsters/shriekfloater.s");
        if (floater) {
            Monster::CastAttempt attempt;
            for (int i = 0; i < 200 && !attempt.result.cast; ++i) {
                attempt = floater->CastSpellAt(&player);
            }
            Check(attempt.result.cast && floater->actorMagicka() == 0,
                  "a creature casts with no magicka at all -- the gate skips it entirely");
            Check(attempt.result.magnitude == 7,
                  "...at its Disease's own SetLevel(7), the creature-caster rule");
        }
    }
    {
        // The cooldown, which only Sanctuary ever arms.
        auto sanctuary = loadItem("spells/Sanctuary.s");
        auto blaze = loadItem("blaze.s");
        sk_bindings::SpellCastCooldown cooldown;
        // The two clauses are not symmetric, and the asymmetry is real: the
        // general one is guarded on "something has actually been cast"
        // (`+0xfc4 != 0`), the Sanctuary one is not. So at a fresh clock
        // Sanctuary alone is refused, and stays refused for its own refire
        // rate -- the first three seconds in a level, you cannot cast it.
        Check(!sk_bindings::SpellCastAllowed(*sanctuary, cooldown, 0),
              "at a zero clock Sanctuary is refused: its clause has no 'never cast' guard");
        Check(sk_bindings::SpellCastAllowed(*sanctuary, cooldown, 768),
              "...and opens three seconds into the level");
        Check(sk_bindings::SpellCastAllowed(*blaze, cooldown, 0),
              "every other spell is free at a zero clock, because its clause is guarded");
        sk_bindings::NoteSpellCast(cooldown, 1000);
        Check(!sk_bindings::SpellCastAllowed(*sanctuary, cooldown, 1500),
              "Sanctuary's 768-unit refire rate blocks a re-cast 500 units later");
        Check(sk_bindings::SpellCastAllowed(*sanctuary, cooldown, 1768),
              "...and opens again exactly 3 seconds after the last cast");
        Check(sk_bindings::SpellCastAllowed(*blaze, cooldown, 1001),
              "a spell with no refire rate is never gated, even one tick after a cast");
    }

    // ---------------------------------------------------------------
    // Part 3. The self-targeted half -- the spells that did nothing.
    // ---------------------------------------------------------------
    std::printf("\n-- 3. the self-targeted spells, applied inline --\n");
    auto tickPlayer = [&](int seconds) {
        const int ticks = seconds * 256 / sk_bindings::kAiFrameDeltaUnits + 1;
        for (int i = 0; i < ticks; ++i) {
            player.TickStatusEffects(sk_bindings::kAiFrameDeltaUnits);
        }
    };
    auto clearPlayer = [&]() {
        // Outlast every duration this part arms.
        tickPlayer(600);
        callInt(&player, "SetMagicka", 50);
        callInt(&player, "SetFatigue", 100);
        callInt(&player, "SetHealth", 100);
    };
    {
        clearPlayer();
        callInt(&player, "SetHealth", 40);
        auto heal = loadItem("HealWound.s");
        sk_bindings::CastSpell(*heal, player);
        Check(player.actorHealth() == 40 + 6 + 5 * 2,
              "HealWound restores 6 + magnitude*2, so 16 at level 5");
        callInt(&player, "SetHealth", 98);
        sk_bindings::CastSpell(*heal, player);
        Check(player.actorHealth() == 100, "...clamped by the real SetHealth, never overhealing");
    }
    {
        clearPlayer();
        callInt(&player, "SetMagicka", 10);
        callInt(&player, "SetFatigue", 60);
        auto body = loadItem("spells/BodyToMind.s");
        sk_bindings::CastSpell(*body, player);
        // The 3-magicka cost comes off first, then all 60 fatigue goes in.
        Check(player.actorFatigue() == 0 && player.actorMagicka() == 50,
              "BodyToMind pours every point of fatigue into magicka, clamped at the maximum");
    }
    {
        clearPlayer();
        callInt(&player, "SetFatigue", 95);
        auto energize = loadItem("spells/Energize.s");
        sk_bindings::CastSpell(*energize, player);
        Check(player.actorStats().periodicKind() == Stats::kPeriodicFatigueRegen,
              "Energize arms periodic kind 6 -- one of WORLD_MODEL's two unfound arms");
        const int before = player.actorFatigue();
        tickPlayer(1);
        Check(player.actorFatigue() == before + 5,
              "...and a second of it adds the caster's own level to fatigue");
        tickPlayer(2);
        Check(player.actorFatigue() > player.actorMaxFatigue(),
              "...with no ceiling at all, which is exactly what the real branch omits");
    }
    {
        clearPlayer();
        auto energize = loadItem("spells/Energize.s");
        callInt(&player, "SetLevel", 13);
        sk_bindings::CastSpell(*energize, player);
        Check(player.actorStats().periodicRemaining() < 0,
              "at level 13 the 16-bit duration store wraps negative -- a real, reproduced bug");
        callInt(&player, "SetLevel", 5);
    }
    {
        clearPlayer();
        callInt(&player, "SetHealth", 50);
        auto sustenance = loadItem("spells/AzraSustenance.s");
        sk_bindings::CastSpell(*sustenance, player);
        Check(player.actorStats().periodicKind() == Stats::kPeriodicHealthRegen,
              "AzraSustenance arms periodic kind 7 -- the other one");
        tickPlayer(2);
        Check(player.actorHealth() == 60, "...regenerating the caster's level every second");
    }
    {
        // The shared-field quirk M34 first documented, reached from a new
        // direction: arming a regeneration overwrites the poison's own
        // damage-per-tick, because they are the same short.
        clearPlayer();
        player.actorStats().ApplyEffectFlag(Stats::kEffectFlagPoison, 3, 30);
        Check(player.actorStats().dotAmount() == 3, "a poison ticks for 3");
        auto energize = loadItem("spells/Energize.s");
        sk_bindings::CastSpell(*energize, player);
        Check(player.actorStats().dotAmount() == 1,
              "...and casting Energize drops it to 1, because +0x76 is one field");
    }
    {
        clearPlayer();
        player.actorStats().ApplyEffectFlag(Stats::kEffectFlagPoison, 3, 30);
        auto cure = loadItem("spells/CurePoison.s");
        sk_bindings::CastSpell(*cure, player);
        Check(!player.actorStats().poisoned(), "CurePoison clears the poison flag");
    }
    {
        clearPlayer();
        player.actorStats().ApplyStatModifier(Stats::kStatAttack, -3, 30);
        player.actorStats().ApplyStatModifier(Stats::kStatDefense, -3, 30);
        auto cure = loadItem("spells/CureDisease.s");
        sk_bindings::CastSpell(*cure, player);
        Check(player.actorStats().statModifier(Stats::kStatAttack) == 0 &&
                  player.actorStats().statModifier(Stats::kStatDefense) == 0,
              "CureDisease removes exactly the two modifiers Disease applies");
    }
    {
        clearPlayer();
        player.actorStats().ApplyStatModifier(Stats::kStatAttack, -10, 30);   // a Drain
        player.actorStats().ApplyStatModifier(Stats::kStatArmor, 6, 30);      // a Shield
        auto remove = loadItem("spells/RemoveEnchantment.s");
        sk_bindings::CastSpell(*remove, player);
        Check(player.actorStats().statModifier(Stats::kStatAttack) == 0 &&
                  player.actorStats().statModifier(Stats::kStatArmor) == 6,
              "RemoveEnchantment strips the debuffs and leaves the buffs standing");
    }
    {
        clearPlayer();
        auto righteous = loadItem("spells/Righteousness.s");
        sk_bindings::CastSpell(*righteous, player);
        Check(player.actorStats().statModifier(Stats::kStatAttack) == 5 &&
                  player.actorStats().statModifier(Stats::kStatArmor) == 5,
              "Righteousness is the only branch that modifies two stats at once");
        clearPlayer();
        auto shield = loadItem("spells/Shield.s");
        sk_bindings::CastSpell(*shield, player);
        Check(player.actorStats().statModifier(Stats::kStatArmor) == 10,
              "Shield gives magnitude*2 armour");
        clearPlayer();
        auto frenzy = loadItem("spells/Frenzy.s");
        sk_bindings::CastSpell(*frenzy, player);
        Check(player.actorStats().statModifier(Stats::kStatAttack) == 5,
              "Frenzy gives magnitude attack");
    }
    {
        clearPlayer();
        auto sanctuary = loadItem("spells/Sanctuary.s");
        sk_bindings::CastSpell(*sanctuary, player);
        const int health = player.actorHealth();
        Check(player.actorStats().periodicKind() == Stats::kPeriodicSanctuaryTimer,
              "Sanctuary arms periodic kind 4");
        tickPlayer(3);
        Check(player.actorHealth() == health,
              "...which FUN_10049780 gives no tick behaviour at all -- it is a bare duration");
    }
    {
        clearPlayer();
        auto wrath = loadItem("spells/AzraWrath.s");
        sk_bindings::SpellCastResult cast = sk_bindings::CastSpell(*wrath, player);
        Check(cast.areaDamage == 20 && cast.projectileSprite == 0,
              "AzraWrath is an area effect, not a projectile: magnitude*4 to everything");
        Check(wrath->refireRate() == 400,
              "...and it writes its own refire rate as it fires, the only runtime writer");
    }
    {
        clearPlayer();
        auto daedric = loadItem("spells/DaedricWeapon.s");
        sk_bindings::SpellCastResult cast = sk_bindings::CastSpell(*daedric, player);
        Check(cast.conjureTypeId == sk_bindings::kConjuredWeaponTypeId,
              "DaedricWeapon asks for entity 4037, weapons\\DaedricSword.s");
        std::unique_ptr<Item> sword = stack.level().CreateItem(cast.conjureTypeId, false);
        Check(sword && sword->itemType() == sk_bindings::kItemTypeWeapon,
              "...which loads as a real weapon, category 16 and all");
    }

    // ---------------------------------------------------------------
    // Part 4. The projectile.
    // ---------------------------------------------------------------
    std::printf("\n-- 4. FUN_1005f928: twelve ticks of flight --\n");
    {
        // Facing +x in this port's own convention.
        auto blaze = loadItem("blaze.s");
        sk_bindings::SpellProjectile shot = sk_bindings::SpawnSpellProjectile(
            blaze.get(), &player, 1000, 2000, 100, sk_bindings::EngineYawFromPortYaw(0.0f), 0, 2,
            0);
        Check(shot.x == 1000 && shot.y == 2000 && shot.z == 100 + 0x20,
              "the spawn takes the caster's position and lifts it 0x20");
        Check(shot.vx == 256 && shot.vy == 0,
              "a heading of +x gives a full 0x100 of velocity on x and none on y");
        sk_bindings::SpellProjectile north = sk_bindings::SpawnSpellProjectile(
            blaze.get(), &player, 0, 0, 0,
            sk_bindings::EngineYawFromPortYaw(1.5707963f /* +y */), 0, 2, 0);
        Check(north.vx == 0 && north.vy == 256,
              "...and a quarter turn puts it all on y, so the two conventions line up");
    }
    {
        // Twelve tiles and no further.
        auto blaze = loadItem("blaze.s");
        sk_bindings::SpellProjectile shot = sk_bindings::SpawnSpellProjectile(
            blaze.get(), &player, 0, 5000, 0, sk_bindings::EngineYawFromPortYaw(0.0f), 0, 2, 0);
        auto never = [](int, int) { return false; };
        std::vector<sk_bindings::ProjectileTarget> nobody;
        int ticks = 0;
        while (shot.alive && ticks < 100) {
            sk_bindings::TickSpellProjectile(shot, 200, 200, never, nobody);
            ++ticks;
        }
        Check(ticks == 13, "an unobstructed projectile dies on its thirteenth tick");
        Check(shot.x == 13 * 256,
              "...having covered thirteen tiles, twelve of them with collision live");
    }
    {
        // A wall stops it.
        auto blaze = loadItem("blaze.s");
        sk_bindings::SpellProjectile shot = sk_bindings::SpawnSpellProjectile(
            blaze.get(), &player, 0, 5000, 0, sk_bindings::EngineYawFromPortYaw(0.0f), 0, 2, 0);
        auto wallAtTile4 = [](int tileX, int) { return tileX >= 4; };
        std::vector<sk_bindings::ProjectileTarget> nobody;
        sk_bindings::ProjectileImpact last;
        int ticks = 0;
        while (shot.alive && ticks < 100) {
            last = sk_bindings::TickSpellProjectile(shot, 200, 200, wallAtTile4, nobody);
            ++ticks;
        }
        Check(ticks == 4 && last.hitWall && !last.hit,
              "a wall four tiles out stops it on the fourth tick, with nothing hit");
    }
    {
        // The impact runs the spell script's own HitTarget.
        auto blaze = loadItem("blaze.s");
        auto rat = loadMonster("monsters/Azra_Rat.s");
        if (rat) {
            callInt(rat.get(), "SetMaxHealth", 200);
            const int before = rat->actorHealth();
            sk_bindings::SpellProjectile shot = sk_bindings::SpawnSpellProjectile(
                blaze.get(), &player, 0, 5000, 0, sk_bindings::EngineYawFromPortYaw(0.0f), 0, 2, 0);
            sk_bindings::ProjectileTarget target;
            target.actor = rat.get();
            target.script = rat.get();
            target.x = 3 * 256;
            target.y = 5000;
            target.halfWidth = sk_bindings::kProjectileRadius;
            target.halfDepth = sk_bindings::kProjectileRadius;
            std::vector<sk_bindings::ProjectileTarget> targets{target};
            auto never = [](int, int) { return false; };
            sk_bindings::ProjectileImpact impact;
            int ticks = 0;
            while (shot.alive && ticks < 100) {
                impact = sk_bindings::TickSpellProjectile(shot, 200, 200, never, targets);
                ++ticks;
            }
            Check(impact.hit && impact.target == rat.get(),
                  "a rat three tiles down the line takes the impact");
            Check(!shot.alive && ticks <= 4, "...on the third tick, and the shot ends there");
            Check(rat->actorHealth() < before,
                  "...and blaze.s's own HitTarget() -> DoAttackRoll() did the damage");
        }
    }
    {
        // The caster is never a target, and the flat impact damage of the
        // greater blaze applies on top of the script's own handler.
        auto blaze = loadItem("blaze.s");
        auto rat = loadMonster("monsters/Azra_Rat.s");
        if (rat) {
            callInt(rat.get(), "SetMaxHealth", 500);
            sk_bindings::SpellProjectile shot = sk_bindings::SpawnSpellProjectile(
                blaze.get(), rat.get(), 0, 5000, 0, sk_bindings::EngineYawFromPortYaw(0.0f), 0, 2,
                50);
            sk_bindings::ProjectileTarget self;
            self.actor = rat.get();
            self.script = rat.get();
            self.x = 256;
            self.y = 5000;
            self.halfWidth = sk_bindings::kProjectileRadius;
            self.halfDepth = sk_bindings::kProjectileRadius;
            std::vector<sk_bindings::ProjectileTarget> targets{self};
            auto never = [](int, int) { return false; };
            sk_bindings::ProjectileImpact impact =
                sk_bindings::TickSpellProjectile(shot, 200, 200, never, targets);
            const int full = rat->actorHealth();
            Check(!impact.hit && rat->actorHealth() == full,
                  "a caster cannot be hit by their own projectile");
        }
    }

    std::printf("\nm48_spell_projectile_smoke: %s\n",
                g_failures ? "FAILED" : "PASSED (all checks)");
    return g_failures ? 1 : 0;
}
