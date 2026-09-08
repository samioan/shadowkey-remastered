#pragma once

// M75: what makes a placed entity offer the "use" prompt at all.
//
// Every interactive thing in the game -- a door, a chest, a world item, a
// talkable NPC, a merchant -- is gated on one byte, `entity+0xd8`. The
// engine's per-frame use-target search reads exactly that:
//
//     FUN_1001dd40(player)                    "find the entity in front of me"
//       ...
//       for (e = tile->firstEntity; e; e = e->next)
//         if (e->+0xd8 != 0) {                             <-- the gate
//           dz = |player->+0x224 - e->+0xa4|;              eye Z vs feet Z
//           if (dz <= (player->+0x1e1 == 3 ? 0xc0 : 0x300))
//             return e;
//         }
//
// called once per frame out of `Render3DScene` (0x10017b60) and parked in
// `engine+0x61c`. The *action* side reads the same byte -- FUN_100646a8 is
//
//     if (entity->+0xd8 == 0) return false;
//     entity->vtable[0xa8]("OnUse");   // run the script handler
//     return true;
//
// so "no prompt" and "pressing Use does nothing" are one condition, not
// two. This port had `usable` on MonsterExecutable only, set only by an
// explicit `SetUsable(true)`, and that is wrong in one specific way that
// silences a third of the game's NPCs -- see kSetUseTextImpliesUsable.
//
// ---------------------------------------------------------------------
// Where the byte comes from
//
// 1. The constructor. The base entity (`FUN_10060d54`) zeroes `+0xd8`, and
//    exactly two of the seventeen factory arms (docs/ZONE_FORMAT.md) set
//    it back to 1: category 4, weapons (`FUN_1002ce9c`, `strb r?,[r4,#0xd8]`
//    at 0x1002cf6c) and category 9, consumables (`FUN_1002e78c`, at
//    0x1002e7f8). Nothing else -- misc loot, armour, spells, doors,
//    creatures and merchants all start unusable. See StartsUsable().
//
// 2. `SetUsable(b)` -- entity binding 30, dispatcher case 0x1e -- assigns
//    it, and additionally clears `engine+0x61c` if the entity being turned
//    off happened to be the current use target.
//
// 3. **`SetUseText(id)` -- and this is the missing rule.** The handler is
//    one instruction longer than it looks (0x10068418):
//
//        str  r1, [r0, #0xdc]     ; useTextId  = id
//        mov  r3, #1
//        strb r3, [r0, #0xd9]     ; hasUseText = 1
//        strb r3, [r0, #0xd8]     ; usable     = 1     <-- !!
//        bx   lr
//
//    Setting a use text *is* what makes a thing usable. That single
//    function is `vtable+0x8c` in **31 of the game's entity vtables** --
//    every class, from the base object through doors, items and creatures
//    -- so the rule is universal, not a creature quirk.
//
// 4. Death clears it: the creature damage path (0x10083c04) ends a fatal
//    hit with `strb r3(0),[sl,#0xd8]` at 0x10083e2c, beside the `+0xd5`
//    "dead" flag. A corpse offers no prompt.
//
// ---------------------------------------------------------------------
// Why it matters
//
// 54 of the 152 talkable creature/merchant placements in the shipped game
// -- 35% -- call `SetUseText(...)` and never call `SetUsable(true)`.
// Among them: Gravel Trothgar (azra's shop), Acolyte Menlin and Priestess
// Almathea (azra's two starting quests), Old Trinket, the four Azra
// villager prisoners, Heather, and every one of Dark Star West's four
// merchants. Under the old rule not one of them showed a prompt or
// responded to Use.
//
// The rest of the corpus corroborates the model from the other side: with
// this rule applied to *every* category, every category-11 door placement
// and every category-8 container placement that has a real script comes
// out usable (434 and 393 of them), and the only category-3 placement that
// does not is `crypt2/controller.s` -- an invisible logic object that is
// not supposed to be touchable. So the byte is the one gate, and no
// shipped door or chest loses its prompt by honouring it.
//
// ---------------------------------------------------------------------
// The prompt text itself
//
// `+0xdc` is a stringtable id and `+0xd9` says whether one was ever set.
// The reader (0x1006842c, `vtable+0x90`) is
//
//     if (entity->+0xd9)  text = stringTable[entity->+0xdc];
//     else                text = stringTable[13];
//
// and shipped string 13 is the literal word `"default"` -- a developer
// placeholder, which is another way of saying every real usable entity is
// expected to have called SetUseText. The ids the NPCs pass are just their
// names: 2063 "Gravel Trothgar", 2027 "Menlin", 177 "Almathea", 1199
// "Refugee", 835 "Heather".

namespace sk_bindings {

// The `SetUseText(id) => usable` rule above, named so the four binding
// classes that implement it can point at one place rather than repeating
// the disassembly four times.
constexpr bool kSetUseTextImpliesUsable = true;

// `stringTable[13]`, the engine's fallback when an entity is usable but
// never called SetUseText (0x1006842c's else branch). Shipped text:
// "default".
constexpr int kDefaultUseTextId = 13;

// The constructor default for `entity+0xd8`, by entities.txt category --
// true only for the two factory arms that write 1. Every other category,
// and every unknown one, starts unusable and has to be turned on by a
// script's own SetUsable(true)/SetUseText(...).
inline bool StartsUsable(int entityCategory) {
    return entityCategory == 4 || entityCategory == 9;
}

}  // namespace sk_bindings
