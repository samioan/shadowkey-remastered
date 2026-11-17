#pragma once

// M140 -- the trap entity (entities.txt category 10).
//
// Three typeIds, all Loth'Na's and all placed in lothcav (34) and broken1
// (5 spike traps): 1011 `ceilingmasher.s`, 1012 `steamtrap.s`, 1013
// `spiketrap.s`. Category 10 is its own class -- `FUN_1002aa14`'s arm
// allocates 0x26c bytes and runs FUN_1009037c -- and the port drew every one
// as static scenery and never ran its script. So `SetTrapType`/`SetActive`
// went nowhere, nothing moved, and M38 stood a box test in for the tick.
//
// The class's script natives are not in any of the 28 binding tries: its
// method override (FUN_1008fd28) `wcscmp`s three names and hands everything
// else to the actor base (FUN_10003810):
//
//   SetTrapType(n)     +0x230 = n; n == 1 also sets the direction +0x240 = -1
//   SetAttackRange(n)  +0x226 (default 0x9b)
//   SetActive(b)       +0x224
//
// and its tick is vtable +0x1c, FUN_1008ff14, transcribed in TickTrapEntity
// below. What it amounts to:
//
//   * Every 0x40 delta units, if the trap is not sprung and its re-arm
//     countdown has run out: the player within `range` (the scaled-squared
//     distance, `dx*dx>>8 + dy*dy>>8`, so 155 is ~199 world units) gets
//     FUN_1003e438(player, 20) -- the disarm roll, here a dodge -- and if
//     that fails the zone's trigger list is told (mode 2). A trigger that
//     fires (damage landed) springs the trap and remembers its height. The
//     countdown restarts at 7 << 8 whether or not anything fired.
//   * A sprung trap steps its animation every 0x2e units *of that same
//     0x40 cadence* -- slow, by the engine's own arithmetic. Type 2 (spikes,
//     direction +1) rises for the first half of a 1000-unit cycle and snaps
//     back; type 1 (the masher, direction -1, two steps a window) falls and
//     climbs back to where it was, playing sound 0x3d each step. Any other
//     type (3, the steam vent) never steps -- and so never un-springs: a
//     steam vent fires once per visit.
//   * A trap the trigger *disarmed* is switched off (FUN_100902a8(trap, 0)
//     from inside FUN_10090818), for good.

#include <cstdint>
#include <functional>

#include "skScriptedExecutable.h"

namespace sk_bindings {

struct TrapEntityState {
    bool active = false;       // +0x224
    bool sprung = false;       // +0x225
    int16_t range = 0x9b;      // +0x226, SetAttackRange
    int rearmCountdown = 0;    // +0x228
    int rearmSeconds = 7;      // +0x22c
    int16_t trapType = 0;      // +0x230, SetTrapType
    int animTime = 0;          // +0x234
    int animDuration = 1000;   // +0x238
    int restZ = 0;             // +0x23c
    int direction = 1;         // +0x240
    int animAccum = 0;         // +0x244
    int checkAccum = 0;        // +0x248
    int16_t z = 0;             // the entity's own +0xa4
};

struct TrapTickHooks {
    // `vtable[0x5c](self, player)`: the scaled-squared distance.
    std::function<int()> distanceToPlayer;
    // FUN_1003e438(player, 0x14) -- true means the player got clear.
    std::function<bool()> playerAvoids;
    // FUN_1007307c(triggers, 2, self): true if a trigger fired.
    std::function<bool()> notifyTriggers;
    // `FUN_1001b204(engine, 0x3d, 100, 0, 0, 1, 1)`, the masher's sound.
    std::function<void()> playMasherSound;
};

constexpr int kTrapMasherSound = 0x3d;

// FUN_1008ff14, one call per entity tick.
void TickTrapEntity(TrapEntityState& trap, int frameDelta, const TrapTickHooks& hooks);

// The category-10 object a placement's script runs against. Owns the
// state the tick reads; everything that is not one of the class's three
// natives falls through to the script's own methods.
class TrapEntityExecutable : public skScriptedExecutable {
public:
    TrapEntityExecutable(const skString& filename, skExecutableContext& ctxt)
        : skScriptedExecutable(filename, ctxt) {}

    bool method(const skString& methodName, skRValueArray& args, skRValue& returnValue,
                skExecutableContext& context) override;

    TrapEntityState& state() { return m_State; }
    const TrapEntityState& state() const { return m_State; }

private:
    TrapEntityState m_State;
};

}  // namespace sk_bindings
