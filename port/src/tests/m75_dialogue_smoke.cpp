// M75 smoke test: talking to people.
//
// The reported symptom was that approaching an NPC never produced the
// prompt that starts a conversation or a shop. Three separate defects sat
// behind it, and each part below drives the real shipped data through the
// real path rather than a fixture.
//
// Decompiled for this milestone:
//   FUN_1001dd40  the per-frame use-target search  (`engine+0x61c`)
//   FUN_100646a8  the use action itself
//   0x10068418    SetUseText -- which also sets `entity+0xd8`
//   0x1006842c    GetUseText, and its `stringTable[13]` default
//   0x1002cf6c / 0x1002e7f8   the two ctor arms that start usable
//   0x10083e2c    the fatal-hit tail that clears it
// See simkin_bindings/use_prompt.h for the writeup.
//
// Part 1  the `usable` byte: where it comes from, on real scripts
// Part 2  the census -- every placed entity in all 21 zones
// Part 3  `Level` is the zone-root script object
// Part 4  the six conversations that died on the first line of Init()
// Part 5  GetOpener(), and the NPC that reads its own field through it
// Part 6  a whole branching conversation, end to end, into the shop
// Part 7  the lock-pick menu: a door as the opener
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "assets/string_table.h"
#include "simkin_bindings/door_executable.h"
#include "simkin_bindings/game_constants.h"
#include "simkin_bindings/item_executable.h"
#include "simkin_bindings/level_executable.h"
#include "simkin_bindings/menu_executable.h"
#include "simkin_bindings/menu_stack.h"
#include "simkin_bindings/monster_executable.h"
#include "simkin_bindings/player_executable.h"
#include "simkin_bindings/store.h"
#include "simkin_bindings/use_prompt.h"
#include "simkin_bindings/zone_script_executable.h"
#include "skExecutableContext.h"
#include "skInterpreter.h"
#include "skParseException.h"
#include "skRValue.h"
#include "skRValueArray.h"
#include "skRuntimeException.h"
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
const char* kZones[] = {"azra",     "broken1",  "broken2",      "crypt1", "crypt2",
                        "crypt3",   "delfhide", "drgnfld",      "dstar_e", "dstar_w",
                        "erthcave", "fearfrst", "ffarena",      "ghstpass", "glaciercrawl",
                        "lakvan",   "lothcav",  "raiders",      "snowline", "stouttp",
                        "twilite"};

std::string ToPath(const std::string& scriptRelative) {
    std::string p = scriptRelative;
    std::replace(p.begin(), p.end(), '\\', '/');
    return p;
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
        std::printf("m75_dialogue_smoke: FAILED to load stringtable.eng\n");
        return 1;
    }
    sk::EntityTypeTable entityTypes;
    if (!entityTypes.Load(scriptRoot)) {
        std::printf("m75_dialogue_smoke: FAILED to load entities.txt\n");
        return 1;
    }

    // One interpreter for the whole run; a fresh MenuStack per scenario,
    // because a conversation's branch depends on player/quest state and on
    // which zone script is attached (M74's own withPlayer() shape).
    skInterpreter interpreter;

    // ---------------------------------------------------------------
    // Part 1: the `usable` byte
    // ---------------------------------------------------------------
    std::printf("\n== Part 1: where `entity+0xd8` comes from ==\n");

    // The two factory arms that write 1 (docs/ZONE_FORMAT.md's table):
    // category 4 weapons (FUN_1002ce9c) and category 9 consumables
    // (FUN_1002e78c). Every other arm inherits the base entity's 0.
    Check(sk_b::StartsUsable(4), "category 4 (weapon) is constructed usable");
    Check(sk_b::StartsUsable(9), "category 9 (consumable) is constructed usable");
    {
        bool anyOther = false;
        for (int cat : {0, 1, 2, 3, 5, 6, 7, 8, 10, 11, 12, 13, 14, 15, 16}) {
            if (sk_b::StartsUsable(cat)) anyOther = true;
        }
        Check(!anyOther, "no other category is constructed usable");
    }
    Check(sk_b::kDefaultUseTextId == 13 && strings.Get(13) == "default",
          "the engine's fallback use text is string 13, \"default\"");

    {
        sk_b::MenuStack stack(scriptRoot, interpreter, &strings);
        stack.level().SetEntityTypes(&entityTypes);
        auto loadMonster = [&](const char* rel) -> std::unique_ptr<sk_b::MonsterExecutable> {
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

        // Gravel Trothgar: `SetUseText(2063)` and no SetUsable anywhere in
        // the script. This is the case the whole milestone is about.
        auto trothgar = loadMonster("monsters/Gravel_Trothgar.s");
        Check(trothgar && trothgar->usable(),
              "SetUseText alone makes Gravel Trothgar usable (no SetUsable in his script)");
        Check(trothgar && trothgar->useTextId() == 2063 && strings.Get(2063) == "Gravel Trothgar",
              "  and his prompt is string 2063, \"Gravel Trothgar\"");

        // Tanyin Aldwyr calls both; the old rule already found her.
        auto tanyin = loadMonster("monsters/Tanyin_Aldwyr.s");
        Check(tanyin && tanyin->usable(), "an explicit SetUsable(true) still works (Tanyin Aldwyr)");

        // Ivgrizt calls SetUseText(1342) and then SetUsable(false), in that
        // order. Script order wins -- he is deliberately not talkable until
        // his own conversation turns him back on.
        auto ivgrizt = loadMonster("fearfrst/Ivgrizt.s");
        Check(ivgrizt && !ivgrizt->usable(),
              "SetUsable(false) after SetUseText wins (fearfrst/Ivgrizt.s)");
        Check(ivgrizt && ivgrizt->useTextId() == 1342,
              "  ...and the use text it set is still there");

        // A hostile creature names neither, so the filter still excludes it.
        auto rat = loadMonster("monsters/Azra_Rat.s");
        Check(rat && !rat->usable(), "a hostile creature (Azra_Rat.s) is not usable");

        // Death clears the byte (0x10083e2c).
        if (trothgar) {
            auto brawler = loadMonster("monsters/Bandit_Brawler.s");
            skRValueArray a1;
            a1.append(skRValue(2063));
            skRValue r1;
            skExecutableContext c1(&interpreter);
            brawler->method(skString("SetUseText"), a1, r1, c1);
            Check(brawler->usable(), "a creature turned usable at runtime reports so");
            // M98: not 100000 -- the engine's damage is a short (FUN_10049e78's
            // local), and 100000 truncates to -31072, which clamps to nothing.
            brawler->ApplyDamage(30000);
            Check(!brawler->alive() && !brawler->usable(), "  ...and death clears it again");
        }

        // A door: `lockeddoor.s` swaps its prompt between "locked" and
        // "Open Door" and never calls SetUsable(true) either.
        {
            std::string full = std::string(scriptRoot) + "/lockeddoor.s";
            skExecutableContext ctxt(&interpreter);
            try {
                auto door = std::make_unique<sk_b::DoorExecutable>(skString(full.c_str()), ctxt,
                                                                    stack.player());
                skRValueArray args;
                args.append(skRValue(0));
                skRValue ret;
                skExecutableContext c2(&interpreter);
                door->method(skString("Init"), args, ret, c2);
                Check(door->usable(), "a door is usable through SetUseText too (lockeddoor.s)");
                Check(door->useTextId() == 1896, "  and its locked prompt is string 1896");
            } catch (skParseException& e) {
                Check(false, std::string("lockeddoor.s parse: ") + e.toString().ptr());
            }
        }

        // An item: category 4 starts usable with no script involvement,
        // category 3 does not until it says so.
        {
            auto makeItem = [&](const char* rel, int category) {
                std::string full = std::string(scriptRoot) + "/" + ToPath(rel);
                skExecutableContext ctxt(&interpreter);
                auto item = std::make_unique<sk_b::ItemExecutable>(skString(full.c_str()), ctxt,
                                                                    stack);
                item->SetEntityCategory(category);
                skRValueArray args;
                args.append(skRValue(0));
                skRValue ret;
                skExecutableContext c2(&interpreter);
                item->method(skString("Init"), args, ret, c2);
                return item;
            };
            try {
                auto club = makeItem("weapons/club.s", 4);
                Check(club->usable(), "a world-dropped weapon is usable from its constructor");
                auto blaze = makeItem("blaze.s", 5);
                Check(blaze->usable(),
                      "a spell scroll is usable only because blaze.s calls SetUseText");
            } catch (skParseException& e) {
                Check(false, std::string("item parse: ") + e.toString().ptr());
            }
        }
    }

    // ---------------------------------------------------------------
    // Part 2: the census over all 21 shipped zones
    // ---------------------------------------------------------------
    std::printf("\n== Part 2: every placed entity in all 21 zones ==\n");

    struct Counts {
        int talkable = 0;      // category 2/7 that end up usable
        int silentNpc = 0;     // category 2/7 with a use text but not usable
        int doors = 0, silentDoors = 0;
        int containers = 0, silentContainers = 0;
    } counts;
    std::set<std::string> usableScripts;

    for (const char* zoneName : kZones) {
        sk::Zone zone;
        if (!zone.Load(scriptRoot, zoneName)) continue;
        sk_b::MenuStack stack(scriptRoot, interpreter, &strings);
        stack.level().SetEntityTypes(&entityTypes);
        // The zone root, so that a placement's Init() touching
        // `Level.<field>` sees the real declared variables (Part 3).
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
            const int cat = desc->category;
            if (cat != 2 && cat != 7 && cat != 11 && cat != 8 && cat != 12) continue;
            // main.cpp's own rule: the placement's script overrides the
            // entities.txt one, and a placement with neither is inert
            // scenery that never becomes a live object at all.
            std::string rel = desc->name;
            if (!e.scriptPath.empty()) rel = e.scriptPath;
            if (rel.size() < 2 || rel.substr(rel.size() - 2) != ".s") rel += ".s";
            rel = ToPath(rel);
            std::string full = std::string(scriptRoot) + "/" + rel;
            // Most placements are an entities.txt "!label" with no script
            // at all (`!urn`, `!footlocker`): main.cpp never builds a live
            // object for one, so neither does this census. Checked by
            // opening the file, because skScriptedExecutable happily
            // constructs an empty object from a missing path rather than
            // throwing.
            {
                std::ifstream probe(full, std::ios::binary);
                if (!probe.good()) continue;
            }

            skExecutableContext ctxt(&interpreter);
            bool usable = false, hasUseText = false;
            try {
                if (cat == 2 || cat == 7) {
                    auto m = std::make_unique<sk_b::MonsterExecutable>(
                        skString(full.c_str()), ctxt, &strings, stack.player(), stack);
                    skRValueArray args;
                    args.append(skRValue(0));
                    skRValue ret;
                    skExecutableContext c2(&interpreter);
                    m->method(skString("Init"), args, ret, c2);
                    usable = m->usable();
                    hasUseText = m->useTextId() >= 0;
                } else if (cat == 11) {
                    auto d = std::make_unique<sk_b::DoorExecutable>(skString(full.c_str()), ctxt,
                                                                     stack.player());
                    skRValueArray args;
                    args.append(skRValue(0));
                    skRValue ret;
                    skExecutableContext c2(&interpreter);
                    d->method(skString("Init"), args, ret, c2);
                    usable = d->usable();
                } else {
                    auto it = std::make_unique<sk_b::ItemExecutable>(skString(full.c_str()), ctxt,
                                                                      stack);
                    it->SetEntityCategory(cat);
                    skRValueArray args;
                    args.append(skRValue(0));
                    skRValue ret;
                    skExecutableContext c2(&interpreter);
                    it->method(skString("Init"), args, ret, c2);
                    usable = it->usable();
                }
            } catch (skParseException&) {
                continue;  // no real script -- inert scenery, never loaded
            } catch (skRuntimeException&) {
                continue;
            }

            std::string key = rel;
            std::transform(key.begin(), key.end(), key.begin(),
                            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (cat == 2 || cat == 7) {
                if (usable) {
                    ++counts.talkable;
                    usableScripts.insert(key);
                } else if (hasUseText) {
                    ++counts.silentNpc;
                }
            } else if (cat == 11) {
                ++counts.doors;
                if (!usable) ++counts.silentDoors;
            } else {
                ++counts.containers;
                if (!usable) ++counts.silentContainers;
            }
        }
    }

    std::printf("  talkable NPC/merchant placements: %d   (with a use text but switched off: %d)\n",
                counts.talkable, counts.silentNpc);
    std::printf("  door placements with a real script: %d   (not usable: %d)\n", counts.doors,
                counts.silentDoors);
    std::printf("  container placements with a real script: %d   (not usable: %d)\n",
                counts.containers, counts.silentContainers);

    // 146 of the 149 placements that name a use text end up usable. The
    // three that do not are the ones that explicitly switch themselves off
    // in Init() and back on later: fearfrst/Ivgrizt.s,
    // stouttp/OldTrinket.s and dstar_w/Volstok_Violet.s.
    Check(counts.talkable == 146, "146 placed NPCs/merchants offer a prompt (was 95)");
    Check(counts.silentNpc == 3, "  and exactly 3 name a use text but call SetUsable(false)");

    // The gate is safe to apply to the other two lists.
    // Of the 434 door placements that have a real script, exactly one
    // comes out unusable: `dstar_e/Cell_Door.s`, whose entire Init() is
    // `SetUsable(false); SetMPUsable(true);` -- a prison door opened by a
    // quest event calling its own `OpenDoor` handler, never by the player
    // walking up to it. Every container is usable.
    Check(counts.doors == 434 && counts.silentDoors == 1,
          "434 door placements have a real script; only Cell_Door.s is switched off");
    Check(counts.containers == 404 && counts.silentContainers == 0,
          "all 404 container placements with a real script are usable");

    // Named spot-checks -- the ones a player actually meets. Every one of
    // these used to be silent.
    for (const char* rel : {"monsters/gravel_trothgar.s", "monsters/acolyte_menlin.s",
                            "monsters/priestess_almathea.s", "monsters/old_trinket.s",
                            "monsters/villager_prisoner1.s", "delfhide/heather.s",
                            "dstar_w/armor_merchant.s", "dstar_w/weapons_merchant.s",
                            "dstar_w/consumables_merchant.s", "monsters/eranthos_merchant.s",
                            "dstar_e/blk_market_merchant.s"}) {
        Check(usableScripts.count(rel) == 1, std::string("placed and talkable: ") + rel);
    }

    // ---------------------------------------------------------------
    // Part 3: `Level` is the zone-root script object
    // ---------------------------------------------------------------
    std::printf("\n== Part 3: Level and the zone-root script are one object ==\n");
    {
        sk_b::MenuStack stack(scriptRoot, interpreter, &strings);
        stack.level().SetEntityTypes(&entityTypes);

        auto readLevelField = [&](const char* name) {
            skRValue v;
            bool found = stack.level().getValue(skString(name), skString(""), v);
            return std::make_pair(found, v.intValue());
        };
        auto writeLevelField = [&](const char* name, int value) {
            return stack.level().setValue(skString(name), skString(""), skRValue(value));
        };

        // With no zone loaded at all -- a menu reached from the main menu
        // -- a Level field must still answer rather than raise.
        Check(readLevelField("saved_Nothing").first,
              "with no zone attached, Level.<field> answers instead of raising");
        Check(readLevelField("saved_Nothing").second == 0, "  and an unset one reads back 0");

        // azra.s declares `EndGame_Trinket [0]` at its top level. Attach it
        // and the field resolves to that declaration.
        std::unique_ptr<sk_b::ZoneScriptExecutable> azra;
        {
            std::string zp = std::string(scriptRoot) + "/azra.s";
            skExecutableContext zctxt(&interpreter);
            try {
                azra = std::make_unique<sk_b::ZoneScriptExecutable>(skString(zp.c_str()), zctxt,
                                                                     stack);
                stack.level().AttachZoneScript(azra.get());
            } catch (skParseException& e) {
                Check(false, std::string("azra.s parse: ") + e.toString().ptr());
            }
        }
        Check(azra != nullptr, "azra.s loads as the zone-root script");
        if (azra) {
            Check(readLevelField("EndGame_Trinket").first &&
                      readLevelField("EndGame_Trinket").second == 0,
                  "Level.EndGame_Trinket resolves to azra.s's own declaration, and is 0");
            Check(writeLevelField("EndGame_Trinket", 1) &&
                      readLevelField("EndGame_Trinket").second == 1,
                  "  writing it through Level round-trips");
            // ...and the zone script itself sees the same slot, which is
            // the whole point: crypt2.s writes `saved_Tele1 = 1` bare and
            // other scripts read `Level.saved_Tele1`.
            skRValue direct;
            azra->getValue(skString("EndGame_Trinket"), skString(""), direct);
            Check(direct.intValue() == 1,
                  "  and the zone script's own bare field is the same slot");
            // A name nobody declared still answers falsy rather than
            // killing the caller (the five shipped typos).
            Check(readLevelField("saved_Openswdoor").first,
                  "an undeclared Level field auto-creates rather than raising");
        }

        // `Level.<handler>()` reaches the zone script's own handlers:
        // crypt2.s defines AddCrystal and crypt2/pedestal_entity.s calls it
        // as Level.AddCrystal().
        {
            sk_b::MenuStack s2(scriptRoot, interpreter, &strings);
            s2.level().SetEntityTypes(&entityTypes);
            std::unique_ptr<sk_b::ZoneScriptExecutable> crypt2;
            std::string zp = std::string(scriptRoot) + "/crypt2.s";
            skExecutableContext zctxt(&interpreter);
            try {
                crypt2 = std::make_unique<sk_b::ZoneScriptExecutable>(skString(zp.c_str()), zctxt,
                                                                       s2);
                s2.level().AttachZoneScript(crypt2.get());
            } catch (skParseException& e) {
                Check(false, std::string("crypt2.s parse: ") + e.toString().ptr());
            }
            if (crypt2) {
                // Read the counter out as a plain int on both sides. An
                // skRValue handed back by skTreeNodeObject::getValue still
                // refers to the live node, so one held across the call
                // reports the *new* value at both ends.
                auto crystals = [&]() {
                    skRValue v;
                    crypt2->getValue(skString("num_crystals"), skString(""), v);
                    return v.intValue();
                };
                const int before = crystals();
                skRValueArray none;
                skRValue ret;
                skExecutableContext c(&interpreter);
                bool handled = s2.level().method(skString("AddCrystal"), none, ret, c);
                Check(before == 0 && handled && crystals() == 1,
                      "Level.AddCrystal() runs crypt2.s's own handler (num_crystals 0 -> 1)");
            }
        }
    }

    // ---------------------------------------------------------------
    // Part 4: the conversations that died on the first line of Init()
    // ---------------------------------------------------------------
    std::printf("\n== Part 4: conversations whose Init() reads a Level field ==\n");
    {
        // Each row: the zone whose root script declares the field, the NPC
        // script, and the conversation its OnUse() opens.
        struct Row {
            const char* zone;
            const char* npc;
            const char* what;
        };
        const Row rows[] = {
            {"azra", "monsters/Old_Trinket.s", "trinketconvo (Level.EndGame_Trinket)"},
            {"azra", "monsters/Skelos_Undriel_Azra.s", "AzraSkelosConvo (Level.EndGame_Skelos)"},
            {"delfhide", "monsters/Villager_Prisoner.s", "RescueConvo (Level.saved_Birgitta)"},
            {"delfhide", "delfhide/Chef.s", "Chef_convo (Level.saved_Given)"},
            {"crypt1", "crypt1/Azra.s", "azra_final_convo (Level.saved_WeTalked)"},
        };
        for (const Row& row : rows) {
            sk_b::MenuStack stack(scriptRoot, interpreter, &strings);
            stack.level().SetEntityTypes(&entityTypes);
            std::unique_ptr<sk_b::ZoneScriptExecutable> zoneScript;
            {
                std::string zp = std::string(scriptRoot) + "/" + row.zone + ".s";
                skExecutableContext zctxt(&interpreter);
                try {
                    zoneScript = std::make_unique<sk_b::ZoneScriptExecutable>(skString(zp.c_str()),
                                                                               zctxt, stack);
                    stack.level().AttachZoneScript(zoneScript.get());
                } catch (skParseException&) {
                } catch (skRuntimeException&) {
                }
            }
            std::string full = std::string(scriptRoot) + "/" + ToPath(row.npc);
            skExecutableContext ctxt(&interpreter);
            std::unique_ptr<sk_b::MonsterExecutable> npc;
            try {
                npc = std::make_unique<sk_b::MonsterExecutable>(skString(full.c_str()), ctxt,
                                                                 &strings, stack.player(), stack);
                skRValueArray args;
                args.append(skRValue(0));
                skRValue ret;
                skExecutableContext c2(&interpreter);
                npc->method(skString("Init"), args, ret, c2);
            } catch (skParseException& e) {
                Check(false, std::string(row.npc) + ": " + e.toString().ptr());
                continue;
            }
            Check(npc->usable(), std::string(row.npc) + " offers a prompt");
            sk_b::MenuExecutable* before = stack.currentMenu();
            npc->InvokeOnUse();
            sk_b::MenuExecutable* after = stack.currentMenu();
            Check(after && after != before && !after->rows().empty(),
                  std::string("  opens ") + row.what);
        }
    }

    // ---------------------------------------------------------------
    // Part 5: GetOpener()
    // ---------------------------------------------------------------
    std::printf("\n== Part 5: the conversation reads the NPC back through GetOpener() ==\n");
    {
        sk_b::MenuStack stack(scriptRoot, interpreter, &strings);
        stack.level().SetEntityTypes(&entityTypes);
        std::string full = std::string(scriptRoot) + "/fearfrst/Ivgrizt.s";
        skExecutableContext ctxt(&interpreter);
        auto npc = std::make_unique<sk_b::MonsterExecutable>(skString(full.c_str()), ctxt, &strings,
                                                              stack.player(), stack);
        skRValueArray args;
        args.append(skRValue(0));
        skRValue ret;
        skExecutableContext c2(&interpreter);
        npc->method(skString("Init"), args, ret, c2);

        // Ivgrizt is switched off at Init and turned on by his own quest;
        // OnUse still works when the game calls it.
        npc->InvokeOnUse();
        sk_b::MenuExecutable* convo = stack.currentMenu();
        // Ivgrizt_convo2.s opens with `if (GetOpener().saved_WeTalked = 0)`,
        // and `saved_WeTalked[0]` is declared at the top of Ivgrizt.s.
        Check(convo != nullptr && !convo->rows().empty(),
              "fearfrst/Ivgrizt_convo2.s opens (GetOpener().saved_WeTalked resolves)");
        if (convo) {
            Check(convo->opener() == static_cast<skiExecutable*>(npc.get()),
                  "  and GetOpener() is the NPC itself, not the player");
        }
    }

    // ---------------------------------------------------------------
    // Part 6: a whole branching conversation, into the shop
    // ---------------------------------------------------------------
    std::printf("\n== Part 6: Gravel Trothgar, branch by branch ==\n");
    {
        sk_b::MenuStack stack(scriptRoot, interpreter, &strings);
        stack.level().SetEntityTypes(&entityTypes);
        sk_b::PlayerExecutable& player = stack.player();
        std::unique_ptr<sk_b::ZoneScriptExecutable> azra;
        {
            std::string zp = std::string(scriptRoot) + "/azra.s";
            skExecutableContext zctxt(&interpreter);
            try {
                azra = std::make_unique<sk_b::ZoneScriptExecutable>(skString(zp.c_str()), zctxt,
                                                                     stack);
                stack.level().AttachZoneScript(azra.get());
            } catch (skParseException&) {
            } catch (skRuntimeException&) {
            }
        }
        std::string full = std::string(scriptRoot) + "/monsters/Gravel_Trothgar.s";
        skExecutableContext ctxt(&interpreter);
        auto npc = std::make_unique<sk_b::MonsterExecutable>(skString(full.c_str()), ctxt, &strings,
                                                              player, stack);
        {
            skRValueArray args;
            args.append(skRValue(0));
            skRValue ret;
            skExecutableContext c2(&interpreter);
            npc->method(skString("Init"), args, ret, c2);
        }
        Check(npc->store().productCount(sk_b::kItemTypeWeapon) > 0,
              "his Init() stocked the shop");

        auto rowCallbacks = [](sk_b::MenuExecutable* m) {
            std::vector<std::string> out;
            if (!m) return out;
            for (const auto& r : m->rows()) {
                if (!r.callback.empty()) out.push_back(r.callback);
            }
            return out;
        };
        auto hasCallback = [&](sk_b::MenuExecutable* m, const char* name) {
            for (const std::string& c : rowCallbacks(m)) {
                if (c == name) return true;
            }
            return false;
        };

        // Visit 1: quest 0 neither assigned nor solved -> the offer.
        npc->InvokeOnUse();
        sk_b::MenuExecutable* convo = stack.currentMenu();
        Check(convo && hasCallback(convo, "YesResponse") && hasCallback(convo, "MenuQuit"),
              "first visit offers the rat quest (YesResponse / MenuQuit)");

        // Take it.
        if (convo) convo->TryInvoke("YesResponse");
        Check(player.questAssigned(0), "  choosing yes assigns quest 0");

        // Visit 2: assigned, not solved -> the "get on with it" branch.
        npc->InvokeOnUse();
        convo = stack.currentMenu();
        Check(convo && rowCallbacks(convo).size() == 1 && hasCallback(convo, "MenuQuit"),
              "second visit, quest still open, is a single Goodbye row");

        // Solve it (eight rats) and come back. Through the real native,
        // exactly as monsters/azra_rat.s's own OnKilled() does it.
        {
            skRValueArray a;
            a.append(skRValue(0));
            skRValue r;
            skExecutableContext c(&interpreter);
            player.method(skString("SetQuestSolved"), a, r, c);
        }
        npc->InvokeOnUse();
        convo = stack.currentMenu();
        Check(convo && hasCallback(convo, "MoreResponse"),
              "with the quest solved he offers the reward branch");

        const int goldBefore = player.gold();
        const int xpBefore = player.experience();
        if (convo) convo->TryInvoke("MoreResponse");
        Check(player.gold() == goldBefore + 400 && player.experience() == xpBefore + 400,
              "  the reward branch pays 400 gold and 400 experience");
        Check(player.questCompleted(0), "  and completes quest 0");

        convo = stack.currentMenu();
        Check(convo && hasCallback(convo, "BuyItems") && hasCallback(convo, "SellItems"),
              "  ...and the shop rows appear");

        // Visit 4: completed -> straight to the shop menu.
        npc->InvokeOnUse();
        convo = stack.currentMenu();
        Check(convo && hasCallback(convo, "BuyItems"),
              "later visits go straight to Buy/Sell");

        // The shop itself. `BuyItems[ () { Quit(); GetPlayer().
        // BuyFromMerchant(); } ]`, and his OnUse() already did
        // SetMerchant(self).
        Check(player.merchant() == &npc->store(),
              "his OnUse() pointed the player at his own store");
        if (convo) convo->TryInvoke("BuyItems");
        sk_b::MenuExecutable* shop = stack.currentMenu();
        Check(shop != nullptr && shop != convo, "choosing Buy opens a different screen");
        Check(shop && shop->screenMode() == sk_b::MenuExecutable::ScreenMode::Buy,
              "  and that screen is buysell.s in Buy mode");
    }

    // ---------------------------------------------------------------
    // Part 7: a door as the opener
    // ---------------------------------------------------------------
    std::printf("\n== Part 7: the lock-pick menu ==\n");
    {
        sk_b::MenuStack stack(scriptRoot, interpreter, &strings);
        stack.level().SetEntityTypes(&entityTypes);
        std::string full = std::string(scriptRoot) + "/lockeddoor.s";
        skExecutableContext ctxt(&interpreter);
        std::unique_ptr<sk_b::DoorExecutable> door;
        try {
            door = std::make_unique<sk_b::DoorExecutable>(skString(full.c_str()), ctxt,
                                                           stack.player());
            skRValueArray args;
            args.append(skRValue(0));
            skRValue ret;
            skExecutableContext c2(&interpreter);
            door->method(skString("Init"), args, ret, c2);
        } catch (skParseException& e) {
            Check(false, std::string("lockeddoor.s: ") + e.toString().ptr());
        }
        if (door) {
            sk_b::MenuExecutable* before = stack.currentMenu();
            door->InvokeOnUse();
            sk_b::MenuExecutable* picks = stack.currentMenu();
            Check(picks && picks != before && !picks->rows().empty(),
                  "a locked door's OnUse() opens Menus/UsePicks");
            if (picks) {
                Check(picks->opener() == static_cast<skiExecutable*>(door.get()),
                      "  with the door as GetOpener(), which UsePicks.s reads resistDisarm from");
                skRValue resist;
                door->getValue(skString("resistDisarm"), skString(""), resist);
                Check(resist.intValue() == 5, "  and lockeddoor.s's own resistDisarm is 5");
            }
        }
    }

    std::printf("\nm75_dialogue_smoke: %d/%d checks passed -- %s\n", g_checks - g_failures, g_checks,
                g_failures == 0 ? "OK" : "FAILED");
    return g_failures == 0 ? 0 : 1;
}
