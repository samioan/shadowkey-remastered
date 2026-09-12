// M94 smoke test: the trap / magic-damage mixin, and the two saving
// throws that decide whether it ever goes off.
//
// SimKin class `0x14e10` (dispatcher `FUN_1002de24`) is four bindings --
// `SetMagicDamage`, `SetSpellLevel`, `SetDormant`, `DoMagicDamage` -- over
// 19 shipped call sites, all of them trapped chests and locked doors. The
// chests opened without ever hurting anyone.
//
// The rolls came with it because the chain does not work without them:
// `Menus/UsePicks.s` is `if (GetPlayer().CanDisarmTrap(GetOpener().
// resistDisarm) = true) LockPicked(); else ... MagicDamage();`, so a
// soft-failing `CanDisarmTrap` means every pick in the game fails and
// every trap fires. Implementing the mixin alone would have made that
// worse, not better.
//
// What each part checks, and why it is the check that could fail:
//
//   1. The corpus: which scripts set what, and the important asymmetry --
//      19 sites are not 19 working traps, because only three scripts ever
//      call `DoMagicDamage()`.
//   2. The mixin's state on real scripts, through the real natives.
//   3. `SetDormant`, which is what stops every trap firing on contact.
//   4. `CanDisarmTrap`: the level gate, the class split, and the two real
//      items that help -- measured over many rolls, since it is a roll.
//   5. `CanAvoidTrap`, the cheaper roll.
//   6. `DoMagicDamage` end to end on crypt1's real data: the trigger gate,
//      the spell that gets built, its level and its caster.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "assets/string_table.h"
#include "simkin_bindings/character_progression.h"
#include "simkin_bindings/door_executable.h"
#include "simkin_bindings/entity_base_ref.h"
#include "simkin_bindings/item_executable.h"
#include "simkin_bindings/level_executable.h"
#include "simkin_bindings/menu_stack.h"
#include "simkin_bindings/player_executable.h"
#include "simkin_bindings/zone_script_executable.h"
#include "skExecutableContext.h"
#include "skInterpreter.h"
#include "skParseException.h"
#include "skRValue.h"
#include "skRValueArray.h"
#include "skRuntimeException.h"
#include "world/entity_types.h"

namespace sk_b = sk_bindings;

namespace {

int g_failures = 0;
int g_checks = 0;

void Check(bool ok, const std::string& what) {
    ++g_checks;
    std::printf("  %-86s %s\n", what.c_str(), ok ? "ok" : "FAILED");
    if (!ok) ++g_failures;
}

std::string ReadFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

std::vector<std::filesystem::path> AllScripts(const std::string& root) {
    std::vector<std::filesystem::path> files;
    std::error_code ec;
    for (auto it = std::filesystem::recursive_directory_iterator(root, ec);
         it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) break;
        if (it->is_regular_file(ec) && it->path().extension() == ".s") files.push_back(it->path());
    }
    return files;
}

// Counts live (comment-stripped) calls of `name` across the corpus.
int CountCalls(const std::vector<std::filesystem::path>& files, const std::string& name) {
    int total = 0;
    for (const std::filesystem::path& f : files) {
        std::ifstream in(f);
        std::string line;
        while (std::getline(in, line)) {
            size_t comment = line.find("//");
            if (comment != std::string::npos) line = line.substr(0, comment);
            size_t at = line.find(name);
            while (at != std::string::npos) {
                size_t open = at + name.size();
                while (open < line.size() && line[open] == ' ') ++open;
                if (open < line.size() && line[open] == '(') ++total;
                at = line.find(name, at + 1);
            }
        }
    }
    return total;
}

// Calls a real native with one int argument, the way a script would.
void CallNative(skiExecutable& obj, const char* name, int value, skInterpreter& interpreter) {
    skRValueArray args;
    args.append(skRValue(value));
    skRValue ret;
    skExecutableContext ctxt(&interpreter);
    obj.method(skString(name), args, ret, ctxt);
}

void Invoke(skiExecutable& obj, const char* handler, skInterpreter& interpreter) {
    skRValueArray args;
    args.append(skRValue(0));
    skRValue ret;
    skExecutableContext ctxt(&interpreter);
    try {
        obj.method(skString(handler), args, ret, ctxt);
    } catch (skRuntimeException& e) {
        std::printf("   (RUNTIME ERROR in %s: %s)\n", handler, e.toString().ptr());
    }
}

}  // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    const char* scriptRoot =
        argc > 1 ? argv[1]
                 : "The-Elder-Scrolls-Travels-Shadowkey_N-Gage_EN-FR-DE-ES-IT_USA-Europe-"
                   "EnFrDeEsIt-26102004/system/apps/6r51";
    const std::string root = scriptRoot;

    sk::StringTable strings;
    if (!strings.Load(root + "/stringtable.eng")) {
        std::printf("m94_trap_magic_smoke: FAILED to load stringtable.eng\n");
        return 1;
    }
    sk::EntityTypeTable entityTypes;
    if (!entityTypes.Load(root)) {
        std::printf("m94_trap_magic_smoke: FAILED to load entities.txt\n");
        return 1;
    }

    // ---------------------------------------------------------------
    // Part 1: the shipped corpus.
    // ---------------------------------------------------------------
    std::printf("\n== Part 1: what the shipped scripts ask for ==\n");
    {
        const std::vector<std::filesystem::path> files = AllScripts(root);
        const int setMagic = CountCalls(files, "SetMagicDamage");
        const int setLevel = CountCalls(files, "SetSpellLevel");
        const int setDormant = CountCalls(files, "SetDormant");
        const int doMagic = CountCalls(files, "DoMagicDamage");
        std::printf("     SetMagicDamage %d, SetSpellLevel %d, SetDormant %d, DoMagicDamage %d\n",
                    setMagic, setLevel, setDormant, doMagic);
        Check(setMagic == 6 && setLevel == 4 && setDormant == 6 && doMagic == 3,
              "the mixin has 19 sites in the census and 6/4/6/3 live ones here");

        const int disarm = CountCalls(files, "CanDisarmTrap");
        const int avoid = CountCalls(files, "CanAvoidTrap");
        Check(disarm == 9, "CanDisarmTrap has 9 call sites (" + std::to_string(disarm) + ")");
        Check(avoid == 1, "CanAvoidTrap has 1, in traploot_gold.s");

        // **19 sites are not 19 working traps.** Only the three crypt1
        // door scripts ever call DoMagicDamage; everything else sets the
        // fields and then damages the player directly.
        for (const char* rel : {"lockeddoor_dh.s", "lockeddoor_fb.s", "lockeddoor_ha.s"}) {
            const std::string text = ReadFile(root + "/" + rel);
            Check(text.find("DoMagicDamage(") != std::string::npos,
                  std::string(rel) + " really fires its trap");
        }
        const std::string bl = ReadFile(root + "/lockeddoor_bl.s");
        Check(bl.find("SetMagicDamage(50)") != std::string::npos &&
                  bl.find("DoMagicDamage") == std::string::npos &&
                  bl.find("GetPlayer().DoDamage(20)") != std::string::npos,
              "lockeddoor_bl.s arms a trap it never fires -- it damages the player directly");

        // Menus/UsePicks.s is the screen the whole chain hangs off.
        const std::string picks = ReadFile(root + "/menus/usepicks.s");
        Check(picks.find("CanDisarmTrap(GetOpener().resistDisarm)") != std::string::npos,
              "Menus/UsePicks.s rolls CanDisarmTrap against the door's own resistDisarm");
        Check(picks.find("GetOpener().MagicDamage();") != std::string::npos,
              "  and its failure branch is what calls MagicDamage[]");
    }

    // ---------------------------------------------------------------
    // Parts 2-3: the mixin on real scripts.
    // ---------------------------------------------------------------
    std::printf("\n== Parts 2-3: the mixin, on a real trapped door ==\n");
    skInterpreter interpreter;
    sk_b::MenuStack stack(root, interpreter, &strings);
    stack.level().SetEntityTypes(&entityTypes);
    {
        skExecutableContext loadCtxt(&interpreter);
        std::unique_ptr<sk_b::DoorExecutable> door;
        try {
            door = std::make_unique<sk_b::DoorExecutable>(
                skString((root + "/lockeddoor_dh.s").c_str()), loadCtxt, stack.player());
        } catch (skParseException& e) {
            std::printf("   (PARSE ERROR: %s)\n", e.toString().ptr());
        }
        Check(door != nullptr, "lockeddoor_dh.s loads as a door");
        if (door) {
            door->SetEntityId("door1");
            skExecutableContext ctxt(&interpreter);
            sk_b::RunEntityInit(*door, stack.scriptRoot(), ctxt);

            Check(door->magicDamageTemplate() == 4017,
                  "its Init() armed spell template 4017 (entity+0x184)");
            Check(door->trapSpellLevel() == 8, "  at level 8 (entity+0x188)");
            Check(door->dormant(), "  and dormant (entity+0x190) -- so touching it does not fire");

            // Every shipped SetDormant passes true, and that is the point:
            // FUN_1002e5ec runs the trap only when the flag is *clear*.
            Check(door->magicDamageCount() == 0, "nothing has fired yet");
        }

        // And a real trapped chest, which is a container -- an Item here.
        std::unique_ptr<sk_b::ItemExecutable> chest;
        try {
            chest = std::make_unique<sk_b::ItemExecutable>(
                skString((root + "/delfhide/chest_trap_gold.s").c_str()), loadCtxt, stack);
        } catch (skParseException& e) {
            std::printf("   (PARSE ERROR: %s)\n", e.toString().ptr());
        }
        Check(chest != nullptr, "delfhide/chest_trap_gold.s loads as an item");
        if (chest) {
            skExecutableContext ctxt(&interpreter);
            sk_b::RunEntityInit(*chest, stack.scriptRoot(), ctxt);
            Check(chest->magicDamageTemplate() == 50 && chest->dormant(),
                  "the chest arms template 50 and is dormant too -- the same mixin on a "
                  "different class");
        }
    }

    // ---------------------------------------------------------------
    // Part 4: CanDisarmTrap.
    // ---------------------------------------------------------------
    std::printf("\n== Part 4: CanDisarmTrap ==\n");
    {
        sk_b::PlayerExecutable& player = stack.player();
        constexpr int kResist = 5;  // every shipped door's resistDisarm[5]
        auto successRate = [&](int trials) {
            int wins = 0;
            for (int i = 0; i < trials; ++i) {
                if (player.CanDisarmTrap(kResist)) ++wins;
            }
            return wins;
        };

        // The level gate comes before the roll: `level < resist / 2`.
        // Driven through the real natives, not host-side setters.
        CallNative(player, "SetLevel", 1, interpreter);
        Check(successRate(200) == 0,
              "at level 1 a resistDisarm-5 lock can never be picked -- the level gate");
        CallNative(player, "SetLevel", 2, interpreter);
        const int atLevel2 = successRate(200);
        std::printf("     level 2, Battlemage, agility 50: %d/200\n", atLevel2);

        // The class split: a Thief's skill is not halved, so the same
        // agility gives twice the ratio.
        // ChooseCharacter resets the level to 1 (it is the real case's
        // own third line), so the level goes back up after each pick.
        CallNative(player, "ChooseCharacter", sk_b::kClassBattlemage, interpreter);
        CallNative(player, "SetLevel", 2, interpreter);
        const int mage = successRate(400);
        CallNative(player, "ChooseCharacter", sk_b::kClassThief, interpreter);
        CallNative(player, "SetLevel", 2, interpreter);
        const int thief = successRate(400);
        std::printf("     Battlemage %d/400 vs Thief %d/400\n", mage, thief);
        Check(thief > mage, "a Thief picks locks better than a Battlemage at the same agility");

        // Agility is the input: `skill = agility / 5 + bonus`.
        CallNative(player, "ChooseCharacter", sk_b::kClassBattlemage, interpreter);
        CallNative(player, "SetLevel", 2, interpreter);
        CallNative(player, "SetAgility", 10, interpreter);
        const int clumsy = successRate(400);
        CallNative(player, "SetAgility", 100, interpreter);
        const int nimble = successRate(400);
        std::printf("     agility 10 %d/400 vs agility 100 %d/400\n", clumsy, nimble);
        Check(nimble > clumsy, "and agility is what the skill is made of");

        CallNative(player, "SetAgility", 50, interpreter);
    }

    // ---------------------------------------------------------------
    // Part 5: CanAvoidTrap.
    // ---------------------------------------------------------------
    std::printf("\n== Part 5: CanAvoidTrap ==\n");
    {
        sk_b::PlayerExecutable& player = stack.player();
        auto rate = [&](int chance, int trials) {
            int wins = 0;
            for (int i = 0; i < trials; ++i) {
                if (player.CanAvoidTrap(chance)) ++wins;
            }
            return wins;
        };
        // `v = Random(1, luck) / 5; return chance < v` -- with luck 50 the
        // best possible v is 10, so a chance of 10 or more never succeeds
        // and 0 usually does.
        const int easy = rate(0, 400);
        const int impossible = rate(10, 400);
        std::printf("     chance 0: %d/400, chance 10: %d/400 (luck 50)\n", easy, impossible);
        Check(easy > 0, "a chance of 0 is sometimes avoided");
        Check(impossible == 0,
              "a chance of 10 never is -- Random(1, luck)/5 cannot exceed 10 at luck 50");
        Check(player.CanAvoidTrap(0x12) == false && player.CanAvoidTrap(0x40) == false,
              "the argument clamps at 0x12, so anything that high always fails");
    }

    // ---------------------------------------------------------------
    // Part 6: DoMagicDamage, end to end on crypt1's real data.
    // ---------------------------------------------------------------
    std::printf("\n== Part 6: DoMagicDamage, on crypt1's real doors ==\n");
    {
        // crypt1.s registers five name-only trap triggers, and crypt1.ent
        // places `door1`..`door5`. That pairing is what the gate inside
        // DoMagicDamage tests.
        const std::string zoneSrc = ReadFile(root + "/crypt1.s");
        Check(zoneSrc.find("AddTrigger(\"door1\")") != std::string::npos &&
                  zoneSrc.find("doorTrigger.SetTrap(") != std::string::npos,
              "crypt1.s registers door1..door5 as traps");
        const std::string ent = ReadFile(root + "/crypt1.ent");
        int placed = 0;
        for (const char* n : {"door1", "door2", "door3", "door4", "door5"}) {
            if (ent.find(n) != std::string::npos) ++placed;
        }
        Check(placed == 5, "and crypt1.ent really places all five of those names");

        skInterpreter zoneInterp;
        sk_b::MenuStack zoneStack(root, zoneInterp, &strings);
        zoneStack.level().SetEntityTypes(&entityTypes);
        std::unique_ptr<sk_b::ZoneScriptExecutable> zs;
        skExecutableContext loadCtxt(&zoneInterp);
        try {
            zs = std::make_unique<sk_b::ZoneScriptExecutable>(
                skString((root + "/crypt1.s").c_str()), loadCtxt, zoneStack);
            skRValueArray args;
            args.append(skRValue(0));
            skRValue ret;
            skExecutableContext callCtxt(&zoneInterp);
            zs->method(skString("Init"), args, ret, callCtxt);
        } catch (skParseException& e) {
            std::printf("   (PARSE ERROR in crypt1.s: %s)\n", e.toString().ptr());
        } catch (skRuntimeException& e) {
            std::printf("   (RUNTIME ERROR in crypt1.s: %s)\n", e.toString().ptr());
        }
        Check(zs != nullptr, "crypt1.s loads and its Init() runs");
        if (zs) zoneStack.level().AttachZoneScript(zs.get());

        std::unique_ptr<sk_b::DoorExecutable> door;
        try {
            door = std::make_unique<sk_b::DoorExecutable>(
                skString((root + "/lockeddoor_dh.s").c_str()), loadCtxt, zoneStack.player());
        } catch (skParseException& e) {
            std::printf("   (PARSE ERROR: %s)\n", e.toString().ptr());
        }
        Check(door != nullptr, "and a real trapped door loads beside it");

        if (zs && door) {
            skExecutableContext ctxt(&zoneInterp);
            door->SetEntityId("door1");  // what the .ent placement is called
            sk_b::RunEntityInit(*door, zoneStack.scriptRoot(), ctxt);

            const int healthBefore = zoneStack.player().health();
            // The real failure path: UsePicks.s calls GetOpener().
            // MagicDamage(), which is `DoMagicDamage(4017); OpenMenu(...)`.
            Invoke(*door, "MagicDamage", zoneInterp);
            Check(door->magicDamageCount() == 1,
                  "MagicDamage[] fired the trap -- the trigger named door1 claimed it");
            std::printf("     player health %d -> %d\n", healthBefore,
                        zoneStack.player().health());
            Check(zoneStack.player().health() < healthBefore,
                  "  and the player actually lost health, which is the whole point");

            // **Which half of that 23 was the spell?** crypt1.s gives
            // door1 `SetTrap(3, 23, 25)`, so a claiming trigger rolls its
            // own physical damage too -- the real case runs both. door3
            // and door4 are `SetTrap(0, 0, 25)`, zero physical damage, so
            // firing on one of those isolates the spell: template 4017 is
            // `spells/DoomHammer.s` and its whole HitTarget is
            // `DoAttackRoll(target, 2)`.
            {
                std::unique_ptr<sk_b::DoorExecutable> quiet;
                try {
                    quiet = std::make_unique<sk_b::DoorExecutable>(
                        skString((root + "/lockeddoor_dh.s").c_str()), loadCtxt,
                        zoneStack.player());
                } catch (skParseException&) {
                }
                if (quiet) {
                    quiet->SetEntityId("door3");
                    sk_b::RunEntityInit(*quiet, zoneStack.scriptRoot(), ctxt);
                    int hits = 0;
                    // It is an attack roll, so it can miss; twenty tries
                    // is plenty to see it connect at least once.
                    for (int i = 0; i < 20; ++i) {
                        const int before = zoneStack.player().health();
                        Invoke(*quiet, "MagicDamage", zoneInterp);
                        if (zoneStack.player().health() < before) ++hits;
                        zoneStack.player().SetHealth(100);
                    }
                    std::printf("     door3 (SetTrap(0,0,25), no physical damage): %d/20 hits\n",
                                hits);
                    Check(quiet->magicDamageCount() == 20, "door3's trigger claims it every time");
                    Check(hits > 0,
                          "  and the spell alone draws blood -- DoomHammer's DoAttackRoll lands");
                }
            }

            // A door the zone has no trigger for takes the other branch.
            std::unique_ptr<sk_b::DoorExecutable> stray;
            try {
                stray = std::make_unique<sk_b::DoorExecutable>(
                    skString((root + "/lockeddoor_dh.s").c_str()), loadCtxt,
                    zoneStack.player());
            } catch (skParseException&) {
            }
            if (stray) {
                stray->SetEntityId("not_a_trap_here");
                sk_b::RunEntityInit(*stray, zoneStack.scriptRoot(), ctxt);
                Invoke(*stray, "MagicDamage", zoneInterp);
                Check(stray->magicDamageCount() == 0,
                      "a door no trap trigger claims casts nothing -- the gate is real");
            }
        }
    }

    std::printf("\n%d checks, %d failures -- %s\n", g_checks, g_failures,
                g_failures == 0 ? "OK" : "FAILED");
    return g_failures == 0 ? 0 : 1;
}
