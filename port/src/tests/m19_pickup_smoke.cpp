// M19 (Action::Use interact binding: pickups) smoke test: proves the
// world-item-pickup flow -- ItemExecutable's new GetPlayer()/
// MirrorDestroyObject() handlers and PlayerExecutable's new
// PickupItem()/TakePendingPickupItem()/AddItem() -- against a real placed
// world item, not a synthetic fixture. Closes the one Action::Use category
// left unbound since M15 (docs/PORT_ROADMAP.md's repeated "Not attempted:
// pickups" notes).
//
// Real script, snowline/foxglove.s (read directly off the install image):
//   Init(s) { SetID("herb5"); SetName(2287); SetIcon(211);
//             SetUseText(2288); SetItemDescription(2287);
//             SetCanDrop(false); SetMPUsable(true); }
//   OnUse(s) { GetPlayer().PickupItem(self); MirrorDestroyObject(self); }
// entities.txt typeId 1008, category 3 (misc loot) -- 3 real placements in
// the `snowline` zone (not `azra`, which this session's earlier research
// confirmed has zero real category-3/8/9 placements at all).
#include <cstdio>
#include <memory>
#include <string>

#include "assets/string_table.h"
#include "simkin_bindings/item_executable.h"
#include "simkin_bindings/menu_stack.h"
#include "simkin_bindings/player_executable.h"
#include "skExecutableContext.h"
#include "skInterpreter.h"
#include "skParseException.h"
#include "skRValue.h"
#include "skRValueArray.h"
#include "skRuntimeException.h"
#include "world/entity_types.h"
#include "world/zone.h"

int main(int argc, char** argv) {
    const char* scriptRoot =
        argc > 1 ? argv[1]
                  : "The-Elder-Scrolls-Travels-Shadowkey_N-Gage_EN-FR-DE-ES-IT_USA-Europe-"
                    "EnFrDeEsIt-26102004/system/apps/6r51";
    const char* zoneName = argc > 2 ? argv[2] : "snowline";

    sk::StringTable strings;
    if (!strings.Load(std::string(scriptRoot) + "/stringtable.eng")) {
        std::printf("m19_pickup_smoke: FAILED to load stringtable.eng\n");
        return 1;
    }
    sk::EntityTypeTable entityTypes;
    if (!entityTypes.Load(scriptRoot)) {
        std::printf("m19_pickup_smoke: FAILED to load entities.txt\n");
        return 1;
    }
    sk::Zone zone;
    if (!zone.Load(scriptRoot, zoneName)) {
        std::printf("m19_pickup_smoke: FAILED to load zone %s\n", zoneName);
        return 1;
    }

    bool ok = true;

    // --- Part 1: real placement + category, structural ---
    int typeId1008Count = 0;
    for (const auto& e : zone.entities()) {
        if (e.typeId == 1008) ++typeId1008Count;
    }
    std::printf("real typeId=1008 (snowline/foxglove.s) placements in %s: %d (expected 3)\n",
                zoneName, typeId1008Count);
    if (typeId1008Count != 3) ok = false;

    const sk::EntityTypeDescriptor* desc = entityTypes.Lookup(1008);
    if (!desc) {
        std::printf("m19_pickup_smoke: FAILED -- typeId 1008 has no entities.txt entry\n");
        return 1;
    }
    bool categoryOk = desc->category == 3;
    std::printf("typeId=1008 entities.txt category: %d (expected 3/misc loot) %s\n", desc->category,
                categoryOk ? "OK" : "FAILED");
    if (!categoryOk) ok = false;

    // --- Part 2: real Init(), same load pattern every prior milestone
    // established (ctxt/args placeholder/Init call) ---
    skInterpreter interpreter;
    sk_bindings::MenuStack stack(scriptRoot, interpreter, &strings);

    std::string fullPath = std::string(scriptRoot) + "/snowline/foxglove.s";
    skExecutableContext loadCtxt(&interpreter);
    std::unique_ptr<sk_bindings::ItemExecutable> item;
    try {
        item = std::make_unique<sk_bindings::ItemExecutable>(skString(fullPath.c_str()), loadCtxt,
                                                               stack);
        skRValueArray args;
        args.append(skRValue(0));  // placeholder for Init's "(s)" parameter
        skRValue ret;
        skExecutableContext callCtxt(&interpreter);
        item->method(skString("Init"), args, ret, callCtxt);
    } catch (skParseException& e) {
        std::printf("m19_pickup_smoke: FAILED -- PARSE ERROR loading %s: %s\n", fullPath.c_str(),
                    e.toString().ptr());
        return 1;
    } catch (skRuntimeException& e) {
        std::printf("m19_pickup_smoke: FAILED -- RUNTIME ERROR loading %s: %s\n", fullPath.c_str(),
                    e.toString().ptr());
        return 1;
    }

    // Real literal values from foxglove.s's own Init() body.
    bool iconOk = item->icon() == 211;
    std::printf("icon: %d (expected 211) %s\n", item->icon(), iconOk ? "OK" : "FAILED");
    if (!iconOk) ok = false;
    bool useTextOk = item->useTextId() == 2288;
    std::printf("useTextId: %d (expected 2288) %s\n", item->useTextId(), useTextOk ? "OK" : "FAILED");
    if (!useTextOk) ok = false;

    // --- Part 3: the payoff -- real OnUse() actually transfers the item ---
    sk_bindings::ItemExecutable* itemRaw = item.get();
    size_t inventoryBefore = stack.player().inventory().size();
    item->InvokeOnUse();

    // M101: MirrorDestroyObject is the entity base's 0x33, a multiplayer
    // notify and nothing else -- it neither marks the item for removal nor
    // takes it out of the world. The pickup is what moves it (below).
    bool markedOk = !item->markedForRemoval() && !item->entityRemoved() &&
                    item->mirroredDestroyCount() == 1;
    std::printf("after OnUse(): markedForRemoval=%s entityRemoved=%s mirroredDestroyCount=%d "
                "(expected false/false/1) %s\n",
                item->markedForRemoval() ? "true" : "false",
                item->entityRemoved() ? "true" : "false", item->mirroredDestroyCount(),
                markedOk ? "OK" : "FAILED");
    if (!markedOk) ok = false;

    // Same check main.cpp's Action::Use handling performs before moving
    // ownership -- confirms PickupItem(self) really recorded *this* item,
    // not some stale/unrelated pointer.
    skiExecutable* pending = stack.player().TakePendingPickupItem();
    bool pendingOk = pending == static_cast<skiExecutable*>(itemRaw);
    std::printf("TakePendingPickupItem() matches the real item (GetPlayer().PickupItem(self)): %s "
                "%s\n",
                pendingOk ? "true" : "false", pendingOk ? "OK" : "FAILED");
    if (!pendingOk) ok = false;

    // A second TakePendingPickupItem() call must come back empty -- proves
    // it actually clears on read (main.cpp relies on this so a later,
    // unrelated Action::Use can never see a stale pickup).
    bool clearedOk = stack.player().TakePendingPickupItem() == nullptr;
    std::printf("TakePendingPickupItem() clears after being read: %s %s\n",
                clearedOk ? "true" : "false", clearedOk ? "OK" : "FAILED");
    if (!clearedOk) ok = false;

    stack.player().AddItem(std::move(item));
    size_t inventoryAfter = stack.player().inventory().size();
    bool inventoryOk = inventoryAfter == inventoryBefore + 1;
    std::printf("player inventory size: %zu -> %zu (expected +1) %s\n", inventoryBefore,
                inventoryAfter, inventoryOk ? "OK" : "FAILED");
    if (!inventoryOk) ok = false;
    bool sameObjectOk = stack.player().inventory().back().get() == itemRaw;
    std::printf("the real picked-up ItemExecutable is the one now in inventory: %s %s\n",
                sameObjectOk ? "true" : "false", sameObjectOk ? "OK" : "FAILED");
    if (!sameObjectOk) ok = false;

    // M101: and it is still there after the purge that runs every tick.
    // MirrorDestroyObject used to set markedForRemoval, which came into the
    // inventory with the item and deleted it here.
    stack.player().PurgeRemovedItems();
    bool survivesPurgeOk = stack.player().inventory().size() == inventoryAfter &&
                           stack.player().FindInventoryById("herb5") == itemRaw;
    std::printf("the herb survives PurgeRemovedItems() and FindInventory(\"herb5\") finds it: %s\n",
                survivesPurgeOk ? "OK" : "FAILED");
    if (!survivesPurgeOk) ok = false;

    std::printf("\nm19_pickup_smoke: %s\n", ok ? "OK" : "FAILED");
    return ok ? 0 : 1;
}
