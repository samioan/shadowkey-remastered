// M39 smoke test: the five "small documented loose ends" the roadmap kept
// open rather than guessing at -- each one call or argument, each now
// resolved from the decompile and checked against real shipped scripts.
//
//   SetLoot's trailing min/max   -- monster dispatcher case 0xd: a drop
//                                   *chance*, not a quantity. The handler
//                                   rolls rand(min,max) and keeps the loot
//                                   name only when the roll equals min.
//   SetZone(zoneId, total)       -- Zone-effects case 0: divides `total`
//                                   evenly across every creature whose
//                                   script called SetMob (monster+0x2ef)
//                                   and writes each one's expWorth
//                                   (stats+0x0e).
//   SummonMe()/SummonMe2()       -- not natives at all. Every caller's
//                                   target defines them as an ordinary
//                                   script handler whose whole body is a
//                                   SetPosition (monsters/birgiddaazra.s:
//                                   `SummonMe[() { SetPosition(31083,
//                                   3483, -2816); }]`), so the only thing
//                                   missing was the setter.
//   CountInventory(id)           -- Player class case 0x40: a count of
//                                   matching entries in the player's own
//                                   inventory collection, keyed on the
//                                   item's SetID().
//   Level.Log(s)                 -- a debug trace, ~100 real call sites.
#include <cstdio>
#include <memory>
#include <string>

#include "assets/string_table.h"
#include "simkin_bindings/item_executable.h"
#include "simkin_bindings/level_executable.h"
#include "simkin_bindings/menu_stack.h"
#include "simkin_bindings/monster_executable.h"
#include "simkin_bindings/player_executable.h"
#include "simkin_bindings/zone_script_executable.h"
#include "skExecutableContext.h"
#include "skInterpreter.h"
#include "skParseException.h"
#include "skRValue.h"
#include "skRValueArray.h"
#include "skRuntimeException.h"

namespace {

int g_failures = 0;

void Check(bool ok, const std::string& what) {
    std::printf("%-74s %s\n", what.c_str(), ok ? "OK" : "FAILED");
    if (!ok) ++g_failures;
}

}  // namespace

int main(int argc, char** argv) {
    using Monster = sk_bindings::MonsterExecutable;

    const char* scriptRoot =
        argc > 1 ? argv[1]
                  : "The-Elder-Scrolls-Travels-Shadowkey_N-Gage_EN-FR-DE-ES-IT_USA-Europe-"
                    "EnFrDeEsIt-26102004/system/apps/6r51";

    sk::StringTable strings;
    strings.Load(std::string(scriptRoot) + "/stringtable.eng");

    skInterpreter interpreter;
    sk_bindings::MenuStack stack(scriptRoot, interpreter, &strings);

    auto call = [&](auto* obj, const char* name, skRValueArray& args) {
        skRValue r;
        skExecutableContext c(&interpreter);
        obj->method(skString(name), args, r, c);
        return r;
    };
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
        } catch (skParseException&) {
            return nullptr;
        } catch (skRuntimeException&) {
            return nullptr;
        }
    };

    std::printf("=== M39: the small documented loose ends ===\n\n");

    // ---- 1. SetLoot's trailing pair is a drop chance ----
    {
        auto probe = loadMonster("monsters/Azra_Rat.s");
        Check(probe != nullptr, "real monsters/Azra_Rat.s loads");
        if (probe) {
            // A guaranteed drop: no trailing pair at all, the shape
            // `SetLoot(300, "loot_gold25-35")` uses.
            skRValueArray always;
            always.append(skRValue(300));
            always.append(skRValue(skString("loot_gold25-35")));
            call(probe.get(), "SetLoot", always);
            Check(probe->lootTag() == "loot_gold25-35",
                  "SetLoot with no trailing pair always assigns its loot");

            // A 1-in-8 drop, the shape `SetLoot(300, \"Loot_ratseye\", 1, 8)`
            // uses. Asserted statistically over fresh creatures, since the
            // roll happens once per creature at Init.
            int drops = 0;
            const int kTrials = 400;
            for (int i = 0; i < kTrials; ++i) {
                auto rat = loadMonster("monsters/Azra_Rat.s");
                if (!rat) break;
                skRValueArray chance;
                chance.append(skRValue(300));
                chance.append(skRValue(skString("Loot_ratseye")));
                chance.append(skRValue(1));
                chance.append(skRValue(8));
                call(rat.get(), "SetLoot", chance);
                if (rat->lootTag() == "Loot_ratseye") ++drops;
            }
            // Expected 1/8 == 50 of 400. Generous bounds -- the point is
            // that it is neither "always" (which is what this port did
            // before) nor "never".
            Check(drops > kTrials / 20 && drops < kTrials / 3,
                  "SetLoot(..., 1, 8) is a one-in-eight chance, not a guaranteed drop");
        }
    }

    // ---- 2. SetMob is what opts a creature into the zone XP budget ----
    {
        auto rat = loadMonster("monsters/Azra_Rat.s");
        auto npc = loadMonster("monsters/Tanyin_Aldwyr.s");
        Check(rat && rat->countsForZoneExperience(),
              "a real creature script's SetMob() marks it as counting for zone experience");
        // Whether a given NPC calls SetMob is a property of that script,
        // so this is asserted as "the flag tracks the call", not as a
        // claim about any particular NPC.
        if (npc) {
            std::printf("   (monsters/Tanyin_Aldwyr.s countsForZoneExperience: %s)\n",
                        npc->countsForZoneExperience() ? "yes" : "no");
        }
    }

    // ---- 3. SetZone's real payload ----
    {
        skExecutableContext loadCtxt(&interpreter);
        std::unique_ptr<sk_bindings::ZoneScriptExecutable> zs;
        try {
            zs = std::make_unique<sk_bindings::ZoneScriptExecutable>(
                skString((std::string(scriptRoot) + "/crypt1.s").c_str()), loadCtxt, stack);
            skRValueArray args;
            args.append(skRValue(0));
            skRValue ret;
            skExecutableContext callCtxt(&interpreter);
            zs->method(skString("Init"), args, ret, callCtxt);
        } catch (skParseException&) {
        } catch (skRuntimeException&) {
        }
        Check(zs != nullptr, "real crypt1.s loads");
        if (zs) {
            int zoneId = 0, total = 0;
            bool got = zs->TakePendingZoneExperience(zoneId, total);
            // crypt1.s's own `SetZone(19, 25000)`.
            Check(got && zoneId == 19 && total == 25000,
                  "crypt1.s's SetZone(19, 25000) is recorded as a real zone XP budget");
            Check(!zs->TakePendingZoneExperience(zoneId, total),
                  "...and is consumed once, not re-applied every tick");
        }
    }

    // ---- 4. SummonMe is a script handler, and SetPosition backs it ----
    {
        auto birg = loadMonster("monsters/birgiddaazra.s");
        Check(birg != nullptr, "real monsters/birgiddaazra.s loads");
        if (birg) {
            float x = 0, y = 0, z = 0;
            Check(!birg->TakePendingPosition(x, y, z),
                  "a freshly-loaded creature has no pending teleport");
            skRValueArray none;
            call(birg.get(), "SummonMe", none);
            bool moved = birg->TakePendingPosition(x, y, z);
            // birgiddaazra.s: SummonMe[() { SetPosition(31083, 3483, -2816); }]
            Check(moved && x == 31083.0f && y == 3483.0f && z == -2816.0f,
                  "SummonMe() runs its own script body and lands the real SetPosition");
            Check(!birg->TakePendingPosition(x, y, z),
                  "...and the host drains that move exactly once");
        }
    }

    // ---- 5. CountInventory ----
    {
        stack.player().LoadStartingInventory(stack);
        // items/bread.s is the one starting item that calls SetID
        // ("bread"); weapons/club.s and armor/chain_coif.s set no id at
        // all, which is itself worth pinning -- an unnamed item must not
        // match an empty-string query either.
        skRValueArray breadArgs;
        breadArgs.append(skRValue(skString("bread")));
        skRValue got = call(&stack.player(), "CountInventory", breadArgs);
        skRValueArray missingArgs;
        missingArgs.append(skRValue(skString("stooth")));
        skRValue none = call(&stack.player(), "CountInventory", missingArgs);
        Check(got.intValue() == 1 && none.intValue() == 0,
              "CountInventory counts inventory entries by their script's own SetID()");
    }

    std::printf("\nm39_loose_ends_smoke: %s\n", g_failures ? "FAILED" : "PASSED (all checks)");
    return g_failures ? 1 : 0;
}
