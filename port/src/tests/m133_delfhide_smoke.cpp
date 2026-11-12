// M133 smoke test: four things a playtest of Delfran's Hideout ran into.
//
// Part 1  `Item = Level.CreateEntity(n); GetPlayer().PickupItem(Item);` --
//         the corpus's give-an-item idiom -- adds the item. It used to
//         record a pointer for the world-pickup path and drop the object:
//         `raiders/loot6.s` (the chest that says it holds an Iron Shield and
//         a Dragonstar Watch Helm) gave nothing, and neither did the chef.
// Part 2  a door script's own `OpenDoor` runs. DoorExecutable answered the
//         name natively with a no-op ahead of the script, so the cell key's
//         Use_CellKey and every lockpick's LockPicked opened nothing.
// Part 3  the door's OnUse opening a screen is detectable by the host
//         (MenuStack::openSerial), including a reopen of the same screen.
// Part 4  popup ClearMenu (`FUN_10087a60` case 2): multiplayermenu.s's
//         askPopup is cleared and refilled, not stacked.
#include <cstdio>
#include <memory>
#include <string>

#include "assets/string_table.h"
#include "simkin_bindings/door_executable.h"
#include "simkin_bindings/item_executable.h"
#include "simkin_bindings/level_executable.h"
#include "simkin_bindings/menu_executable.h"
#include "simkin_bindings/menu_stack.h"
#include "simkin_bindings/player_executable.h"
#include "simkin_bindings/popup_menu_executable.h"
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
    std::printf("  %-92s %s\n", what.c_str(), ok ? "ok" : "FAILED");
    if (!ok) ++g_failures;
}

bool HasTemplate(const sk_b::PlayerExecutable& player, int templateId) {
    for (const auto& item : player.inventory()) {
        if (item && item->templateId() == templateId) return true;
    }
    return false;
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
        std::printf("m133_delfhide_smoke: FAILED to load stringtable.eng\n");
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
    auto loadDoor = [&](const char* rel) {
        std::unique_ptr<sk_b::DoorExecutable> door;
        try {
            skExecutableContext ctxt(&interpreter);
            const std::string path = root + "/" + rel;
            door = std::make_unique<sk_b::DoorExecutable>(skString(path.c_str()), ctxt, player);
            call(door.get(), "Init", one(skRValue(0)));
        } catch (skParseException& e) {
            std::printf("   (PARSE ERROR: %s)\n", e.toString().ptr());
        }
        return door;
    };

    // ---------------------------------------------------------------
    std::printf("\n== Part 1: CreateEntity + PickupItem gives the item ==\n");
    // ---------------------------------------------------------------
    {
        // The chest itself is the opener: Take writes `GetOpener().Loot6`.
        std::unique_ptr<sk_b::DoorExecutable> chest = loadDoor("raiders/chest06.s");
        Check(chest != nullptr, "raiders/chest06.s loads (Delfran's Hideout places it)");
        stack.OpenMenu("raiders\\Loot6", chest.get());
        sk_b::MenuExecutable* loot = stack.currentMenu();
        Check(loot != nullptr && loot->rows().size() >= 3 &&
                  loot->rows()[0].textId == 0x89d,
              "raiders\\Loot6 opens: \"The chest contains an Iron Shield and a ...Helm.\"");
        if (loot) call(loot, "Take", skRValueArray());
        Check(HasTemplate(player, 646) && HasTemplate(player, 630),
              "Take: the Iron Shield (646) and the Dragonstar Watch Helm (630) are carried");
        skRValue flag;
        if (chest) chest->getValue(skString("Loot6"), skString(), flag);
        Check(flag.intValue() == 1, "and the handler ran on to GetOpener().Loot6 = 1");
        Check(stack.level().TakePendingCreatedEntity() == nullptr,
              "nothing is left parked in Level's pending slot");

        stack.level().setValue(skString("saved_Given"), skString(), skRValue(0));
        stack.OpenMenu("delfhide\\Chef_convo");
        if (sk_b::MenuExecutable* chef = stack.currentMenu()) {
            call(chef, "GiveKey", skRValueArray());
        }
        Check(player.FindInventoryById("cellkey") != nullptr,
              "the chef's GiveKey puts the cell key in the inventory");
    }

    // ---------------------------------------------------------------
    std::printf("\n== Part 2: a door script's own OpenDoor runs ==\n");
    // ---------------------------------------------------------------
    {
        std::unique_ptr<sk_b::DoorExecutable> cell = loadDoor("delfhide/Cell_lockeddoor.s");
        Check(cell != nullptr && !cell->passable(), "delfhide/Cell_lockeddoor.s loads, shut");
        if (cell) {
            const unsigned serial = stack.openSerial();
            cell->InvokeOnUse();
            sk_b::MenuExecutable* use = stack.currentMenu();
            Check(stack.openSerial() != serial && use != nullptr && use->opener() == cell.get(),
                  "Use with the key opens delfhide\\Use_Cellkey, the door as opener");
            if (use) call(use, "Use_CellKey", skRValueArray());
            skRValue open;
            cell->getValue(skString("saved_Open"), skString(), open);
            Check(cell->passable() && open.intValue() == 1 && !cell->usable(),
                  "Use_CellKey -> GetOpener().OpenDoor(\" \"): passable, saved_Open = 1, unusable");

            // Part 3 rides along: the same screen opened again is still news.
            const unsigned again = stack.openSerial();
            stack.ReopenMenu("delfhide\\Use_Cellkey", cell.get());
            Check(stack.openSerial() == again + 1,
                  "reopening the screen that is already current still bumps openSerial");
        }

        std::unique_ptr<sk_b::DoorExecutable> picked = loadDoor("lockeddoor.s");
        if (picked) {
            call(picked.get(), "LockPicked", skRValueArray());
            Check(picked->passable(), "lockeddoor.s: a successful pick's LockPicked opens the door");
        }
        // A door with no OpenDoor of its own still accepts the call quietly
        // (crypt1.s's trigger callback, M38).
        std::unique_ptr<sk_b::DoorExecutable> plain = loadDoor("door.s");
        if (plain) {
            skRValueArray args;
            args.append(skRValue(skString("Open Door")));
            bool threw = false;
            try {
                skRValue ret;
                skExecutableContext ctxt(&interpreter);
                plain->method(skString("OpenDoor"), args, ret, ctxt);
            } catch (skRuntimeException&) {
                threw = true;
            }
            Check(!threw, "door.s (no OpenDoor) still takes OpenDoor as a no-op");
        }
    }

    // ---------------------------------------------------------------
    std::printf("\n== Part 4: popup ClearMenu ==\n");
    // ---------------------------------------------------------------
    {
        stack.OpenMenu("Menus\\MultiPlayerMenu");
        sk_b::MenuExecutable* mp = stack.currentMenu();
        Check(mp != nullptr, "Menus\\MultiPlayerMenu opens");
        if (mp) {
            call(mp, "MenuAskHost", skRValueArray());
            sk_b::PopupMenuExecutable* ask = mp->activePopup();
            Check(ask != nullptr, "MenuAskHost shows askPopup");
            if (ask) {
                bool stale = false;
                for (const auto& item : ask->items()) stale = stale || item.textId == 4022;
                Check(!ask->items().empty() && ask->items()[0].textId == 4023 && !stale,
                      "it reads \"Use character from which game?\" with no stale \"saved game?\"");
                const size_t first = ask->items().size();
                call(mp, "MenuAskHost", skRValueArray());
                Check(ask->items().size() == first, "asking twice does not stack the rows");
                call(ask, "ClearMenu", skRValueArray());
                Check(ask->items().empty(), "ClearMenu empties the popup");
            }
        }
    }

    std::printf("\nm133_delfhide_smoke: %d/%d checks passed\n", g_checks - g_failures, g_checks);
    return g_failures == 0 ? 0 : 1;
}
