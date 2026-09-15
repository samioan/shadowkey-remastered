// M49 smoke test: the arrow.
//
// M47 left a pointer -- "the ranged branch spawns a projectile via
// FUN_10005730 with entity type id 598 (thrown) or 599 (bow)" -- and M48
// established that this is a different class from the spell projectile it
// had just implemented. This closes it.
//
// The decompiled pieces:
//
//   * FUN_1006ca90 case 2 -- SetRange, which is where the ranged-path flag
//     `weapon+0x1a7` actually comes from (`value > 0x400`), not SetBow.
//   * FUN_10005730 / FUN_10007da4 -- the spawn and the 0x16c-byte class.
//   * FUN_10007214 -- the flight: no lifetime, 1.25 tiles a tick, four
//     floor-height wall probes, two entity sweeps.
//   * FUN_100425bc's and FUN_100835b8's ranged branches -- the player's and
//     a creature's, which share the same damage roll and the same spawn.
//   * FUN_1001beac / FUN_1001bd50 / FUN_1001bcac -- the collision height an
//     arrow is stopped by.
//
// Part 1 is the load-bearing one: it re-derives the whole weapon/projectile
// mapping from the shipped scripts, entities.txt, models.txt and the sound
// tables rather than restating this port's own constants.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "assets/string_table.h"
#include "render3d/camera.h"
#include "simkin_bindings/arrow_projectile.h"
#include "simkin_bindings/combat.h"
#include "simkin_bindings/game_constants.h"
#include "simkin_bindings/item_executable.h"
#include "simkin_bindings/level_executable.h"
#include "simkin_bindings/menu_stack.h"
#include "simkin_bindings/monster_executable.h"
#include "simkin_bindings/player_executable.h"
#include "simkin_bindings/effects.h"
#include "simkin_bindings/spell_projectile.h"
#include "simkin_bindings/stats_damage.h"
#include "simkin_bindings/weapon_viewmodel.h"
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

std::string ReadFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::string();
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// A deliberately dumb "does this script call Name( value )" reader -- the
// same shape M48's part 1 uses. Returns -1 when the call is absent.
int ScriptIntCall(const std::string& text, const std::string& name) {
    const std::string needle = name + "(";
    size_t at = 0;
    while ((at = text.find(needle, at)) != std::string::npos) {
        size_t i = at + needle.size();
        while (i < text.size() && (text[i] == ' ' || text[i] == '\t')) ++i;
        size_t start = i;
        while (i < text.size() && (std::isdigit(static_cast<unsigned char>(text[i])) != 0)) ++i;
        if (i > start) return std::atoi(text.substr(start, i - start).c_str());
        at += needle.size();
    }
    return -1;
}

bool ScriptCallsTrue(const std::string& text, const std::string& name) {
    const std::string needle = name + "(";
    size_t at = text.find(needle);
    if (at == std::string::npos) return false;
    size_t end = text.find(')', at);
    if (end == std::string::npos) return false;
    return text.substr(at, end - at).find("true") != std::string::npos;
}

}  // namespace

int main(int argc, char** argv) {
    using Item = sk_bindings::ItemExecutable;
    using Monster = sk_bindings::MonsterExecutable;

    const char* scriptRootArg =
        argc > 1 ? argv[1]
                  : "The-Elder-Scrolls-Travels-Shadowkey_N-Gage_EN-FR-DE-ES-IT_USA-Europe-"
                    "EnFrDeEsIt-26102004/system/apps/6r51";
    const std::string scriptRoot = scriptRootArg;

    sk::StringTable strings;
    strings.Load(scriptRoot + "/stringtable.eng");
    sk::EntityTypeTable entityTypes;
    if (!entityTypes.Load(scriptRoot.c_str())) {
        std::printf("m49_arrow_projectile_smoke: FAILED to load entities.txt\n");
        return 1;
    }

    skInterpreter interpreter;
    sk_bindings::MenuStack stack(scriptRoot, interpreter, &strings);
    stack.level().SetEntityTypes(&entityTypes);

    auto loadItem = [&](const std::string& rel) -> std::unique_ptr<Item> {
        skExecutableContext loadCtxt(&interpreter);
        try {
            auto item = std::make_unique<Item>(skString((scriptRoot + "/" + rel).c_str()), loadCtxt,
                                                stack);
            skRValueArray args;
            args.append(skRValue(0));
            skRValue ret;
            skExecutableContext callCtxt(&interpreter);
            item->method(skString("Init"), args, ret, callCtxt);
            return item;
        } catch (skParseException&) {
            return nullptr;
        } catch (skRuntimeException&) {
            return nullptr;
        }
    };
    auto loadMonster = [&](const std::string& rel) -> std::unique_ptr<Monster> {
        skExecutableContext loadCtxt(&interpreter);
        try {
            auto m = std::make_unique<Monster>(skString((scriptRoot + "/" + rel).c_str()), loadCtxt,
                                                &strings, stack.player(), stack);
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

    std::printf("=== M49: the arrow ===\n\n");

    // -------------------------------------------------------------------
    // 1. The shipped data, read fresh.
    // -------------------------------------------------------------------
    std::printf("--- 1. the weapon/projectile mapping, re-derived from shipped data ---\n");

    {
        // Split every weapon script two ways: by the real engine's own
        // threshold, and by which of the three marker setters it calls.
        // FUN_1006ca90 case 2 says the first is what the ranged branch
        // actually reads; the corpus says the two agree exactly, which is
        // what makes reading it that way safe.
        //
        // The probe set is the whole ranged half (all 16) plus a spread of
        // melee weapons -- named rather than directory-walked, since the
        // rest of this port's tests avoid <filesystem>.
        static const char* kProbe[] = {
            "bandit_double-bow", "bandit_longbow", "daedric_dart", "daedric_longbow",
            "dwarven_crossbow", "ebony_dart", "glass_throwing_knife", "iron_throwing_knife",
            "penumbric_throwing_knife", "ra-_gada_longbow", "shadow_dart", "silver_dart",
            "spire_thief_longbow", "steel_crossbow", "steel_dart", "steel_throwing_knife",
            "iron_longsword", "steel_longsword", "iron_mace", "silver_dagger", "iron_claymore",
            "ebony_broadsword", "steel_battle_axe", "iron_dagger", "club", "steel_staff"};
        int rangedByThreshold = 0, rangedByFlag = 0, meleeByThreshold = 0;
        int launched = 0, thrownCount = 0, total = 0;
        std::set<int> rangeValues;
        bool everyRangedIsFlagged = true, noMeleeIsFlagged = true;
        for (const char* probeName : kProbe) {
            const std::string rel = std::string("weapons/") + probeName + ".s";
            const std::string text = ReadFile(scriptRoot + "/" + rel);
            if (text.empty()) continue;
            const int range = ScriptIntCall(text, "SetRange");
            if (range < 0) continue;
            ++total;
            rangeValues.insert(range);
            const bool bow = ScriptCallsTrue(text, "SetBow");
            const bool crossbow = ScriptCallsTrue(text, "SetCrossbow");
            const bool thrown = ScriptCallsTrue(text, "SetThrowingWeapon");
            const bool flagged = bow || crossbow || thrown;
            if (range >= sk_bindings::kRangedPathMinRange) {
                ++rangedByThreshold;
                if (bow || crossbow) ++launched;
                if (thrown) ++thrownCount;
                if (!flagged) everyRangedIsFlagged = false;
            } else {
                ++meleeByThreshold;
                if (flagged) noMeleeIsFlagged = false;
            }
            if (flagged) ++rangedByFlag;
        }
        Check(total >= 24, "the weapon probe set actually loaded");
        Check(rangeValues.size() == 2 && rangeValues.count(384) == 1 &&
                  rangeValues.count(16384) == 1,
              "SetRange is bimodal across the corpus: only 384 and 16384");
        Check(rangedByThreshold == rangedByFlag && everyRangedIsFlagged && noMeleeIsFlagged,
              "SetRange > 0x400 selects exactly the bow/crossbow/thrown weapons");
        Check(rangedByThreshold == 16, "...and there are 16 of them");
        Check(launched == 7 && thrownCount == 9,
              "7 bows and crossbows (typeId 599) against 9 darts and knives (598)");
        Check(meleeByThreshold >= 8 && meleeByThreshold == total - rangedByThreshold,
              "every other weapon in the probe set is on the melee side");
    }

    {
        // entities.txt names the two projectiles, and models.txt names what
        // they resolve to. Nothing in this port had to be told either.
        const sk::EntityTypeDescriptor* thrownType =
            entityTypes.Lookup(sk_bindings::kThrownProjectileTypeId);
        const sk::EntityTypeDescriptor* arrowType =
            entityTypes.Lookup(sk_bindings::kBowProjectileTypeId);
        Check(thrownType != nullptr && arrowType != nullptr,
              "entities.txt defines both 598 and 599");
        Check(thrownType && thrownType->modelArchiveIndex == sk_bindings::kThrownProjectileModel,
              "598 (!throwing) resolves to models.idx 176");
        Check(arrowType && arrowType->modelArchiveIndex == sk_bindings::kBowProjectileModel,
              "599 (!arrow) resolves to models.idx 175");

        // ...and models.txt says what those two are.
        std::map<int, std::string> modelNames;
        {
            std::ifstream in(scriptRoot + "/models.txt");
            std::string line;
            while (std::getline(in, line)) {
                std::istringstream ls(line);
                int idx = -1, a = 0, b = 0, c = 0;
                std::string name;
                if (ls >> idx >> a >> b >> c >> name) modelNames[idx] = name;
            }
        }
        Check(modelNames[sk_bindings::kBowProjectileModel] == "arrow.bin",
              "models.txt 175 is arrow.bin");
        Check(modelNames[sk_bindings::kThrownProjectileModel] == "throw_dagger.bin",
              "models.txt 176 is throw_dagger.bin");
        Check(modelNames[225] == "bow.bin",
              "...and 225, every archer's SetAttachedWeapon, is bow.bin");
    }

    {
        // The twelve creature archers, and what they agree on.
        static const char* kArchers[] = {
            "monsters/ace_archer.s",   "monsters/archer.s",          "monsters/arrow_shade.s",
            "monsters/deadeye.s",      "monsters/dragonstar_archer.s", "monsters/elite_bowman.s",
            "monsters/glacier_captain.s", "monsters/icebowman.s",    "monsters/skyrim_archer.s",
            "monsters/spire_archer.s", "monsters/tharn_archer.s",    "lakvan/deadeye.s"};
        int found = 0, art175 = 0, attachedBow = 0, noise1 = 0, gaveWeapon = 0, range12000 = 0;
        for (const char* rel : kArchers) {
            const std::string text = ReadFile(scriptRoot + "/" + rel);
            if (text.empty()) continue;
            ++found;
            if (ScriptIntCall(text, "SetProjectile") == 175) ++art175;
            if (ScriptIntCall(text, "SetAttachedWeapon") == 225) ++attachedBow;
            if (ScriptIntCall(text, "SetAttackNoise") == 1) ++noise1;
            if (text.find("GiveWeapon") != std::string::npos) ++gaveWeapon;
            if (ScriptIntCall(text, "SetAttackRange") == 12000) ++range12000;
        }
        Check(found == 12, "twelve shipped scripts call SetProjectile");
        Check(art175 == 12, "...all twelve pass 175, models.txt's own index for arrow.bin");
        Check(attachedBow == 12, "...all twelve carry a visible bow (SetAttachedWeapon 225)");
        Check(noise1 == 12, "...all twelve fire on sound slot 1");
        Check(gaveWeapon == 0,
              "...and none calls GiveWeapon, which is why FUN_100835b8's weapon gate passes");
        Check(range12000 == 11,
              "eleven stand off at 12000; lakvan/deadeye.s is the one outlier");
    }

    {
        // Sound slot 1 across every shipped table -- the same shape M48
        // used to confirm the two cast sounds.
        static const char* kZones[] = {"azra",     "snowline", "crypt1",  "crypt2",  "lothcav",
                                        "ghstpass", "fearfrst", "twilite", "lakvan",  "broken1"};
        int named = 0, tables = 0;
        for (const char* zone : kZones) {
            const std::string text = ReadFile(scriptRoot + "/" + zone + "_sounds.txt");
            if (text.empty()) continue;
            ++tables;
            std::istringstream ls(text);
            std::string line;
            while (std::getline(ls, line)) {
                std::istringstream l2(line);
                int slot = -1;
                std::string name;
                if ((l2 >> slot >> name) && slot == sk_bindings::kBowFireSound) {
                    if (name == "barch_firebow.wav") ++named;
                    break;
                }
            }
        }
        Check(tables >= 8, "the sound tables loaded");
        Check(named == tables,
              "sound slot 1 is barch_firebow.wav in every table sampled");
    }

    // -------------------------------------------------------------------
    // 2. Real weapons, through the port's own Item class.
    // -------------------------------------------------------------------
    std::printf("\n--- 2. real weapon scripts through ItemExecutable ---\n");
    {
        std::unique_ptr<Item> longbow = loadItem("weapons/bandit_longbow.s");
        std::unique_ptr<Item> dart = loadItem("weapons/steel_dart.s");
        std::unique_ptr<Item> crossbow = loadItem("weapons/steel_crossbow.s");
        Check(longbow && dart && crossbow, "the three ranged weapon scripts load");
        if (longbow && dart && crossbow) {
            Check(longbow->range() == 16384 && longbow->usesRangedPath(),
                  "bandit_longbow.s: SetRange(16384) sets the ranged path");
            Check(longbow->launched() && !longbow->thrown(),
                  "...via SetBow, so +0x17a and not +0x179");
            Check(longbow->projectileTypeId() == sk_bindings::kBowProjectileTypeId,
                  "...and it therefore fires entities.txt 599, the arrow");
            Check(crossbow->launched() &&
                      crossbow->projectileTypeId() == sk_bindings::kBowProjectileTypeId,
                  "a crossbow is 'launched' too, and also fires 599");
            Check(dart->thrown() && !dart->launched(),
                  "steel_dart.s marks the other byte, +0x179");
            Check(dart->projectileTypeId() == sk_bindings::kThrownProjectileTypeId,
                  "...so a dart throws 598, the throw_dagger");
            Check(dart->usesRangedPath(),
                  "...and it is on the ranged path all the same, by its range alone");
        }
        std::unique_ptr<Item> sword = loadItem("weapons/iron_longsword.s");
        if (sword) {
            Check(sword->range() == 384 && !sword->usesRangedPath(),
                  "a melee weapon's 384 stays below the 0x401 threshold");
            Check(!sword->launched() && !sword->thrown(),
                  "...and it sets neither projectile byte");
        }
    }

    // -------------------------------------------------------------------
    // 3. The spawn.
    // -------------------------------------------------------------------
    std::printf("\n--- 3. FUN_10005730, the spawn ---\n");
    {
        // Engine heading 0 points along +y, so a shot at heading 0 should
        // have no x velocity and a full-speed +y one. Full speed is
        // sine(1.0) * 0xa00 >> 11 == 256 * 2560 / 2048 == 320.
        sk_bindings::ArrowProjectile north = sk_bindings::SpawnArrowProjectile(
            nullptr, /*ownerIsPlayer=*/false, 1000, 2000, 500, 0, /*yaw=*/0, /*pitch=*/0,
            sk_bindings::kBowProjectileTypeId, 7, 11);
        Check(north.vx == 0 && north.vy == 320,
              "heading 0 flies along +y at 320 units a tick -- 1.25 tiles");
        Check(north.x == 1000 && north.y == 2000,
              "x and y are copied verbatim: there is no muzzle offset at all");
        Check(north.z == 500 + sk_bindings::kArrowSpawnZOffset,
              "a creature's arrow leaves one whole tile above its feet");
        Check(north.modelIndex == sk_bindings::kBowProjectileModel,
              "...and carries models.idx 175, arrow.bin");

        sk_bindings::ArrowProjectile east = sk_bindings::SpawnArrowProjectile(
            nullptr, false, 0, 0, 0, 0, /*yaw=*/sk_bindings::kEngineTurn / 4, 0,
            sk_bindings::kThrownProjectileTypeId, 1, 1);
        Check(east.vx == 320 && east.vy == 0, "a quarter turn puts the whole speed on +x");
        Check(east.modelIndex == sk_bindings::kThrownProjectileModel,
              "a 598 spawn carries models.idx 176, throw_dagger.bin");

        sk_bindings::ArrowProjectile mine = sk_bindings::SpawnArrowProjectile(
            nullptr, /*ownerIsPlayer=*/true, 10, 20, 300, /*shooterEyeZ=*/777, 0, 0,
            sk_bindings::kBowProjectileTypeId, 3, 4);
        Check(mine.z == 777,
              "the player's arrow leaves at the eye-height field, not feet plus a tile");

        // The pitch term's own oddity: this class indexes the sine table at
        // `>> 6`, a half-scale angle, where every other consumer -- the
        // spell projectile's identical-looking pitch term included -- uses
        // `>> 5`. A quarter turn of pitch therefore reads as an eighth.
        const int quarter = sk_bindings::kEngineTurn / 4;
        sk_bindings::ArrowProjectile down = sk_bindings::SpawnArrowProjectile(
            nullptr, false, 0, 0, 0, 0, 0, /*pitch=*/quarter, sk_bindings::kBowProjectileTypeId, 1,
            1);
        const int halfScale =
            (static_cast<int16_t>((sk_bindings::SwaySine(((-quarter) >> 6) & 0x7ff) * 0xa00) >> 8)) >>
            3;
        Check(down.vz == halfScale, "the vertical term reads pitch at half scale (>> 6, not >> 5)");
        Check(down.vz != 0, "...but it is a real elevation, not a dropped term");
    }

    // -------------------------------------------------------------------
    // 4. The flight.
    // -------------------------------------------------------------------
    std::printf("\n--- 4. FUN_10007214, the flight ---\n");
    {
        // A world with a floor at 0 everywhere: nothing ever blocks.
        auto openField = [](int, int, int) {
            sk_bindings::ArrowCell cell;
            cell.onMap = true;
            cell.floorHeight = 0;
            return cell;
        };
        std::vector<sk_bindings::ArrowTarget> nobody;

        sk_bindings::ArrowProjectile shot = sk_bindings::SpawnArrowProjectile(
            nullptr, false, 0x8000, 0x8000, 0x400, 0, 0, 0, sk_bindings::kBowProjectileTypeId, 5,
            50);
        for (int i = 0; i < 200; ++i) sk_bindings::TickArrowProjectile(shot, openField, nobody);
        Check(shot.alive, "an arrow has no lifetime at all -- 200 ticks and still flying");
        Check(shot.y == 0x8000 + 200 * 320, "...and it has covered 250 tiles doing it");

        // A floor above the arrow stops it, reflects that axis and marks it
        // dead -- all three at once, which is what the real probe does.
        auto ridgeAtY = [](int, int worldY, int) {
            sk_bindings::ArrowCell cell;
            cell.onMap = true;
            cell.floorHeight = (worldY >> 8) >= 4 ? 0x8000 : 0;
            return cell;
        };
        sk_bindings::ArrowProjectile intoWall = sk_bindings::SpawnArrowProjectile(
            nullptr, false, 0x180, 0x180, 0x400, 0, 0, 0, sk_bindings::kBowProjectileTypeId, 5, 50);
        int ticks = 0;
        while (intoWall.alive && ticks < 50) {
            sk_bindings::TickArrowProjectile(intoWall, ridgeAtY, nobody);
            ++ticks;
        }
        Check(!intoWall.alive && ticks < 50, "a floor taller than the arrow stops it");
        Check(intoWall.vy < 0, "...and reflects its velocity on that axis before despawning");
        Check(intoWall.y < 4 * 256,
              "...having pushed it back out of the cell it could not enter");

        // Off the map is not a stop: the real code returns without
        // despawning, so the shot silently keeps going.
        auto offMap = [](int, int, int) { return sk_bindings::ArrowCell{}; };
        sk_bindings::ArrowProjectile lost = sk_bindings::SpawnArrowProjectile(
            nullptr, false, 0, 0, 0, 0, 0, 0, sk_bindings::kBowProjectileTypeId, 5, 50);
        sk_bindings::TickArrowProjectile(lost, offMap, nobody);
        Check(lost.alive && lost.y == 320,
              "off the map the arrow is neither tested nor stopped -- it just keeps going");
    }

    // -------------------------------------------------------------------
    // 5. The impact.
    // -------------------------------------------------------------------
    std::printf("\n--- 5. the impact ---\n");
    {
        auto openField = [](int, int, int) {
            sk_bindings::ArrowCell cell;
            cell.onMap = true;
            cell.floorHeight = 0;
            return cell;
        };
        std::unique_ptr<Monster> archer = loadMonster("monsters/archer.s");
        Check(archer != nullptr, "monsters/archer.s loads");
        if (archer) {
            Check(archer->shootsProjectile() && archer->projectileArt() == 175,
                  "archer.s reads back as a creature that shoots, art 175");
            // Note these are *not* archer.s's literals (3..9, 25 health):
            // its trailing SetMob(4) rescales them, which is M43's finding
            // and is what makes a real archer's numbers what they are.
            std::printf("  archer.s reads back: armour %d, damage %d..%d, health %d\n",
                        archer->armorValue(), archer->damageMin(), archer->damageMax(),
                        archer->actorHealth());
            Check(archer->armorValue() == 1 && archer->damageMax() == 8 &&
                      archer->actorHealth() == 23,
                  "...with the post-SetMob stats the real creature actually has");

            // Put the creature one tile ahead of a player's arrow. The
            // arrow's damage is applied without ever consulting armour --
            // which is the one thing that separates this from the melee
            // path, where the armour rating is subtracted first.
            const int before = archer->actorHealth();
            sk_bindings::ArrowProjectile shot = sk_bindings::SpawnArrowProjectile(
                &stack.player(), /*ownerIsPlayer=*/true, 0x80, 0x80, 0, /*eyeZ=*/0x40, 0, 0,
                sk_bindings::kBowProjectileTypeId, /*damage=*/5, /*attackSkill=*/100000);
            std::vector<sk_bindings::ArrowTarget> targets{{archer.get(), 0x80, 0x180}};
            sk_bindings::ArrowImpact impact =
                sk_bindings::TickArrowProjectile(shot, openField, targets);
            Check(impact.struck && !shot.alive, "an actor in the arrow's tile stops it");
            Check(impact.hit && impact.target == archer.get(),
                  "...and a heavily-favoured roll lands");
            // M98: "exactly" now includes the shooter's Strength term, which
            // the stats DoDamage adds -- and still no armour.
            const int str = sk_bindings::StrengthDamageTerm(
                stack.player().strength(),
                stack.player().EffectStatValue(sk_bindings::kEffectStatStrength));
            Check(archer->actorHealth() == before - 5 - str,
                  "...for exactly the rolled damage: armour is not subtracted on the ranged path");
        }

        // The shooter is never a target.
        std::unique_ptr<Monster> rat = loadMonster("monsters/arat.s");
        if (rat) {
            const int full = rat->actorHealth();
            sk_bindings::ArrowProjectile self = sk_bindings::SpawnArrowProjectile(
                rat.get(), /*ownerIsPlayer=*/false, 0x80, 0x80, 0, 0, 0, 0,
                sk_bindings::kBowProjectileTypeId, 9, 100000);
            std::vector<sk_bindings::ArrowTarget> targets{{rat.get(), 0x80, 0x180}};
            sk_bindings::TickArrowProjectile(self, openField, targets);
            Check(rat->actorHealth() == full, "a shooter cannot be hit by their own arrow");
        }
    }

    // -------------------------------------------------------------------
    // 6. The collision height, against a real zone.
    // -------------------------------------------------------------------
    std::printf("\n--- 6. FUN_1001beac against real azra geometry ---\n");
    {
        sk::Zone azra;
        Check(azra.Load(scriptRoot, "azra"), "azra loads");
        if (azra.width() > 0) {
            // The point of the whole floor-height model: an arrow at the
            // player's own eye height must not pass through walls. Sample
            // every wall tile in the zone and check its collision height
            // clears a shot fired from the player's start.
            const float eyeZ = static_cast<float>(azra.playerStartZ) + sk::kEyeHeightOffset;
            int wallTiles = 0, wallsThatStop = 0, openTiles = 0, openThatPass = 0;
            for (int ty = 0; ty < azra.height(); ++ty) {
                for (int tx = 0; tx < azra.width(); ++tx) {
                    const sk::ZmpCell& cell = azra.CellAt(tx, ty);
                    const float cx = tx * 256.0f + 128.0f, cy = ty * 256.0f + 128.0f;
                    const float h = azra.CollisionFloorHeightAt(cx, cy, eyeZ);
                    if (cell.IsWall()) {
                        ++wallTiles;
                        if (eyeZ < h) ++wallsThatStop;
                    } else {
                        ++openTiles;
                        if (eyeZ >= h) ++openThatPass;
                    }
                }
            }
            Check(wallTiles > 0 && openTiles > 0, "azra has both wall and open tiles");
            std::printf("  walls stopping an eye-height shot: %d/%d; open tiles passable: %d/%d\n",
                        wallsThatStop, wallTiles, openThatPass, openTiles);
            Check(wallTiles > 0 && wallsThatStop == wallTiles,
                  "every one of azra's wall tiles stops an eye-height arrow");

            // The open-tile figure is the interesting one, and it is the
            // whole argument for reading FUN_1001beac rather than reusing
            // the spell projectile's wall-flag test: 1858 of azra's tiles
            // are **not** flagged as walls and still stop an arrow, because
            // their floor is above head height. A bare `IsWall()` test would
            // fire an arrow straight through every one of them.
            //
            // What is left once those are excluded has to be a single large
            // connected space, or the level would not be playable -- so
            // flood-fill it from the player start and see.
            std::vector<uint8_t> seen(static_cast<size_t>(azra.width()) * azra.height(), 0);
            std::vector<std::pair<int, int>> frontier;
            const int startX = azra.playerStartX >> 8, startY = azra.playerStartY >> 8;
            if (azra.InBounds(startX, startY)) {
                seen[static_cast<size_t>(startY) * azra.width() + startX] = 1;
                frontier.push_back({startX, startY});
            }
            int reachable = 0;
            while (!frontier.empty()) {
                const std::pair<int, int> at = frontier.back();
                frontier.pop_back();
                ++reachable;
                static const int kDx[4] = {1, -1, 0, 0}, kDy[4] = {0, 0, 1, -1};
                for (int d = 0; d < 4; ++d) {
                    const int nx = at.first + kDx[d], ny = at.second + kDy[d];
                    if (!azra.InBounds(nx, ny)) continue;
                    const size_t idx = static_cast<size_t>(ny) * azra.width() + nx;
                    if (seen[idx]) continue;
                    if (azra.CellAt(nx, ny).IsWall()) continue;
                    const float nxw = nx * 256.0f + 128.0f, nyw = ny * 256.0f + 128.0f;
                    if (eyeZ < azra.CollisionFloorHeightAt(nxw, nyw, eyeZ)) continue;
                    seen[idx] = 1;
                    frontier.push_back({nx, ny});
                }
            }
            std::printf("  connected space an arrow can fly through, from the start: %d tiles\n",
                        reachable);
            Check(openTiles - openThatPass > 1000,
                  "over a thousand of azra's non-wall tiles still stop an arrow -- a wall-flag "
                  "test alone would miss every one");
            Check(reachable > 3000,
                  "...and what is left is one connected space thousands of tiles across");
        }
    }

    // -------------------------------------------------------------------
    // 7. The heading conversion round-trips.
    // -------------------------------------------------------------------
    std::printf("\n--- 7. heading conversion ---\n");
    {
        bool ok = true;
        for (int i = 0; i < 16; ++i) {
            const float portYaw = static_cast<float>(i) * 0.3f;
            const int engine = sk_bindings::EngineYawFromPortYaw(portYaw);
            const float back = sk_bindings::PortYawFromEngineYaw(engine);
            const float dx = std::cos(back) - std::cos(portYaw);
            const float dy = std::sin(back) - std::sin(portYaw);
            if (std::fabs(dx) > 0.001f || std::fabs(dy) > 0.001f) ok = false;
        }
        Check(ok, "PortYawFromEngineYaw inverts EngineYawFromPortYaw, so a drawn arrow points "
                  "where it flies");
    }

    std::printf("\nm49_arrow_projectile_smoke: %s\n",
                g_failures ? "FAILED" : "PASSED (all checks)");
    return g_failures ? 1 : 0;
}
