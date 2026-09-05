// M58 smoke test: the effects system.
//
// `AddEffect` was the largest unhandled native in the shipped corpus -- 83
// real call sites, 63 on `GetOwner()` and 20 on `GetPlayer()`. Everything
// checked here is either decompiled (`FUN_1004aa28`, `FUN_1004ad40`,
// `FUN_1004b3b4`, `FUN_1004abe4`, `FUN_10048140`, `FUN_10049698`, and the
// Character-stats dispatcher `FUN_10048244`) or read out of the shipped
// scripts, which are also run for real here rather than paraphrased.
//
// Part 1  the recovered constant table
// Part 2  the applier: three ops, the field map, the entry guard
// Part 3  the three durations, and expiry undoing its own node
// Part 4  real shipped scripts, run end to end
// Part 5  the named natives: FindStringEffect, RemoveEffect,
//         RemoveEnchantments, SetSpellEffect, SetBlindness
#include <cstdio>
#include <memory>
#include <string>

#include "assets/string_table.h"
#include "simkin_bindings/actor_stats.h"
#include "simkin_bindings/effects.h"
#include "simkin_bindings/item_executable.h"
#include "simkin_bindings/level_executable.h"
#include "simkin_bindings/menu_executable.h"
#include "simkin_bindings/menu_stack.h"
#include "simkin_bindings/monster_executable.h"
#include "simkin_bindings/player_executable.h"
#include "skExecutableContext.h"
#include "skInterpreter.h"
#include "skRValue.h"
#include "skParseException.h"
#include "skRValueArray.h"
#include "skRuntimeException.h"
#include "world/entity_types.h"

namespace sk_b = sk_bindings;

namespace {

int g_failures = 0;
int g_checks = 0;

void Check(bool ok, const std::string& what) {
    std::printf("%-74s %s\n", what.c_str(), ok ? "OK" : "FAILED");
    ++g_checks;
    if (!ok) ++g_failures;
}

int ConstantValue(const char* name) {
    for (int i = 0; i < sk_b::kEffectConstantCount; ++i) {
        if (std::string(sk_b::kEffectConstants[i].name) == name) {
            return sk_b::kEffectConstants[i].value;
        }
    }
    return -1;
}

}  // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    const char* scriptRoot =
        argc > 1 ? argv[1]
                  : "The-Elder-Scrolls-Travels-Shadowkey_N-Gage_EN-FR-DE-ES-IT_USA-Europe-"
                    "EnFrDeEsIt-26102004/system/apps/6r51";

    sk::StringTable strings;
    strings.Load(std::string(scriptRoot) + "/stringtable.eng");
    sk::EntityTypeTable entityTypes;
    if (!entityTypes.Load(scriptRoot)) {
        std::printf("m58_effects_smoke: FAILED to load entities.txt\n");
        return 1;
    }

    skInterpreter interpreter;
    sk_b::MenuStack stack(scriptRoot, interpreter, &strings);
    stack.level().SetEntityTypes(&entityTypes);
    sk_b::PlayerExecutable& player = stack.player();

    auto call = [&](auto* obj, const char* name, skRValueArray& args) {
        skRValue ret;
        skExecutableContext ctxt(&interpreter);
        obj->method(skString(name), args, ret, ctxt);
        return ret;
    };

    // ---- Part 1: the recovered constant table ----
    {
        std::printf("\n-- the constant table --\n");
        Check(sk_b::kEffectConstantCount == 59,
              "59 named constants, the union of the two registration runs");
        Check(ConstantValue("Timed") == 1 && ConstantValue("Equipped") == 2 &&
                  ConstantValue("Permanent") == 3 && ConstantValue("InvalidDuration") == 0,
              "the duration enum is InvalidDuration/Timed/Equipped/Permanent = 0..3");
        Check(ConstantValue("Increment") == 1 && ConstantValue("Set") == 2 &&
                  ConstantValue("Decrement") == 3,
              "the op enum is Increment/Set/Decrement = 1..3");
        // The three the engine's own switch pins independently, and the
        // three M43 had already guessed from unrelated dispatcher cases.
        Check(ConstantValue("Attack") == 1 && ConstantValue("Defense") == 2 &&
                  ConstantValue("ArmorValue") == 7,
              "Attack/Defense/ArmorValue = 1/2/7, confirming ActorStats::StatIndex");
        Check(ConstantValue("Experience") == 23 && ConstantValue("Gold") == 25,
              "the stat enum really does skip 24 in the global registration");
        Check(ConstantValue("StatLevel") == 24,
              "...and 24 is StatLevel, which only the per-menu interpreter registers");
        Check(ConstantValue("ItemUsed") == 30 && ConstantValue("MonsterAttackBonus") == 31 &&
                  ConstantValue("Blindness") == 32,
              "the three tail stats, 29 skipped, are ItemUsed/MonsterAttackBonus/Blindness");
        // The families this port had been carrying as placeholders.
        Check(ConstantValue("AR_Light") == 2 && ConstantValue("AR_Medium") == 4 &&
                  ConstantValue("AR_Heavy") == 8,
              "the armour ratings are bit flags 2/4/8, not the ordinals 0/1/2");
        Check(ConstantValue("WR_Melee") == 4 && ConstantValue("WR_LightBow") == 8 &&
                  ConstantValue("WR_MediumBow") == 16 && ConstantValue("WR_ShortBlade") == 32 &&
                  ConstantValue("WR_LongBlade") == 64 && ConstantValue("WR_Blunt") == 128 &&
                  ConstantValue("WR_Axe") == 256 && ConstantValue("WR_Dagger") == 512 &&
                  ConstantValue("WR_EnchantedBlade") == 1024,
              "the nine weapon ratings are one bit each, 4 through 1024");
        Check(ConstantValue("SR_Small") == 8 && ConstantValue("SR_Medium") == 16,
              "SR_Small/SR_Medium, two the port never had, are 8 and 16");
        // The two names 83 shipped call sites use that the engine never
        // registers -- so in the real game they resolve to nothing.
        Check(ConstantValue("Damage") == -1 && ConstantValue("Perception") == -1,
              "`Damage` and `Perception` are not registered by either run");
    }

    // ---- Part 2: the applier ----
    {
        std::printf("\n-- FUN_1004ad40, the stat applier --\n");
        // A fresh player: the documented placeholder block, 50 across.
        skRValueArray none;
        const int strengthBefore = call(&player, "GetStrength", none).intValue();
        Check(strengthBefore == 50, "the port's placeholder player starts at 50 strength");

        // Increment adds; the return is the value *before*.
        int previous = sk_b::ApplyEffectStat(player, sk_b::kEffectStatAttack, sk_b::kOpIncrement, 7);
        Check(previous == 50 && player.attack() == 57,
              "Increment adds to the field and returns its previous value");
        // Set assigns.
        previous = sk_b::ApplyEffectStat(player, sk_b::kEffectStatAttack, sk_b::kOpSet, 12);
        Check(previous == 57 && player.attack() == 12, "Set assigns, and returns the old value");
        // Decrement, as written: magnitude minus previous, not the reverse.
        previous = sk_b::ApplyEffectStat(player, sk_b::kEffectStatAttack, sk_b::kOpDecrement, 30);
        Check(previous == 12 && player.attack() == 18,
              "Decrement computes `magnitude - previous` (30 - 12), reproduced as written");
        sk_b::ApplyEffectStat(player, sk_b::kEffectStatAttack, sk_b::kOpSet, 50);

        // The entry guard: stat inside the switch's range with op 0 does
        // nothing at all. Stat 30 is outside it, which is exactly how
        // twi_crystals.s gets to pass an op of 0.
        sk_b::ApplyEffectStat(player, sk_b::kEffectStatAttack, sk_b::kOpInvalid, 99);
        Check(player.attack() == 50, "the entry guard rejects `stat <= 28 && op == 0`");

        // `Strength` is the bonus field, not strength.
        sk_b::ApplyEffectStat(player, sk_b::kEffectStatStrength, sk_b::kOpIncrement, 5);
        skRValueArray noArgs;
        Check(call(&player, "GetStrength", noArgs).intValue() == 50 &&
                  call(&player, "GetStrengthBonus", noArgs).intValue() == 5,
              "AddEffect's `Strength` moves the *bonus* at +0x10, not strength at +0x14");

        // The derived-stat recompute, and which stats trigger it.
        // maxHealth = healthBonus + (strength + endurance) / 2
        // maxFatigue = will + strength + endurance
        sk_b::ApplyEffectStat(player, sk_b::kEffectStatEndurance, sk_b::kOpIncrement, 4);
        Check(player.maxHealth() == 52 && player.maxFatigue() == 154,
              "an Endurance effect recomputes maxHealth (52) and maxFatigue (154)");
        Check(player.maxMagicka() == 50,
              "...and maxMagicka, which the non-player arm derives from intelligence");
        const int maxHealthBefore = player.maxHealth();
        sk_b::ApplyEffectStat(player, sk_b::kEffectStatSpeed, sk_b::kOpIncrement, 10);
        Check(player.maxHealth() == maxHealthBefore,
              "a Speed effect does not recompute -- only six of the eight attributes do");

        // Health goes through the clamping setter.
        sk_b::ApplyEffectStat(player, sk_b::kEffectStatHealth, sk_b::kOpIncrement, 1000);
        Check(player.health() == player.maxHealth(),
              "a Health effect is clamped by the same setter SetHealth uses");

        // Blindness is a flag, not a field -- and its clear path is the
        // engine's mvn/and/mvn, which sets everything except blind.
        sk_b::ApplyEffectStat(player, sk_b::kEffectStatBlindness, sk_b::kOpSet, 1);
        Check(player.blinded(), "`Blindness` with a positive magnitude sets the blind flag");
        sk_b::ApplyEffectStat(player, sk_b::kEffectStatBlindness, sk_b::kOpSet, 0);
        // `~(~4 & 4)` is `~0`, so clearing blindness through this arm turns
        // *every* flag on, blindness included. (From the other starting
        // state it turns on everything except blindness.) Either way it is
        // the mvn/and/mvn where `bic` was meant.
        Check(player.blinded() && player.actorStats().poisoned() &&
                  player.actorStats().effectFlags() == -1,
              "...and its clear path `~(~flags & 4)` turns on every flag instead");
        player.actorStats().SetEffectFlagsRaw(0);
    }

    // ---- Part 3: the three durations ----
    {
        std::printf("\n-- FUN_1004aa28, the three durations --\n");
        sk_b::ActorStats& stats = player.actorStats();
        const int armorBefore = player.armorRating();

        // Permanent: applied, no node.
        sk_b::AddEffect(player, sk_b::kDurationPermanent, sk_b::kEffectStatArmorValue,
                        sk_b::kOpIncrement, 5);
        Check(player.armorRating() == armorBefore + 5 && stats.timedEffects().empty(),
              "Permanent applies the change and creates no node");

        // Timed: applied, one node, and expiry undoes it.
        sk_b::AddEffect(player, sk_b::kDurationTimed, sk_b::kEffectStatArmorValue,
                        sk_b::kOpIncrement, 3, 2);
        Check(player.armorRating() == armorBefore + 8 && stats.timedEffects().size() == 1,
              "Timed applies the change and creates exactly one node");
        // Weaker on the same stat is rejected outright.
        sk_b::AddEffect(player, sk_b::kDurationTimed, sk_b::kEffectStatArmorValue,
                        sk_b::kOpIncrement, 1, 60);
        Check(player.armorRating() == armorBefore + 8 && stats.timedEffects().size() == 1,
              "a weaker Timed effect on the same stat is dropped, timer and all");
        // Stronger replaces -- and the replacement undoes the old one first.
        sk_b::AddEffect(player, sk_b::kDurationTimed, sk_b::kEffectStatArmorValue,
                        sk_b::kOpIncrement, 10, 60);
        Check(player.armorRating() == armorBefore + 15 && stats.timedEffects().size() == 1,
              "a stronger one undoes the weaker before applying itself (+15, not +18)");

        // Expiry. 2 seconds of nodes at 256 units per second.
        for (int i = 0; i < 60 * 256; i += sk_b::kAiFrameDeltaUnits) {
            player.TickStatusEffects(sk_b::kAiFrameDeltaUnits);
        }
        Check(stats.timedEffects().empty() && player.armorRating() == armorBefore + 5,
              "expiry undoes the Timed node and leaves the Permanent one standing");

        // Equipped: a second list, and it stacks.
        sk_b::AddEffect(player, sk_b::kDurationEquipped, sk_b::kEffectStatArmorValue,
                        sk_b::kOpIncrement, 2, 0);
        sk_b::AddEffect(player, sk_b::kDurationEquipped, sk_b::kEffectStatArmorValue,
                        sk_b::kOpIncrement, 1, 0);
        Check(stats.equippedEffects().size() == 2 && stats.timedEffects().empty() &&
                  player.armorRating() == armorBefore + 8,
              "Equipped stacks in a second list the strongest-wins rule never sees");
        for (int i = 0; i < 600 * 256; i += sk_b::kAiFrameDeltaUnits) {
            player.TickStatusEffects(sk_b::kAiFrameDeltaUnits);
        }
        Check(stats.equippedEffects().size() == 2,
              "...and nothing ever expires it: the removal path has no callers in the image");
        stats.equippedEffects().clear();
        sk_b::ApplyEffectStat(player, sk_b::kEffectStatArmorValue, sk_b::kOpSet, 0);
    }

    // ---- Part 4: the real shipped scripts ----
    {
        std::printf("\n-- shipped scripts, run for real --\n");
        skExecutableContext ctxt(&interpreter);
        // The real use path: Init(), then `OnUsedBy(GetPlayer())`, which is
        // what inventory.s calls and what binds the item's `GetOwner()`.
        // That binding is the whole point here -- 63 of the corpus's 83
        // AddEffect sites are `GetOwner().AddEffect(...)`, so an item used
        // without an owner does not reach a single one of them.
        auto useItem = [&](const char* relPath) -> std::unique_ptr<sk_b::ItemExecutable> {
            std::string full = std::string(scriptRoot) + "/" + relPath;
            try {
                auto item =
                    std::make_unique<sk_b::ItemExecutable>(skString(full.c_str()), ctxt, stack);
                skRValueArray args;
                args.append(skRValue(0));
                skRValue ret;
                skExecutableContext c2(&interpreter);
                item->method(skString("Init"), args, ret, c2);
                skRValueArray usedBy;
                usedBy.append(skRValue(static_cast<skiExecutable*>(&player), false));
                item->method(skString("OnUsedBy"), usedBy, ret, c2);
                return item;
            } catch (skParseException& e) {
                std::printf("  PARSE ERROR in %s: %s\n", relPath, e.toString().ptr());
            } catch (skRuntimeException& e) {
                std::printf("  RUNTIME ERROR in %s: %s\n", relPath, e.toString().ptr());
            }
            return nullptr;
        };

        // items/bounder_skin.s: three Timed effects at once.
        //   AddEffect(Timed, ArmorValue, Increment, 2, 120)
        //   AddEffect(Timed, Defense,    Increment, 2, 120)
        //   AddEffect(Timed, Strength,   Increment, 10, 120)
        const int armorBefore = player.armorRating();
        const int defenseBefore = player.defense();
        skRValueArray noArgs;
        const int bonusBefore = call(&player, "GetStrengthBonus", noArgs).intValue();
        useItem("items/bounder_skin.s");
        Check(player.armorRating() == armorBefore + 2 && player.defense() == defenseBefore + 2 &&
                  call(&player, "GetStrengthBonus", noArgs).intValue() == bonusBefore + 10,
              "items/bounder_skin.s applies all three of its Timed effects");
        Check(player.actorStats().timedEffects().size() == 3,
              "...as three separate nodes, one per stat");

        // crypt1/strength.s: one of the six attribute shrines, and not an
        // item at all -- a menu whose only line of substance is in its
        // MenuQuit handler:
        //   GetPlayer().AddEffect(Permanent, Strength, Increment, 5)
        const size_t nodesBefore = player.actorStats().timedEffects().size();
        skRValueArray openArgs;
        openArgs.append(skRValue(skString("crypt1\\strength")));
        call(&player, "OpenMenu", openArgs);
        bool shrineOpened = stack.currentMenu() != nullptr;
        if (shrineOpened) {
            skRValueArray quitArgs;
            skRValue ret;
            skExecutableContext c2(&interpreter);
            stack.currentMenu()->method(skString("MenuQuit"), quitArgs, ret, c2);
        }
        Check(shrineOpened &&
                  call(&player, "GetStrengthBonus", noArgs).intValue() == bonusBefore + 15 &&
                  player.actorStats().timedEffects().size() == nodesBefore,
              "crypt1/strength.s's Permanent shrine adds 5 and leaves no node behind");

        // items/shadowseed.s is the most interesting call site in the corpus,
        // and it is broken in the shipped game. It reads as "+10 to all eight
        // attributes for two minutes":
        //
        //   Strength, Intelligence, Agility, Speed,
        //   Perception,                       <-- line 5
        //   Luck, Endurance, Will
        //
        // but `Perception` is a constant the engine registers **nowhere** --
        // it is in neither of the two registration runs and in no `.s` file.
        // An unresolved bare identifier is not zero in this dialect; it
        // raises "Field Perception not found" out of the interpreter, which
        // aborts the rest of the handler. So the first four land, the last
        // three never run, and neither does the PlaySound or the
        // DestroyObject after them -- the seed is not even consumed.
        const int agilityBefore = call(&player, "GetAgility", noArgs).intValue();
        const int willBefore = call(&player, "GetWill", noArgs).intValue();
        const int luckBefore = call(&player, "GetLuck", noArgs).intValue();
        auto seed = useItem("items/shadowseed.s");
        Check(call(&player, "GetAgility", noArgs).intValue() == agilityBefore + 10,
              "items/shadowseed.s: the four effects before its `Perception` line land");
        Check(call(&player, "GetWill", noArgs).intValue() == willBefore &&
                  call(&player, "GetLuck", noArgs).intValue() == luckBefore,
              "...and the three after it never run: the unresolved name throws");
        Check(seed == nullptr, "...taking the item's own DestroyObject with it -- not consumed");

        // thunder_herb.s has the same bug in its *first* line (`Damage`,
        // also unregistered), so the whole handler is dead: no effect, no
        // sound, and the herb is not consumed either.
        const int damageMaxBefore = player.EffectStatValue(sk_b::kEffectStatMaxDamage);
        auto herb = useItem("items/thunder_herb.s");
        Check(herb == nullptr &&
                  player.EffectStatValue(sk_b::kEffectStatMaxDamage) == damageMaxBefore,
              "items/thunder_herb.s dies on its own first line, `Damage`: it does nothing");

        player.actorStats().timedEffects().clear();
    }

    // ---- Part 5: the named natives ----
    {
        std::printf("\n-- FindStringEffect, RemoveEffect, RemoveEnchantments --\n");
        sk_b::ActorStats& stats = player.actorStats();
        stats.timedEffects().clear();

        // twi_crystals.s's cooldown idiom, by hand:
        //   AddEffect(Timed, ItemUsed, 0, 0, 60, "crystals")
        skRValueArray args;
        args.append(skRValue(sk_b::kDurationTimed));
        args.append(skRValue(static_cast<int>(sk_b::kEffectStatItemUsed)));
        args.append(skRValue(0));
        args.append(skRValue(0));
        args.append(skRValue(60));
        args.append(skRValue(skString("crystals")));
        call(&player, "AddEffect", args);
        Check(stats.timedEffects().size() == 1 && stats.timedEffects()[0].name == "crystals",
              "AddEffect(Timed, ItemUsed, 0, 0, 60, \"crystals\") stores a named marker node");

        skRValueArray find;
        find.append(skRValue(static_cast<int>(sk_b::kEffectStatItemUsed)));
        find.append(skRValue(skString("crystals")));
        Check(call(&player, "FindStringEffect", find).boolValue(),
              "FindStringEffect(ItemUsed, \"crystals\") finds it");
        skRValueArray findOther;
        findOther.append(skRValue(static_cast<int>(sk_b::kEffectStatItemUsed)));
        findOther.append(skRValue(skString("CRYSTALS")));
        Check(call(&player, "FindStringEffect", findOther).boolValue(),
              "...case-insensitively, the way the real strcasecmp does");
        skRValueArray findMissing;
        findMissing.append(skRValue(static_cast<int>(sk_b::kEffectStatItemUsed)));
        findMissing.append(skRValue(skString("nothing")));
        Check(!call(&player, "FindStringEffect", findMissing).boolValue(),
              "...and answers false for a name nothing set");

        // A nameless node is invisible to it, whatever its stat.
        sk_b::AddEffect(player, sk_b::kDurationTimed, sk_b::kEffectStatAttack,
                        sk_b::kOpIncrement, -10, 30);
        skRValueArray findNameless;
        findNameless.append(skRValue(static_cast<int>(sk_b::kEffectStatAttack)));
        findNameless.append(skRValue(skString("")));
        Check(!call(&player, "FindStringEffect", findNameless).boolValue(),
              "a nameless node cannot be found by name -- the real check is `name != 0` first");

        // RemoveEffect, faithfully broken: the node goes, the stat stays.
        const int attackWithDebuff = player.attack();
        sk_b::AddEffect(player, sk_b::kDurationTimed, sk_b::kEffectStatDefense,
                        sk_b::kOpIncrement, -4, 30, "TESTDEBUFF");
        const int defenseWithDebuff = player.defense();
        skRValueArray remove;
        remove.append(skRValue(static_cast<int>(sk_b::kEffectStatDefense)));
        remove.append(skRValue(skString("TESTDEBUFF")));
        call(&player, "RemoveEffect", remove);
        Check(player.defense() == defenseWithDebuff,
              "RemoveEffect drops the node and does NOT undo the stat -- the real bug");
        Check(sk_b::FindStringEffect(stats, sk_b::kEffectStatDefense, "TESTDEBUFF") == nullptr,
              "...the node itself really is gone");
        // The engine's own remover, which is the correct one.
        sk_b::AddEffect(player, sk_b::kDurationTimed, sk_b::kEffectStatDefense,
                        sk_b::kOpIncrement, -4, 30, "TESTDEBUFF2");
        sk_b::RemoveNamedEffect(player, sk_b::kEffectStatDefense, "TESTDEBUFF2");
        Check(player.defense() == defenseWithDebuff,
              "FUN_1004bd24 unlinks, undoes and frees -- the stat comes back");

        // RemoveEnchantments: every negative node, and every Decrement node.
        sk_b::AddEffect(player, sk_b::kDurationTimed, sk_b::kEffectStatSpellcast,
                        sk_b::kOpIncrement, 6, 60);
        const int spellcastBuffed = player.actorStats().statModifier(sk_b::kEffectStatSpellcast);
        skRValueArray noArgs2;
        call(&player, "RemoveEnchantments", noArgs2);
        Check(player.attack() == attackWithDebuff + 10,
              "RemoveEnchantments undoes the -10 attack debuff");
        Check(spellcastBuffed == 6 &&
                  player.actorStats().statModifier(sk_b::kEffectStatSpellcast) == 6,
              "...and keeps the +6 buff: negatives only");
        Check((stats.effectFlags() & 2) != 0,
              "...and sets flag bit 1, which the real function does unconditionally");
        stats.SetEffectFlagsRaw(0);
        stats.timedEffects().clear();

        // SetSpellEffect: two stores, and kind 4 is what Sanctuary arms.
        skRValueArray spellEffect;
        spellEffect.append(skRValue(4));
        spellEffect.append(skRValue(2000));
        call(&player, "SetSpellEffect", spellEffect);
        Check(stats.periodicKind() == 4 && stats.periodicRemaining() == 2000,
              "SetSpellEffect(4, 2000) writes the periodic channel's kind and duration");
        const int healthBefore = player.health();
        player.ApplyDamage(25);
        Check(player.health() == healthBefore,
              "...and kind 4 makes DoDamage refuse outright -- FUN_10049e78's first line");
        skRValueArray clearEffect;
        clearEffect.append(skRValue(0));
        clearEffect.append(skRValue(0));
        call(&player, "SetSpellEffect", clearEffect);
        player.ApplyDamage(25);
        Check(player.health() == healthBefore - 25, "...and damage lands again once it is cleared");

        // SetBlindness, the one flag setter whose clear path is correct --
        // and cure_blindness_balm.s is its only caller.
        skRValueArray blindOn;
        blindOn.append(skRValue(true));
        blindOn.append(skRValue(0));
        call(&player, "SetBlindness", blindOn);
        Check(player.blinded(), "SetBlindness(true, 0) sets the blind flag");
        skRValueArray blindOff;
        blindOff.append(skRValue(false));
        blindOff.append(skRValue(0));
        call(&player, "SetBlindness", blindOff);
        Check(!player.blinded() && stats.effectFlags() == 0,
              "SetBlindness(false, 0) clears it properly -- a real `& ~4`, unlike its siblings");
    }

    // ---- a creature is the same stats block ----
    {
        std::printf("\n-- and a creature answers the same bindings --\n");
        skExecutableContext ctxt(&interpreter);
        std::string full = std::string(scriptRoot) + "/monsters/azra_rat.s";
        auto rat = std::make_unique<sk_b::MonsterExecutable>(skString(full.c_str()), ctxt, &strings,
                                                             player, stack);
        skRValueArray args;
        args.append(skRValue(0));
        skRValue ret;
        skExecutableContext c2(&interpreter);
        rat->method(skString("Init"), args, ret, c2);

        const int attackBefore = rat->attack();
        sk_b::AddEffect(*rat, sk_b::kDurationTimed, sk_b::kEffectStatAttack, sk_b::kOpIncrement,
                        -10, 30);
        Check(rat->attack() == (attackBefore - 10 < 0 ? 0 : attackBefore - 10),
              "a creature takes the same Timed attack debuff, written through the same field");
        // No Personality field on this side: the engine writes one nothing
        // reads, this port writes nothing at all. Same observable.
        sk_b::AddEffect(*rat, sk_b::kDurationTimed, sk_b::kEffectStatPersonality,
                        sk_b::kOpIncrement, 20, 30);
        Check(rat->actorStats().timedEffects().size() == 2,
              "...and an attribute it has no field for still books a node, and changes nothing");
    }

    std::printf("\nm58_effects_smoke: %s (%d checks)\n",
                g_failures ? "FAILED" : "PASSED (all checks)", g_checks);
    return g_failures ? 1 : 0;
}
