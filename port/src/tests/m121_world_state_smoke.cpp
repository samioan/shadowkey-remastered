// M121 smoke test: the world the player leaves behind, and the ground they
// walk on.
//
//   1. Actor footing. A creature stands at `floor + 0x80` (the actor move's
//      ground clamp, FUN_10000c64) -- checked against where the level
//      designer actually put every creature in all 21 zones, which is what
//      says moving creatures can be clamped there without popping.
//   2. FUN_10000ab8, the tile rule: the 0x180 step limit, the
//      creature-only barrier bit 0x40, and how many real tile edges the
//      step limit turns into walls the port used to let you walk up.
//   3. A script object's variables (the entity root's save tail): fields
//      collected, method bodies skipped, and restored into a fresh object.
//   4. The eighteen global story flags in character.dat.
//   5. `current.sav`'s level members through a real save slot: written by
//      ActuallySaveGame, replaced wholesale by LoadGameFromSlot.
//   6. Every scripted arrival: each `LoadLevel("X")` followed by
//      `SetCameraStart(x, y, z, ...)` in the shipped scripts, checked against
//      zone X -- the point is inside the map, on open floor, at the height
//      the script gives (Player binding 0x39's field order, read as the
//      engine reads it, has to put every one of them on the ground).
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#include "assets/save_records.h"
#include "assets/save_stream.h"
#include "assets/string_table.h"
#include "simkin_bindings/door_executable.h"
#include "simkin_bindings/menu_stack.h"
#include "simkin_bindings/monster_ai.h"
#include "simkin_bindings/player_executable.h"
#include "simkin_bindings/script_vars.h"
#include "skExecutableContext.h"
#include "skInterpreter.h"
#include "skRValue.h"
#include "skString.h"
#include "skTreeNode.h"
#include <memory>
#include "world/entity_types.h"
#include "world/zone.h"

namespace {

int g_failures = 0;
int g_checks = 0;

void Check(bool ok, const std::string& what) {
    ++g_checks;
    std::printf("%-84s %s\n", what.c_str(), ok ? "OK" : "FAILED");
    if (!ok) ++g_failures;
}

const char* const kZones[] = {"azra",     "broken1", "broken2", "crypt1",  "crypt2",
                              "crypt3",   "delfhide", "drgnfld", "dstar_e", "dstar_w",
                              "erthcave", "fearfrst", "ffarena", "ghstpass", "glaciercrawl",
                              "lakvan",   "lothcav",  "raiders", "snowline", "stouttp",
                              "twilite"};

}  // namespace

int main(int argc, char** argv) {
    const std::string root =
        argc > 1 ? argv[1]
                 : "The-Elder-Scrolls-Travels-Shadowkey_N-Gage_EN-FR-DE-ES-IT_USA-Europe-"
                   "EnFrDeEsIt-26102004/system/apps/6r51";

    sk::EntityTypeTable types;
    Check(types.Load(root), "entities.txt loads");

    // ------------------------------------------------------------------
    std::printf("\n--- 1. where creatures stand ---\n");
    // ------------------------------------------------------------------
    {
        int creatures = 0, atLift = 0, atFloor = 0;
        std::map<int, int> other;
        for (const char* name : kZones) {
            sk::Zone zone;
            if (!zone.Load(root, name)) continue;
            for (const sk::Zone::EntPlacement& e : zone.entities()) {
                const sk::EntityTypeDescriptor* d = types.Lookup(e.typeId);
                if (!d || (d->category != 2 && d->category != 7)) continue;
                const float x = static_cast<float>(e.x), y = static_cast<float>(e.y);
                const float floor = zone.CollisionFloorHeightAt(x, y, e.z + 384.0f);
                const int offset = static_cast<int>(std::lround(e.z - floor));
                ++creatures;
                if (std::abs(offset - 0x80) <= 8) {
                    ++atLift;
                } else if (std::abs(offset) <= 8) {
                    ++atFloor;
                } else {
                    ++other[offset / 64 * 64];
                }
            }
        }
        std::printf("  %d creature placements: %d at floor+0x80, %d on the floor, %d elsewhere\n",
                    creatures, atLift, atFloor, creatures - atLift - atFloor);
        for (const auto& kv : other) {
            if (kv.second >= 10) std::printf("    offset ~%d: %d\n", kv.first, kv.second);
        }
        Check(creatures > 1000, "every zone's creatures were measured");
        Check(atLift > atFloor && atLift * 2 > creatures,
              "most creatures are authored standing at floor + 0x80, the engine's clamp");
    }

    // ------------------------------------------------------------------
    std::printf("\n--- 2. FUN_10000ab8: step limit and creature barrier ---\n");
    // ------------------------------------------------------------------
    {
        int cliffs = 0, steps = 0, barrierTiles = 0, zonesWithBarrier = 0;
        bool cliffBlocks = true, stepPasses = true, barrierCreatureOnly = true;
        for (const char* name : kZones) {
            sk::Zone zone;
            if (!zone.Load(root, name)) continue;
            int zoneBarrier = 0;
            for (int ty = 1; ty + 1 < zone.height(); ++ty) {
                for (int tx = 1; tx + 1 < zone.width(); ++tx) {
                    const sk::ZmpCell& a = zone.CellAt(tx, ty);
                    if (a.blockFlags & 0x40) ++zoneBarrier;
                    const sk::ZmpCell& b = zone.CellAt(tx + 1, ty);
                    if (a.IsWall() || b.IsWall() || a.IsBlocked() || b.IsBlocked()) continue;
                    const float ax = tx * 256.0f + 128.0f, ay = ty * 256.0f + 128.0f;
                    const float bx = ax + 256.0f;
                    const float fa = zone.CollisionFloorHeightAt(ax, ay, -100000.0f);
                    const float fb = zone.CollisionFloorHeightAt(bx, ay, -100000.0f);
                    if ((b.blockFlags & 0x40) || (a.blockFlags & 0x40)) continue;
                    // Standing on A's floor, can the player step onto B?
                    const bool blocked = zone.ActorTileBlocked(bx, ay, fa, 0.0f, false);
                    if (fb - fa > 384.0f + 1.0f) {
                        ++cliffs;
                        if (!blocked) cliffBlocks = false;
                    } else if (fb - fa > 64.0f && fb - fa < 384.0f - 1.0f &&
                               zone.CollisionCeilingHeightAt(bx, ay, fa + 384.0f) > fb) {
                        ++steps;
                        if (blocked) stepPasses = false;
                    }
                }
            }
            if (zoneBarrier > 0) {
                ++zonesWithBarrier;
                barrierTiles += zoneBarrier;
                // One barrier tile: a creature is refused, the player not.
                for (int ty = 1; ty + 1 < zone.height() && barrierCreatureOnly; ++ty) {
                    for (int tx = 1; tx + 1 < zone.width(); ++tx) {
                        const sk::ZmpCell& c = zone.CellAt(tx, ty);
                        if (!(c.blockFlags & 0x40) || c.IsWall() || c.IsBlocked()) continue;
                        const float x = tx * 256.0f + 128.0f, y = ty * 256.0f + 128.0f;
                        const float f = zone.CollisionFloorHeightAt(x, y, -100000.0f);
                        barrierCreatureOnly = zone.ActorTileBlocked(x, y, f + 128.0f, 0.0f, true) &&
                                              !zone.ActorTileBlocked(x, y, f, 0.0f, false);
                        break;
                    }
                }
            }
        }
        // The player's standing Height() is CMap+0x1a, the same field as the
        // eye offset -- 0x200 (M84). Measure what a 0x200-tall player is
        // refused that a zero-height one is not: open, unflagged floor
        // under a ceiling lower than its head.
        int lowHeadroom = 0, openTiles = 0;
        for (const char* name : kZones) {
            sk::Zone zone;
            if (!zone.Load(root, name)) continue;
            for (int ty = 1; ty + 1 < zone.height(); ++ty) {
                for (int tx = 1; tx + 1 < zone.width(); ++tx) {
                    const sk::ZmpCell& c = zone.CellAt(tx, ty);
                    if (c.IsWall() || c.IsBlocked()) continue;
                    const float x = tx * 256.0f + 128.0f, y = ty * 256.0f + 128.0f;
                    const float f = zone.CollisionFloorHeightAt(x, y, -100000.0f);
                    if (zone.ActorTileBlocked(x, y, f, 0.0f, false)) continue;
                    ++openTiles;
                    if (zone.ActorTileBlocked(x, y, f, 512.0f, false)) ++lowHeadroom;
                }
            }
        }
        // FUN_10000498: a 0x20 cell kills whatever stands at or below its
        // floor. Count the hazard cells a player can step onto from safe,
        // open floor (an edge the tile rule passes), per zone.
        for (const char* name : kZones) {
            sk::Zone zone;
            if (!zone.Load(root, name)) continue;
            int hazard = 0, steppable = 0, exampleX = -1, exampleY = -1;
            for (int ty = 1; ty + 1 < zone.height(); ++ty) {
                for (int tx = 1; tx + 1 < zone.width(); ++tx) {
                    const sk::ZmpCell& c = zone.CellAt(tx, ty);
                    if (!(c.blockFlags & 0x20)) continue;
                    ++hazard;
                    const float hx = tx * 256.0f + 128.0f, hy = ty * 256.0f + 128.0f;
                    const int dx[] = {1, -1, 0, 0}, dy[] = {0, 0, 1, -1};
                    for (int k = 0; k < 4; ++k) {
                        const sk::ZmpCell& n = zone.CellAt(tx + dx[k], ty + dy[k]);
                        if (n.IsWall() || n.IsBlocked() || (n.blockFlags & 0x20)) continue;
                        const float nx = hx + dx[k] * 256.0f, ny = hy + dy[k] * 256.0f;
                        const float f = zone.CollisionFloorHeightAt(nx, ny, -100000.0f);
                        if (!zone.ActorTileBlocked(hx, hy, f, 512.0f, false)) {
                            ++steppable;
                            if (exampleX < 0) { exampleX = tx; exampleY = ty; }
                            break;
                        }
                    }
                }
            }
            if (hazard) std::printf("  %-12s %5d hazard (0x20) cells, %4d steppable from safe floor\n",
                                    name, hazard, steppable);
            if (exampleX >= 0) std::printf("               e.g. tile %d %d\n", exampleX, exampleY);
        }
        std::printf("  %d of %d open tiles have under 0x200 of headroom\n", lowHeadroom, openTiles);
        Check(lowHeadroom * 1000 < openTiles,
              "a 0x200-tall player is refused almost nowhere a zero-height one walks");
        std::printf("  %d open tile edges rise more than 0x180, %d rise less\n", cliffs, steps);
        std::printf("  %d creature-barrier (0x40) tiles across %d zones\n", barrierTiles,
                    zonesWithBarrier);
        Check(cliffs > 0, "the shipped zones have open edges that rise more than 0x180");
        Check(cliffBlocks, "and the tile rule refuses every one of them (the wall you could climb)");
        Check(steps > 0 && stepPasses, "while every smaller rise is still a step");
        Check(barrierTiles > 0 && barrierCreatureOnly,
              "a 0x40 tile refuses a creature and lets the player through");
    }

    // ------------------------------------------------------------------
    std::printf("\n--- 3. a script object's variables ---\n");
    // ------------------------------------------------------------------
    sk::StringTable strings;
    strings.Load(root + "/stringtable.eng");
    skInterpreter interpreter;
    sk_bindings::MenuStack stack(root, interpreter, &strings);
    {
        const std::string doorPath = root + "/door.s";
        skExecutableContext ctxt(&interpreter);
        sk_bindings::DoorExecutable door(skString(doorPath.c_str()), ctxt, stack.player());
        door.setValue(skString("saved_Open"), skString(), skRValue(1));
        std::vector<sk::SavedScriptVar> vars = sk_bindings::CollectScriptVars(door);
        bool sawOpen = false, sawMethod = false;
        for (const sk::SavedScriptVar& v : vars) {
            if (v.name == "saved_Open" && v.value == "1") sawOpen = true;
            if (v.name == "Init" || v.name == "OnUse") sawMethod = true;
        }
        Check(sawOpen, "door.s's saved_Open = 1 is collected");
        Check(!sawMethod, "and its Init/OnUse bodies are not (they contain a `{`)");

        sk_bindings::DoorExecutable fresh(skString(doorPath.c_str()), ctxt, stack.player());
        // A field read through Simkin is a tree-node wrapper object; the
        // value the save carries is the node's data, so read that.
        auto fieldData = [](sk_bindings::DoorExecutable& d, const char* name) {
            return std::string(d.getNode()->findChildData(skString(name)).ptr());
        };
        const std::string before = fieldData(fresh, "saved_Open");
        sk_bindings::RestoreScriptVars(fresh, vars);
        const std::string after = fieldData(fresh, "saved_Open");
        Check(before == "0" && after == "1",
              "restored into a fresh door, saved_Open reads 1 before Init would run");
        sk_bindings::RestoreScriptVars(fresh, {{"saved_NewField", "7"}});
        Check(fieldData(fresh, "saved_NewField") == "7", "a field the script never declared is added");
    }

    // ------------------------------------------------------------------
    std::printf("\n--- 4. the eighteen story flags ---\n");
    // ------------------------------------------------------------------
    {
        sk_bindings::PlayerExecutable& player = stack.player();
        player.setValue(skString("saved_Guild"), skString(), skRValue(3));
        player.setValue(skString("saved_EndGame"), skString(), skRValue(1));
        sk::SavedEntity rec = player.BuildSaveRecord("azra");
        Check(rec.player.trailingInts[0] == 3 && rec.player.trailingInts[17] == 1,
              "saved_Guild and saved_EndGame land in their own trailing ints");
        player.setValue(skString("saved_Guild"), skString(), skRValue(0));
        player.ApplySaveRecord(rec, stack);
        skRValue guild;
        player.getValue(skString("saved_Guild"), skString(), guild);
        Check(guild.intValue() == 3, "and come back from the record on a load");
        player.ResetForNewSession();
        player.getValue(skString("saved_Guild"), skString(), guild);
        Check(guild.intValue() == 0, "a new session's player starts with them clear");
    }

    // ------------------------------------------------------------------
    std::printf("\n--- 5. current.sav's level members through a slot ---\n");
    // ------------------------------------------------------------------
    {
        std::string dir = ".";
        if (const char* tmp = std::getenv("TEMP")) dir = tmp;
        stack.SetSaveDirectory(dir);
        constexpr int kSlot = 3;

        sk::SavedLevelState snowline;
        snowline.level.scriptVars.push_back({"saved_Bridge", "1"});
        sk::SaveStream s1;
        snowline.Write(s1);
        stack.StoreLevelState("SnowLine", s1.bytes());

        int snapshots = 0;
        stack.SetLevelSnapshotter([&]() {
            ++snapshots;
            sk::SavedLevelState azra;
            azra.level.scriptVars.push_back({"saved_MetTrinket", "1"});
            sk::SavedEntity rat;
            rat.typeId = 202;
            rat.kind = sk::SavedEntityKind::Actor;
            azra.entities.push_back(rat);
            sk::SaveStream s;
            azra.Write(s);
            return s.bytes();
        });
        stack.SetCurrentLevelName("azra");
        Check(stack.ActuallySaveGame(kSlot), "the slot is written");
        Check(snapshots == 1, "and the level being played was snapshotted for it");

        stack.ClearLevelStates();
        stack.StoreLevelState("dstar_w", s1.bytes());  // the session a load abandons
        Check(stack.LoadGameFromSlot(kSlot) == "azra", "the slot loads back into azra");
        Check(stack.levelStateCount() == 2 && stack.FindLevelState("azra") &&
                  stack.FindLevelState("snowline") && !stack.FindLevelState("dstar_w"),
              "current.sav is now exactly the slot's two levels, case-insensitively");
        Check(stack.TakeDiscardDepartingLevel() && !stack.TakeDiscardDepartingLevel(),
              "and the level the load walks away from is not written over them (once)");
        if (const std::vector<uint8_t>* azraBytes = stack.FindLevelState("azra")) {
            sk::SaveStream s;
            s.Reset(*azraBytes);
            sk::SavedLevelState back;
            back.Read(s, [](int32_t) { return sk::SavedEntityKind::Actor; });
            Check(!s.failed() && back.entities.size() == 1 && back.entities[0].typeId == 202 &&
                      back.level.scriptVars.size() == 1 &&
                      back.level.scriptVars[0].name == "saved_MetTrinket",
                  "azra.dat reads back: one rat, one Level.saved_ variable");
        }
        stack.DeleteGame(kSlot);
    }

    // ------------------------------------------------------------------
    std::printf("\n--- 6. every scripted arrival point ---\n");
    // ------------------------------------------------------------------
    {
        const std::regex loadRe(R"re(LoadLevel\s*\(\s*"([A-Za-z_0-9]+)"\s*\))re");
        const std::regex camRe(
            R"re(SetCameraStart\s*\(\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(-?[\d+ ]+?)\s*,)re");
        std::map<std::string, std::unique_ptr<sk::Zone>> zones;
        int arrivals = 0, onFloor = 0;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
            if (!entry.is_regular_file() || entry.path().extension() != ".s") continue;
            std::ifstream in(entry.path(), std::ios::binary);
            std::stringstream buf;
            buf << in.rdbuf();
            const std::string text = buf.str();
            for (auto it = std::sregex_iterator(text.begin(), text.end(), camRe);
                 it != std::sregex_iterator(); ++it) {
                // The destination is the nearest LoadLevel above the call.
                const std::string before = text.substr(0, static_cast<size_t>(it->position()));
                std::string dest;
                for (auto lt = std::sregex_iterator(before.begin(), before.end(), loadRe);
                     lt != std::sregex_iterator(); ++lt) {
                    dest = (*lt)[1].str();
                }
                if (dest.empty()) continue;
                for (char& c : dest) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                if (!zones.count(dest)) {
                    auto z = std::make_unique<sk::Zone>();
                    if (!z->Load(root, dest)) continue;
                    zones[dest] = std::move(z);
                }
                const sk::Zone& zone = *zones[dest];
                const float x = std::stof((*it)[1].str()), y = std::stof((*it)[2].str());
                // `0 + 512` is a real argument (broken1.s): sum the terms.
                float z = 0.0f;
                std::stringstream terms((*it)[3].str());
                std::string term;
                while (std::getline(terms, term, '+')) {
                    if (term.find_first_not_of(' ') != std::string::npos) z += std::stof(term);
                }
                // The engine keeps only the low 16 bits of z (`+0xa4`).
                z = static_cast<float>(static_cast<int16_t>(static_cast<int>(z) & 0xffff));
                ++arrivals;
                const int tx = static_cast<int>(x) >> 8, ty = static_cast<int>(y) >> 8;
                const bool inside = zone.InBounds(tx, ty) && !zone.CellAt(tx, ty).IsWall();
                const float floor = inside ? zone.CollisionFloorHeightAt(x, y, z + 384.0f) : 0.0f;
                if (inside && std::fabs(floor - z) <= 1024.0f) {
                    ++onFloor;
                } else {
                    std::printf("  %s -> %s (%.0f, %.0f, %.0f): %s, floor %.0f\n",
                                entry.path().filename().string().c_str(), dest.c_str(), x, y, z,
                                inside ? "open" : "WALL/outside", floor);
                }
            }
        }
        std::printf("  %d scripted arrivals, %d on open floor within 4 tiles of their z\n",
                    arrivals, onFloor);
        Check(arrivals >= 40, "the shipped scripts' LoadLevel+SetCameraStart pairs were found");
        Check(onFloor * 10 >= arrivals * 9,
              "and SetCameraStart's (x, y, z) puts nearly every one on open ground");
    }

    // ------------------------------------------------------------------
    std::printf("\n--- 7. M122: the creature walk ---\n");
    // ------------------------------------------------------------------
    {
        // Friction then thrust, every frame, at the 25Hz delta of 10.
        int v = 0;
        int firstStep = 0, steady = 0;
        for (int tick = 0; tick < 60; ++tick) {
            v = sk_bindings::DecayVelocity(v, sk_bindings::kGroundFriction) +
                sk_bindings::CreatureThrust(sk_bindings::kDefaultCreatureSpeed, 1.0f);
            const int step = (v * 10) >> 8;
            if (tick == 0) firstStep = step;
            steady = step;
        }
        std::printf("  Speed 35: first tick %d units, steady %d units/tick (v=%d)\n", firstStep,
                    steady, v);
        Check(sk_bindings::CreatureThrust(35, 1.0f) == 131,
              "thrust is Speed*30 through the sine table's >>3, >>8: 131 at Speed 35");
        Check(steady >= 15 && steady <= 17, "the steady chase is ~16 world units a tick");
        Check(firstStep < steady, "and a creature accelerates into it rather than starting at it");
        int coast = v;
        int ticks = 0;
        while (coast != 0 && ticks < 100) {
            coast = sk_bindings::DecayVelocity(coast, sk_bindings::kGroundFriction);
            ++ticks;
        }
        Check(coast == 0 && ticks < 30, "with nothing pushing, the rounding brings it to exactly 0");
    }

    std::printf("\nm121_world_state_smoke: %s (%d checks)\n", g_failures == 0 ? "OK" : "FAILED",
                g_checks);
    return g_failures == 0 ? 0 : 1;
}
