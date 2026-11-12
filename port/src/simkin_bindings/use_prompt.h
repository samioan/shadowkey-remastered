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

// ---------------------------------------------------------------------
// M131: the search itself, `FUN_1001dd40`, transcribed.
//
// Twelve rays leave the player (its position clamped a tile inside the
// map), at `heading + 0x600` down to `heading - 0x500` in steps of 0x100
// -- a fan about eight degrees either side of dead ahead. A ray's step is
// `sin >> 1`, half a tile, and it takes six of them: the tile the player
// stands in, then out to two and a half tiles. Every round visits all
// twelve rays, the `heading - 0x500` one first; each looks at the tile it
// is in:
//
//   * a usable entity there, within the height window, **is the answer**
//     -- the first one any ray meets, nearest round first;
//   * a wall tile there (`.zmp` bit 2) **ends the whole search with
//     nothing**, not just that ray. Entities are tested first, which is
//     how a door standing in a wall tile is still found.
//
// `isWall(tx, ty)` and `usableAt(tx, ty)` are the caller's: the second
// returns an id >= 0 for the tile's first usable entity (the `+0xd8` and
// height tests are its business) or -1.
int SwaySine(int index2048);  // weapon_viewmodel.h: the engine's sine table

constexpr int kUseRayCount = 12;
constexpr int kUseRaySteps = 6;
constexpr int kUseHeightWindow = 0x300;      // |player+0x224 - entity+0xa4|
constexpr int kUseHeightWindowDead = 0xc0;   // ... when `player+0x1e1 == 3`

template <class WallFn, class UsableFn>
int FindUseTarget(int playerX, int playerY, int headingUnits, int mapWidth, int mapHeight,
                  WallFn isWall, UsableFn usableAt) {
    auto clampInside = [](int v, int tiles) {
        const int hi = (tiles - 2) * 0x100;
        if (v < 0x100) v = 0x100;
        return v <= hi ? v : hi;
    };
    struct Ray {
        int x, y, dx, dy, steps;
        bool live;
    };
    Ray rays[kUseRayCount];
    const int startX = clampInside(playerX, mapWidth);
    const int startY = clampInside(playerY, mapHeight);
    for (int i = 0; i < kUseRayCount; ++i) {
        // Slot 0 is the last one the engine fills: heading - 0x500.
        const int angle = headingUnits - 0x500 + i * 0x100;
        rays[i] = Ray{startX, startY, SwaySine((angle >> 5) & 0x7ff) >> 1,
                      SwaySine(((angle + 0x4000) >> 5) & 0x7ff) >> 1, 0, true};
    }
    int live = kUseRayCount;
    while (live > 0) {
        for (Ray& ray : rays) {
            if (!ray.live) continue;
            const int tx = ray.x >> 8, ty = ray.y >> 8;
            if (tx <= 0 || ty <= 0 || tx >= mapWidth - 1 || ty >= mapHeight - 1) {
                ray.live = false;
                --live;
                continue;
            }
            const int found = usableAt(tx, ty);
            if (found >= 0) return found;
            if (isWall(tx, ty)) return -1;
            ray.x += ray.dx;
            ray.y += ray.dy;
            if (++ray.steps >= kUseRaySteps) {
                ray.live = false;
                --live;
            }
        }
    }
    return -1;
}

// ---------------------------------------------------------------------
// M131: the prompt, `FUN_1002acd4` -- drawn every frame for whatever the
// search parked in `engine+0x61c`, between the viewmodel and the HUD.
//
//     text = entity->useText()                      // vtable slot, above
//     if (!entity->isMonster())           text += " ";            white
//     else {
//         if (entity->+0x2f0 && entity->isDead())   text = "Examine:  " + text;
//         else                                      text += " ";
//         if (!entity->+0x1e2)            colour = (0xed, 0xf9, 0x00);
//     }
//     FUN_100231b4(text, 10, 0x1e, 0xa6, ..., italic, centred, wrap 25)
//
// `+0x2f0` is the creature's loot typeId and `+0x1e2` its invulnerable
// flag: a creature you could fight is named in yellow, one you cannot --
// every townsperson -- in white, and a lootable corpse says "Examine:".
// There is no health readout; this port's "name  12/30" line was its own.
constexpr int kUsePromptX = 10;
constexpr int kUsePromptY = 0x1e;
constexpr int kUsePromptRight = 0xa6;
constexpr int kUsePromptWrapChars = 0x19;
constexpr int kUsePromptHostileR = 0xed, kUsePromptHostileG = 0xf9, kUsePromptHostileB = 0x00;

}  // namespace sk_bindings

#include <string>

namespace sk_bindings {

inline std::string UsePromptText(const std::string& useText, bool isMonster, bool hasLoot,
                                 bool isDead) {
    if (isMonster && hasLoot && isDead) return "Examine:  " + useText;
    return useText + " ";
}
inline bool UsePromptIsHostileColour(bool isMonster, bool invulnerable) {
    return isMonster && !invulnerable;
}

}  // namespace sk_bindings
