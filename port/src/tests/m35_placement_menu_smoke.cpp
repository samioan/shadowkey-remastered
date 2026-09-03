// M35 smoke test: the two structural fixes behind the reported bugs that
// are not visible from a screenshot -- the .ent record's second string
// (which is where a chest's loot script lives, and therefore why no chest
// in the game could be opened) and the equip screen's real 2D layout
// navigation. Both are checked against real shipped data: a real zone's
// own .ent file, and inventory.s's own OnDisplay() layout.
//
// Also pins the weapon-viewmodel restart guard, which is one line but was
// the whole of "pressing attack repeatedly resets the swing animation".
#include <cstdio>
#include <string>

#include "assets/string_table.h"
#include "simkin_bindings/item_executable.h"
#include "simkin_bindings/level_executable.h"
#include "simkin_bindings/menu_executable.h"
#include "simkin_bindings/menu_stack.h"
#include "simkin_bindings/table_executable.h"
#include "simkin_bindings/weapon_viewmodel.h"
#include "skExecutableContext.h"
#include "skInterpreter.h"
#include "skRValue.h"
#include "skRValueArray.h"
#include "world/entity_types.h"
#include "world/zone.h"

namespace {

int g_failures = 0;

void Check(bool cond, const char* what) {
    std::printf("%-74s %s\n", what, cond ? "OK" : "FAILED");
    if (!cond) ++g_failures;
}

bool FileExists(const std::string& path) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fclose(f);
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    const std::string scriptRoot =
        argc > 1 ? argv[1]
                  : "The-Elder-Scrolls-Travels-Shadowkey_N-Gage_EN-FR-DE-ES-IT_USA-Europe-"
                    "EnFrDeEsIt-26102004/system/apps/6r51";

    // ---- Part 1: the .ent record tail is two strings, not one ----
    //
    // "raiders" is used because it is the zone whose containers most
    // clearly show the bug: reading the tail as one 40-byte name produced
    // "ContaineRaiders\RT_A.s", which is the 8-byte name field ("Containe",
    // a truncated "Container") run straight into the script field.
    sk::EntityTypeTable entityTypes;
    if (!entityTypes.Load(scriptRoot)) {
        std::printf("m35_placement_menu_smoke: FAILED to load entities.txt\n");
        return 1;
    }
    sk::Zone zone;
    if (!zone.Load(scriptRoot, "raiders")) {
        std::printf("m35_placement_menu_smoke: FAILED to load raiders zone\n");
        return 1;
    }

    size_t containers = 0, containersWithScript = 0, longNames = 0;
    std::string sampleScript;
    for (const sk::Zone::EntPlacement& e : zone.entities()) {
        // The name field is 8 bytes, so nothing parsed out of it can be
        // longer than that -- a full field simply has no terminator (which
        // is how "Container" ends up stored as "Containe"). Before the fix
        // these came back 20+ characters long, with the script field run
        // onto the end of the name.
        if (e.name.size() > 8) ++longNames;
        const sk::EntityTypeDescriptor* desc = entityTypes.Lookup(e.typeId);
        if (!desc || desc->category != 8) continue;
        ++containers;
        if (e.scriptPath.empty()) continue;
        std::string rel = e.scriptPath;
        for (char& c : rel) {
            if (c == '\\') c = '/';
        }
        std::string full = scriptRoot + "/" + rel;
        if (full.size() < 2 || full.compare(full.size() - 2, 2, ".s") != 0) full += ".s";
        if (FileExists(full)) {
            ++containersWithScript;
            // Prefer RT_A, the randomised-contents chest script: it is the
            // one that shows the *native* default, having no OnUse of its
            // own. (Some chests, e.g. Raiders\ShadowChest.s, do define one
            // and were already working through the script path.)
            if (sampleScript.empty() || rel.find("RT_A") != std::string::npos) {
                sampleScript = rel;
            }
        }
    }
    std::printf("raiders.ent: %zu container placements, %zu naming a real loot script (e.g. \"%s\")\n",
                containers, containersWithScript, sampleScript.c_str());
    Check(longNames == 0, "no placement name overruns its real 8-byte field");
    Check(containers > 0 && containersWithScript > 0,
          "a real zone's containers carry their own loot script in the .ent record");

    // The container script must be a real, usable one -- SetUsable(true)
    // plus a CreateEntity/AddObject chain, and (the point of the native
    // default) NO OnUse of its own.
    skInterpreter interpreter;
    sk::StringTable strings;
    strings.Load(scriptRoot + "/stringtable.eng");
    sk_bindings::MenuStack stack(scriptRoot.c_str(), interpreter, &strings);
    // Before loading a container script: its Init() rolls its contents via
    // Level.CreateEntity(), which needs the entity table the game-start
    // path installs.
    stack.level().SetEntityTypes(&entityTypes);  // same wiring main.cpp does at startup
    stack.RequestGameStart("azra");

    if (!sampleScript.empty()) {
        const std::string& rel = sampleScript;
        skExecutableContext loadCtxt(&interpreter);
        auto chest = std::make_unique<sk_bindings::ItemExecutable>(
            skString((scriptRoot + "/" + rel).c_str()), loadCtxt, stack);
        skRValueArray args;
        args.append(skRValue(0));
        skRValue ret;
        skExecutableContext callCtxt(&interpreter);
        chest->method(skString("Init"), args, ret, callCtxt);
        bool hadOnUse = chest->InvokeOnUse();
        std::printf("%s: %zu item(s) rolled by its own Init(), OnUse defined: %s\n",
                    sampleScript.c_str(), chest->contents().size(), hadOnUse ? "yes" : "no");
        Check(!chest->contents().empty(),
              "...whose Init() really does roll contents into the container");
        // RT_A.s does define OnUse (it opens the loot menu on itself), so
        // once the placement's script is found at all, the existing bag
        // path takes over -- the .ent parse *is* the whole fix for chests.
        // main.cpp's native default is the fallback for a usable container
        // whose script omits the handler, not what makes these work.
        Check(hadOnUse, "...and whose own OnUse opens the loot menu, once the script is found");

        // ---- M36: what happens once that container is emptied ----
        //
        // lootmenu.s's UpdateMenu() calls LootExit() and returns *before
        // adding any rows* when GetFirst() is null, so the screen's whole
        // content -- including its "Okay" row (string 528) -- is gone by
        // design, and LootExit()'s two natives are what close it. With
        // neither implemented the player was left staring at a blank menu.
        auto call = [&](skiExecutable& obj, const char* name) {
            skRValueArray a;
            skRValue r;
            skExecutableContext c(&interpreter);
            obj.method(skString(name), a, r, c);
            return r;
        };
        // GetDestroy defaults false -- a placed chest stays in the world.
        Check(!chest->destroyWhenEmpty(),
              "a placed chest does not set SetDestroy, so it survives being emptied");
        {
            skRValueArray a;
            a.append(skRValue(true));
            skRValue r;
            skExecutableContext c(&interpreter);
            chest->method(skString("SetDestroy"), a, r, c);
        }
        Check(chest->destroyWhenEmpty() && call(*chest, "GetDestroy").boolValue(),
              "...but SetDestroy(true) (what a monster's dropped loot bag does) reads back");

        stack.ClearCloseMenuRequest();
        stack.OpenMenu("LootMenu", chest.get());
        sk_bindings::MenuExecutable* loot = stack.currentMenu();
        bool haveLoot = loot != nullptr;
        if (haveLoot) {
            stack.ClearCloseMenuRequest();
            call(*loot, "QueryDestroy");
            Check(stack.closeMenuRequested(),
                  "QueryDestroy closes the loot screen (the chest-stays branch)");
            stack.ClearCloseMenuRequest();
            call(*loot, "QuitAndDestroyOpener");
            Check(stack.closeMenuRequested() && chest->markedForRemoval(),
                  "QuitAndDestroyOpener closes it and condemns the container too");
        } else {
            Check(false, "loot menu opened for the destroy-branch checks");
        }
    }

    // ---- Part 2: the equip screen's real layout navigation ----
    stack.OpenMenu("inventory");
    sk_bindings::MenuExecutable* inv = stack.currentMenu();
    if (!inv) {
        std::printf("m35_placement_menu_smoke: FAILED to open inventory.s\n");
        return 1;
    }

    // Same call main.cpp's input loop makes once a tick -- a freshly
    // displayed screen has no selection until something snaps it to the
    // first selectable row.
    inv->EnsureValidSelection();

    // inventory.s lays five category buttons out on one row: x = 3, 37,
    // 71, 105, 139 at y = 25, then the item table at y = 60. Confirm the
    // port actually captured that geometry before relying on it.
    int buttonBandY = -1, buttonsInBand = 0, tableRowIndex = -1;
    for (size_t i = 0; i < inv->rows().size(); ++i) {
        const auto& row = inv->rows()[i];
        if (row.kind == sk_bindings::MenuExecutable::RowKind::Table) tableRowIndex = int(i);
        if (!row.selectable || row.y < 0) continue;
        if (row.kind == sk_bindings::MenuExecutable::RowKind::Table) continue;
        if (buttonBandY < 0) buttonBandY = row.y;
        if (row.y == buttonBandY) ++buttonsInBand;
    }
    std::printf("inventory.s layout: %d category buttons on the row at y=%d, table row index %d\n",
                buttonsInBand, buttonBandY, tableRowIndex);
    std::printf("  selectedItem=%d of %zu rows\n", inv->selectedItem(), inv->rows().size());
    for (size_t i = 0; i < inv->rows().size(); ++i) {
        const auto& r = inv->rows()[i];
        std::printf("    row %zu kind=%d sel=%d x=%d y=%d\n", i, int(r.kind), int(r.selectable), r.x,
                    r.y);
    }
    Check(buttonsInBand >= 4 && tableRowIndex >= 0,
          "inventory.s's real category-button row and item table are both laid out");

    // Right walks the tab strip. Before M35 this went to CycleSelectedCombo
    // and did nothing at all, which is why the armour/consumables/spells
    // tabs were unreachable.
    int startSelection = inv->selectedItem();
    bool movedRight = inv->NavigateDirectional(1, 0);
    int afterRight = inv->selectedItem();
    Check(movedRight && afterRight != startSelection,
          "Right moves along the category tab strip instead of doing nothing");

    bool movedBack = inv->NavigateDirectional(-1, 0);
    Check(movedBack && inv->selectedItem() == startSelection,
          "...and Left comes back to the tab it started on");

    // Every tab in the strip is reachable by repeating Right.
    int distinctTabs = 1;
    int firstTab = inv->selectedItem();
    for (int i = 0; i < buttonsInBand - 1; ++i) {
        inv->NavigateDirectional(1, 0);
        if (inv->selectedItem() != firstTab) ++distinctTabs;
    }
    Check(distinctTabs == buttonsInBand, "every category tab in the row is reachable");
    while (inv->selectedItem() != firstTab) inv->NavigateDirectional(1, 0);  // back to the start

    // Down leaves the button band for the table below it...
    bool movedDown = inv->NavigateDirectional(0, 1);
    Check(movedDown && inv->selectedItem() == tableRowIndex + 1,
          "Down from the tab strip moves into the item table below it");

    // ...and Up from the table's first row releases focus back to the tabs,
    // rather than the table swallowing Up/Down forever.
    auto* table = static_cast<sk_bindings::TableExecutable*>(
        inv->rows()[size_t(tableRowIndex)].widget.get());
    bool tableHasRows = table && table->rowCount() > 0;
    if (tableHasRows) table->SetSelectedRow(0);
    bool escaped = inv->NavigateDirectional(0, -1);
    Check(escaped && inv->selectedItem() != tableRowIndex + 1,
          "Up from the table's first row returns to the tab strip, not stuck inside");

    // ---- Part 3: the swing-restart guard ----
    sk_bindings::WeaponViewmodel vm;
    auto club = std::make_unique<sk_bindings::ItemExecutable>(
        skString((scriptRoot + "/weapons/club.s").c_str()),
        *(new skExecutableContext(&interpreter)), stack);
    {
        skRValueArray args;
        args.append(skRValue(0));
        skRValue ret;
        skExecutableContext callCtxt(&interpreter);
        club->method(skString("Init"), args, ret, callCtxt);
    }
    sk_bindings::StartWeaponSwing(vm, club.get());
    for (int i = 0; i < sk_bindings::kViewmodelFrameTicks + 1; ++i) {
        sk_bindings::TickWeaponViewmodel(vm);
    }
    int advanced = vm.frame;
    sk_bindings::StartWeaponSwing(vm, club.get());  // mashing the attack key
    Check(advanced > 0 && vm.frame == advanced,
          "attacking again mid-swing does not restart the swing animation");

    // The post-swing hold is still interruptible -- that is the recovery
    // pose, and cutting it short is what chains one swing into the next.
    vm.phase = sk_bindings::WeaponViewmodel::Phase::Hold;
    sk_bindings::StartWeaponSwing(vm, club.get());
    Check(vm.phase == sk_bindings::WeaponViewmodel::Phase::Swinging && vm.frame == 0,
          "...but a new swing can still begin out of the post-swing hold");

    std::printf("\nm35_placement_menu_smoke: %s (%d failure(s))\n",
                g_failures == 0 ? "OK" : "FAILED", g_failures);
    return g_failures == 0 ? 0 : 1;
}
