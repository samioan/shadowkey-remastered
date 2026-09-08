// M74 smoke test: where an item's type comes from, and what follows.
//
// Reported: the Blaze pickup at the start of the game lands in the
// inventory's **Miscellaneous** page instead of Spells, and cannot be
// equipped or cast. Root cause: this port *inferred* the item type from
// whichever category setter a script happened to call, and nothing a spell
// script calls implies "spell" -- so `kItemTypeSpell` was never produced by
// anything and every spell in the game reported Misc. The engine does not
// infer: `GetItemType()` reads a stored word (`entity+0x16c`) that the
// item's C++ constructor wrote, and the class comes from the
// `entities.txt` **category** column.
//
// What each part checks, and why it is the check that could fail:
//
//   1. The category table itself, against the eight factory arms.
//   2. The same table against the **real entities.txt**: every row of every
//      item category classified, and the directory each one lives in used
//      as an independent witness (`weapons\` really is category 4, and so
//      on). A transposed row would show up here immediately.
//   3. Blaze end to end -- the real `blaze.s` through the real
//      `Level.CreateEntity(50)`, onto the real `inventory.s` Spells page,
//      which is the reported bug reproduced and fixed in one pass.
//   4. `RestrictUse` -> the real class bitmask, and `IsItemEnabledFor`'s
//      three arms against the real class table.
//   5. The two use texts. `blaze.s` picks between string 404 and 405 on
//      `IsItemEnabledFor` -- the "Learn Blaze" / "Take Blaze Scroll" fork
//      the report describes -- so a caster and a non-caster must get
//      different text out of the *same* script.
//   6. The pickup tail: taking a spell arms it, and taking it as a
//      non-caster does not.
//   7. The equip path: a spell equips into the right hand, unequips as a
//      toggle, and a refusal is a 3 for the class gate and never for the
//      type.
#include <cstdio>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "assets/string_table.h"
#include "simkin_bindings/character_progression.h"
#include "simkin_bindings/game_constants.h"
#include "simkin_bindings/item_executable.h"
#include "simkin_bindings/level_executable.h"
#include "simkin_bindings/menu_executable.h"
#include "simkin_bindings/menu_stack.h"
#include "simkin_bindings/player_executable.h"
#include "simkin_bindings/table_executable.h"
#include "skInterpreter.h"
#include "skRValue.h"
#include "skRValueArray.h"
#include "world/entity_types.h"

namespace sk_b = sk_bindings;

namespace {

int g_failures = 0;
int g_checks = 0;

void Check(bool ok, const std::string& what) {
    ++g_checks;
    std::printf("%-88s %s\n", what.c_str(), ok ? "OK" : "FAILED");
    if (!ok) ++g_failures;
}

// The directory an entities.txt row's script lives in, lowercased --
// the independent witness for part 2.
std::string DirOf(const std::string& name) {
    std::string s;
    for (char c : name) {
        if (c == '\\') c = '/';
        s.push_back(static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c));
    }
    const size_t slash = s.find('/');
    return slash == std::string::npos ? std::string("(root)") : s.substr(0, slash);
}

sk_b::TableExecutable* FindTable(sk_b::MenuExecutable* menu) {
    if (!menu) return nullptr;
    for (const auto& row : menu->rows()) {
        if (row.kind == sk_b::MenuExecutable::RowKind::Table) {
            return static_cast<sk_b::TableExecutable*>(row.widget.get());
        }
    }
    return nullptr;
}

}  // namespace

int main(int argc, char** argv) {
    const char* scriptRoot =
        argc > 1 ? argv[1]
                 : "The-Elder-Scrolls-Travels-Shadowkey_N-Gage_EN-FR-DE-ES-IT_USA-Europe-"
                   "EnFrDeEsIt-26102004/system/apps/6r51";

    // ---------------------------------------------------------------
    std::printf("\n== 1. the category -> item type table ==\n");
    {
        struct Row {
            int category, type, slot;
            const char* what;
        };
        // Transcribed from the factory (`FUN_1002aa14`) and each arm's
        // constructor -- the `+0x16c` and `+0x1c0` stores.
        const Row kExpected[] = {
            {3, sk_b::kItemTypeMisc, sk_b::kEquipSlotNone, "FUN_1002eeb8 misc"},
            {4, sk_b::kItemTypeWeapon, sk_b::kEquipSlotRight, "FUN_1002ce9c weapon"},
            {5, sk_b::kItemTypeSpell, sk_b::kEquipSlotRight, "FUN_10047740 spell"},
            {6, sk_b::kItemTypeArmor, sk_b::kEquipSlotNone, "FUN_1002e954 armor"},
            {9, sk_b::kItemTypeConsumable, sk_b::kEquipSlotLeft, "FUN_1002e78c consumable"},
            {14, sk_b::kItemTypeSpell, sk_b::kEquipSlotRight, "FUN_1002e5ac scroll : spell"},
            {15, sk_b::kItemTypeArmor, sk_b::kEquipSlotNone, "FUN_1002e844 shield"},
            {16, sk_b::kItemTypeWeapon, sk_b::kEquipSlotRight, "FUN_10047500 : weapon"},
        };
        bool allOk = true;
        for (const Row& r : kExpected) {
            const bool ok = sk_b::ItemTypeForCategory(r.category) == r.type &&
                            sk_b::EquipSlotForCategory(r.category) == r.slot &&
                            sk_b::IsInventoryItemCategory(r.category);
            if (!ok) allOk = false;
            std::printf("   cat %2d -> type %d slot %d   %-28s %s\n", r.category,
                        sk_b::ItemTypeForCategory(r.category),
                        sk_b::EquipSlotForCategory(r.category), r.what, ok ? "" : "  <-- FAILED");
        }
        Check(allOk, "all eight factory item arms transcribed correctly");
        // Categories 1, 2, 7, 8, 10, 11, 12 and 13 are creatures, doors and
        // triggers -- they never reach an item constructor and have no
        // `+0x16c` at all.
        bool nonItemsRejected = true;
        for (int c : {0, 1, 2, 7, 8, 10, 11, 12, 13, 17, 99}) {
            if (sk_b::IsInventoryItemCategory(c)) nonItemsRejected = false;
        }
        Check(nonItemsRejected, "and every non-item category is rejected, including the unused 13");
        Check(sk_b::ItemTypeForCategory(2) == sk_b::kItemTypeMisc &&
                  sk_b::EquipSlotForCategory(2) == sk_b::kEquipSlotNone,
              "a creature category answers Misc / no slot rather than pretending to be an item");
    }

    // ---------------------------------------------------------------
    std::printf("\n== 2. the table against the real entities.txt ==\n");
    sk::EntityTypeTable entityTypes;
    if (!entityTypes.Load(scriptRoot)) {
        std::printf("m74_item_category_smoke: FAILED to load entities.txt\n");
        return 1;
    }
    {
        // The shipped data is the independent witness: if the category
        // column really is the item class, then the *directory* each script
        // lives in has to line up with it.
        struct Expect {
            int category;
            const char* dir;
            int minRows;
        };
        const Expect kWitness[] = {
            {4, "weapons", 60}, {5, "spells", 20}, {6, "armor", 60},
            {9, "items", 40},   {14, "spells", 7}, {15, "armor", 8},
        };
        std::map<int, int> total;
        std::map<int, std::map<std::string, int>> byDir;
        for (int typeId = 0; typeId < 6000; ++typeId) {
            const sk::EntityTypeDescriptor* d = entityTypes.Lookup(typeId);
            if (!d) continue;
            total[d->category] += 1;
            byDir[d->category][DirOf(d->name)] += 1;
        }
        bool witnessOk = true;
        for (const Expect& e : kWitness) {
            const int inDir = byDir[e.category][e.dir];
            const int rows = total[e.category];
            std::printf("   cat %2d: %3d row(s), %3d of them under %-8s -> %s\n", e.category, rows,
                        inDir, e.dir,
                        sk_b::ItemTypeForCategory(e.category) == sk_b::kItemTypeWeapon ? "weapon"
                        : sk_b::ItemTypeForCategory(e.category) == sk_b::kItemTypeSpell
                            ? "spell"
                            : sk_b::ItemTypeForCategory(e.category) == sk_b::kItemTypeArmor
                                  ? "armor"
                                  : "consumable");
            if (inDir < e.minRows || inDir * 2 < rows) witnessOk = false;
        }
        Check(witnessOk,
              "every item category is dominated by the matching script directory in the real data");

        // And the reported item, by name.
        const sk::EntityTypeDescriptor* blazeDesc = entityTypes.Lookup(50);
        Check(blazeDesc && blazeDesc->category == 5 && blazeDesc->name == "blaze.s",
              "typeId 50 is `blaze.s`, entities.txt category 5 -- a spell");
        // Its two shipped shields and the conjured sword, the three
        // categories CreateItem used to refuse outright.
        const sk::EntityTypeDescriptor* shield = entityTypes.Lookup(637);
        const sk::EntityTypeDescriptor* scroll = entityTypes.Lookup(4902);
        const sk::EntityTypeDescriptor* daedric = entityTypes.Lookup(4037);
        Check(shield && shield->category == 15 && scroll && scroll->category == 14 && daedric &&
                  daedric->category == 16,
              "categories 14/15/16 are real and populated -- CreateItem used to answer null for all");
    }

    // ---------------------------------------------------------------
    std::printf("\n== 3. Blaze, end to end ==\n");
    sk::StringTable strings;
    if (!strings.Load(std::string(scriptRoot) + "/stringtable.eng")) {
        std::printf("m74_item_category_smoke: FAILED to load stringtable.eng\n");
        return 1;
    }
    skInterpreter interpreter;
    sk_b::MenuStack stack(scriptRoot, interpreter, &strings);
    stack.level().SetEntityTypes(&entityTypes);
    sk_b::PlayerExecutable& player = stack.player();
    {
        std::unique_ptr<sk_b::ItemExecutable> blaze = stack.level().CreateItem(50);
        Check(blaze != nullptr, "`Level.CreateEntity(50)` builds the real blaze.s");
        if (!blaze) {
            std::printf("m74_item_category_smoke: FAILED, cannot continue\n");
            return 1;
        }
        Check(blaze->itemType() == sk_b::kItemTypeSpell,
              "and it reports IPT_Spell (2), not the Misc (0) the bug produced");
        Check(blaze->equipSlot() == sk_b::kEquipSlotRight,
              "and names the right hand, the same slot a weapon names");
        Check(blaze->templateId() == 50,
              "the typeId is stamped before Init(), the way the real factory does it");

        // The page it lands on, through the real inventory.s. The five
        // Display* natives are the only consumers of the type, and their
        // whole filter is `FUN_1006d508(item) == page`.
        player.AddItem(std::move(blaze));
        stack.OpenMenu("inventory");
        sk_b::MenuExecutable* inv = stack.currentMenu();
        auto pageRows = [&](const char* method) {
            skRValueArray tab;
            tab.append(skRValue(0));
            skRValue ret;
            skExecutableContext ctxt(&interpreter);
            inv->method(skString(method), tab, ret, ctxt);
            sk_b::TableExecutable* t = FindTable(inv);
            return t ? t->rowCount() : 0;
        };
        const int spells = pageRows("DisplaySpellsPage");
        const int misc = pageRows("DisplayMiscItemsMenu");
        std::printf("   inventory.s: Spells page %d row(s), Misc page %d row(s)\n", spells, misc);
        Check(spells == 1, "Blaze is on the Spells page -- the reported bug, fixed");
        Check(misc == 0, "and no longer on the Miscellaneous page");
    }

    // ---------------------------------------------------------------
    std::printf("\n== 4. RestrictUse and the class gate ==\n");
    {
        sk_b::ItemExecutable* blaze = nullptr;
        for (const auto& item : player.inventory()) {
            if (item->templateId() == 50) blaze = item.get();
        }
        Check(blaze != nullptr, "the created Blaze is in the inventory");
        if (!blaze) return 1;

        // `RestrictUse(2, 4, 6, 7)` -> `2 << classId` per argument.
        const int expectedMask = (2 << 2) | (2 << 4) | (2 << 6) | (2 << 7);
        std::printf("   blaze.s RestrictUse(2,4,6,7) -> mask 0x%x (expected 0x%x)\n",
                    blaze->restrictUseMask(), expectedMask);
        Check(blaze->restrictUseMask() == expectedMask,
              "RestrictUse stores one `2 << classId` bit per argument");

        // The four classes it names are exactly the four the class table
        // marks HasMagic -- two independent tables agreeing is what makes
        // both readings safe.
        int named = 0, magical = 0, agree = 0;
        for (int c = 0; c < sk_b::kClassCount; ++c) {
            const bool inMask = (blaze->restrictUseMask() & (2 << c)) != 0;
            const bool hasMagic = sk_b::ClassHasMagic(c);
            if (inMask) ++named;
            if (hasMagic) ++magical;
            if (inMask == hasMagic) ++agree;
        }
        std::printf("   %d class(es) named by the mask, %d with HasMagic, %d agree\n", named,
                    magical, agree);
        Check(named == 4 && magical == 4 && agree == sk_b::kClassCount,
              "blaze.s's own restriction list is exactly the classes the class table calls magical");

        // The gate itself, class by class.
        int allowed = 0;
        for (int c = 0; c < sk_b::kClassCount; ++c) {
            skRValueArray pick;
            pick.append(skRValue(c));
            skRValue ret;
            skExecutableContext ctxt(&interpreter);
            player.method(skString("ChooseCharacter"), pick, ret, ctxt);
            if (player.IsItemEnabledFor(*blaze)) ++allowed;
        }
        Check(allowed == 4, "IsItemEnabledFor lets exactly those four classes use it");

        // The armour arm, on real shipped data: `armor/chain_coif.s` is
        // AR_Medium, and the class rows split cleanly on it.
        std::unique_ptr<sk_b::ItemExecutable> coif = stack.level().CreateItem(614, false);
        if (!coif) {  // resolve it by name instead if 614 is not the coif
            for (int typeId = 600; typeId < 700 && !coif; ++typeId) {
                const sk::EntityTypeDescriptor* d = entityTypes.Lookup(typeId);
                if (d && DirOf(d->name) == "armor" &&
                    d->name.find("Chain_Coif") != std::string::npos) {
                    coif = stack.level().CreateItem(typeId, false);
                }
            }
        }
        Check(coif != nullptr, "a real `armor\\Chain_Coif.s` loads through the factory");
        if (coif) {
            Check(coif->itemType() == sk_b::kItemTypeArmor && !coif->isShield(),
                  "and is armour, not a shield -- category 6, whose vtable+0x180 returns 0");
            int wearers = 0;
            for (int c = 0; c < sk_b::kClassCount; ++c) {
                skRValueArray pick;
                pick.append(skRValue(c));
                skRValue ret;
                skExecutableContext ctxt(&interpreter);
                player.method(skString("ChooseCharacter"), pick, ret, ctxt);
                if (player.IsItemEnabledFor(*coif)) ++wearers;
            }
            // field0 & AR_Medium(4): Barbarian, Knight, Rogue, Spellsword,
            // Sorcerer -- the five rows with bit 2 set.
            std::printf("   %d of %d classes may wear AR_Medium armour\n", wearers,
                        sk_b::kClassCount);
            Check(wearers == 5,
                  "the armour arm splits the nine class rows on their own AR_ mask");
        }
    }

    // ---------------------------------------------------------------
    std::printf("\n== 5. the two use texts ==\n");
    {
        // `blaze.s`'s Init() asks IsItemEnabledFor(self) and picks 404 or
        // 405 -- so the *same script* has to produce different text for a
        // caster and a non-caster. This is the prompt the report quotes.
        auto useTextFor = [&](int classId) {
            skRValueArray pick;
            pick.append(skRValue(classId));
            skRValue ret;
            skExecutableContext ctxt(&interpreter);
            player.method(skString("ChooseCharacter"), pick, ret, ctxt);
            std::unique_ptr<sk_b::ItemExecutable> item = stack.level().CreateItem(50);
            return item ? item->useTextId() : -1;
        };
        const int caster = useTextFor(sk_b::kClassBattlemage);
        const int fighter = useTextFor(sk_b::kClassBarbarian);
        std::printf("   Battlemage: string %d \"%s\"\n   Barbarian:  string %d \"%s\"\n", caster,
                    strings.Get(caster).c_str(), fighter, strings.Get(fighter).c_str());
        Check(caster != fighter,
              "blaze.s's own IsItemEnabledFor fork produces two different prompts");
        Check(caster == 404 && fighter == 405,
              "and they are string 404 and string 405, the script's own two ids");
    }

    // ---------------------------------------------------------------
    // Parts 6 and 7 each want a character who has picked up nothing yet,
    // and a class change on a player already holding something is a real
    // one-way door -- `UpdateEquipStatus` runs the class gate *before* it
    // looks at the hands, so a Barbarian cannot even put down a spell a
    // Sorcerer was holding. Faithful, and the reason each scenario below
    // builds its own stack instead of resetting this one.
    auto withPlayer = [&](int classId, const std::function<void(sk_b::MenuStack&)>& body) {
        skInterpreter interp;
        sk_b::MenuStack own(scriptRoot, interp, &strings);
        own.level().SetEntityTypes(&entityTypes);
        skRValueArray pick;
        pick.append(skRValue(classId));
        skRValue ret;
        skExecutableContext ctxt(&interp);
        own.player().method(skString("ChooseCharacter"), pick, ret, ctxt);
        body(own);
    };

    std::printf("\n== 6. picking one up arms it ==\n");
    withPlayer(sk_b::kClassSorcerer, [&](sk_b::MenuStack& s) {
        std::unique_ptr<sk_b::ItemExecutable> spell = s.level().CreateItem(50);
        sk_b::ItemExecutable* raw = spell.get();
        s.player().AddItem(std::move(spell));
        Check(raw && s.player().rightItem() == raw && raw->equipped(),
              "a Sorcerer who picks up Blaze is holding it, ready to cast");
    });
    withPlayer(sk_b::kClassBarbarian, [&](sk_b::MenuStack& s) {
        std::unique_ptr<sk_b::ItemExecutable> spell = s.level().CreateItem(50);
        sk_b::ItemExecutable* raw = spell.get();
        s.player().AddItem(std::move(spell));
        Check(raw && s.player().rightItem() != raw && !raw->equipped(),
              "a Barbarian who picks up the same scroll is not -- the gate is in the pickup too");
        Check(s.player().inventory().size() == 1,
              "but it is in their bag either way; the gate refuses the hand, not the item");
    });

    // ---------------------------------------------------------------
    std::printf("\n== 7. equipping one by hand ==\n");
    withPlayer(sk_b::kClassSpellsword, [&](sk_b::MenuStack& s) {
        std::unique_ptr<sk_b::ItemExecutable> spell = s.level().CreateItem(50);
        sk_b::ItemExecutable* blaze = spell.get();
        s.player().AddItem(std::move(spell));
        Check(blaze && s.player().rightItem() == blaze, "a Spellsword picks Blaze up armed");
        if (!blaze) return;

        // The toggle. `inventory.s` shows "Unequip" for an item already in
        // a hand and passes `false`, but the real function does not consult
        // the flag at all for that case -- being in a hand is what decides.
        const int outRet = s.player().UpdateEquipStatus(blaze, false);
        Check(outRet == 1 && s.player().rightItem() != blaze && !blaze->equipped(),
              "PerformEquipAction takes it back out, and answers the real 1");
        const int inRet = s.player().UpdateEquipStatus(blaze, true);
        Check(inRet == 1 && s.player().rightItem() == blaze && blaze->equipped(),
              "...and puts it back in the right hand -- a spell equips exactly like a sword");
    });
    withPlayer(sk_b::kClassBarbarian, [&](sk_b::MenuStack& s) {
        std::unique_ptr<sk_b::ItemExecutable> spell = s.level().CreateItem(50);
        sk_b::ItemExecutable* blaze = spell.get();
        s.player().AddItem(std::move(spell));
        Check(blaze && s.player().UpdateEquipStatus(blaze, true) == 3,
              "a Barbarian equipping a spell gets the real 3 -- from the class gate, not the type");
    });
    withPlayer(sk_b::kClassBarbarian, [&](sk_b::MenuStack& s) {
        // A consumable is a left-hand item and equips like anything else --
        // the old code returned 3 for it because it was neither armour nor
        // a weapon.
        std::unique_ptr<sk_b::ItemExecutable> food;
        for (int typeId = 0; typeId < 6000 && !food; ++typeId) {
            const sk::EntityTypeDescriptor* d = entityTypes.Lookup(typeId);
            if (d && d->category == 9) food = s.level().CreateItem(typeId);
        }
        Check(food != nullptr && food->itemType() == sk_b::kItemTypeConsumable &&
                  food->equipSlot() == sk_b::kEquipSlotLeft,
              "a real category-9 consumable is a left-hand item");
        if (!food) return;
        sk_b::ItemExecutable* raw = food.get();
        s.player().AddItem(std::move(food));
        Check(s.player().leftItem() == raw && s.player().rightItem() != raw,
              "and picking one up fills the *left* hand, not the right");
    });

    std::printf("\nm74_item_category_smoke: %s (%d checks)\n",
                g_failures == 0 ? "PASSED (all checks)" : "FAILED", g_checks);
    return g_failures == 0 ? 0 : 1;
}
