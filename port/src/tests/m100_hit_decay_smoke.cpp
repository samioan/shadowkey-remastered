// M100 smoke test: OnHit, OnDecay, the dead flag, and the path table.
//
//   FUN_10081844  the creature damage slot: OnHit(damage) for the player's
//                 hits, before the damage lands
//   FUN_10083c04  the death routine: `if (+0x1e4) return;`, then
//                 vtable[0x178](monster, 1, 5)
//   FUN_10005d60  vtable[0x178]: the dead flag, and a decay armed as
//                 time(NULL) + seconds
//   FUN_1006410c  the entity tick: a due decay leaves the world, then OnDecay
//   FUN_10003810  Actor case 0x20, SetDead(dead [, seconds])
//   case 5 of the Monster dispatcher, FindPathNode, over `<zone>.pth`
//
// What this checks:
//
//   1. The path table: every shipped .pth parses to the byte, the three
//      crypts' UmbraKeth paths, and the nearest-waypoint search.
//   2. OnHit: who triggers it, when, with what, and the crystal exemption;
//      chef.s's real handler.
//   3. The owed death, the decay clock, OnDecay; devron.s's quest.
//   4. Umbra Keth end to end through its real script: the fake death at
//      under 375, a fatal hit that cannot kill it, the return through
//      FindPathNode, the real death, and saved_EndGame.
#include <cstdio>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "assets/string_table.h"
#include "simkin_bindings/level_executable.h"
#include "simkin_bindings/menu_stack.h"
#include "simkin_bindings/monster_executable.h"
#include "simkin_bindings/path_table.h"
#include "simkin_bindings/player_executable.h"
#include "simkin_bindings/stats_damage.h"
#include "simkin_bindings/effects.h"
#include "skExecutableContext.h"
#include "skInterpreter.h"
#include "skParseException.h"
#include "skRValue.h"
#include "skRValueArray.h"
#include "skRuntimeException.h"
#include "world/entity_types.h"

namespace sk_b = sk_bindings;
using Monster = sk_b::MonsterExecutable;

namespace {

int g_failures = 0;
int g_checks = 0;

void Check(bool ok, const std::string& what) {
    ++g_checks;
    std::printf("  %-92s %s\n", what.c_str(), ok ? "ok" : "FAILED");
    if (!ok) ++g_failures;
}

std::vector<uint8_t> ReadBytes(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)),
                                std::istreambuf_iterator<char>());
}

}  // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    const std::string root =
        argc > 1 ? argv[1]
                 : "The-Elder-Scrolls-Travels-Shadowkey_N-Gage_EN-FR-DE-ES-IT_USA-Europe-"
                   "EnFrDeEsIt-26102004/system/apps/6r51";

    sk::StringTable strings;
    if (!strings.Load(root + "/stringtable.eng")) {
        std::printf("m100_hit_decay_smoke: FAILED to load stringtable.eng\n");
        return 1;
    }
    sk::EntityTypeTable entityTypes;
    entityTypes.Load(root);
    skInterpreter interpreter;
    sk_b::MenuStack stack(root, interpreter, &strings);
    stack.level().SetEntityTypes(&entityTypes);
    sk_b::PlayerExecutable& player = stack.player();

    auto call = [&](skiExecutable* obj, const char* name, skRValueArray args) {
        skRValue ret;
        skExecutableContext ctxt(&interpreter);
        try {
            obj->method(skString(name), args, ret, ctxt);
        } catch (skRuntimeException& e) {
            std::printf("   (RUNTIME ERROR in %s: %s)\n", name, e.toString().ptr());
        }
        return ret;
    };
    auto one = [](skRValue v) {
        skRValueArray a;
        a.append(v);
        return a;
    };
    auto loadMonsterAt = [&](const std::string& path) -> std::unique_ptr<Monster> {
        skExecutableContext loadCtxt(&interpreter);
        try {
            auto m = std::make_unique<Monster>(skString(path.c_str()), loadCtxt, &strings, player,
                                               stack);
            call(m.get(), "Init", one(skRValue(0)));
            return m;
        } catch (skParseException& e) {
            std::printf("   (PARSE ERROR loading %s: %s)\n", path.c_str(), e.toString().ptr());
        } catch (skRuntimeException& e) {
            std::printf("   (RUNTIME ERROR loading %s: %s)\n", path.c_str(), e.toString().ptr());
        }
        return nullptr;
    };
    auto loadMonster = [&](const std::string& rel) { return loadMonsterAt(root + "/" + rel); };
    auto gold = [&]() { return call(&player, "GetGold", skRValueArray()).intValue(); };
    auto playerField = [&](const char* name) {
        skRValue v;
        player.getValue(skString(name), skString(), v);
        return v.intValue();
    };
    const int strengthTerm = sk_b::StrengthDamageTerm(
        player.EffectStatValue(sk_b::kEffectStatStrengthProper),
        player.EffectStatValue(sk_b::kEffectStatStrength));

    // A probe creature whose handlers report through the player's gold.
    const std::string probePath = "m100_probe_monster.s";
    {
        std::ofstream probe(probePath, std::ios::binary);
        probe << "{\n"
                 "\tInit[ (s) { SetMaxHealth(500); } ]\n"
                 "\tOnHit[ (damage) { GetPlayer().StatModGold(damage); } ]\n"
                 "\tOnDecay[ () { GetPlayer().StatModGold(1000); } ]\n"
                 "}\n";
    }

    // ---------------------------------------------------------------
    // Part 1: .pth
    // ---------------------------------------------------------------
    std::printf("\n== Part 1: the path table ==\n");
    {
        const char* const kZones[] = {"azra",     "broken1",  "broken2", "crypt1",   "crypt2",
                                      "crypt3",   "delfhide", "drgnfld", "dstar_e",  "dstar_w",
                                      "erthcave", "fearfrst", "ffarena", "ghstpass", "glaciercrawl",
                                      "lakvan",   "lothcav",  "raiders", "snowline", "stouttp",
                                      "twilite"};
        int exact = 0;
        int named = 0;
        for (const char* zone : kZones) {
            const std::vector<uint8_t> bytes = ReadBytes(root + "/" + zone + ".pth");
            const std::vector<sk_b::PathNode> paths = sk_b::ParsePathTable(bytes);
            size_t size = 2;
            for (const sk_b::PathNode& p : paths) size += 0x44 + 8 * p.waypoints.size();
            if (!bytes.empty() && size == bytes.size()) ++exact;
            for (const sk_b::PathNode& p : paths) {
                if (!p.waypoints.empty()) ++named;
            }
        }
        Check(exact == 21, "all 21 shipped .pth files parse to the byte (" +
                               std::to_string(exact) + ")");
        Check(named == 3, "and exactly three paths have waypoints");
        const auto c1 = sk_b::LoadPathTable(root + "/crypt1.pth");
        const auto c2 = sk_b::LoadPathTable(root + "/crypt2.pth");
        const auto c3 = sk_b::LoadPathTable(root + "/crypt3.pth");
        const sk_b::PathNode* u1 = sk_b::FindPath(c1, "UmbraKeth");
        const sk_b::PathNode* u2 = sk_b::FindPath(c2, "UmbraKeth");
        const sk_b::PathNode* u3 = sk_b::FindPath(c3, "UmbraKeth");
        Check(u1 && u2 && u3 && u1->waypoints.size() == 32 && u2->waypoints.size() == 31 &&
                  u3->waypoints.size() == 23 && u1->waypoints[0].x == 4787 &&
                  u1->waypoints[0].y == 29901,
              "UmbraKeth in crypt1/2/3: 32/31/23 waypoints, crypt1's first at (4787, 29901)");
        Check(!sk_b::FindPath(c1, "umbraketh") && !sk_b::FindPath(c1, "Umbra"),
              "the lookup is strcmp: case and length both matter");

        sk_b::PathNode synthetic;
        synthetic.waypoints = {{0, 0}, {1000, 0}, {1000, 0}, {-400, 0}};
        const sk_b::PathWaypoint* near = sk_b::NearestWaypoint(synthetic, 900, 10);
        Check(near == &synthetic.waypoints[1],
              "nearest to (900, 10) is (1000, 0) -- and the first of two equal ones");
        Check(sk_b::NearestWaypoint(sk_b::PathNode{}, 0, 0) == nullptr,
              "a path with no waypoints answers nothing");
    }

    // ---------------------------------------------------------------
    // Part 2: OnHit
    // ---------------------------------------------------------------
    std::printf("\n== Part 2: OnHit ==\n");
    {
        std::unique_ptr<Monster> probe = loadMonsterAt(probePath);
        std::unique_ptr<Monster> archer = loadMonster("monsters/archer.s");
        Check(probe && archer && probe->currentHealth() == 500, "the probe loads with 500 health");
        if (probe && archer) {
            int before = gold();
            int health = probe->currentHealth();
            probe->ApplyDamage(40, &player, false);
            Check(gold() - before == 40 && health - probe->currentHealth() == 40 + strengthTerm,
                  "a player hit of 40: OnHit(40) -- the site's figure, before the Strength term");
            before = gold();
            probe->ApplyDamage(40, archer.get(), true);
            probe->ApplyDamage(40, nullptr, false);
            Check(gold() == before, "a creature's hit and an unsourced one send no OnHit");
            probe->SetEntityTypeId(0x3f7);
            probe->ApplyDamage(10, &player, false);
            Check(gold() == before, "type 0x3f7 (the fortifying crystal) is never sent OnHit");
            probe->SetEntityTypeId(0);
            call(probe.get(), "SetInvulnerable", one(skRValue(true)));
            probe->ApplyDamage(10, &player, false);
            Check(gold() == before, "an invulnerable creature is refused before OnHit");
        }

        std::unique_ptr<Monster> chef = loadMonster("monsters/chef.s");
        if (chef) {
            const std::string lootBefore = chef->lootTag();
            chef->ApplyDamage(1, &player, false);
            Check(chef->lootTag() == "delfhide\\\\Loot_DropedKey" && !chef->usable(),
                  "chef.s's real OnHit: the player hit him, so he carries the cell key and won't talk");
            std::printf("     loot tag before %s, after %s\n", lootBefore.c_str(),
                        chef->lootTag().c_str());
        }
    }

    // ---------------------------------------------------------------
    // Part 3: the owed death and the decay
    // ---------------------------------------------------------------
    std::printf("\n== Part 3: death owed, SetDead, the decay and OnDecay ==\n");
    {
        long long now = 100000;
        std::unique_ptr<Monster> probe = loadMonsterAt(probePath);
        if (probe) {
            probe->SetWallClock([&] { return now; });
            probe->ApplyDamage(10000, &player, false);
            Check(!probe->alive() && probe->deathOwed(), "a fatal hit owes the death routine");
            Check(probe->TakeDeathOwed() && !probe->TakeDeathOwed(), "...once");
            probe->SetDead(true, sk_b::kCorpseDecaySeconds);  // what handleDeath does
            Check(probe->decayArmed() && probe->decayDeadline() == now + 5,
                  "the routine's (1, 5): a decay due at time(NULL) + 5");
            const int before = gold();
            now += 4;
            Check(!probe->TickDecay() && !probe->destroyed(), "four seconds on, still a corpse");
            now += 1;
            Check(probe->TickDecay() && probe->destroyed() && probe->outOfWorld() &&
                      gold() - before == 1000 && !probe->decayArmed(),
                  "at five: out of the world, and OnDecay has run");
            Check(!probe->TickDecay(), "and it runs once");
        }

        std::unique_ptr<Monster> scripted = loadMonsterAt(probePath);
        if (scripted) {
            scripted->SetWallClock([&] { return now; });
            call(scripted.get(), "SetDead", one(skRValue(true)));
            Check(!scripted->alive() && !scripted->deathOwed() && !scripted->decayArmed(),
                  "SetDead(true): dead, but nothing owed and no decay without seconds");
            Check(scripted->ApplyDamage(50, &player, false) == 0 && scripted->currentHealth() == 500,
                  "a dead creature takes no damage (FUN_10081844's `+0x1e4` gate)");
            skRValueArray withSeconds;
            withSeconds.append(skRValue(true));
            withSeconds.append(skRValue(30));
            call(scripted.get(), "SetDead", withSeconds);
            Check(scripted->decayArmed() && scripted->decayDeadline() == now + 30,
                  "SetDead(true, 30) arms a thirty-second decay");
            call(scripted.get(), "SetDead", one(skRValue(false)));
            Check(scripted->alive() && !scripted->decayArmed(),
                  "SetDead(false) revives and cancels the decay");
            Check(call(scripted.get(), "GetHealth", skRValueArray()).intValue() == 500,
                  "GetHealth() on a creature reads its health (was a soft-fail answering 0)");
        }

        std::unique_ptr<Monster> devron = loadMonster("monsters/devron.s");
        if (devron) {
            devron->SetWallClock([&] { return now; });
            devron->ApplyDamage(10000, &player, false);
            devron->TakeDeathOwed();
            devron->SetDead(true, sk_b::kCorpseDecaySeconds);
            now += 5;
            devron->TickDecay();
            Check(call(&player, "QuestSolved", one(skRValue(33))).boolValue(),
                  "devron.s's OnDecay solves quest 33 -- which nothing did before");
        }
    }

    // ---------------------------------------------------------------
    // Part 4: Umbra Keth
    // ---------------------------------------------------------------
    std::printf("\n== Part 4: Umbra Keth, through monsters/umbra_keth.s ==\n");
    {
        long long now = 500000;
        stack.level().SetPaths(sk_b::LoadPathTable(root + "/crypt2.pth"));
        player.SetWorldPosition(3300, 26800, 0);
        std::unique_ptr<Monster> umbra = loadMonster("monsters/umbra_keth.s");
        Check(umbra && umbra->currentHealth() == 600, "umbra_keth.s loads: 600 health");
        if (umbra) {
            umbra->SetWallClock([&] { return now; });
            umbra->SetEntityTypeId(274);
            umbra->ApplyDamage(100, &player, false);
            Check(umbra->alive() && !umbra->entityHidden(),
                  "a hit at 600: OnHit's `GetHealth() < 375` is false, it fights on");
            call(umbra.get(), "SetHealth", one(skRValue(300)));
            umbra->ApplyDamage(1000, &player, false);
            Check(!umbra->alive() && umbra->entityHidden() && umbra->entityPassable() &&
                      !umbra->deathOwed() && umbra->killer() == nullptr &&
                      umbra->currentHealth() == 0,
                  "at 300: it fakes its death in OnHit, and the 1000 that follows cannot kill it");
            Check(umbra->delay().armed(), "...and arms its return (Delay(Random(8, 12), 1))");

            // DelayReached(1): the return, through FindPathNode.
            call(umbra.get(), "DelayReached", one(skRValue(1)));
            float x = 0, y = 0, z = 0;
            const bool moved = umbra->TakePendingPosition(x, y, z);
            const int lift = umbra->TakePendingSurfaceLift();
            Check(umbra->alive() && !umbra->entityHidden() && umbra->currentHealth() == 225 &&
                      umbra->maxHealth() == 225,
                  "DelayReached(1): FindPathNode(\"UmbraKeth\") is true, so it returns at 225");
            Check(moved && static_cast<int>(x) == 3276 && static_cast<int>(y) == 26855 && lift == 300,
                  "onto crypt2's waypoint nearest the player, (3276, 26855), 300 above the floor");

            // Still under 375 and `Level.saved_Umbra` still 0: the next hit
            // fakes it again. That loop is the fight's design -- crypt2.s's
            // crystals set saved_Umbra = 1, and only then can it die.
            umbra->ApplyDamage(10, &player, false);
            Check(!umbra->alive() && !umbra->deathOwed(),
                  "at 225 with saved_Umbra 0 the next hit fakes its death again (until the crystals)");
            call(umbra.get(), "DelayReached", one(skRValue(1)));
            stack.level().setValue(skString("saved_Umbra"), skString(), skRValue(1));
            umbra->ApplyDamage(10000, &player, false);
            Check(umbra->deathOwed() && umbra->TakeDeathOwed(),
                  "with crypt2.s's `Level.saved_Umbra = 1` set, the death is real and owed");
            umbra->SetDead(true, sk_b::kCorpseDecaySeconds);
            now += 5;
            umbra->TickDecay();
            Check(playerField("saved_EndGame") == 1,
                  "and its OnDecay sets GetPlayer().saved_EndGame -- the ending's flag");
        }

        stack.level().SetPaths({});
        stack.level().setValue(skString("saved_Umbra"), skString(), skRValue(0));
        std::unique_ptr<Monster> lost = loadMonster("monsters/umbra_keth.s");
        if (lost) {
            call(lost.get(), "SetHealth", one(skRValue(300)));
            lost->ApplyDamage(1, &player, false);
            call(lost.get(), "DelayReached", one(skRValue(1)));
            Check(!lost->alive() && lost->entityHidden(),
                  "with no UmbraKeth path it stays gone and waits again -- why .pth had to be decoded");
        }
    }

    std::remove(probePath.c_str());
    std::printf("\nm100_hit_decay_smoke: %d/%d checks passed\n", g_checks - g_failures, g_checks);
    return g_failures == 0 ? 0 : 1;
}
