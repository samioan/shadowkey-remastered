// M44 smoke test: where a named zone region lives, and what the Level
// global does with one.
//
// The roadmap's longest-standing open question was "where does a named
// zone region live?" -- the tags `EnterZone(s)` receives ("Skelos_Dead",
// "YouSure", "ghasts") and the Encounter spawner's regions ("battle41",
// "fight12W"). M38 ruled out `.ent` placements by dumping every named one
// in three zones.
//
// The answer is **`<zone>.zon`**, whose 72-byte room records ZONE_FORMAT.md
// had already decoded byte-for-byte without recognising what they were for.
// The four `u16`s are an axis-aligned tile rectangle; the 64-byte name is
// the region tag.
//
// The assertions below are chosen to be the ones that could have caught a
// wrong reading:
//   * the rectangle interpretation, against azra's own player start
//     (the region called "start" has to contain it, and does);
//   * the Y flip, which is what makes that true and is the thing
//     ZONE_FORMAT.md had guessed was an array index;
//   * that the four names the roadmap bullet actually quotes are present,
//     in the zones whose scripts use them;
//   * the Lock/Unlock asymmetry, which is real engine behaviour and the
//     kind of thing a tidy-minded port would "fix" by accident.
#include <cstdio>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "assets/string_table.h"
#include "simkin_bindings/level_executable.h"
#include "simkin_bindings/menu_stack.h"
#include "simkin_bindings/zone_script_executable.h"
#include "skExecutableContext.h"
#include "skInterpreter.h"
#include "skParseException.h"
#include "skRValue.h"
#include "skRValueArray.h"
#include "skRuntimeException.h"
#include "world/zone.h"

namespace {

int g_failures = 0;

void Check(bool ok, const std::string& what) {
    std::printf("%-76s %s\n", what.c_str(), ok ? "OK" : "FAILED");
    if (!ok) ++g_failures;
}

const sk::Zone::Region* FindRegion(const sk::Zone& zone, const std::string& name) {
    std::vector<const sk::Zone::Region*> hits = zone.RegionsNamed(name);
    return hits.empty() ? nullptr : hits.front();
}

}  // namespace

int main(int argc, char** argv) {
    const char* scriptRoot =
        argc > 1 ? argv[1]
                  : "The-Elder-Scrolls-Travels-Shadowkey_N-Gage_EN-FR-DE-ES-IT_USA-Europe-"
                    "EnFrDeEsIt-26102004/system/apps/6r51";

    std::printf("=== M44: named zone regions (<zone>.zon) ===\n\n");

    sk::StringTable strings;
    strings.Load(std::string(scriptRoot) + "/stringtable.eng");

    sk::Zone azra;
    if (!azra.Load(scriptRoot, "azra")) {
        std::printf("m44_zone_region_smoke: FAILED to load azra\n");
        return 1;
    }

    // ---- 1. The record decodes at all ----
    Check(azra.regions().size() == 17, "azra.zon parses to its 17 records (2 + n*72 bytes exactly)");

    // ---- 2. The rectangle reading, checked against something external ----
    //
    // This is the assertion that makes the whole decode more than a
    // plausible story. azra's player start comes from a completely
    // different file (.ent), and the region called "start" has to contain
    // it. It does -- and only because the Y fields are flipped.
    {
        const sk::Zone::Region* start = FindRegion(azra, "start");
        Check(start != nullptr, "azra has a region called \"start\"");
        if (start) {
            const int ptx = azra.playerStartX >> 8;
            const int pty = azra.playerStartY >> 8;
            std::printf("   (region \"start\" = x %d..%d, y %d..%d; azra's .ent player start is "
                        "tile (%d, %d))\n",
                        start->x0, start->x1, start->y0, start->y1, ptx, pty);
            Check(start->Contains(ptx, pty),
                  "...and it contains the player start, which comes from a different file entirely");
            // And it only does so because containment is inclusive at both
            // ends: azra's spawn sits exactly on this rectangle's corner
            // (x == x0 and y == y1). See Region::Contains for the
            // all-21-zones survey that settles the convention, and for why
            // Lock/UnlockRegion's own loops are half-open anyway.
            Check(ptx == start->x0 && pty == start->y1,
                  "...on its very corner, which is why the bounds have to be inclusive");
            // The stored fields are 118 / 82 / 121 / 86. Without the flip
            // the y range would be 82..86 and the check above would fail
            // by 40 tiles.
            Check(start->y1 == azra.height() - 82 && start->y0 == azra.height() - 86,
                  "the Y fields really are stored as `gridHeight - y` (128-82=46, 128-86=42)");
        }
    }

    // ---- 3. Every record is a well-formed rectangle ----
    {
        bool allOrdered = true;
        bool allInBounds = true;
        for (const sk::Zone::Region& r : azra.regions()) {
            if (r.x0 >= r.x1 || r.y0 >= r.y1) allOrdered = false;
            if (r.x0 < 0 || r.y0 < 0 || r.x1 > azra.width() || r.y1 > azra.height()) {
                allInBounds = false;
            }
        }
        Check(allOrdered, "every record has x0 < x1 and y0 < y1 -- it is a rectangle, not a pair "
                          "of indices");
        Check(allInBounds, "...and lies inside the tile grid");
    }

    // ---- 3b. The survey that settles inclusive-vs-half-open ----
    //
    // Every zone's `.ent` player start against its own `.zon` rectangles.
    // The aggregate matters more than any single zone: 12 of the 21 spawn
    // inside a region, every one of those regions is named like an arrival
    // point (`start`, `entry`, `Entrance`, `Enter`, `Exit`, `Zvoldoor`,
    // `vig`), and four of the twelve hold only because the bounds are
    // inclusive. See Zone::Region::Contains.
    {
        static const char* const kAllZones[] = {
            "azra",     "broken1",  "broken2",      "crypt1",  "crypt2",   "crypt3",   "delfhide",
            "drgnfld",  "dstar_e",  "dstar_w",      "erthcave", "fearfrst", "ffarena",  "ghstpass",
            "glaciercrawl", "lakvan", "lothcav",    "raiders", "snowline", "stouttp",  "twilite",
        };
        int loaded = 0, inclusiveHits = 0, halfOpenHits = 0;
        for (const char* name : kAllZones) {
            sk::Zone z;
            if (!z.Load(scriptRoot, name)) continue;
            ++loaded;
            const int tx = z.playerStartX >> 8;
            const int ty = z.playerStartY >> 8;
            for (const sk::Zone::Region& r : z.regions()) {
                if (r.Contains(tx, ty)) {
                    ++inclusiveHits;
                    if (tx < r.x1 && ty < r.y1) ++halfOpenHits;
                    break;
                }
            }
        }
        std::printf("   (%d zones loaded; spawn lands in a region for %d of them, and only %d of "
                    "those survive half-open bounds)\n",
                    loaded, inclusiveHits, halfOpenHits);
        Check(loaded == 21, "all 21 shipped zones load");
        Check(inclusiveHits == 12 && halfOpenHits == 8,
              "12 of 21 spawns land in an arrival region, and 4 of those need inclusive bounds");
    }

    // ---- 4. The names the roadmap bullet quotes are all there ----
    {
        Check(FindRegion(azra, "Skelos_Dead") && FindRegion(azra, "YouSure") &&
                  FindRegion(azra, "ghasts"),
              "azra.zon holds \"Skelos_Dead\", \"YouSure\" and \"ghasts\" -- azra.s's EnterZone tags");
        // Names are not unique, and that is load-bearing: azra's four
        // "YouSure" rectangles are four separate doorways into the same
        // scripted question.
        Check(azra.RegionsNamed("YouSure").size() == 4,
              "...and \"YouSure\" is FOUR separate rectangles, so a name is a set, not one room");
    }
    {
        sk::Zone lothcav;
        sk::Zone ghstpass;
        Check(lothcav.Load(scriptRoot, "lothcav") && ghstpass.Load(scriptRoot, "ghstpass"),
              "lothcav and ghstpass load");
        Check(FindRegion(lothcav, "battle41") != nullptr,
              "lothcav.zon holds \"battle41\" -- the Encounter region lothcav.s registers");
        Check(FindRegion(ghstpass, "snowline") && FindRegion(ghstpass, "wolves"),
              "ghstpass.zon holds \"snowline\" and \"wolves\" -- ghstpass.s's own three regions");
        // M38 checked `.ent` and found none of these. Worth pinning that
        // the two name spaces really are disjoint, so the earlier
        // elimination stays sound.
        bool anyEntNameIsARegion = false;
        for (const sk::Zone::EntPlacement& e : ghstpass.entities()) {
            if (!e.name.empty() && !ghstpass.RegionsNamed(e.name).empty()) {
                anyEntNameIsARegion = true;
            }
        }
        Check(!anyEntNameIsARegion,
              "no .ent placement name is also a region name -- M38's elimination holds");
    }

    // ---- 5. LockZone / UnlockZone ----
    //
    // The two are deliberately asymmetric in the real engine: LockZone
    // goes through the name-to-room map (one room) and *assigns* 4;
    // UnlockZone walks all 40 slots with strcmp and clears bit 2 (every
    // room with the name). Reproduced rather than made symmetric.
    {
        const sk::Zone::Region* first = FindRegion(azra, "YouSure");
        Check(first != nullptr, "azra's \"YouSure\" resolves");
        if (first) {
            int expectedTiles = (first->x1 - first->x0) * (first->y1 - first->y0);
            int locked = azra.LockRegion("YouSure");
            std::printf("   (LockZone(\"YouSure\") touched %d tiles; the first of its four "
                        "rectangles is %d)\n",
                        locked, expectedTiles);
            Check(locked == expectedTiles,
                  "LockZone locks ONE rectangle, even though four share the name");
            Check(azra.CellAt(first->x0, first->y0).IsBlocked(),
                  "...and the cells it covers really carry the block bit");

            int unlocked = azra.UnlockRegion("YouSure");
            Check(unlocked > locked,
                  "UnlockZone reaches ALL four -- the real asymmetry, not a tidy-up");
            Check(!azra.CellAt(first->x0, first->y0).IsBlocked(), "...and clears the bit again");
        }
    }

    // ---- 6. The per-level tile-change journal ----
    //
    // FUN_1006d7bc records every Lock/Unlock into a fixed 100-entry array
    // keyed by tile, replacing rather than appending. That array is the
    // level-state half of a save file (SAVE_FORMAT.md's open "what is
    // inside a `<level>.dat`").
    {
        sk::Zone fresh;
        Check(fresh.Load(scriptRoot, "azra") && fresh.tileChanges().empty(),
              "a freshly-loaded zone has an empty tile-change journal");
        fresh.LockRegion("ghasts");
        size_t afterLock = fresh.tileChanges().size();
        fresh.UnlockRegion("ghasts");
        size_t afterUnlock = fresh.tileChanges().size();
        std::printf("   (journal after lock: %zu entries; after unlocking the same region: %zu)\n",
                    afterLock, afterUnlock);
        Check(afterLock > 0 && afterUnlock == afterLock,
              "unlocking the same tiles updates their entries in place rather than appending");
        Check(fresh.tileChanges().size() <= sk::Zone::kMaxTileChanges,
              "...and the journal never exceeds the real 100-entry cap");
    }

    // ---- 7. LightRect ----
    //
    // The zone-effects dispatcher's case 1: `light = level << 8` over a
    // half-open tile rectangle, NOT Y-flipped (unlike .zon's own records --
    // the script hands these in directly). crypt2/controller.s lights one
    // alcove per puzzle step with exactly this.
    {
        sk::Zone crypt2;
        if (crypt2.Load(scriptRoot, "crypt2")) {
            // controller.s's first call, verbatim.
            crypt2.LightRect(11, 36, 14, 39, 64);
            Check(crypt2.CellAt(11, 36).lightLevel == (64 << 8) &&
                      crypt2.CellAt(13, 38).lightLevel == (64 << 8),
                  "LightRect(11,36,14,39,64) sets light to 64<<8 across its rectangle");
            Check(crypt2.CellAt(14, 39).lightLevel != (64 << 8),
                  "...and is half-open at the top, so (14,39) is untouched");
        }
    }

    // ---- 8. The Vignette table ----
    //
    // Six slideshows, recovered from FUN_1002bc6c's switch. Each is a
    // contiguous sprite run plus a caption-string base; the per-frame tick
    // indexes both by `screenMode - 0x20`.
    {
        using Level = sk_bindings::LevelExecutable;
        Level::VignetteDefinition def;
        Check(Level::VignetteById(1, def) && def.firstSprite == 0xdc && def.lastSprite == 0xdf &&
                  def.slides() == 4,
              "Vignette(1) -- crypt1.s's -- is sprites 0xdc..0xdf, four slides");
        Check(Level::VignetteById(5, def) && def.slides() == 5 && def.firstTextId == 700,
              "Vignette(5) -- azra.s's -- is the only five-slide one, and its captions start at 700");
        bool allSix = true;
        for (int i = 0; i < 6; ++i) {
            if (!Level::VignetteById(i, def)) allSix = false;
        }
        Check(allSix && !Level::VignetteById(6, def) && !Level::VignetteById(-1, def),
              "exactly six vignettes exist -- ids 0..5, and anything else is refused");
    }

    // ---- 9. End to end: region name -> script handler -> native ----
    //
    // The whole chain this milestone assembles, on a real script. crypt1's
    // spawn tile is inside its region named "vig", and crypt1.s says
    //
    //     EnterZone[ (zone) { ... if (zone = "vig") {
    //         if (saved_vig = 0) { Level.Vignette(1); saved_vig = 1; } } } ]
    //
    // so arriving in crypt1 must arm vignette 1 -- and must arm it exactly
    // once, because the handler latches its own saved flag.
    {
        sk::Zone crypt1;
        skInterpreter interpreter;
        sk_bindings::MenuStack stack(scriptRoot, interpreter, &strings);
        std::unique_ptr<sk_bindings::ZoneScriptExecutable> script;
        skExecutableContext loadCtxt(&interpreter);
        try {
            script = std::make_unique<sk_bindings::ZoneScriptExecutable>(
                skString((std::string(scriptRoot) + "/crypt1.s").c_str()), loadCtxt, stack);
            skRValueArray args;
            args.append(skRValue(0));
            skRValue ret;
            skExecutableContext callCtxt(&interpreter);
            script->method(skString("Init"), args, ret, callCtxt);
        } catch (skParseException& e) {
            std::printf("   (PARSE ERROR loading crypt1.s: %s)\n", e.toString().ptr());
        } catch (skRuntimeException& e) {
            std::printf("   (RUNTIME ERROR loading crypt1.s: %s)\n", e.toString().ptr());
        }
        Check(crypt1.Load(scriptRoot, "crypt1") && script != nullptr,
              "crypt1 and its zone-root script both load");
        if (script && crypt1.width() > 0) {
            // crypt1 has **two** rectangles named "vig", and only the
            // second contains the spawn -- so this has to ask the set, not
            // the first match. Exactly the reason RegionsNamed() returns a
            // vector.
            const int ptx = crypt1.playerStartX >> 8;
            const int pty = crypt1.playerStartY >> 8;
            std::vector<const sk::Zone::Region*> vigs = crypt1.RegionsNamed("vig");
            bool spawnInVig = false;
            for (const sk::Zone::Region* r : vigs) {
                if (r->Contains(ptx, pty)) spawnInVig = true;
            }
            Check(vigs.size() == 2 && spawnInVig,
                  "crypt1's spawn is inside one of its TWO rectangles named \"vig\"");

            sk_bindings::LevelExecutable::VignetteDefinition armed;
            Check(!stack.level().TakePendingVignette(armed), "nothing is armed before entering it");
            script->EnterRegion("vig");
            bool fired = stack.level().TakePendingVignette(armed);
            Check(fired && armed.firstSprite == 0xdc,
                  "entering \"vig\" runs crypt1.s's real EnterZone and arms Vignette(1)");
            // The script's own `saved_vig` latch means a second entry does
            // nothing -- which is the behaviour that makes edge-triggering
            // on the host side safe rather than merely tidy.
            script->EnterRegion("vig");
            Check(!stack.level().TakePendingVignette(armed),
                  "...and re-entering does not, because the script latched its own saved flag");
        }
    }

    // ---- 10. The edge-triggering rule, on a real walk ----
    //
    // main.cpp diffs the set of regions containing the player once per
    // tick and fires EnterZone only for newly-entered ones. The loop
    // itself is mechanical; the part with judgement in it is the rule, so
    // that is what is asserted here -- against real azra geometry, by
    // walking the player's tile straight across the map and counting.
    //
    // Why it matters: azra's "YouSure" opens a menu. Firing per tick
    // instead of per crossing would reopen it 25 times a second.
    {
        auto fireCount = [&](const sk::Zone& zone, int fromX, int toX, int y) {
            std::map<std::string, int> fires;
            std::set<size_t> occupied;
            for (int x = fromX; x <= toX; ++x) {
                std::set<size_t> now;
                for (size_t i = 0; i < zone.regions().size(); ++i) {
                    if (!zone.regions()[i].Contains(x, y)) continue;
                    now.insert(i);
                    if (!occupied.count(i)) ++fires[zone.regions()[i].name];
                }
                occupied.swap(now);
            }
            return fires;
        };
        // y = 44 runs through the middle of azra's "start" (x 118..121,
        // y 42..46) and its neighbour "help1" (x 116..117).
        std::map<std::string, int> fires = fireCount(azra, 110, 127, 44);
        std::printf("   (walking azra's row y=44 from x=110 to x=127 fires:");
        for (const auto& kv : fires) std::printf(" %s x%d", kv.first.c_str(), kv.second);
        std::printf(")\n");
        Check(fires["start"] == 1 && fires["help1"] == 1,
              "crossing a region fires it exactly once, however many tiles wide it is");
        // Standing still must never re-fire it.
        std::map<std::string, int> still = fireCount(azra, 119, 119, 44);
        Check(still["start"] == 1,
              "...and entering it fires once even when the walk starts inside");
    }

    std::printf("\nm44_zone_region_smoke: %s\n", g_failures ? "FAILED" : "PASSED (all checks)");
    return g_failures ? 1 : 0;
}
