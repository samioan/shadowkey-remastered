// M38 smoke test: the physical-trap half of AddTrigger, run against the
// two real zone-root scripts that use it.
//
// The finding this pins down is that there is no trigger-*volume* data
// anywhere. Decompiling FUN_10090818 (the trigger's fire routine) and
// FUN_1007307c (the zone's trigger-list walk in front of it) shows a
// trigger is notified about an entity, with a mode saying what happened,
// and identifies that entity either through its own AddEntity() list
// (integer entities.txt typeIds or entity names) or by comparing the
// trigger's own name to the entity's. Both shapes are exercised here:
//
//   lothcav.s   -- three traps matched by typeId:
//                    spikeTrap.AddEntity(1013); SetTrap(8, 20, 10);
//                    ceilTrap.AddEntity(1011);  SetTrap(4, 32, 10);
//                    steamTrap.AddEntity(1012); SetTrap(20, 35, 10);
//                  (entities.txt: 1013 lothna\spiketrap.s, 1011
//                   lothna\ceilingmasher.s, 1012 lothna\steamtrap.s --
//                   all category 10, the trap category)
//   crypt1.s    -- five traps matched by *name* only, no AddEntity at all:
//                    doorTrigger = AddTrigger("door1");
//                    doorTrigger.SetTrap(3, 23, 25);
//                    doorTrigger.SetCallback("OnOpenDoor");
//                  and crypt1.stn independently binds those same five
//                  names (door1..door5) to resistDisarm[28] -- corroboration
//                  from a different file that they are real, individually
//                  trapped doors.
//
// The three notification modes come from FUN_1007307c's three real call
// sites: 0 entity killed, 1 door opened, 2 trap proximity.
#include <cstdio>
#include <memory>
#include <string>

#include "assets/string_table.h"
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

namespace {

int g_failures = 0;

void Check(bool ok, const std::string& what) {
    std::printf("%-74s %s\n", what.c_str(), ok ? "OK" : "FAILED");
    if (!ok) ++g_failures;
}

}  // namespace

int main(int argc, char** argv) {
    using Trigger = sk_bindings::TriggerExecutable;

    const char* scriptRoot =
        argc > 1 ? argv[1]
                  : "The-Elder-Scrolls-Travels-Shadowkey_N-Gage_EN-FR-DE-ES-IT_USA-Europe-"
                    "EnFrDeEsIt-26102004/system/apps/6r51";

    sk::StringTable strings;
    strings.Load(std::string(scriptRoot) + "/stringtable.eng");
    sk::EntityTypeTable entityTypes;
    if (!entityTypes.Load(scriptRoot)) {
        std::printf("m38_trap_trigger_smoke: FAILED to load entities.txt\n");
        return 1;
    }

    std::printf("=== M38: the physical-trap half of AddTrigger ===\n\n");

    // The three real trap typeIds must genuinely be the trap category, or
    // the whole reading is wrong.
    {
        const sk::EntityTypeDescriptor* spike = entityTypes.Lookup(1013);
        const sk::EntityTypeDescriptor* masher = entityTypes.Lookup(1011);
        const sk::EntityTypeDescriptor* steam = entityTypes.Lookup(1012);
        Check(spike && masher && steam && spike->category == 10 && masher->category == 10 &&
                  steam->category == 10,
              "lothcav.s's three AddEntity typeIds are all real category-10 (trap) entities");
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

    // --- lothcav.s: matched by typeId ---
    {
        skInterpreter interpreter;
        sk_bindings::MenuStack stack(scriptRoot, interpreter, &strings);
        auto zs = loadZoneScript(interpreter, stack, "lothcav.s");
        Check(zs != nullptr, "real lothcav.s loads and its Init() runs to completion");
        if (zs) {
            Check(zs->AnyTrapWatches(1013, "") && zs->AnyTrapWatches(1011, "") &&
                      zs->AnyTrapWatches(1012, ""),
                  "...registering trap triggers for all three of its AddEntity typeIds");
            Check(!zs->AnyTrapWatches(272, "") && !zs->AnyTrapWatches(1015, ""),
                  "...and not for its two kill-count triggers' entity ids (272, 1015)");

            // The spike trap: SetTrap(8, 20, 10) -> 8..20 damage. No
            // mitigation of any kind in the real branch, so the whole roll
            // lands (see TriggerExecutable::FireResult on the two saving
            // throws that are not reproduced).
            int lo = 1000, hi = -1;
            for (int i = 0; i < 60; ++i) {
                int before = stack.player().health();
                bool fired = zs->Notify(Trigger::kNotifyTrapProximity, 1013, "");
                int dealt = before - stack.player().health();
                if (!fired) {
                    Check(false, "the spike trap fires on a mode-2 proximity notification");
                    break;
                }
                if (dealt < lo) lo = dealt;
                if (dealt > hi) hi = dealt;
                stack.player().SetHealth(stack.player().maxHealth());
            }
            Check(lo >= 8 && hi <= 20 && hi > lo,
                  "spikeTrap.SetTrap(8, 20, 10) deals a real 8..20 roll, unmitigated");

            // RemainActive() is called on all three, so the trap must not
            // arm-and-forget after one hit.
            int before = stack.player().health();
            zs->Notify(Trigger::kNotifyTrapProximity, 1011, "");
            bool firstHit = stack.player().health() < before;
            before = stack.player().health();
            zs->Notify(Trigger::kNotifyTrapProximity, 1011, "");
            Check(firstHit && stack.player().health() < before,
                  "a RemainActive() trap keeps firing instead of disarming after one hit");

            // Mode 0 is the kill-count variant's mode; a trap must ignore
            // it (the real guard is an unsigned `mode - 1 > 1`).
            before = stack.player().health();
            zs->Notify(Trigger::kNotifyEntityKilled, 1013, "");
            Check(stack.player().health() == before,
                  "...but ignores mode 0 entirely -- the real guard excludes it");
        }
    }

    // --- crypt1.s: matched by the trigger's own name ---
    {
        skInterpreter interpreter;
        sk_bindings::MenuStack stack(scriptRoot, interpreter, &strings);
        auto zs = loadZoneScript(interpreter, stack, "crypt1.s");
        Check(zs != nullptr, "real crypt1.s loads and its Init() runs to completion");
        if (zs) {
            Check(zs->AnyTrapWatches(-1, "door1") && zs->AnyTrapWatches(-1, "door5"),
                  "crypt1.s's name-only triggers watch the placements named door1..door5");
            Check(!zs->AnyTrapWatches(-1, "door9") && !zs->AnyTrapWatches(-1, ""),
                  "...and nothing else -- an unnamed or unknown placement matches no trigger");

            // door3/door4 are SetTrap(0, 0, 25) -- a trapped-door trigger
            // that deals no damage at all. It still has to fire, because
            // its SetCallback("OnOpenDoor") is the point.
            int before = stack.player().health();
            bool fired = zs->Notify(Trigger::kNotifyDoorOpened, -1, "door3");
            Check(fired && stack.player().health() == before,
                  "door3's SetTrap(0, 0, 25) fires its callback while dealing zero damage");

            // door1 is SetTrap(3, 23, 25).
            int lo = 1000, hi = -1;
            for (int i = 0; i < 60; ++i) {
                int hp = stack.player().health();
                zs->Notify(Trigger::kNotifyDoorOpened, -1, "door1");
                int dealt = hp - stack.player().health();
                if (dealt < lo) lo = dealt;
                if (dealt > hi) hi = dealt;
                stack.player().SetHealth(stack.player().maxHealth());
            }
            Check(lo >= 3 && hi <= 23 && hi > lo,
                  "door1's SetTrap(3, 23, 25) bites for 3..23 when the door is opened");

            // The trap branch takes mode 1 as well as mode 2 -- that is
            // what makes a trapped door a trapped door.
            int hp = stack.player().health();
            zs->Notify(Trigger::kNotifyTrapProximity, -1, "door1");
            Check(stack.player().health() < hp,
                  "...and the same trigger also answers a mode-2 proximity notification");
        }
    }

    // --- The Encounter spawner object, on real ghstpass.s data ---
    //
    // Also the regression this milestone had to fix first: lothcav.s
    // *declares* `fight41` and `doorTrigger` at the top of the class, and
    // the vendored Simkin stores a native object assigned to a declared
    // field as its string form -- so `fight41.AddRandomSets(272, 1)` threw
    // and aborted lothcav's whole Init() before any of its three trap
    // triggers were registered. ghstpass.s does the same thing without
    // declaring the fields, which is why the bug was invisible until now.
    {
        skInterpreter interpreter;
        sk_bindings::MenuStack stack(scriptRoot, interpreter, &strings);
        auto zs = loadZoneScript(interpreter, stack, "ghstpass.s");
        Check(zs != nullptr, "real ghstpass.s loads and its Init() runs to completion");

        // Reaching into the object the script itself got back: read it
        // through the same field the script wrote, which is exactly what
        // the declared-field bug broke.
        skRValue held;
        bool got = zs && zs->getValue(skString("fight12"), skString(""), held);
        auto* enc = got ? dynamic_cast<sk_bindings::EncounterExecutable*>(held.obj()) : nullptr;
        Check(enc != nullptr,
              "a native object assigned to a script field survives as an object, not a string");
        if (enc) {
            Check(enc->regions().size() == 3 && enc->regions()[0] == "fight12" &&
                      enc->regions()[2] == "fight12S",
                  "AddEncounters(\"fight12\",\"fight12W\",\"fight12S\") records three regions");
            // Five separate AddRandomSets calls, one of which passes two
            // (typeId, count) pairs.
            Check(enc->sets().size() == 5, "...and its five AddRandomSets calls are five sets");
            bool pairsOk = enc->sets()[0].size() == 1 && enc->sets()[0][0].typeId == 205 &&
                            enc->sets()[0][0].count == 2 && enc->sets()[2].size() == 2 &&
                            enc->sets()[2][0].typeId == 200 && enc->sets()[2][1].typeId == 205;
            Check(pairsOk,
                  "...each read as (typeId, count) pairs, so AddRandomSets(200,1,205,1) is one set "
                  "of two");
        }
    }

    std::printf("\nm38_trap_trigger_smoke: %s\n", g_failures ? "FAILED" : "PASSED (all checks)");
    return g_failures ? 1 : 0;
}
