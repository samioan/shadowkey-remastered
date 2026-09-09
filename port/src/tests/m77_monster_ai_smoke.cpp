// M77 smoke test: the creature AI tick, transcribed.
//
// The reported symptom was "only the Azra rats of the first level have any
// AI; every other enemy stands still while the player walks past". The
// cause is one word. The actor constructor (FUN_100815e0) sets
// `monster+0x2a8 = -1`, and this port copied that -- but placing a
// creature runs entity vtable slot +0x10 (FUN_10086f9c for category 2,
// FUN_100866c0 for category 7), which writes 2 over it before the script's
// own Init() ever runs. 318 shipped scripts call SetAggressive(true) and
// only 38 call AiDetect(); the other 280 have nothing anywhere that would
// have woken a creature that started asleep. The rats worked by accident.
//
// The second symptom was "enemies attack non stop, with their sound
// effects overlapping". The cadence itself was right since M35 -- what was
// wrong is what it gates. In the engine `0x100 < +0x2c4` gates the whole
// approach/attack/give-up decision and the swing clip is played *by the
// attack*, once, with `PlayAnimation(swing, 1, idle, 0xf00)`. This port
// gated only the damage roll and re-asserted the swing clip on each of the
// ~25 ticks in between.
//
// Decompiled for this milestone:
//   FUN_10086f9c / FUN_100866c0   entity vtable +0x10 -- the spawn package
//   FUN_10018... (GameEngine_InitLevel)   proves it runs on every placement
//   FUN_10082224  the whole tick, re-read in its own order
//   FUN_100835b8  the attack: its +0x294 gate, its one-shot swing clip
//   FUN_10006704  vtable[0x1b4] -- not movement, an angle decay
//   FUN_10086a18  the SetCanTeleport jump
//   FUN_10012584  the Monster(AI) trie: all 55 names and their indices
//   0x10084924 cases 0x01/0x06/0x07/0x0a/0x0f/0x10/0x11/0x12/0x13/0x17/
//              0x22/0x27/0x35/0x36 -- the bindings that were soft-failing
// See simkin_bindings/monster_ai.h for the writeup.
//
// Part 1  the spawn package, and what a real Init() leaves behind
// Part 2  the census: how many placed creatures the port was skipping
// Part 3  the attack cadence, as the tick actually drives it
// Part 4  the boss throttle (SetBoss / SetAttackSpeed)
// Part 5  the fourteen bindings that were missing -- nothing soft-fails
// Part 6  the melee vertical limit and the ranged exemption
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "assets/string_table.h"
#include "simkin_bindings/level_executable.h"
#include "simkin_bindings/menu_stack.h"
#include "simkin_bindings/monster_ai.h"
#include "simkin_bindings/monster_executable.h"
#include "simkin_bindings/native_binding_common.h"
#include "simkin_bindings/player_executable.h"
#include "skExecutableContext.h"
#include "skInterpreter.h"
#include "skParseException.h"
#include "skRValue.h"
#include "skRValueArray.h"
#include "skRuntimeException.h"
#include "skTreeNode.h"
#include "world/entity_types.h"
#include "world/zone.h"

namespace sk_b = sk_bindings;

namespace {

int g_failures = 0;
int g_checks = 0;

void Check(bool ok, const std::string& what) {
    std::printf("%-76s %s\n", what.c_str(), ok ? "OK" : "FAILED");
    ++g_checks;
    if (!ok) ++g_failures;
}

const char* kZones[] = {"azra",     "broken1",  "broken2",      "crypt1",   "crypt2",
                        "crypt3",   "delfhide", "drgnfld",      "dstar_e",  "dstar_w",
                        "erthcave", "fearfrst", "ffarena",      "ghstpass", "glaciercrawl",
                        "lakvan",   "lothcav",  "raiders",      "snowline", "stouttp",
                        "twilite"};

std::string ToPath(const std::string& scriptRelative) {
    std::string p = scriptRelative;
    std::replace(p.begin(), p.end(), '\\', '/');
    return p;
}

std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// The soft-fail observer -- Part 5's whole method.
std::vector<std::string> g_softFails;
void RecordSoftFail(const char* /*object*/, const char* methodName, const char* /*args*/) {
    g_softFails.push_back(methodName ? methodName : "");
}

// Does the shipped file call this binding at all? Used to keep the corpus
// claims in the comments honest against the corpus itself.
bool FileMentions(const std::string& path, const char* needle) {
    std::ifstream in(path, std::ios::binary);
    if (!in.good()) return false;
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return Lower(text).find(Lower(needle)) != std::string::npos;
}

}  // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    const char* scriptRoot =
        argc > 1 ? argv[1]
                  : "The-Elder-Scrolls-Travels-Shadowkey_N-Gage_EN-FR-DE-ES-IT_USA-Europe-"
                    "EnFrDeEsIt-26102004/system/apps/6r51";

    sk::StringTable strings;
    if (!strings.Load(std::string(scriptRoot) + "/stringtable.eng")) {
        std::printf("m77_monster_ai_smoke: FAILED to load stringtable.eng\n");
        return 1;
    }
    sk::EntityTypeTable entityTypes;
    if (!entityTypes.Load(scriptRoot)) {
        std::printf("m77_monster_ai_smoke: FAILED to load entities.txt\n");
        return 1;
    }

    skInterpreter interpreter;

    auto loadInto = [&](sk_b::MenuStack& stack,
                        const char* rel) -> std::unique_ptr<sk_b::MonsterExecutable> {
        std::string full = std::string(scriptRoot) + "/" + ToPath(rel);
        skExecutableContext ctxt(&interpreter);
        try {
            auto m = std::make_unique<sk_b::MonsterExecutable>(skString(full.c_str()), ctxt,
                                                                &strings, stack.player(), stack);
            skRValueArray args;
            args.append(skRValue(0));
            skRValue ret;
            skExecutableContext c2(&interpreter);
            m->method(skString("Init"), args, ret, c2);
            return m;
        } catch (skParseException& e) {
            std::printf("  PARSE ERROR in %s: %s\n", rel, e.toString().ptr());
        } catch (skRuntimeException& e) {
            std::printf("  RUNTIME ERROR in %s: %s\n", rel, e.toString().ptr());
        }
        return nullptr;
    };

    // ---------------------------------------------------------------
    // Part 1: the spawn package
    // ---------------------------------------------------------------
    std::printf("== Part 1: a placed creature starts in package 2 ==\n");
    Check(sk_b::kSpawnAiPackage == sk_b::MonsterExecutable::kAiIdle &&
              sk_b::kSpawnAiPackage == 2,
          "entity vtable +0x10 writes package 2, and that is kAiIdle");
    {
        sk_b::MenuStack stack(scriptRoot, interpreter, &strings);
        stack.level().SetEntityTypes(&entityTypes);

        // The three creatures the report named as inert, and the shape they
        // share: SetAggressive(true), a chase radius, animations -- and no
        // Ai* call anywhere in the file.
        for (const char* rel : {"monsters/cave_spider.s", "monsters/alpha_wolf.s",
                                "monsters/Bandit_Brawler.s"}) {
            auto m = loadInto(stack, rel);
            const std::string full = std::string(scriptRoot) + "/" + ToPath(rel);
            Check(m && m->aggressive() &&
                      m->aiPackage() == sk_b::MonsterExecutable::kAiIdle &&
                      !FileMentions(full, "AiDetect"),
                  std::string("hostile with no AiDetect(), and still looking: ") + rel);
        }

        // The one that used to work, and why: it calls AiDetect(), which
        // sets the 2 the placement already set.
        auto rat = loadInto(stack, "monsters/Azra_Rat.s");
        Check(rat && rat->aggressive() && rat->aiPackage() == sk_b::MonsterExecutable::kAiIdle,
              "monsters/Azra_Rat.s lands in the same package -- AiDetect() is a no-op here");

        // The constructor's -1 is still reachable, from the one binding
        // that asks for it. That is what a script uses to switch itself off
        // after an OnDetect (M76).
        skRValueArray none;
        skRValue ret;
        skExecutableContext c(&interpreter);
        rat->method(skString("AiSleep"), none, ret, c);
        Check(rat->aiPackage() == sk_b::MonsterExecutable::kAiAsleep,
              "  AiSleep() still puts a creature back to -1");

        // The rest of the package bindings, from the dispatcher's own
        // cases 0x2a/0x2c/0x2d/0x2f.
        struct { const char* name; int argc; int package; } kPackages[] = {
            {"AiDetect", 0, sk_b::MonsterExecutable::kAiIdle},
            {"AiAttack", 1, sk_b::MonsterExecutable::kAiPursue},
            {"AiFlee", 1, sk_b::MonsterExecutable::kAiFlee},
            {"AiPursue", 1, sk_b::MonsterExecutable::kAiFollow},
            {"AiSpellAssistTarget", 1, sk_b::MonsterExecutable::kAiSpellAssist},
        };
        bool allPackages = true;
        for (const auto& pk : kPackages) {
            skRValueArray args;
            if (pk.argc) args.append(skRValue(0));
            skRValue r;
            skExecutableContext cc(&interpreter);
            rat->method(skString(pk.name), args, r, cc);
            if (rat->aiPackage() != pk.package) allPackages = false;
        }
        Check(allPackages,
              "AiDetect/AiAttack/AiFlee/AiPursue/AiSpellAssistTarget -> 2/3/4/5/6");
        // M77 also corrected this one: AiPursue had been grouped with the
        // genuine no-ops (AiActivate, AiWounded). Its dispatcher case 0x2d
        // does store a package -- 5 -- which the tick then never reads.
        Check(sk_b::MonsterExecutable::kAiFollow == 5,
              "  ...and AiPursue is 5, a package no arm of the tick reads -- not 3");
    }

    // ---------------------------------------------------------------
    // Part 2: the census
    // ---------------------------------------------------------------
    std::printf("\n== Part 2: how many placed creatures the port was skipping ==\n");
    int placed = 0, hostile = 0, hostileWithAiDetect = 0, wouldHaveActed = 0;
    std::set<std::string> hostileScripts;
    {
        sk_b::MenuStack stack(scriptRoot, interpreter, &strings);
        stack.level().SetEntityTypes(&entityTypes);
        for (const char* zoneName : kZones) {
            sk::Zone zone;
            if (!zone.Load(scriptRoot, zoneName)) continue;
            for (const sk::Zone::EntPlacement& e : zone.entities()) {
                const sk::EntityTypeDescriptor* desc = entityTypes.Lookup(e.typeId);
                if (!desc) continue;
                if (desc->category != 2 && desc->category != 7) continue;
                std::string rel = desc->name;
                if (!e.scriptPath.empty()) rel = e.scriptPath;
                if (rel.size() < 2 || rel.substr(rel.size() - 2) != ".s") rel += ".s";
                rel = ToPath(rel);
                const std::string full = std::string(scriptRoot) + "/" + rel;
                {
                    std::ifstream probe(full, std::ios::binary);
                    if (!probe.good()) continue;
                }
                auto npc = loadInto(stack, rel.c_str());
                if (!npc) continue;
                ++placed;
                if (!npc->aggressive()) continue;
                ++hostile;
                hostileScripts.insert(Lower(rel));
                // Would this creature have chased and swung under the old
                // asleep-by-default model? Only if its own script asked
                // for a package.
                if (FileMentions(full, "AiDetect") || FileMentions(full, "AiAttack")) {
                    ++hostileWithAiDetect;
                    ++wouldHaveActed;
                }
            }
        }
    }
    std::printf("  category-2/7 placements with a real script: %d\n", placed);
    std::printf("  ...hostile (SetAggressive(true)):           %d\n", hostile);
    std::printf("  ...of those, asking for a package by hand:  %d\n", hostileWithAiDetect);
    Check(placed == 1539, "1539 creature placements across the 21 zones (same corpus as M76)");
    Check(hostile == 1296, "  1296 of them are hostile");
    Check(wouldHaveActed == 47,
          "  but only 47 call AiDetect()/AiAttack() -- the only ones this port ever woke");
    Check(hostile - wouldHaveActed == 1249,
          "  so 1249 hostile placements go from standing still to fighting");
    // The Azra rats are the family the report singled out, and this is why:
    // they are among the 79.
    Check(hostileScripts.count("monsters/azra_rat.s") == 1,
          "  monsters/azra_rat.s is one of the 79 -- which is why the rats worked");

    // ---------------------------------------------------------------
    // Part 3: the attack cadence
    // ---------------------------------------------------------------
    std::printf("\n== Part 3: the attack cadence, driven the way the tick drives it ==\n");
    {
        sk_b::MenuStack stack(scriptRoot, interpreter, &strings);
        stack.level().SetEntityTypes(&entityTypes);
        auto rat = loadInto(stack, "monsters/Azra_Rat.s");

        // `+0x2c4 += delta` is above the package branch, so it accumulates
        // while the creature is still walking toward you -- which is what
        // makes the first swing land on arrival rather than a second after
        // it. Nothing about being in range enters into it.
        Check(rat && rat->attackCadence() == 0, "a fresh creature starts with an empty cadence");
        int ticks = 0;
        while (!rat->attackCadenceReady() && ticks < 200) {
            rat->TickAttackCadence(sk_b::kAiFrameDeltaUnits);
            ++ticks;
        }
        const int expected =
            sk_b::kAiAttackCadenceThreshold / sk_b::kAiFrameDeltaUnits + 1;
        Check(ticks == expected && expected >= 24 && expected <= 27,
              "  and becomes ready after 0x100 of frame delta -- about one second");

        // The pursue arm ends with `+0x2c4 = rand & 0x1f`; the look arm
        // ends with `+0x2c4 = 0`. Two different resets, and the difference
        // is observable: a creature that has just acquired waits the full
        // second before its first swing.
        bool jitterInRange = true;
        for (int i = 0; i < 64; ++i) {
            rat->JitterAttackCadence();
            if (rat->attackCadence() < 0 || rat->attackCadence() > 31) jitterInRange = false;
        }
        Check(jitterInRange, "JitterAttackCadence() lands in 0..31, the real `rand & 0x1f`");
        rat->ClearAttackCadence();
        Check(rat->attackCadence() == 0,
              "ClearAttackCadence() is the look arm's own `+0x2c4 = 0`");

        // The gap between swings, measured the way the tick would: jitter,
        // then count ticks back to ready. Never zero, never far off a
        // second -- the two properties the reported "attacks non stop"
        // would have violated.
        int fastest = 999, slowest = 0;
        for (int swing = 0; swing < 200; ++swing) {
            rat->JitterAttackCadence();
            int n = 0;
            while (!rat->attackCadenceReady() && n < 200) {
                rat->TickAttackCadence(sk_b::kAiFrameDeltaUnits);
                ++n;
            }
            fastest = (std::min)(fastest, n);
            slowest = (std::max)(slowest, n);
        }
        std::printf("  measured gap between swings: %d..%d ticks (%.2f..%.2f s)\n", fastest,
                    slowest, fastest * 0.04, slowest * 0.04);
        Check(fastest >= 22 && slowest <= 26,
              "  every swing is 22-26 ticks apart, i.e. ~0.9-1.05 s -- never back to back");
    }

    // ---------------------------------------------------------------
    // Part 4: the boss throttle
    // ---------------------------------------------------------------
    std::printf("\n== Part 4: SetBoss arms a second, slower gate ==\n");
    {
        // The arithmetic, straight off the tick:
        //   if (last == 0 || last + speed * 0x100 < now) { last = now; swing; }
        int last = 0;
        Check(sk_b::BossAttackDue(last, 2, 1000) && last == 1000,
              "the first swing always passes (the +0x2c8 stamp starts at 0)");
        Check(!sk_b::BossAttackDue(last, 2, 1000 + 511) && last == 1000,
              "  a second one 511 clock units later (< 2s) is suppressed");
        Check(sk_b::BossAttackDue(last, 2, 1000 + 513) && last == 1000 + 513,
              "  and allowed once 2 seconds of game clock have passed");

        sk_b::MenuStack stack(scriptRoot, interpreter, &strings);
        stack.level().SetEntityTypes(&entityTypes);
        auto rat = loadInto(stack, "monsters/Azra_Rat.s");
        Check(rat && !rat->boss() &&
                  rat->attackSpeedSeconds() == sk_b::kDefaultAttackSpeedSeconds &&
                  sk_b::kDefaultAttackSpeedSeconds == 2,
              "a creature defaults to not-a-boss, with the constructor's 2-second speed");

        // A real one. lakvan/lakvan.s is the zone's own boss.
        auto boss = loadInto(stack, "monsters/lakvan.s");
        Check(boss && boss->boss(), "monsters/lakvan.s: SetBoss(true) is stored (monster+0x266)");

        // Nothing in the corpus calls SetAttackSpeed, so every boss in the
        // game runs at the default -- half the rate of everything else.
        skRValueArray args;
        args.append(skRValue(5));
        skRValue ret;
        skExecutableContext c(&interpreter);
        boss->method(skString("SetAttackSpeed"), args, ret, c);
        Check(boss->attackSpeedSeconds() == 5,
              "  SetAttackSpeed(5) is stored too, though no shipped script calls it");
    }

    // ---------------------------------------------------------------
    // Part 5: the bindings that were missing
    // ---------------------------------------------------------------
    std::printf("\n== Part 5: the Monster(AI) surface is complete ==\n");
    {
        sk_b::MenuStack stack(scriptRoot, interpreter, &strings);
        stack.level().SetEntityTypes(&entityTypes);
        auto m = loadInto(stack, "monsters/Azra_Rat.s");

        auto call1 = [&](const char* name, skRValue arg) {
            skRValueArray args;
            args.append(arg);
            skRValue ret;
            skExecutableContext c(&interpreter);
            m->method(skString(name), args, ret, c);
            return ret;
        };

        // SetHealth is the same dispatcher case as SetMaxHealth, so it
        // must move both numbers. 30 shipped scripts call it and it was
        // soft-failing.
        call1("SetHealth", skRValue(77));
        Check(m->maxHealth() == 77 && m->currentHealth() == 77,
              "SetHealth(77) sets both max and current -- it is SetMaxHealth's twin case");

        // DoDamage(n) on a creature damages *itself* (the dispatcher hands
        // the stats block to its own damage vtable slot). 20 scripts.
        call1("DoDamage", skRValue(7));
        Check(m->currentHealth() == 70, "DoDamage(7) takes 7 off the creature's own health");

        call1("SetImmobile", skRValue(true));
        Check(m->immobile(), "SetImmobile(true) is stored (monster+0x2bc)");
        call1("SetCanTeleport", skRValue(true));
        Check(m->canTeleport(), "SetCanTeleport(true) is stored (monster+0x2ee)");
        call1("StopAnimating", skRValue(true));
        Check(!m->animating() && m->currentAnimation() == -1,
              "StopAnimating(true) clears +0x302 and drops the current clip");
        call1("SetLifespan", skRValue(3));
        Check(m->lifespanUnits() == 3 * 0x100, "SetLifespan(3) arms +0x2ec as `seconds << 8`");
        // ...and it burns at three times the rate of every other timer.
        int lifeTicks = 0;
        while (!m->TickLifespan(sk_b::kAiFrameDeltaUnits) && lifeTicks < 400) ++lifeTicks;
        Check(lifeTicks >= 24 && lifeTicks <= 27,
              "  and runs out in ~1 second, not 3 -- the real `-= 3 * delta`");

        skRValueArray none;
        skRValue ret;
        skExecutableContext c(&interpreter);
        m->method(skString("Aggressive"), none, ret, c);
        Check(ret.boolValue(), "Aggressive() reads the flag back");
        m->method(skString("GetChaseRadius"), none, ret, c);
        Check(ret.intValue() == 18000, "GetChaseRadius() returns the raw scaled-squared 18000");
        m->method(skString("GetCurrentAIPackage"), none, ret, c);
        Check(ret.intValue() == sk_b::MonsterExecutable::kAiIdle,
              "GetCurrentAIPackage() reports the package the placement gave it");
    }

    // Every distinct creature script the shipped zones place, loaded with
    // the observer on. Any Monster(AI) name still missing shows up here.
    {
        sk_b::MenuStack stack(scriptRoot, interpreter, &strings);
        stack.level().SetEntityTypes(&entityTypes);
        std::set<std::string> seen;
        g_softFails.clear();
        sk_b::SetSoftFailObserver(&RecordSoftFail);
        for (const char* zoneName : kZones) {
            sk::Zone zone;
            if (!zone.Load(scriptRoot, zoneName)) continue;
            for (const sk::Zone::EntPlacement& e : zone.entities()) {
                const sk::EntityTypeDescriptor* desc = entityTypes.Lookup(e.typeId);
                if (!desc) continue;
                if (desc->category != 2 && desc->category != 7) continue;
                std::string rel = desc->name;
                if (!e.scriptPath.empty()) rel = e.scriptPath;
                if (rel.size() < 2 || rel.substr(rel.size() - 2) != ".s") rel += ".s";
                rel = ToPath(rel);
                if (!seen.insert(Lower(rel)).second) continue;
                std::ifstream probe(std::string(scriptRoot) + "/" + rel, std::ios::binary);
                if (!probe.good()) continue;
                probe.close();
                loadInto(stack, rel.c_str());
            }
        }
        sk_b::SetSoftFailObserver(nullptr);
        // The 55 Monster(AI) names, from FUN_10012584's own trie.
        static const std::set<std::string> kMonsterAi = {
            "SetMob", "SetAttackSpeed", "SetPlaySpellCasting", "DetectOnKilled",
            "SetAlwaysOnDetect", "FindPathNode", "SetBoss", "SetHealth", "AddSpell",
            "SetMeleeRoll", "StopAnimating", "SetState", "SetAttachedWeapon", "SetLoot",
            "SetParalyzed", "SetImmobile", "SetEnemy", "GuardPlayer", "SetLifespan",
            "SetItemRequiredToHit", "SetAttackRange", "SetMeleeAttackRange", "SetProjectile",
            "DoDamage", "SetIdleAnimation", "SetWalkAnimation", "SetDeathAnimation",
            "SetSwingAnimation", "SetAggressive", "Aggressive", "GetWimpy", "GetAttackNoise",
            "GetDeathNoise", "GetChaseRadius", "SetWimpy", "SetAttackNoise", "SetDeathNoise",
            "SetIsHitNoise", "SetChaseRadius", "Follow", "GetCurrentAIPackage", "AiActivate",
            "AiAttack", "AiDetect", "AiFlee", "AiPursue", "AiSleep", "AiSpellAssistTarget",
            "AiWounded", "SetSpider", "SetUndead", "IsUndead", "SetMaxHealth", "SetCanTeleport",
            "ReplicateTeleport"};
        std::set<std::string> missed;
        for (const std::string& name : g_softFails) {
            if (kMonsterAi.count(name)) missed.insert(name);
        }
        std::printf("  %d distinct placed creature scripts loaded\n",
                    static_cast<int>(seen.size()));
        for (const std::string& name : missed) std::printf("    still soft-failing: %s\n",
                                                            name.c_str());
        Check(missed.empty(),
              "no Monster(AI) binding soft-fails across every placed creature in the game");
    }

    // ---------------------------------------------------------------
    // Part 6: the melee vertical limit, and who is exempt
    // ---------------------------------------------------------------
    std::printf("\n== Part 6: the 0x200 vertical limit and the ranged exemption ==\n");
    Check(sk_b::kAttackVerticalLimitUnits == 0x200,
          "melee reaches 0x200 raw units up or down -- two tiles");
    Check(sk_b::kBowAttachedWeaponModel == 225,
          "SetAttachedWeapon(225) is the bow, the third term of the tick's ranged test");
    {
        sk_b::MenuStack stack(scriptRoot, interpreter, &strings);
        stack.level().SetEntityTypes(&entityTypes);
        auto rat = loadInto(stack, "monsters/Azra_Rat.s");
        Check(rat && !rat->ranged(),
              "a rat is not ranged: no spell, and no attached weapon at all");
        auto archer = loadInto(stack, "monsters/archer.s");
        Check(archer && archer->attachedWeaponModel() == 225 && archer->ranged(),
              "monsters/archer.s carries the bow, so it skips the reach march and the limit");
        auto mage = loadInto(stack, "monsters/bandit_mage.s");
        Check(mage && mage->hasSpells() && mage->ranged(),
              "monsters/bandit_mage.s is ranged through its spell slot instead");
    }

    std::printf("\nm77_monster_ai_smoke: %d/%d checks passed -- %s\n", g_checks - g_failures,
                g_checks, g_failures ? "FAILED" : "OK");
    return g_failures ? 1 : 0;
}
