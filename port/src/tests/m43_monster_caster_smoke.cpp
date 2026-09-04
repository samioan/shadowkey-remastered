// M43 smoke test: monsters as spell casters.
//
// The gap this closes was the last implementable item on the roadmap's
// "next milestones" list: M37 recovered the real caster stat and hit gate,
// but read both off the *player*, and Absorb's heal always went to the
// player too -- correct only because nothing else in this port could cast.
// 32 shipped creature scripts call AddSpell.
//
// Everything asserted here is decompiled ground truth, from four functions
// that had not been read before this pass:
//
//   * FUN_1002fd30 -- resolves an actor to its stats block through two
//     vtable predicates: `+0xe4` is "is a monster" (block at actor+0x224)
//     and `+0xcc` is "is the player" (block at actor+0x3ac). Pinning those
//     two is what corrects three branch readings below.
//   * dispatcher 0x10084924 case 8 (AddSpell) and case 9 (SetMeleeRoll) --
//     a fixed four-slot `{u8 chance; Spell*}` table, a cumulative
//     rand(0,100) threshold, and a cached Blind slot at monster+0x32c.
//   * FUN_1008457c -- the spell chooser, including the "don't re-blind an
//     already-blind target" special case.
//   * FUN_100835b8 -- the melee-vs-spell branch, whose direction is the
//     opposite of what "SetMeleeRoll" suggests.
//
// The shipped scripts are the cross-check throughout: the numbers asserted
// are the ones monsters/shriekfloater.s, monsters/tunnel_wight.s and
// monsters/pergan_asuul_crypt2.s actually contain.
#include <cstdio>
#include <memory>
#include <string>

#include "assets/string_table.h"
#include "simkin_bindings/actor_stats.h"
#include "simkin_bindings/combat.h"
#include "simkin_bindings/item_executable.h"
#include "simkin_bindings/level_executable.h"
#include "simkin_bindings/menu_stack.h"
#include "simkin_bindings/monster_executable.h"
#include "simkin_bindings/player_executable.h"
#include "simkin_bindings/spell_actor.h"
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
        std::printf("m43_monster_caster_smoke: FAILED to load entities.txt\n");
        return 1;
    }

    skInterpreter interpreter;
    sk_bindings::MenuStack stack(scriptRoot, interpreter, &strings);
    // Real creature scripts build their spells with
    // `Level.CreateEntity(<typeId>)`, so the typeId table has to be live
    // for any of this to load at all.
    stack.level().SetEntityTypes(&entityTypes);

    // Every assertion below that a spell *lands* has to get past the real
    // hit gate, which is probabilistic in general. Zeroing the player's
    // willpower and magic resistance makes their spell resistance exactly
    // 0, which makes the gate's chance exactly 0x100 -- the value the
    // engine's own `chance == 0x100 ||` special case turns into a
    // certainty. Same trick m22_spell_smoke already uses on the creature
    // side, and for the same reason: the gate's arithmetic is pinned in
    // m37_spellpower_smoke, not re-rolled here.
    {
        skRValueArray a;
        a.append(skRValue(0));
        skRValue r;
        skExecutableContext c(&interpreter);
        stack.player().method(skString("SetWillpower"), a, r, c);
        stack.player().method(skString("SetMagicResistance"), a, r, c);
    }

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
        } catch (skParseException& e) {
            std::printf("  (parse error loading %s: %s)\n", rel.c_str(), e.toString().ptr());
            return nullptr;
        } catch (skRuntimeException& e) {
            std::printf("  (runtime error loading %s: %s)\n", rel.c_str(), e.toString().ptr());
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

    std::printf("=== M43: monsters as spell casters ===\n\n");

    // ---- 1. AddSpell really fills the real four-slot table ----
    //
    // monsters/tunnel_wight.s is the clearest specimen in the corpus: three
    // spells, three explicit SetLevels, cumulative thresholds 30/70/100,
    // and its own comments spelling out the percentages those are meant to
    // produce ("30%", "40% of the time", "30%").
    std::unique_ptr<Monster> wight = loadMonster("monsters/tunnel_wight.s");
    Check(wight != nullptr, "monsters/tunnel_wight.s loads and runs its real Init()");
    if (!wight) {
        std::printf("\nm43_monster_caster_smoke: FAILED\n");
        return 1;
    }
    {
        Check(wight->hasSpells(), "...and AddSpell filled slot 0, which is the melee branch's test");
        Check(wight->spellSlot(0).chance == 30 && wight->spellSlot(1).chance == 70 &&
                  wight->spellSlot(2).chance == 100 && wight->spellSlot(3).spell == nullptr,
              "the three cumulative thresholds are stored in order, and the 4th slot is free");
        Check(wight->spellSlot(0).spell && wight->spellSlot(0).spell->templateId() == 4008 &&
                  wight->spellSlot(1).spell && wight->spellSlot(1).spell->templateId() == 4009 &&
                  wight->spellSlot(2).spell && wight->spellSlot(2).spell->templateId() == 4023,
              "...each holding the real spell entity the script created (4008/4009/4023)");
        // The owner write is the whole point: it is what DoAttackRoll reads
        // the caster off.
        Check(wight->spellSlot(0).spell->spellOwner() == static_cast<sk_bindings::SpellActor*>(
                                                             wight.get()),
              "AddSpell set the creature as each spell's owner (FUN_1006d510)");
        // And the levels, which are the magnitude for a creature-cast spell.
        Check(wight->spellSlot(0).spell->spellLevel() == 9 &&
                  wight->spellSlot(1).spell->spellLevel() == 7 &&
                  wight->spellSlot(2).spell->spellLevel() == 9,
              "...and the SetLevel each one carries (9 / 7 / 9)");
    }

    // ---- 2. The magnitude rule really takes the other arm ----
    //
    // `if (!scroll && (owner == null || owner->vtable[0xcc]())) mag =
    // casterLevel; else mag = spell->level`. With 0xcc pinned as the player
    // predicate, a creature-cast spell always scales with the spell's own
    // SetLevel and never with the creature's SetLevel -- which is exactly
    // why every caster script sets both, and sets them differently.
    {
        Check(wight->level() == 9 && wight->spellSlot(2).spell->spellLevel() == 9 &&
                  wight->spellSlot(1).spell->spellLevel() == 7,
              "tunnel_wight is level 9, and gives its Absorb a different SetLevel(7)");

        // HarmArmor is the cleanest probe: its modifier is literally
        // `-magnitude`, so the number lands where it can be read back.
        auto victim = loadMonster("monsters/Azra_Rat.s");
        Check(victim != nullptr, "monsters/Azra_Rat.s loads as a target");
        if (victim) {
            callInt(victim.get(), "SetMagicResistance", 0);  // make the gate a certainty
            callInt(&stack.player(), "SetLevel", 20);        // a caster level to NOT see
            skRValueArray a;
            a.append(skRValue(static_cast<skiExecutable*>(victim.get()), false));
            skRValue r;
            skExecutableContext c(&interpreter);
            wight->spellSlot(2).spell->method(skString("DoAttackRoll"), a, r, c);
            int mod = victim->statModifier(Stats::kStatArmor);
            std::printf("   (HarmArmor cast by a level-9 creature at SetLevel(9), player level 20 "
                        "-> armour modifier %d)\n",
                        mod);
            Check(mod == -9,
                  "a creature's HarmArmor scales with the spell's SetLevel(9), not any actor level");
        }
    }

    // ---- 3. The player is a legal target, with the same stats block ----
    //
    // This is the half that had no representation at all before M43: every
    // status primitive in the engine takes a stats block, and the player
    // owns one at actor+0x3ac exactly as a creature owns one at +0x224.
    {
        sk_bindings::PlayerExecutable& player = stack.player();
        Check(sk_bindings::ResolveSpellActor(&player) == static_cast<sk_bindings::SpellActor*>(
                                                             &player) &&
                  player.isPlayerActor() && !player.isMonsterActor(),
              "FUN_1002fd30 resolves the player too -- 0xcc true, 0xe4 false");
        Check(sk_bindings::ResolveSpellActor(wight.get()) ==
                      static_cast<sk_bindings::SpellActor*>(wight.get()) &&
                  wight->isMonsterActor() && !wight->isPlayerActor(),
              "...and a creature the other way round");

        // Weakness (4008), the first of the two branches M43 added, cast at
        // the player by the creature that is the only one in the game that
        // casts it. -10 attack for `magnitude + 8` = 17 seconds.
        int attackBefore = player.attack();
        {
            skRValueArray a;
            a.append(skRValue(static_cast<skiExecutable*>(&player), false));
            skRValue r;
            skExecutableContext c(&interpreter);
            wight->spellSlot(0).spell->method(skString("DoAttackRoll"), a, r, c);
        }
        Check(wight->spellSlot(0).spell->statusEffect() == Item::kEffectWeakness,
              "typeId 4008 resolves to spells\\Weakness.s -- a branch only creatures cast");
        Check(player.actorStats().statModifier(Stats::kStatAttack) == -10 &&
                  player.attack() == attackBefore - 10,
              "...and a creature's Weakness really takes 10 off the player's attack");

        // ...and it expires on the player's own tick, at the real
        // `duration * 0x100` = 17 * 256 units.
        for (int i = 0; i < 17 * 256 / sk_bindings::kAiFrameDeltaUnits + 2; ++i) {
            player.TickStatusEffects(sk_bindings::kAiFrameDeltaUnits);
        }
        Check(player.attack() == attackBefore,
              "...and expires after its real 17 seconds on the player's own stats tick");
    }

    // ---- 4. Absorb heals the caster, and the caster can be a creature ----
    //
    // Five shipped creatures cast Absorb. The heal is
    // `damage * magnitude / 25 + 6` onto the *caster's* stats block, which
    // is what the decompile always said and what nothing could exercise.
    {
        auto victim = loadMonster("monsters/Azra_Rat.s");
        if (victim) {
            callInt(victim.get(), "SetMagicResistance", 0);
            // Hurt the wight first, so a heal has somewhere to go.
            wight->ApplyDamage(wight->maxHealth() / 2);
            int casterHealthBefore = wight->currentHealth();
            skRValueArray a;
            a.append(skRValue(static_cast<skiExecutable*>(victim.get()), false));
            skRValue r;
            skExecutableContext c(&interpreter);
            wight->spellSlot(1).spell->method(skString("DoAttackRoll"), a, r, c);
            std::printf("   (tunnel_wight's own Absorb: caster health %d -> %d, rat %d -> %d)\n",
                        casterHealthBefore, wight->currentHealth(), victim->maxHealth(),
                        victim->currentHealth());
            // Absorb has no variance (its min and max are equal), so this
            // is exact: magnitude 7 -> damage 19, heal 19*7/25 + 6 = 11.
            Check(victim->currentHealth() == victim->maxHealth() - (7 + 12),
                  "Absorb at SetLevel(7) deals its fixed magnitude + 12 = 19");
            Check(wight->currentHealth() == casterHealthBefore + (19 * 7 / 25) + 6,
                  "...and heals the *creature that cast it* by damage*magnitude/25 + 6");
        }
    }

    // ---- 5. The melee-vs-spell branch, in the direction it really reads --
    //
    // `if (slot0 == 0 || (meleeRoll != 0 && meleeRoll <= rand(0,100)))
    // melee; else cast`. Two consequences worth pinning, because both are
    // counterintuitive and both are load-bearing for the shipped corpus.
    {
        // (a) A creature with no spells always melees, whatever the roll.
        auto rat = loadMonster("monsters/Azra_Rat.s");
        bool alwaysMelee = true;
        if (rat) {
            for (int i = 0; i < 200; ++i) {
                if (!rat->RollForMelee()) alwaysMelee = false;
            }
        }
        Check(rat && alwaysMelee, "a creature with no spells melees every time (the slot-0 test)");

        // (b) The mages -- the eight scripts that call AddSpell and never
        // SetMeleeRoll -- cast every time, because the default is 0 and the
        // guard is `meleeRoll != 0`. bandit_mage.s is one of them.
        auto mage = loadMonster("monsters/bandit_mage.s");
        Check(mage != nullptr, "monsters/bandit_mage.s loads");
        if (mage) {
            Check(mage->meleeRoll() == 0 && mage->hasSpells(),
                  "...and it adds spells but never calls SetMeleeRoll, leaving the default 0");
            bool neverMelee = true;
            for (int i = 0; i < 200; ++i) {
                if (mage->RollForMelee()) neverMelee = false;
            }
            Check(neverMelee, "...so a mage casts on every single attack, which is the point of it");
        }

        // (c) And the direction: a higher SetMeleeRoll means *less* melee.
        // pergan_asuul_crypt2.s sets 60, shriekfloater.s sets 75.
        auto pergan = loadMonster("monsters/pergan_asuul_crypt2.s");
        auto floater = loadMonster("monsters/shriekfloater.s");
        Check(pergan && pergan->meleeRoll() == 60, "pergan_asuul_crypt2.s's real SetMeleeRoll(60)");
        Check(floater && floater->meleeRoll() == 75, "shriekfloater.s's real SetMeleeRoll(75)");
        if (pergan && floater) {
            int pergonMelee = 0, floaterMelee = 0;
            for (int i = 0; i < 4000; ++i) {
                if (pergan->RollForMelee()) ++pergonMelee;
                if (floater->RollForMelee()) ++floaterMelee;
            }
            std::printf("   (melee share over 4000 rolls: SetMeleeRoll(60) -> %.0f%%, "
                        "SetMeleeRoll(75) -> %.0f%%)\n",
                        pergonMelee / 40.0, floaterMelee / 40.0);
            Check(pergonMelee > floaterMelee,
                  "a higher SetMeleeRoll produces *less* melee -- the name reads backwards");
        }
    }

    // ---- 6. The spell chooser, including its Blind special case ----
    {
        // Over the cumulative table 30/70/100 the three slots should come
        // up in roughly 30/40/30 proportion, matching tunnel_wight.s's own
        // comments. Asserted as an ordering rather than a tolerance so the
        // test cannot go flaky.
        int counts[3] = {0, 0, 0};
        for (int i = 0; i < 6000; ++i) {
            Item* picked = wight->ChooseSpell(false);
            for (int s = 0; s < 3; ++s) {
                if (picked == wight->spellSlot(s).spell) ++counts[s];
            }
        }
        std::printf("   (tunnel_wight's slot shares over 6000 draws: %.0f%% / %.0f%% / %.0f%% "
                    "-- the script says 30 / 40 / 30)\n",
                    counts[0] / 60.0, counts[1] / 60.0, counts[2] / 60.0);
        Check(counts[1] > counts[0] && counts[1] > counts[2],
              "the middle slot's 40% band really is the widest of the three");
        Check(counts[0] + counts[1] + counts[2] == 6000,
              "...and a table that runs to 100 always picks something");

        // The special case: a creature that knows Blind and whose target is
        // already blind casts something else instead. monsters/shadowray.s
        // has Blind and nothing else, so it is the case where "something
        // else" does not exist and the creature falls back to the roll.
        auto mage = loadMonster("monsters/bandit_mage.s");
        if (mage) {
            // bandit_mage.s: blaze (50) at 50, Blind (4010) at 100.
            Check(mage->spellSlot(1).spell && mage->spellSlot(1).spell->templateId() == 4010,
                  "bandit_mage.s's second spell is Blind, whose slot AddSpell caches specially");
            bool everBlindAgain = false;
            for (int i = 0; i < 500; ++i) {
                Item* picked = mage->ChooseSpell(/*targetBlinded=*/true);
                if (picked && picked->templateId() == 4010) everBlindAgain = true;
            }
            Check(!everBlindAgain,
                  "a caster never re-Blinds an already-blind target -- it picks its other spell");
            // ...and with a target that is not blind, the roll runs
            // normally and Blind is reachable again.
            bool blindReachable = false;
            for (int i = 0; i < 500; ++i) {
                Item* picked = mage->ChooseSpell(/*targetBlinded=*/false);
                if (picked && picked->templateId() == 4010) blindReachable = true;
            }
            Check(blindReachable, "...but it is back in the table against an unblinded one");
        }
    }

    // ---- 7. The Blind flag's sign flip ----
    //
    // The correction M43's reading of FUN_1002fd30 forced: the real branch
    // is `if (!target->vtable[0xe4]()) applyFlag()`, and 0xe4 is the
    // *monster* predicate -- so the blindness flag lands on the player and
    // not on a creature. The two stat penalties land on both. This port had
    // it exactly the other way round, with a note saying it did not matter
    // because only creatures could be targets; M43 makes both halves wrong.
    {
        auto mage = loadMonster("monsters/bandit_mage.s");
        auto rat = loadMonster("monsters/Azra_Rat.s");
        if (mage && rat) {
            Item* blind = mage->spellSlot(1).spell;
            callInt(rat.get(), "SetMagicResistable", 0);
            callInt(rat.get(), "SetMagicResistance", 0);
            skRValueArray a;
            a.append(skRValue(static_cast<skiExecutable*>(rat.get()), false));
            skRValue r;
            skExecutableContext c(&interpreter);
            blind->method(skString("DoAttackRoll"), a, r, c);
            Check(rat->statModifier(Stats::kStatDefense) == -10 && !rat->blinded(),
                  "a blinded creature takes the -10s but never the flag itself");

            sk_bindings::PlayerExecutable& player = stack.player();
            int defenseBefore = player.defense();
            skRValueArray pa;
            pa.append(skRValue(static_cast<skiExecutable*>(&player), false));
            skRValue pr;
            skExecutableContext pc(&interpreter);
            blind->method(skString("DoAttackRoll"), pa, pr, pc);
            Check(player.blinded() && player.defense() == defenseBefore - 10,
                  "...while the player takes both -- the flag is the player's alone");
        }
    }

    // ---- 8. Paralyze's real duration ----
    //
    // Also corrected here: `(magnitude + 4) * 0x100`, read straight off the
    // branch. The port had `magnitude * 5 * 256`, which its own comment
    // admitted was "the same magnitude-scaled shape" rather than a
    // recovered value. raiders/blu_bounder.s casts Paralyze at SetLevel(5),
    // so the real lockout is 9 seconds, not 25.
    {
        auto bounder = loadMonster("raiders/blu_bounder.s");
        Check(bounder && bounder->hasSpells() &&
                  bounder->spellSlot(0).spell->templateId() == 4025 &&
                  bounder->spellSlot(0).spell->spellLevel() == 5,
              "raiders/blu_bounder.s's only spell is Paralyze at SetLevel(5)");
        auto rat = loadMonster("monsters/Azra_Rat.s");
        if (bounder && rat) {
            callInt(rat.get(), "SetMagicResistance", 0);
            skRValueArray a;
            a.append(skRValue(static_cast<skiExecutable*>(rat.get()), false));
            skRValue r;
            skExecutableContext c(&interpreter);
            bounder->spellSlot(0).spell->method(skString("DoAttackRoll"), a, r, c);
            Check(rat->paralyzed(), "...and it paralyses its target");
            // Tick to just before (5+4) seconds, then past it.
            int units = (5 + 4) * 256;
            int ticks = units / sk_bindings::kAiFrameDeltaUnits;
            for (int i = 0; i < ticks - 2; ++i) rat->TickAi(sk_bindings::kAiFrameDeltaUnits);
            bool stillHeld = rat->paralyzed();
            for (int i = 0; i < 4; ++i) rat->TickAi(sk_bindings::kAiFrameDeltaUnits);
            Check(stillHeld && !rat->paralyzed(),
                  "...for the real (magnitude + 4) seconds, not the magnitude * 5 this port had");
        }
    }

    // ---- 9. The whole path, end to end ----
    //
    // A creature attacking the player: roll melee-or-cast, choose a spell,
    // run it. This is the shape main.cpp's AI block now calls.
    {
        auto floater = loadMonster("monsters/shriekfloater.s");
        sk_bindings::PlayerExecutable& player = stack.player();
        callInt(&player, "SetHealth", 100);
        // Clear the -10s section 7's Blind left on the player: the real
        // FUN_1004aa28 rejects a *weaker* modifier on a stat that already
        // carries one, so Disease's -3 would legitimately never land on top
        // of a live Blind. 60 seconds of ticks outlasts every duration this
        // test has armed.
        for (int i = 0; i < 60 * 256 / sk_bindings::kAiFrameDeltaUnits; ++i) {
            player.TickStatusEffects(sk_bindings::kAiFrameDeltaUnits);
        }
        if (floater) {
            // shriekfloater.s's one spell is Disease at SetLevel(7), which
            // sits precisely on that branch's own `magnitude < 7` boundary
            // -- so it takes the -3 arm, not the -2 one.
            Check(floater->spellSlot(0).spell->templateId() == 4033 &&
                      floater->spellSlot(0).spell->spellLevel() == 7,
                  "shriekfloater.s casts Disease at SetLevel(7)");
            bool cast = false;
            for (int i = 0; i < 200 && !cast; ++i) {
                if (!floater->RollForMelee()) cast = floater->CastSpellAt(&player);
            }
            Check(cast, "a creature's own CastSpellAt() runs, with the player as the target");
            Check(player.actorStats().statModifier(Stats::kStatAttack) == -3 &&
                      player.actorStats().statModifier(Stats::kStatDefense) == -3,
                  "...and Disease at magnitude 7 takes the -3 arm of its own `< 7` branch");
        }
    }

    std::printf("\nm43_monster_caster_smoke: %s\n",
                g_failures ? "FAILED" : "PASSED (all checks)");
    return g_failures ? 1 : 0;
}
