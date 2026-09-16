// M101 smoke test: the player natives, DestroyObject on every entity, and the
// player's death.
//
//   FUN_10061a60  the entity base: 0x20 DestroyObject, 0x21
//                 DestroyObjectMirror, 0x2c Random, 0x33 MirrorDestroyObject
//   FUN_1001817c  what 0x20/0x21 do: out of the world, out of the grid, dead
//   FUN_1002c848  the Item class's own DestroyObject (case 3)
//   FUN_10048244  Character stats: 2 ModHealthBonus, 0x2f SetMagicka,
//                 0x34 SetMaxHealth, 0x36 SetMaxMagicka
//   FUN_10003810  Actor: 0x17 SetInvulnerable, 0x19 GetInvulnerable
//   FUN_1003f130  Player: 0x44 MoveToLeftQueue
//   FUN_10042cb0  the player's kill slot: DeathMenu, gated on +0x1099/+0x1e2
//
// What this checks:
//
//   1. A door leaves the world through `twilite/shadow_open.s`'s real TurnKey:
//      removed, its grid footprint lifted, and no longer found by GetEntity.
//   2. A creature through DestroyObjectMirror: out of the world and dead, but
//      no kill owed; a string argument destroys nobody.
//   3. The Item class's DestroyObject: `fearfrst/loot7_menu.s`'s chest leaves
//      the world; `sisithik_will.s` carried by the player is removed from the
//      inventory; MirrorDestroyObject is only counted.
//   4. The player's stat natives, through `cheatmenu.s`'s GiveBlaze sequence
//      and `lothna/treasure_menu.s`'s Grace3.
//   5. Random on the player, `ghchestgold.s`'s roll.
//   6. SetInvulnerable/GetInvulnerable, death once, death refused, the save
//      round trip of the flag and the health bonus.
//   7. MoveToLeftQueue's refusals.
#include <cstdio>
#include <memory>
#include <string>

#include "assets/save_records.h"
#include "assets/string_table.h"
#include "simkin_bindings/door_executable.h"
#include "simkin_bindings/effects.h"
#include "simkin_bindings/game_constants.h"
#include "simkin_bindings/item_executable.h"
#include "simkin_bindings/level_executable.h"
#include "simkin_bindings/menu_executable.h"
#include "simkin_bindings/menu_stack.h"
#include "simkin_bindings/monster_executable.h"
#include "simkin_bindings/player_executable.h"
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

class RecordingStamp : public sk_b::DoorExecutable::TileStamp {
public:
    void StampEntityBox(int, int, int, int, int, uint8_t mask, bool set) override {
        ++calls;
        lastMask = mask;
        lastSet = set;
    }
    int calls = 0;
    int lastMask = -1;
    bool lastSet = true;
};

}  // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    const std::string root =
        argc > 1 ? argv[1]
                 : "The-Elder-Scrolls-Travels-Shadowkey_N-Gage_EN-FR-DE-ES-IT_USA-Europe-"
                   "EnFrDeEsIt-26102004/system/apps/6r51";

    sk::StringTable strings;
    if (!strings.Load(root + "/stringtable.eng")) {
        std::printf("m101_destroy_death_smoke: FAILED to load stringtable.eng\n");
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
    auto none = []() { return skRValueArray(); };
    auto one = [](skRValue v) {
        skRValueArray a;
        a.append(v);
        return a;
    };
    auto two = [](skRValue a0, skRValue a1) {
        skRValueArray a;
        a.append(a0);
        a.append(a1);
        return a;
    };
    auto obj = [](skiExecutable* e) { return skRValue(e, false); };
    auto getEntity = [&](const char* name) {
        return call(&stack.level(), "GetEntity", one(skRValue(skString(name))));
    };
    auto runMenuHandler = [&](const char* path, skiExecutable* opener, const char* handler) {
        stack.OpenMenu(path, opener);
        sk_b::MenuExecutable* menu = stack.currentMenu();
        if (!menu) return false;
        call(menu, handler, none());
        return true;
    };

    // ---------------------------------------------------------------
    // Part 1: a door leaves the world
    // ---------------------------------------------------------------
    std::printf("\n== Part 1: twilite's shadow door ==\n");
    {
        RecordingStamp stamp;
        std::unique_ptr<sk_b::DoorExecutable> door;
        try {
            skExecutableContext ctxt(&interpreter);
            const std::string path = root + "/twilite/shadow_door.s";
            door = std::make_unique<sk_b::DoorExecutable>(skString(path.c_str()), ctxt, player);
            door->AttachTileStamp(&stamp, 300, 300, 0);
            door->SetEntityId("sdwdoor");
            call(door.get(), "Init", one(skRValue(0)));
        } catch (skParseException& e) {
            std::printf("   (PARSE ERROR: %s)\n", e.toString().ptr());
        }
        Check(door != nullptr, "twilite/shadow_door.s loads as a door");
        if (door) {
            stack.level().RegisterEntity("sdwdoor", door.get());
            Check(getEntity("sdwdoor").obj() == door.get(), "GetEntity(\"sdwdoor\") finds it");
            const bool opened = runMenuHandler("twilite\\Shadow_open", nullptr, "TurnKey");
            Check(opened, "twilite\\Shadow_open opens and its TurnKey runs");
            Check(door->entityRemoved(), "the door is out of the world (it used to soft-fail)");
            Check(door->entityOutOfWorld(), "  entityOutOfWorld() says so too");
            Check(stamp.calls >= 1 && stamp.lastMask == 4 && !stamp.lastSet,
                  "  its footprint is lifted: flag 4, cleared");
            Check(getEntity("sdwdoor").type() != skRValue::T_Object,
                  "  GetEntity(\"sdwdoor\") no longer finds it");
            skRValue ss;
            stack.level().getValue(skString("saved_SSdoor"), skString(), ss);
            Check(ss.intValue() == 1, "  and the script carried on: Level.saved_SSdoor = 1");
            const int before = stamp.calls;
            call(door.get(), "DestroyObjectMirror", one(obj(door.get())));
            Check(stamp.calls == before, "a second destroy does nothing more");
            stack.level().ClearEntities();
        }
    }

    // ---------------------------------------------------------------
    // Part 2: a creature
    // ---------------------------------------------------------------
    std::printf("\n== Part 2: a creature, and a string argument ==\n");
    {
        std::unique_ptr<sk_b::MonsterExecutable> rat;
        try {
            skExecutableContext ctxt(&interpreter);
            const std::string path = root + "/monsters/azra_rat.s";
            rat = std::make_unique<sk_b::MonsterExecutable>(skString(path.c_str()), ctxt, &strings,
                                                             player, stack);
            call(rat.get(), "Init", one(skRValue(0)));
        } catch (skParseException& e) {
            std::printf("   (PARSE ERROR: %s)\n", e.toString().ptr());
        }
        Check(rat != nullptr, "monsters/azra_rat.s loads");
        if (rat) {
            stack.level().RegisterEntity("ctaker", rat.get());
            call(&player, "DestroyObjectMirror", one(skRValue(skString("ctaker"))));
            Check(!rat->destroyed() && rat->alive(),
                  "GetPlayer().DestroyObjectMirror(\"ctaker\") -- a string -- destroys nobody");
            call(rat.get(), "DestroyObjectMirror", one(obj(rat.get())));
            Check(rat->destroyed() && rat->outOfWorld(), "Rat.DestroyObjectMirror(Rat): out of the world");
            Check(!rat->alive() && !rat->actorAlive(), "  and dead (vtable[0x178](1, 0))");
            Check(!rat->deathOwed() && !rat->decayArmed(), "  but no kill owed and no decay armed");
            Check(getEntity("ctaker").type() != skRValue::T_Object, "  GetEntity no longer finds it");
            stack.level().ClearEntities();
        }
    }

    // ---------------------------------------------------------------
    // Part 3: items
    // ---------------------------------------------------------------
    std::printf("\n== Part 3: the Item class's DestroyObject ==\n");
    auto loadItem = [&](const std::string& rel) -> std::unique_ptr<sk_b::ItemExecutable> {
        try {
            skExecutableContext ctxt(&interpreter);
            const std::string path = root + "/" + rel;
            auto item = std::make_unique<sk_b::ItemExecutable>(skString(path.c_str()), ctxt, stack);
            call(item.get(), "Init", one(skRValue(0)));
            return item;
        } catch (skParseException& e) {
            std::printf("   (PARSE ERROR loading %s: %s)\n", rel.c_str(), e.toString().ptr());
        } catch (skRuntimeException& e) {
            std::printf("   (RUNTIME ERROR loading %s: %s)\n", rel.c_str(), e.toString().ptr());
        }
        return nullptr;
    };
    {
        std::unique_ptr<sk_b::ItemExecutable> chest = loadItem("fearfrst/loot7.s");
        Check(chest != nullptr, "fearfrst/loot7.s loads");
        if (chest) {
            const bool ran = runMenuHandler("fearfrst\\loot7_menu", chest.get(), "Take");
            Check(ran, "fearfrst\\loot7_menu opens over it and Take runs");
            Check(chest->entityRemoved(),
                  "GetOpener().DestroyObject(GetOpener()): the unowned chest leaves the world");
            Check(!chest->markedForRemoval(), "  (an inventory purge flag is not what it uses)");
            stack.ForgetOpener(chest.get());
            Check(!stack.currentMenu() || stack.currentMenu()->opener() == nullptr,
                  "ForgetOpener() clears it as the screen's opener");
        }

        std::unique_ptr<sk_b::ItemExecutable> will = loadItem("fearfrst/sisithik_will.s");
        Check(will != nullptr, "fearfrst/sisithik_will.s loads");
        if (will) {
            sk_b::ItemExecutable* raw = will.get();
            player.AddItem(std::move(will));
            Check(player.CarriesItem(raw), "the player carries it");
            call(raw, "DestroyObject", one(obj(raw)));
            Check(raw->markedForRemoval() && !raw->entityRemoved(),
                  "Will.DestroyObject(Will): carried, so the owner's RemoveItem takes it");
            player.PurgeRemovedItems();
            Check(!player.CarriesItem(raw), "  and the purge removes it from the inventory");
        }

        std::unique_ptr<sk_b::ItemExecutable> herb = loadItem("snowline/foxglove.s");
        if (herb) {
            call(herb.get(), "MirrorDestroyObject", one(obj(herb.get())));
            Check(!herb->entityRemoved() && !herb->markedForRemoval() &&
                      herb->mirroredDestroyCount() == 1,
                  "MirrorDestroyObject(self) is a counted multiplayer notify, nothing more");
            call(&player, "DestroyObject", one(obj(herb.get())));
            Check(herb->entityRemoved() && !herb->markedForRemoval(),
                  "GetPlayer().DestroyObject(Herb) is the base 0x20 on the herb");
        }
    }

    // ---------------------------------------------------------------
    // Part 4: the stat natives
    // ---------------------------------------------------------------
    std::printf("\n== Part 4: SetMaxHealth, SetMaxMagicka, SetMagicka, ModHealthBonus ==\n");
    {
        call(&player, "SetMaxMagicka", one(skRValue(5000)));
        call(&player, "SetMagicka", one(skRValue(5000)));
        Check(player.maxMagicka() == 5000 && player.magicka() == 5000,
              "cheatmenu.s GiveBlaze: SetMaxMagicka(5000) then SetMagicka(5000) -> 5000/5000");
        call(&player, "SetMaxMagicka", one(skRValue(40)));
        Check(player.magicka() == 5000, "SetMaxMagicka alone does not clamp the current value");
        call(&player, "SetMagicka", one(skRValue(900)));
        Check(player.magicka() == 40, "SetMagicka clamps to the maximum");
        call(&player, "SetMagicka", one(skRValue(-3)));
        Check(player.magicka() == 0, "  and to zero");

        call(&player, "SetHealth", one(skRValue(player.maxHealth())));
        const int health = player.health();
        call(&player, "SetMaxHealth", one(skRValue(health - 5)));
        Check(player.maxHealth() == health - 5 && player.health() == health - 5,
              "SetMaxHealth below health pulls health down with it");
        call(&player, "SetMaxHealth", one(skRValue(health + 50)));
        Check(player.health() == health - 5, "  raising it gives nothing");

        const int maxBefore = [&]() {
            call(&player, "SetLuck", one(call(&player, "GetLuck", none())));
            return player.maxHealth();
        }();
        call(&player, "ModHealthBonus", one(skRValue(20)));
        Check(player.maxHealth() == maxBefore, "ModHealthBonus(20) moves only the bonus");
        Check(runMenuHandler("lothna\\treasure_menu", nullptr, "Grace3"),
              "lothna\\treasure_menu opens and Grace3 runs");
        Check(player.maxHealth() == maxBefore + 40,
              "  its own ModHealthBonus(20) and SetLuck recompute: +20 twice = +40");
        Check(player.EffectStatValue(sk_b::kEffectStatHealthBonus) == 40,
              "  stats+0x12 holds 40");
    }

    // ---------------------------------------------------------------
    // Part 5: Random
    // ---------------------------------------------------------------
    std::printf("\n== Part 5: GetPlayer().Random ==\n");
    {
        bool inRange = true;
        bool nonZero = false;
        for (int i = 0; i < 200; ++i) {
            const int v = call(&player, "Random", two(skRValue(40), skRValue(100))).intValue();
            if (v < 40 || v > 100) inRange = false;
            if (v != 0) nonZero = true;
        }
        Check(inRange && nonZero, "GetPlayer().Random(40, 100) stays in 40..100 (it answered 0)");
        // ghchestgold.s's Init ends in `GetOpener().SetUsable(false)`, so it
        // needs a real opener to finish; any item will do.
        std::unique_ptr<sk_b::ItemExecutable> ghChest = loadItem("fearfrst/loot7.s");
        const int goldBefore = player.gold();
        const sk_b::MenuExecutable* beforeMenu = stack.currentMenu();
        stack.OpenMenu("ghchestgold", ghChest.get());
        Check(ghChest && stack.currentMenu() != beforeMenu && !ghChest->usable(),
              "ghchestgold opens over its chest and its Init runs to the end");
        const int gained = player.gold() - goldBefore;
        Check(gained >= 40 && gained <= 100, "  and pays 40..100 gold");
    }

    // ---------------------------------------------------------------
    // Part 6: invulnerability and death
    // ---------------------------------------------------------------
    std::printf("\n== Part 6: SetInvulnerable, death ==\n");
    {
        player.ResetForNewSession();
        Check(!call(&player, "GetInvulnerable", none()).boolValue(), "GetInvulnerable starts false");
        call(&player, "SetInvulnerable", one(skRValue(true)));
        Check(call(&player, "GetInvulnerable", none()).boolValue() && player.actorInvulnerable(),
              "SetInvulnerable(true) reads back, and DoAttackRoll's gate sees it");

        call(&player, "SetHealth", one(skRValue(10)));
        player.ApplyDamage(500);
        Check(player.health() == 0, "an invulnerable player's health still reaches 0");
        Check(!player.TakePendingDeath() && !player.deathHandled(),
              "  but the kill slot opens no death screen");

        call(&player, "SetInvulnerable", one(skRValue(false)));
        call(&player, "SetHealth", one(skRValue(10)));
        player.ApplyDamage(4);
        Check(player.health() == 6 && !player.TakePendingDeath(), "a hit that leaves health is no death");
        player.ApplyDamage(6);
        Check(player.health() == 0 && player.deathHandled(), "a hit to exactly 0 kills");
        Check(player.TakePendingDeath(), "  the death screen is owed");
        Check(!player.TakePendingDeath(), "  once (it clears on read)");
        call(&player, "SetHealth", one(skRValue(10)));
        player.ApplyDamage(50);
        Check(!player.TakePendingDeath(), "a second fatal hit: +0x1099 is already set, nothing");

        player.ResetForNewSession();
        call(&player, "SetHealth", one(skRValue(10)));
        call(&player, "DoDamage", one(skRValue(99)));
        Check(player.TakePendingDeath(), "after ResetForNewSession a script's DoDamage kills again");

        // The save round trip.
        player.ResetForNewSession();
        call(&player, "SetInvulnerable", one(skRValue(true)));
        const sk::SavedEntity rec = player.BuildSaveRecord("azra");
        Check(rec.holder.invulnerable == 1 && rec.stats.healthBonus == 40,
              "the save record carries +0x1e2 and stats+0x12");
        call(&player, "SetInvulnerable", one(skRValue(false)));
        call(&player, "ModHealthBonus", one(skRValue(-40)));
        player.ApplySaveRecord(rec, stack);
        Check(player.invulnerable() && player.EffectStatValue(sk_b::kEffectStatHealthBonus) == 40,
              "  and ApplySaveRecord restores both");
        player.ResetForNewSession();
    }

    // ---------------------------------------------------------------
    // Part 7: MoveToLeftQueue
    // ---------------------------------------------------------------
    std::printf("\n== Part 7: MoveToLeftQueue ==\n");
    {
        Check(!call(&player, "MoveToLeftQueue", one(skRValue(0))).boolValue(),
              "a non-object is refused");
        std::unique_ptr<sk_b::ItemExecutable> bread = loadItem("items/bread.s");
        if (bread) {
            bread->SetEntityCategory(9);  // entities.txt category 9: consumable
            Check(!call(&player, "MoveToLeftQueue", one(obj(bread.get()))).boolValue(),
                  "a consumable (type 4) is refused");
        }
    }

    std::printf("\nm101_destroy_death_smoke: %d/%d checks passed -- %s\n", g_checks - g_failures,
                g_checks, g_failures == 0 ? "OK" : "FAILED");
    return g_failures == 0 ? 0 : 1;
}
