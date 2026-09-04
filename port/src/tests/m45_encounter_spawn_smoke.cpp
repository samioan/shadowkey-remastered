// M45 smoke test: the encounter spawner (`FUN_1008a76c`).
//
// M38 built the Encounter object model (regions, random sets, limits) and
// M44 gave the regions real rectangles out of `<zone>.zon`. This is the
// part in between: the gate that decides whether a region may spawn, the
// set draw, the placement search, and the live-count bookkeeping that a
// spawned creature's death feeds back into.
//
// The first thing this test asserts is how little of it the shipped game
// uses, because that is the most useful fact about the feature and it is
// checkable: of the three encounters in the whole corpus, two name regions
// that **do not exist in their zone's `.zon`** and can never fire. Getting
// that wrong would mean inventing behaviour for six dead regions.
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "assets/string_table.h"
#include "simkin_bindings/level_executable.h"
#include "simkin_bindings/menu_stack.h"
#include "simkin_bindings/monster_executable.h"
#include "simkin_bindings/zone_script_executable.h"
#include "skExecutableContext.h"
#include "skInterpreter.h"
#include "skParseException.h"
#include "skRValue.h"
#include "skRValueArray.h"
#include "skRuntimeException.h"
#include "world/entity_types.h"
#include "world/zone.h"

namespace {

int g_failures = 0;

void Check(bool ok, const std::string& what) {
    std::printf("%-76s %s\n", what.c_str(), ok ? "OK" : "FAILED");
    if (!ok) ++g_failures;
}

}  // namespace

int main(int argc, char** argv) {
    using Encounter = sk_bindings::EncounterExecutable;

    const char* scriptRoot =
        argc > 1 ? argv[1]
                  : "The-Elder-Scrolls-Travels-Shadowkey_N-Gage_EN-FR-DE-ES-IT_USA-Europe-"
                    "EnFrDeEsIt-26102004/system/apps/6r51";

    std::printf("=== M45: the encounter spawner ===\n\n");

    sk::StringTable strings;
    strings.Load(std::string(scriptRoot) + "/stringtable.eng");
    sk::EntityTypeTable entityTypes;
    if (!entityTypes.Load(scriptRoot)) {
        std::printf("m45_encounter_spawn_smoke: FAILED to load entities.txt\n");
        return 1;
    }

    auto loadZoneScript = [&](skInterpreter& interp, sk_bindings::MenuStack& stack,
                               const char* rel) -> std::unique_ptr<sk_bindings::ZoneScriptExecutable> {
        skExecutableContext loadCtxt(&interp);
        try {
            auto zs = std::make_unique<sk_bindings::ZoneScriptExecutable>(
                skString((std::string(scriptRoot) + "/" + rel).c_str()), loadCtxt, stack);
            skRValueArray args;
            args.append(skRValue(0));
            skRValue ret;
            skExecutableContext callCtxt(&interp);
            zs->method(skString("Init"), args, ret, callCtxt);
            return zs;
        } catch (skParseException& e) {
            std::printf("PARSE ERROR loading %s: %s\n", rel, e.toString().ptr());
            return nullptr;
        } catch (skRuntimeException& e) {
            std::printf("RUNTIME ERROR loading %s: %s\n", rel, e.toString().ptr());
            return nullptr;
        }
    };

    // ---- 1. The whole shipped encounter population ----
    //
    // Three `AddEncounters` call sites exist. This walks the two zones
    // that have them and checks each declared region against the zone's
    // own `.zon`.
    {
        skInterpreter interpreter;
        sk_bindings::MenuStack stack(scriptRoot, interpreter, &strings);
        auto ghstpass = loadZoneScript(interpreter, stack, "ghstpass.s");
        sk::Zone ghstpassZone;
        Check(ghstpass != nullptr && ghstpassZone.Load(scriptRoot, "ghstpass"),
              "ghstpass.s and ghstpass.zon both load");
        if (ghstpass && ghstpassZone.width() > 0) {
            Check(ghstpass->encounters().size() == 2,
                  "ghstpass.s builds its two encounters (fight12, fight13)");
            int resolvable = 0, declared = 0;
            for (const auto& e : ghstpass->encounters()) {
                for (const std::string& r : e->regions()) {
                    ++declared;
                    if (!ghstpassZone.RegionsNamed(r).empty()) ++resolvable;
                }
            }
            std::printf("   (ghstpass declares %d encounter regions; %d exist in ghstpass.zon)\n",
                        declared, resolvable);
            Check(declared == 6 && resolvable == 0,
                  "...and NONE of its six region names exists in the zone -- both are dead");
            Check(ghstpass->encounters()[0]->sets().size() == 5,
                  "fight12's five AddRandomSets calls really are five alternative sets");
        }
    }
    {
        skInterpreter interpreter;
        sk_bindings::MenuStack stack(scriptRoot, interpreter, &strings);
        auto lothcav = loadZoneScript(interpreter, stack, "lothcav.s");
        sk::Zone lothcavZone;
        Check(lothcav != nullptr && lothcavZone.Load(scriptRoot, "lothcav"),
              "lothcav.s and lothcav.zon both load");
        if (lothcav && lothcavZone.width() > 0) {
            Check(lothcav->encounters().size() == 1 &&
                      lothcav->encounters()[0]->regions().size() == 1 &&
                      lothcav->encounters()[0]->regions()[0] == "battle41",
                  "lothcav.s builds one encounter over one region, \"battle41\"");
            Check(!lothcavZone.RegionsNamed("battle41").empty(),
                  "...and battle41 DOES exist in lothcav.zon -- the game's only live encounter");
            const std::vector<std::vector<Encounter::SpawnEntry>>& sets =
                lothcav->encounters()[0]->sets();
            Check(sets.size() == 1 && sets[0].size() == 1 && sets[0][0].typeId == 272 &&
                      sets[0][0].count == 1,
                  "...spawning exactly one typeId 272");
            const sk::EntityTypeDescriptor* desc = entityTypes.Lookup(272);
            Check(desc && desc->name == "monsters\\Tunnel_Wight.s",
                  "...which entities.txt resolves to monsters\\Tunnel_Wight.s");
        }
    }

    // ---- 2. The defaults, and what lothcav.s does to them ----
    //
    // ghstpass's encounters take the constructor's values untouched;
    // lothcav.s is the corpus's one `SetLimit` call site *and* switches
    // its encounter off at build time, which is what makes it a quest
    // reward rather than an ambush.
    {
        skInterpreter interpreter;
        sk_bindings::MenuStack stack(scriptRoot, interpreter, &strings);
        auto ghstpass = loadZoneScript(interpreter, stack, "ghstpass.s");
        auto lothcav = loadZoneScript(interpreter, stack, "lothcav.s");
        if (ghstpass && !ghstpass->encounters().empty()) {
            Encounter& enc = *ghstpass->encounters()[0];
            Check(enc.active() && enc.regionLive(0) == 0 && enc.respawnSeconds() == 0 &&
                      enc.regionLimit(0) == Encounter::kDefaultRegionLimit,
                  "an untouched region is active, live 0, limit 99 -- the real ctor's own values");
            Check(Encounter::kDefaultRegionLimit == 99,
                  "...and that 99 is the constant FUN_1008b000 writes");
        }
        if (lothcav && !lothcav->encounters().empty()) {
            Encounter& enc = *lothcav->encounters()[0];
            Check(!enc.active(),
                  "lothcav's encounter is built and then SetActive(false) -- dormant by design");
            Check(enc.regionLimit(0) == 1,
                  "...with SetLimit(0,1), the corpus's only call, capping battle41 at one creature");
        }
    }

    // ---- 3. The gate ----
    //
    // `(live < 1 || respawnSeconds != 0) && active && live <= limit - 1`.
    // With respawnSeconds 0 -- which every shipped encounter has -- the
    // first clause is what makes an encounter refuse to top a region up
    // while anything it spawned is still alive.
    {
        skInterpreter interpreter;
        sk_bindings::MenuStack stack(scriptRoot, interpreter, &strings);
        auto lothcav = loadZoneScript(interpreter, stack, "lothcav.s");
        if (lothcav && !lothcav->encounters().empty()) {
            Encounter& enc = *lothcav->encounters()[0];
            // As built it is inactive -- so walking into battle41 reaches
            // it and gets nothing, which is the shipped behaviour until
            // the pilgrim's-remains menu runs.
            Check(!enc.CanSpawnInRegion(0), "a dormant encounter refuses even an empty region");
            skRValueArray on;
            on.append(skRValue(true));
            skRValue ret;
            skExecutableContext c(&interpreter);
            enc.method(skString("SetActive"), on, ret, c);  // pilgrim_remains.s's own call
            Check(enc.CanSpawnInRegion(0), "an empty, active region may spawn");
            enc.NoteSpawned(0);
            Check(!enc.CanSpawnInRegion(0),
                  "...and refuses a second time while its first creature is alive (respawn is 0)");
            enc.NoteDied(0);
            Check(enc.CanSpawnInRegion(0), "...but may again once the region is cleared");
            Check(!enc.CanSpawnInRegion(1) && !enc.CanSpawnInRegion(99),
                  "an out-of-range region index never spawns");

            skRValueArray off;
            off.append(skRValue(false));
            enc.method(skString("SetActive"), off, ret, c);
            Check(!enc.active() && !enc.CanSpawnInRegion(0), "SetActive(false) shuts it off again");
        }
    }

    // ---- 4. The set draw ----
    //
    // `(setCount == 1) ? 0 : rand() % setCount` -- the real code skips the
    // RNG entirely for one set, which is what makes lothcav's encounter
    // deterministic and ghstpass's (had it worked) a five-way draw.
    {
        skInterpreter interpreter;
        sk_bindings::MenuStack stack(scriptRoot, interpreter, &strings);
        auto lothcav = loadZoneScript(interpreter, stack, "lothcav.s");
        auto ghstpass = loadZoneScript(interpreter, stack, "ghstpass.s");
        if (lothcav && !lothcav->encounters().empty()) {
            bool alwaysZero = true;
            for (int i = 0; i < 200; ++i) {
                if (lothcav->encounters()[0]->PickSetIndex() != 0) alwaysZero = false;
            }
            Check(alwaysZero, "a one-set encounter always draws set 0, without touching the RNG");
        }
        if (ghstpass && !ghstpass->encounters().empty()) {
            std::vector<int> seen(5, 0);
            for (int i = 0; i < 2000; ++i) {
                int s = ghstpass->encounters()[0]->PickSetIndex();
                if (s >= 0 && s < 5) ++seen[static_cast<size_t>(s)];
            }
            bool allSeen = true;
            for (int n : seen) {
                if (n == 0) allSeen = false;
            }
            Check(allSeen, "a five-set encounter draws all five");
        }
    }

    // ---- 5. The placement search ----
    //
    // Zone::FindFreeTileInRegion, transcribed from FUN_1008ae94. Two
    // details are worth pinning because both are surprising: the draw is
    // inclusive at both ends, and the retry budget is the rectangle's
    // *width* rather than anything to do with its area.
    {
        sk::Zone lothcav;
        Check(lothcav.Load(scriptRoot, "lothcav"), "lothcav loads for the placement search");
        std::vector<const sk::Zone::Region*> rects = lothcav.RegionsNamed("battle41");
        Check(!rects.empty(), "battle41 resolves to a rectangle");
        if (!rects.empty()) {
            const sk::Zone::Region& r = *rects.front();
            std::printf("   (battle41 = x %d..%d, y %d..%d)\n", r.x0, r.x1, r.y0, r.y1);
            // Every tile it ever returns is inside the rectangle, and no
            // tile outside is reachable -- the inclusive-draw check.
            bool allInside = true;
            bool sawMaxEdge = false;
            int found = 0;
            auto never = [](int, int) { return false; };
            for (int i = 0; i < 4000; ++i) {
                int tx = -1, ty = -1;
                if (!lothcav.FindFreeTileInRegion(r, never, tx, ty)) continue;
                ++found;
                if (tx < r.x0 || tx > r.x1 || ty < r.y0 || ty > r.y1) allInside = false;
                if (tx == r.x1 || ty == r.y1) sawMaxEdge = true;
            }
            Check(found > 0 && allInside, "every placement lands inside the region's rectangle");
            Check(sawMaxEdge,
                  "...including its far edge, because the draw is inclusive at both ends");

            // With every tile occupied it must give up rather than loop,
            // and it must give up after exactly the real budget.
            int probes = 0;
            auto countingAlwaysOccupied = [&](int, int) {
                ++probes;
                return true;
            };
            int tx = -1, ty = -1;
            bool placed = lothcav.FindFreeTileInRegion(r, countingAlwaysOccupied, tx, ty);
            Check(!placed, "a fully occupied region places nothing");
            Check(probes <= r.x1 - r.x0,
                  "...after at most `x1 - x0` tries -- the region's width, not its area");
        }
    }

    // ---- 6. End to end: the script call that actually fires ----
    //
    // `lothna/pilgrim_remains.s` does
    // `Level.fight41.SpawnEncounter("battle41")`, the corpus's only
    // explicit spawn. The handler resolves the name against the
    // encounter's own region list and hands the index to the host.
    {
        skInterpreter interpreter;
        sk_bindings::MenuStack stack(scriptRoot, interpreter, &strings);
        auto lothcav = loadZoneScript(interpreter, stack, "lothcav.s");
        if (lothcav && !lothcav->encounters().empty()) {
            Encounter& enc = *lothcav->encounters()[0];
            Check(enc.TakePendingSpawnRegion() == -1, "nothing is pending before the call");
            skRValueArray args;
            args.append(skRValue(skString("battle41")));
            skRValue ret;
            skExecutableContext c(&interpreter);
            // Dormant: the handler's own first line is `if (!active)
            // return`, so this does nothing at all.
            enc.method(skString("SpawnEncounter"), args, ret, c);
            Check(enc.TakePendingSpawnRegion() == -1,
                  "SpawnEncounter on a dormant encounter queues nothing -- the real first line");
            // pilgrim_remains.s's CreateWight, in order.
            skRValueArray on;
            on.append(skRValue(true));
            enc.method(skString("SetActive"), on, ret, c);
            enc.method(skString("SpawnEncounter"), args, ret, c);
            Check(enc.TakePendingSpawnRegion() == 0,
                  "SpawnEncounter(\"battle41\") resolves to region index 0 and is queued");
            Check(enc.TakePendingSpawnRegion() == -1, "...and the request clears on read");

            // A name the encounter does not know resolves to -1, which the
            // real spawner then treats as region 0 through its own bounds
            // check. Reproduced rather than tidied.
            skRValueArray bogus;
            bogus.append(skRValue(skString("nosuchregion")));
            enc.method(skString("SpawnEncounter"), bogus, ret, c);
            Check(enc.TakePendingSpawnRegion() == -1,
                  "an unknown region name resolves to -1, as the real wcscmp walk does");
        }
    }

    // ---- 7. Creating the creatures ----
    //
    // The spawner builds each one through the same path
    // Level.CreateEntity's creature form uses (`FUN_100715a8` in the real
    // engine, LevelExecutable::CreateCreature here).
    {
        skInterpreter interpreter;
        sk_bindings::MenuStack stack(scriptRoot, interpreter, &strings);
        stack.level().SetEntityTypes(&entityTypes);
        std::unique_ptr<sk_bindings::MonsterExecutable> wight = stack.level().CreateCreature(272);
        Check(wight != nullptr, "typeId 272 builds a real creature with its Init() run");
        if (wight) {
            // Its own script values, so this is the real Tunnel_Wight and
            // not an empty shell.
            Check(wight->level() == 9 && wight->hasSpells(),
                  "...and it is monsters\\Tunnel_Wight.s -- level 9, three spells (M43)");
        }
        // A non-creature typeId is refused, the same way the one-argument
        // CreateEntity refuses non-item categories.
        Check(stack.level().CreateCreature(704) == nullptr,
              "an item typeId is refused -- CreateCreature is category 2 only");
    }

    std::printf("\nm45_encounter_spawn_smoke: %s\n",
                g_failures ? "FAILED" : "PASSED (all checks)");
    return g_failures ? 1 : 0;
}
