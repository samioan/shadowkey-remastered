// M140 -- the playtest bug list: the port-side fixes that can be checked
// without a window.
//
//   1. a Spell's level defaults to 1 (FUN_10047740), not 0;
//   2. a found scroll stays a scroll across a save and a load;
//   3. an open cancels a Quit() made before it -- and only before it;
//   4. the trap trigger's saving throws and the screens they open;
//   5. the trap entity's own tick (FUN_1008ff14) and its three natives.
//
// The hidden-but-usable use target, the level-load music stop, the
// end-of-tick menu hand-off and the boss teleport live in main.cpp's loop
// and are checked in the game itself (docs/PORT_ROADMAP.md, M140).

#include <cstdio>
#include <memory>
#include <string>

#include "assets/string_table.h"
#include "simkin_bindings/item_executable.h"
#include "simkin_bindings/level_executable.h"
#include "simkin_bindings/menu_executable.h"
#include "simkin_bindings/menu_stack.h"
#include "simkin_bindings/player_executable.h"
#include "simkin_bindings/trap_entity.h"
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
    using sk_bindings::TrapEntityState;
    using sk_bindings::TrapTickHooks;
    using Trigger = sk_bindings::TriggerExecutable;

    const char* scriptRoot =
        argc > 1 ? argv[1]
                  : "The-Elder-Scrolls-Travels-Shadowkey_N-Gage_EN-FR-DE-ES-IT_USA-Europe-"
                    "EnFrDeEsIt-26102004/system/apps/6r51";

    sk::StringTable strings;
    strings.Load(std::string(scriptRoot) + "/stringtable.eng");
    sk::EntityTypeTable entityTypes;
    if (!entityTypes.Load(scriptRoot)) {
        std::printf("m140_bug_triage_smoke: FAILED to load entities.txt\n");
        return 1;
    }

    std::printf("=== M140: the playtest bug list ===\n\n");

    // ---- 1 and 2: the spell's level, and the scroll across a save ----
    {
        skInterpreter interpreter;
        sk_bindings::MenuStack stack(scriptRoot, interpreter, &strings);
        stack.level().SetEntityTypes(&entityTypes);

        auto ignite = stack.level().CreateItem(4024, /*requireItemCategory=*/false);
        Check(ignite != nullptr, "Level.CreateEntity(4024) builds spells\\IgniteFoe.s");
        if (ignite) {
            Check(ignite->spellLevel() == 1,
                  "a spell no script gave a level casts at the constructor's 1, not 0");
        }

        auto scroll = stack.level().CreateItem(4022, false);
        auto plain = stack.level().CreateItem(4023, false);
        if (scroll && plain) {
            scroll->SetScrollFlag(true);  // the loot scripts' Scroll.SetScroll(true)
            stack.player().AddItem(std::move(scroll));
            stack.player().AddItem(std::move(plain));
            const sk::SavedEntity rec = stack.player().BuildSaveRecord("crypt1");
            stack.player().ApplySaveRecord(rec, stack);
            int scrolls = 0, plains = 0;
            for (const auto& item : stack.player().inventory()) {
                if (!item) continue;
                if (item->templateId() == 4022 && item->scroll()) ++scrolls;
                if (item->templateId() == 4023 && !item->scroll()) ++plains;
            }
            Check(scrolls == 1, "a found scroll is still a scroll after a save and a load");
            Check(plains == 1, "...and the plain spell beside it is still not one");
        } else {
            Check(false, "templates 4022/4023 build");
        }
    }

    // ---- 3: Quit() and an open ----
    {
        skInterpreter interpreter;
        sk_bindings::MenuStack stack(scriptRoot, interpreter, &strings);

        stack.RequestCloseMenu();
        stack.OpenMenu("Menus\\TrapAvoided");
        Check(stack.currentMenu() && !stack.closeMenuRequested(),
              "Quit() then OpenMenu(x): the open cancels the quit and x stays up");

        stack.RequestCloseMenu();
        Check(stack.closeMenuRequested(),
              "OpenMenu(x) then Quit() (x's own Init, the arena): the quit stands");
        stack.ClearCloseMenuRequest();

        stack.RequestCloseMenu();
        stack.OpenMenu("Menus\\NoSuchScreen");
        Check(stack.closeMenuRequested(),
              "an open that resolves nothing leaves an earlier quit in force");
        stack.ClearCloseMenuRequest();
    }

    // ---- 3b: the name screen, OpenEditText (menu case 0x1f) ----
    {
        skInterpreter interpreter;
        sk_bindings::MenuStack stack(scriptRoot, interpreter, &strings);
        stack.OpenMenu("Menus\\NameChar");
        sk_bindings::MenuExecutable* menu = stack.currentMenu();
        Check(menu && menu->textEntryActive(), "menus/namechar.s opens its edit widget");
        if (menu) {
            using Row = sk_bindings::MenuExecutable::MenuRow;
            const Row* a = nullptr;
            const Row* h = nullptr;
            const Row* del = nullptr;
            const Row* done = nullptr;
            int letters = 0;
            for (const Row& row : menu->rows()) {
                if (row.editKey != 0) ++letters;
                if (row.editKey == 'A') a = &row;
                if (row.editKey == 'H') h = &row;
                if (row.editKey == '-') del = &row;
                if (row.callback == "Done") done = &row;
            }
            Check(letters == 27, "Alpha.txt gives 27 buttons: A..Z and DEL");
            Check(a && a->x == 30 && a->y == 67 && h && h->x == 30 && h->y == 84,
                  "laid out from (30, 50 + 0x11), seven to a row, 17px apart");
            Check(del && del->literalText == "DEL" && del->x == 30 + 5 * 17 && del->y == 67 + 3 * 17,
                  "DEL closes the fourth row, after V..Z");
            Check(done && done->x == 160 - 0x32 && done->y == 150 - 0xf,
                  "and Done sits at (maxX - 0x32, bottom - 0xf)");

            sk_bindings::PlayerExecutable& player = stack.player();
            player.AppendCharNameChar('O');
            player.AppendCharNameChar('L');
            player.AppendCharNameChar('D');
            menu->EditName('N', false);
            Check(player.charNameBuffer() == "N",
                  "the first edit clears the name the screen opened with (+0xa4)");
            for (int c : {'E', 'W', 'N', 'A', 'M', 'E'}) menu->EditName(c, false);
            Check(player.charNameBuffer() == "NEWNAM", "a name stops at six characters");
            menu->EditName('-', false);
            Check(player.charNameBuffer() == "NEWNA", "DEL ('-') takes the last one off");
            menu->EditName('Q', true);
            Check(menu->typedSinceNavigation(),
                  "a typed edit makes Enter mean Done until the focus moves");
        }
    }

    // ---- 4: the trigger's two saving throws and their screens ----
    {
        skInterpreter interpreter;
        skExecutableContext ctxt(&interpreter);
        auto call = [&](Trigger& t, const char* name, std::initializer_list<int> ints) {
            skRValueArray args;
            for (int v : ints) args.append(skRValue(v));
            skRValue ret;
            t.method(skString(name), args, ret, ctxt);
        };

        Trigger trap("spikes");
        {
            skRValueArray args;
            args.append(skRValue(1013));
            skRValue ret;
            trap.method(skString("AddEntity"), args, ret, ctxt);
        }
        call(trap, "SetTrap", {8, 20, 10});
        call(trap, "RemainActive", {});

        Trigger::TrapSaves disarm{[](int id) { return id == 10; }, [](int) { return false; }};
        Trigger::FireResult r = trap.Fire(Trigger::kNotifyTrapProximity, 1013, "", &disarm);
        Check(r.outcome == Trigger::TrapOutcome::Disarmed && !r.fired && r.damage == 0,
              "FUN_1003e438 against SetTrap's third argument disarms: no damage, no fire");

        Trigger::TrapSaves avoid{[](int) { return false; }, [](int) { return true; }};
        r = trap.Fire(Trigger::kNotifyTrapProximity, 1013, "", &avoid);
        Check(r.outcome == Trigger::TrapOutcome::Avoided && !r.fired && r.damage == 0,
              "FUN_10044e58 is the second chance: avoided, no damage, no fire");

        r = trap.Fire(Trigger::kNotifyTrapProximity, 1013, "", nullptr);
        Check(r.outcome == Trigger::TrapOutcome::Damaged && r.fired && r.damage >= 8 &&
                  r.damage <= 20,
              "both failing lands the 8..20 roll, and only that counts as firing");
    }
    {
        // On the real lothcav.s, through the zone: a fresh level-1 player
        // cannot disarm (the level < resist/2 gate) and rarely dodges, and
        // the spike and masher triggers have ShowDamageMessage(true).
        skInterpreter interpreter;
        sk_bindings::MenuStack stack(scriptRoot, interpreter, &strings);
        skExecutableContext loadCtxt(&interpreter);
        std::unique_ptr<sk_bindings::ZoneScriptExecutable> zs;
        try {
            zs = std::make_unique<sk_bindings::ZoneScriptExecutable>(
                skString((std::string(scriptRoot) + "/lothcav.s").c_str()), loadCtxt, stack);
            skRValueArray args;
            args.append(skRValue(0));
            skRValue ret;
            skExecutableContext callCtxt(&interpreter);
            zs->method(skString("Init"), args, ret, callCtxt);
        } catch (skRuntimeException& e) {
            std::printf("RUNTIME ERROR loading lothcav.s: %s\n", e.toString().ptr());
        }
        Check(zs != nullptr, "real lothcav.s loads");
        if (zs) {
            bool sawDamagedScreen = false;
            for (int i = 0; i < 50 && !sawDamagedScreen; ++i) {
                stack.player().SetHealth(stack.player().maxHealth());
                const int hp = stack.player().health();
                const bool fired = zs->Notify(Trigger::kNotifyTrapProximity, 1013, "");
                if (fired && stack.player().health() < hp) {
                    sawDamagedScreen = stack.currentMenu() &&
                                       stack.currentMenu()->scriptName() == "TrapDamaged";
                }
            }
            Check(sawDamagedScreen,
                  "a spike hit opens Menus\\TrapDamaged (spikeTrap.ShowDamageMessage(true))");

            // The steam vents' trigger never got its ShowDamageMessage --
            // lothcav.s calls it on ceilTrap twice instead. A hit, no screen.
            bool steamHitQuietly = false;
            for (int i = 0; i < 50 && !steamHitQuietly; ++i) {
                stack.player().SetHealth(stack.player().maxHealth());
                const int hp = stack.player().health();
                const unsigned serial = stack.openSerial();
                if (zs->Notify(Trigger::kNotifyTrapProximity, 1012, "") &&
                    stack.player().health() < hp) {
                    steamHitQuietly = stack.openSerial() == serial;
                }
            }
            Check(steamHitQuietly, "...but a steam vent's hit opens nothing (the script's slip)");
        }
    }

    // ---- 5: the trap entity ----
    {
        int notifies = 0;
        TrapTickHooks hooks;
        int distance = 0;
        bool fire = true;
        hooks.distanceToPlayer = [&] { return distance; };
        hooks.playerAvoids = [] { return false; };
        hooks.notifyTriggers = [&] {
            ++notifies;
            return fire;
        };
        int sounds = 0;
        hooks.playMasherSound = [&] { ++sounds; };
        constexpr int kDelta = 0x100 / 25;

        // Spikes: type 2, direction +1.
        TrapEntityState spike;
        spike.active = true;
        spike.trapType = 2;
        spike.z = 128;
        int ticks = 0;
        while (!spike.sprung && ticks < 100) {
            sk_bindings::TickTrapEntity(spike, kDelta, hooks);
            ++ticks;
        }
        Check(spike.sprung && spike.restZ == 128 && notifies == 1,
              "in range, a firing trigger springs the trap on the first 0x40 window");
        int peak = spike.z;
        int guard = 0;
        while (spike.sprung && guard++ < 40000) {
            sk_bindings::TickTrapEntity(spike, kDelta, hooks);
            if (spike.z > peak) peak = spike.z;
        }
        Check(!spike.sprung && spike.z == 128 && peak > 128 + 300,
              "the spikes rise a few hundred units and snap back to rest");
        Check(notifies == 1, "...and nothing re-notifies while the trap is sprung");

        // Masher: type 1, direction -1, two steps a window and a sound.
        TrapEntityState masher;
        masher.active = true;
        masher.trapType = 1;
        masher.direction = -1;
        masher.z = 1418;
        notifies = 0;
        guard = 0;
        while (!masher.sprung && guard++ < 100) sk_bindings::TickTrapEntity(masher, kDelta, hooks);
        int low = masher.z;
        guard = 0;
        while (masher.sprung && guard++ < 40000) {
            sk_bindings::TickTrapEntity(masher, kDelta, hooks);
            if (masher.z < low) low = masher.z;
        }
        Check(low < 1418 - 300 && masher.z >= 1418 && sounds > 0,
              "the masher drops, climbs back to its height, and plays 0x3d as it goes");

        // Steam: type 3 never steps, so never un-springs.
        TrapEntityState steam;
        steam.active = true;
        steam.trapType = 3;
        notifies = 0;
        for (int i = 0; i < 20000; ++i) sk_bindings::TickTrapEntity(steam, kDelta, hooks);
        Check(steam.sprung && notifies == 1, "a steam vent fires once and stays sprung");

        // Out of range: 155 scaled-squared is ~199 units.
        TrapEntityState far;
        far.active = true;
        far.trapType = 2;
        distance = 155;
        notifies = 0;
        for (int i = 0; i < 2000; ++i) sk_bindings::TickTrapEntity(far, kDelta, hooks);
        Check(notifies == 0 && far.rearmCountdown == 0,
              "at the range itself nothing is told, and no countdown starts");

        // In range, nothing fires (the player dodged or saved): the
        // countdown restarts all the same.
        distance = 0;
        fire = false;
        notifies = 0;
        TrapEntityState missed;
        missed.active = true;
        missed.trapType = 2;
        for (int i = 0; i < 7; ++i) sk_bindings::TickTrapEntity(missed, kDelta, hooks);
        Check(notifies == 1 && !missed.sprung && missed.rearmCountdown == 7 << 8,
              "a miss re-arms on the 7 << 8 countdown without springing");
        for (int i = 0; i < 700; ++i) sk_bindings::TickTrapEntity(missed, kDelta, hooks);
        Check(notifies == 1, "...which runs one frame's delta per 0x40 window, so slowly");

        // The script natives, on the real scripts.
        skInterpreter interpreter;
        auto runTrap = [&](const char* rel) -> std::unique_ptr<sk_bindings::TrapEntityExecutable> {
            skExecutableContext loadCtxt(&interpreter);
            try {
                auto t = std::make_unique<sk_bindings::TrapEntityExecutable>(
                    skString((std::string(scriptRoot) + "/" + rel).c_str()), loadCtxt);
                skRValueArray args;
                skRValue ret;
                skExecutableContext callCtxt(&interpreter);
                t->method(skString("Init"), args, ret, callCtxt);
                return t;
            } catch (skParseException& e) {
                std::printf("PARSE ERROR %s: %s\n", rel, e.toString().ptr());
            } catch (skRuntimeException& e) {
                std::printf("RUNTIME ERROR %s: %s\n", rel, e.toString().ptr());
            }
            return nullptr;
        };
        auto m = runTrap("lothna/ceilingmasher.s");
        auto s = runTrap("lothna/spiketrap.s");
        auto v = runTrap("lothna/steamtrap.s");
        Check(m && m->state().active && m->state().trapType == 1 && m->state().direction == -1,
              "ceilingmasher.s: SetTrapType(1) turns the direction down, SetActive(true)");
        Check(s && s->state().active && s->state().trapType == 2 && s->state().direction == 1 &&
                  s->state().range == 0x9b,
              "spiketrap.s: type 2, rising, on the constructor's 0x9b range");
        Check(v && v->state().active && v->state().trapType == 3, "steamtrap.s: type 3");
    }

    std::printf("\n%s (%d failure%s)\n", g_failures ? "FAILED" : "PASSED", g_failures,
                g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
