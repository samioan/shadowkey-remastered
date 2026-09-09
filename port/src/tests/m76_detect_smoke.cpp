// M76 smoke test: the NPC that speaks first.
//
// M75 restored the prompt you walk up to and press Use on. This is the
// other mechanism in the same AI tick: a non-aggressive creature runs the
// *same* perception test an aggressive one runs, and where that one would
// acquire a target and attack, this one runs its script's `OnDetect`
// handler instead. 31 shipped scripts define one.
//
// Decompiled for this milestone:
//   FUN_10082224  the AI tick's idle/pursue arm, and the 0x10083094 guard
//   FUN_100683d4  vtable[0x5c] -- the scaled-squared distance
//   FUN_10004d70  vtable[0x21c] -- the line-of-sight ray march
//   FUN_100182d0  the march itself (half-tile steps, `.zmp` bit 2)
//   FUN_10044b8c / FUN_1004b848   stats vtable[0x40] -- Agility / 5
//   FUN_100815e0  the creature constructor's defaults (+0x258 = 1, ...)
//   0x10084924 cases 3 and 4   DetectOnKilled / SetAlwaysOnDetect
//   FUN_10064c60  the MenuClosed notifier -- which nothing calls
// See simkin_bindings/on_detect.h for the writeup.
//
// Part 1  the perception arithmetic, transcribed
// Part 2  what a real Init() leaves behind: armed, or dead on arrival
// Part 3  three real OnDetect handlers, run for real
// Part 4  the census -- every category-2/7 placement in all 21 zones
// Part 5  the two multiplayer-only bindings stop soft-failing
// Part 6  MenuClosed: shipped, unreachable, deliberately not implemented
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
#include "simkin_bindings/menu_executable.h"
#include "simkin_bindings/menu_stack.h"
#include "simkin_bindings/monster_executable.h"
#include "simkin_bindings/native_binding_common.h"
#include "simkin_bindings/on_detect.h"
#include "simkin_bindings/player_executable.h"
#include "simkin_bindings/zone_script_executable.h"
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

// The 21 shipped zones, i.e. every `<name>.ent` next to a `<name>.zon`.
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

// Does this script define a handler of its own, without running it? The
// same lookup skTreeNodeObject::method() does -- a top-level child node of
// that name -- which is exactly what `monster+0x120`'s dispatch resolves
// against in the engine.
bool DefinesHandler(sk_b::MonsterExecutable& script, const char* name) {
    skTreeNode* node = script.getNode();
    return node != nullptr && node->findChild(skString(name)) != nullptr;
}

// The soft-fail observer, so Part 5 can prove a binding stopped missing.
std::vector<std::string> g_softFails;
void RecordSoftFail(const char* /*object*/, const char* methodName, const char* /*args*/) {
    g_softFails.push_back(methodName ? methodName : "");
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
        std::printf("m76_detect_smoke: FAILED to load stringtable.eng\n");
        return 1;
    }
    sk::EntityTypeTable entityTypes;
    if (!entityTypes.Load(scriptRoot)) {
        std::printf("m76_detect_smoke: FAILED to load entities.txt\n");
        return 1;
    }

    skInterpreter interpreter;

    // A creature loaded through its real binding class, with Init() run --
    // the same three lines main.cpp's zone-load loop uses.
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
    // Part 1: the perception arithmetic
    // ---------------------------------------------------------------
    std::printf("\n== Part 1: the perception roll, transcribed ==\n");

    // `monster+0x258` is written to 1 by FUN_100815e0 and by nothing else
    // in the whole binary -- no binding, no script, no engine path.
    Check(sk_b::kDetectStrength == 1, "the creature's detect strength is the constructor's 1");

    // r = (d << 16) / ((d + agi/5) << 8);  percent = (r * 100) >> 8
    Check(sk_b::DetectMissPercent(1, 0) == 100,
          "Agility 0: 100% miss per tick (still ~1 tick in 101 -- rand(0,100) is inclusive)");
    Check(sk_b::DetectMissPercent(1, 40) == 10, "Agility 40: 10% chance of going unnoticed");
    Check(sk_b::DetectMissPercent(1, 100) == 4, "Agility 100: 4%");
    {
        // Monotone: more Agility never makes you easier to spot. (The real
        // getter would also add two equipped-item bonuses this port has no
        // model for; both only ever raise the term.)
        bool monotone = true;
        int previous = 101;
        for (int agility = 0; agility <= 200; ++agility) {
            int p = sk_b::DetectMissPercent(1, agility);
            if (p > previous) monotone = false;
            previous = p;
        }
        Check(monotone, "  and the curve never rises with Agility");
    }
    // `if (rand(0,100) < missPercent) noticed = false`
    Check(!sk_b::DetectionNoticed(10, 9), "a roll below the threshold means unnoticed");
    Check(sk_b::DetectionNoticed(10, 10), "  and a roll at it means noticed");

    // The sightline budget: `monster+0x2dc >> 8`, in tiles.
    Check(sk_b::SightRangeTiles(0x6a4) == 6,
          "the default SetAttackRange (0x6a4) buys 6 tiles of OnDetect sight");
    {
        sk_b::MenuStack stack(scriptRoot, interpreter, &strings);
        stack.level().SetEntityTypes(&entityTypes);
        auto rat = loadInto(stack, "monsters/Azra_Rat.s");
        Check(rat && rat->attackRangeRaw() == 0x6a4,
              "  and no shipped creature changes it (Azra_Rat.s still has the default)");
    }

    // ---------------------------------------------------------------
    // Part 2: armed, or dead on arrival
    // ---------------------------------------------------------------
    std::printf("\n== Part 2: what a real Init() leaves behind ==\n");
    {
        sk_b::MenuStack stack(scriptRoot, interpreter, &strings);
        stack.level().SetEntityTypes(&entityTypes);

        // The engine's own two conditions for reaching the OnDetect arm:
        // the package must be idle (or pursue-with-no-target, which a
        // non-aggressive creature never leaves), and `+0x2ac` must be 0.
        auto armed = [](sk_b::MonsterExecutable& m) {
            return !m.aggressive() &&
                   (m.aiPackage() == sk_b::MonsterExecutable::kAiIdle ||
                    m.aiPackage() == sk_b::MonsterExecutable::kAiPursue);
        };

        // The canonical shape: SetAggressive(false) + AiDetect(), with the
        // script's own comment saying exactly what it is doing.
        auto brawler = loadInto(stack, "monsters/bbrawler_talk.s");
        Check(brawler && DefinesHandler(*brawler, "OnDetect") && armed(*brawler),
              "monsters/bbrawler_talk.s: SetAggressive(false) + AiDetect() -> armed");

        // Same shape, in a zone folder.
        auto trailslag = loadInto(stack, "monsters/olpac_trailslag.s");
        Check(trailslag && DefinesHandler(*trailslag, "OnDetect") && armed(*trailslag),
              "monsters/olpac_trailslag.s: armed");

        // A guard that calls SetAggressive(true) in its own Init reaches
        // the *attack* arm of the same `if`, so its OnDetect can never run.
        // Shipped content, faithfully reproduced.
        auto guard = loadInto(stack, "delfhide/dh_guard_talk.s");
        Check(guard && DefinesHandler(*guard, "OnDetect") && guard->aggressive() && !armed(*guard),
              "delfhide/dh_guard_talk.s defines OnDetect but SetAggressive(true) kills it");

        // M77 -- CORRECTED. M76 read these two as dead ends: neither
        // calls AiDetect() (Pergan's is `//AiDetect();`, commented out in
        // the shipped file), so both were believed to stay in the actor
        // constructor's package -1. They do not. Placement puts every
        // creature in package 2 before Init() runs, so AiDetect() only ever
        // re-states what is already true, and *both of these work*. Calling
        // it is decoration; not calling it changes nothing.
        auto lakvan = loadInto(stack, "monsters/lakvan.s");
        Check(lakvan && DefinesHandler(*lakvan, "OnDetect") && armed(*lakvan),
              "monsters/lakvan.s never calls AiDetect() -- and is armed anyway (M77)");
        Check(lakvan && lakvan->usable(), "  (he is also talkable through the M75 use prompt)");

        auto pergan = loadInto(stack, "twilite/pergan_asuul.s");
        Check(pergan && DefinesHandler(*pergan, "OnDetect") && armed(*pergan),
              "twilite/pergan_asuul.s: its AiDetect() is commented out, and armed anyway");

        // A hostile creature is armed for the *other* arm.
        auto rat = loadInto(stack, "monsters/Azra_Rat.s");
        Check(rat && rat->aggressive() && rat->aiPackage() == sk_b::MonsterExecutable::kAiIdle,
              "monsters/Azra_Rat.s: aggressive + AiDetect() -> the attack arm, as before");
    }

    // ---------------------------------------------------------------
    // Part 3: the handlers, run for real
    // ---------------------------------------------------------------
    std::printf("\n== Part 3: three shipped OnDetect handlers ==\n");

    // bbrawler_talk.s: `OpenMenu("Talker"); AiSleep();`
    {
        sk_b::MenuStack stack(scriptRoot, interpreter, &strings);
        stack.level().SetEntityTypes(&entityTypes);
        auto brawler = loadInto(stack, "monsters/bbrawler_talk.s");
        sk_b::MenuExecutable* before = stack.currentMenu();
        bool ran = brawler && brawler->InvokeOnDetect(
                                   static_cast<skiExecutable*>(&stack.player()));
        sk_b::MenuExecutable* after = stack.currentMenu();
        Check(ran, "bbrawler_talk.s has an OnDetect the host can run");
        Check(after != nullptr && after != before && !after->rows().empty(),
              "  it opens \"Talker\" with rows");
        Check(after != nullptr && after->opener() == static_cast<skiExecutable*>(brawler.get()),
              "  and GetOpener() is the brawler, because he opened it on himself");
        Check(brawler && brawler->aiPackage() == sk_b::MonsterExecutable::kAiAsleep,
              "  its own AiSleep() takes him out of the branch, so it cannot re-fire");
    }

    // olpac_trailslag.s: guarded by its own `done_menu` field and by
    // `not GetPlayer().QuestSolved(14)`, then AiSleep().
    {
        sk_b::MenuStack stack(scriptRoot, interpreter, &strings);
        stack.level().SetEntityTypes(&entityTypes);
        auto npc = loadInto(stack, "monsters/olpac_trailslag.s");
        sk_b::MenuExecutable* before = stack.currentMenu();
        npc->InvokeOnDetect(static_cast<skiExecutable*>(&stack.player()));
        sk_b::MenuExecutable* first = stack.currentMenu();
        Check(first != nullptr && first != before && !first->rows().empty(),
              "olpac_trailslag.s opens ghstpass/GP_Menu3 on the player");
        // Its own latch: a second detection does nothing.
        npc->SetAiPackage(sk_b::MonsterExecutable::kAiIdle);
        npc->InvokeOnDetect(static_cast<skiExecutable*>(&stack.player()));
        Check(stack.currentMenu() == first,
              "  and its own `done_menu` flag stops a second one");
    }

    // erthcave/azra_zombie.s: turns hostile *and* opens a menu in the same
    // handler -- the two arms of the branch in one script.
    {
        sk_b::MenuStack stack(scriptRoot, interpreter, &strings);
        stack.level().SetEntityTypes(&entityTypes);
        auto zombie = loadInto(stack, "erthcave/azra_zombie.s");
        Check(zombie && !zombie->aggressive(), "erthcave/azra_zombie.s starts non-aggressive");
        sk_b::MenuExecutable* before = stack.currentMenu();
        zombie->InvokeOnDetect(static_cast<skiExecutable*>(&stack.player()));
        Check(zombie->aggressive(), "  its OnDetect calls SetAggressive(true)");
        Check(stack.currentMenu() != before && stack.currentMenu() != nullptr,
              "  and opens erthcave/EC_Menu7 in the same handler");
    }

    // ---------------------------------------------------------------
    // Part 4: the census
    // ---------------------------------------------------------------
    std::printf("\n== Part 4: every category-2/7 placement in all 21 zones ==\n");
    int placed = 0, armedPlacements = 0, armedWithHandler = 0, deadHandlers = 0;
    std::set<std::string> armedScripts;
    std::set<std::string> deadHandlerScripts;
    for (const char* zoneName : kZones) {
        sk::Zone zone;
        if (!zone.Load(scriptRoot, zoneName)) continue;
        sk_b::MenuStack stack(scriptRoot, interpreter, &strings);
        stack.level().SetEntityTypes(&entityTypes);
        std::unique_ptr<sk_b::ZoneScriptExecutable> zoneScript;
        {
            std::string zp = std::string(scriptRoot) + "/" + zoneName + ".s";
            skExecutableContext zctxt(&interpreter);
            try {
                zoneScript = std::make_unique<sk_b::ZoneScriptExecutable>(skString(zp.c_str()),
                                                                           zctxt, stack);
                stack.level().AttachZoneScript(zoneScript.get());
            } catch (skParseException&) {
            } catch (skRuntimeException&) {
            }
        }

        for (const sk::Zone::EntPlacement& e : zone.entities()) {
            const sk::EntityTypeDescriptor* desc = entityTypes.Lookup(e.typeId);
            if (!desc) continue;
            if (desc->category != 2 && desc->category != 7) continue;
            std::string rel = desc->name;
            if (!e.scriptPath.empty()) rel = e.scriptPath;
            if (rel.size() < 2 || rel.substr(rel.size() - 2) != ".s") rel += ".s";
            rel = ToPath(rel);
            // Same existence probe M75's census uses: skScriptedExecutable
            // builds an empty object from a missing path rather than
            // throwing, and most placements are an entities.txt "!label"
            // with no script behind them.
            {
                std::ifstream probe(std::string(scriptRoot) + "/" + rel, std::ios::binary);
                if (!probe.good()) continue;
            }
            auto npc = loadInto(stack, rel.c_str());
            if (!npc) continue;
            ++placed;
            const bool isArmed = !npc->aggressive() &&
                                  (npc->aiPackage() == sk_b::MonsterExecutable::kAiIdle ||
                                   npc->aiPackage() == sk_b::MonsterExecutable::kAiPursue);
            const bool hasHandler = DefinesHandler(*npc, "OnDetect");
            if (isArmed) ++armedPlacements;
            if (isArmed && hasHandler) {
                ++armedWithHandler;
                armedScripts.insert(Lower(rel));
            }
            if (!isArmed && hasHandler) {
                ++deadHandlers;
                deadHandlerScripts.insert(Lower(rel));
            }
        }
    }
    std::printf("  category-2/7 placements with a real script: %d\n", placed);
    std::printf("  ...armed to detect (non-aggressive, package idle): %d\n", armedPlacements);
    std::printf("  ...of those, with an OnDetect handler: %d\n", armedWithHandler);
    std::printf("  placements whose OnDetect can never run: %d\n", deadHandlers);

    for (const std::string& s : armedScripts) std::printf("    armed: %s\n", s.c_str());
    for (const std::string& s : deadHandlerScripts) std::printf("    dead:  %s\n", s.c_str());
    // M77 -- CORRECTED, and this is the measurement the milestone is for.
    // M76 asserted 29 / 28 / 34 here, computed with every creature that
    // never calls an Ai* binding sitting in package -1. With the spawn
    // package right (simkin_bindings/monster_ai.h), 241 of the 1539
    // placements are non-aggressive and looking, 58 of them have an
    // OnDetect the engine reaches, and only 4 ship one it cannot.
    Check(placed == 1539 && armedPlacements == 241,
          "1539 creature placements; 241 of them are non-aggressive and looking");
    Check(armedWithHandler == 58,
          "  58 have an OnDetect handler that actually runs (M76 could only reach 28)");
    Check(deadHandlers == 4,
          "  and only 4 ship one the engine cannot reach (M76 believed 34)");

    // The 20 distinct scripts behind those 58 placements. The six M76
    // found are still here; the other fourteen are the ones it wrote off
    // for never calling AiDetect().
    const char* kArmed[] = {
        "broken2/perosius_temp.s",       // a boss: convo, then turns hostile and attacks
        "delfhide/delfran.s",            // Delfran, the Delve Hideout quest-giver
        "dstar_e/dse_skyrim_archer.s",   // city guard, hostile once you have trespassed
        "dstar_e/dse_skyrim_soldier.s",  //   ...its partner
        "dstar_e/thief_ace_archer.s",    // the thieves who greet you before they fight
        "dstar_e/thief_raider.s",        //
        "erthcave/azra_zombie.s",        // SetAggressive(true) + EC_Menu7 in one handler
        "monsters/lakvan.s",             // M77: works after all
        "monsters/olpac_trailslag.s",    // opens ghstpass/GP_Menu3, once per save
        "raiders/blu_bounder.s",         // the nine arena creatures, three per colour
        "raiders/blu_spiders.s",         //
        "raiders/blu_wickeder.s",        //
        "raiders/gld_bounder.s",         //
        "raiders/gld_spider.s",          //
        "raiders/gld_wickeder.s",        //
        "raiders/raider_enter.s",        // the arena doorman, opens Raiders/enter
        "raiders/red_bounder.s",         //
        "raiders/red_spider.s",          //
        "raiders/red_wickeder.s",        //
        "twilite/pergan_asuul.s",        // M77: works after all
    };
    for (const char* rel : kArmed) {
        Check(armedScripts.count(rel) == 1, std::string("placed and armed: ") + rel);
    }
    Check(armedScripts.size() == sizeof(kArmed) / sizeof(kArmed[0]),
          "  and those are all of them");

    // What is left dead is one shape only: an Init() that calls
    // SetAggressive(true), sending the creature to the attack arm of the
    // same `if` instead. Shipped content, not a gap.
    Check(deadHandlerScripts.count("delfhide/dh_guard_talk.s") == 1,
          "placed with a dead OnDetect (SetAggressive(true)): delfhide/dh_guard_talk.s");
    // `monsters/bbrawler_talk.s`, the script this mechanism is best
    // documented by, is not placed in any of the 21 zones -- only the
    // plain `monsters/Bandit_Brawler.s` is. Its MenuClosed half being dead
    // (Part 6) and the whole creature being unplaced are the same story:
    // "talk to him, then kill him" did not ship.
    Check(armedScripts.count("monsters/bbrawler_talk.s") == 0 &&
              deadHandlerScripts.count("monsters/bbrawler_talk.s") == 0,
          "monsters/bbrawler_talk.s is never placed in the shipped game");

    // ---------------------------------------------------------------
    // Part 5: SetAlwaysOnDetect / DetectOnKilled
    // ---------------------------------------------------------------
    std::printf("\n== Part 5: the two multiplayer-only bindings ==\n");
    {
        sk_b::MenuStack stack(scriptRoot, interpreter, &strings);
        stack.level().SetEntityTypes(&entityTypes);
        g_softFails.clear();
        sk_b::SetSoftFailObserver(&RecordSoftFail);
        auto spider = loadInto(stack, "raiders/blu_spiders.s");
        sk_b::SetSoftFailObserver(nullptr);
        Check(spider && spider->alwaysOnDetect(),
              "raiders/blu_spiders.s's SetAlwaysOnDetect(true) is stored (monster+0x306)");
        bool sawSoftFail = false;
        for (const std::string& name : g_softFails) {
            if (name == "SetAlwaysOnDetect" || name == "DetectOnKilled") sawSoftFail = true;
        }
        Check(!sawSoftFail, "  and neither binding soft-fails any more");

        // The flag changes nothing in singleplayer: its only reader sits
        // behind `engine+0x5c0`, and the guard it is one term of
        // (`+0x306 || !multiplayer || isHost`) is already satisfied by
        // `!multiplayer`. M77: what M76 asserted here instead -- that
        // blu_spiders.s is asleep because it never calls AiDetect() -- was
        // an artefact of the wrong spawn package. It is armed, and its
        // OnDetect runs; the flag is still inert.
        Check(spider && spider->aiPackage() == sk_b::MonsterExecutable::kAiIdle,
              "  and the creature is armed by its placement, not by the flag");

        // DetectOnKilled has no shipped caller at all; drive it directly.
        auto rat = loadInto(stack, "monsters/Azra_Rat.s");
        skRValueArray args;
        args.append(skRValue(true));
        skRValue ret;
        skExecutableContext c(&interpreter);
        rat->method(skString("DetectOnKilled"), args, ret, c);
        Check(rat->detectOnKilled(), "DetectOnKilled(true) is stored (monster+0x307)");
    }

    // ---------------------------------------------------------------
    // Part 6: MenuClosed
    // ---------------------------------------------------------------
    std::printf("\n== Part 6: MenuClosed is shipped but unreachable ==\n");
    {
        sk_b::MenuStack stack(scriptRoot, interpreter, &strings);
        stack.level().SetEntityTypes(&entityTypes);
        auto brawler = loadInto(stack, "monsters/bbrawler_talk.s");
        Check(brawler && DefinesHandler(*brawler, "MenuClosed"),
              "monsters/bbrawler_talk.s ships a MenuClosed handler");
        // The notifier that would run it (FUN_10064c60) sits at vtable+0x3c
        // in all 31 entity vtables and has no call site anywhere in the
        // image -- so the brawler stays friendly after the conversation on
        // the device too. Not implementing it is the faithful choice.
        brawler->InvokeOnDetect(static_cast<skiExecutable*>(&stack.player()));
        Check(brawler && !brawler->aggressive(),
              "  after the conversation he stays non-aggressive, as on the device");
        Check(brawler && brawler->aiPackage() == sk_b::MonsterExecutable::kAiAsleep,
              "  and asleep -- his own AiSleep() put him there, and only MenuClosed "
              "would have re-armed him");
    }

    std::printf("\nm76_detect_smoke: %d/%d checks passed -- %s\n", g_checks - g_failures, g_checks,
                g_failures ? "FAILED" : "OK");
    return g_failures ? 1 : 0;
}
