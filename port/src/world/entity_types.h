#pragma once

// Loads entities.txt (system/apps/6r51/entities.txt), a single global
// file mapping an entity typeId to its models.idx archive index and
// coarse category enum -- docs/ZONE_FORMAT.md's "Where the type-
// descriptor tree itself comes from: entities.txt" section. Sourced once
// at startup in the real engine (EntityTypeConfig_Load, called from
// GameEngine_FirstTickBootstrap) -- not per-zone, matched here by owning
// one EntityTypeTable for the whole process rather than per-Zone.

#include <cstdint>
#include <map>
#include <string>

namespace sk {

struct EntityTypeDescriptor {
    int32_t modelArchiveIndex = -1;
    int32_t category = 0;
    std::string name;
};

// M72 -- the real creature-height table, transcribed from the binary.
//
// Every creature in the game is entities.txt category 2, so they all share
// one C++ class (the factory at 0x1002aa14 news `0x330` bytes and runs
// 0x100815e0 for category 2 -> vtable 0x100fe2b8). That class overrides the
// entity `Height()` virtual (vtable slot 0x108) with 0x1008679c, which is
// nothing but a switch on the creature's own **entities.txt model index**
// (`EntityTypeDescriptor_Lookup(...)->modelArchiveIndex`, this struct's
// first field):
//
//     sub r3, r3, #0x12        ; index -= 18
//     cmp r3, #0x32            ; 51 cases, model indices 18..68
//     ldrls pc, [pc, r3, lsl #2]
//     ...
//     100868a8  mov r0, #0x100  ; 256 -- models 18, 55, 56, 66, 68
//     100868b4  mov r0, #0x200  ; 512 -- every other case, the default,
//     100868c0  mov r0, #0x200  ;        and the descriptor-not-found path
//
// Those five short-model entries are, from entities.txt: 18 = every rat,
// 55 = every spider, 56 = the wormmouths, 66 = the stingers/rays, 68 =
// every wolf. Bandits (22, 23), mages, skeletons and the rest of the
// humanoids all fall through to 512.
//
// It is a *collision* height (the engine uses Height() for the `z .. z +
// Height()` band in its vertical hit tests), not the drawn model's extent
// -- and it is the only per-creature size the engine has. The automatic
// aim-assist pitch is gated on it; see main.cpp's auto-aim block.
inline int32_t MonsterCollisionHeight(int32_t modelArchiveIndex) {
    switch (modelArchiveIndex) {
        case 18:  // rats
        case 55:  // spiders
        case 56:  // wormmouths
        case 66:  // stingers / rays
        case 68:  // wolves
            return 0x100;
        default:
            return 0x200;
    }
}

class EntityTypeTable {
public:
    // scriptRoot e.g. ".../system/apps/6r51" -- reads entities.txt from it.
    bool Load(const std::string& scriptRoot);

    // Returns nullptr if typeId has no entry. Note typeId==1 has its own
    // entities.txt line but is never actually looked up here -- the real
    // engine (and this port) special-cases it as the player-start marker
    // in .ent records before any type-descriptor lookup happens
    // (docs/ZONE_FORMAT.md).
    const EntityTypeDescriptor* Lookup(int32_t typeId) const;

    // M68: the whole table, for tools that need to enumerate rather than
    // look one up -- the debug suite's `catalog entities`. Read-only, and
    // nothing in the game itself calls it.
    const std::map<int32_t, EntityTypeDescriptor>& all() const { return byTypeId_; }

private:
    std::map<int32_t, EntityTypeDescriptor> byTypeId_;
};

}  // namespace sk
