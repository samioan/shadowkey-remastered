// M78 smoke test: the player's attack, wired to its own swing.
//
// Reported: the swing animation is slow and unrelated to the attack; some
// swings show a different weapon on the last frame; the sound and the
// damage fire once per keypress regardless of the swing; there is no
// cooldown at all; and an empty hand attacks. Five symptoms, all of them
// the same missing function.
//
// `FUN_100425bc` -- player vtable +0x288, the attack -- opens with a
// three-part gate before it does anything else:
//
//     if (0 < player->+0xf48) { player->+0xf48 -= frameDelta(); return; }
//     player->+0xf48 = (s16)stats->+0x1c;                 // Speed
//     if (player->+0x204 && player->+0x204->+0x1a7)       // SetRange > 0x400
//         player->+0xf48 = (s16)stats->+0x1c * 6;
//     if (0 < player->+0x238) return;                     // weapon swap
//     if (0 < player->+0x234) return;                     // swing in flight
//
// and only then spends the 4 fatigue, starts the swing, searches for a
// target, plays a sound and applies damage -- all in one call. `+0x1c` is
// the Speed attribute: `FUN_10048244`'s `TestSpeed` (trie index 0xf) and
// `GetSpeed` (0x20) both read that short, next to Agility at `+0x18` and
// Endurance at `+0x1e`.
//
// Its only caller is `FUN_10042394` (player vtable +0x280), "use the item
// in this hand", whose entire body is inside `if (param_2 != 0)` -- so an
// empty hand does nothing, and the port's bare-fisted attack was invented.
// The input handler calls it once per tick for each attack key that is
// **held** (`InputState_GetBoundButton(input, 0xf)` / `0xe`), which is what
// makes the cooldown a real-time one.
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "assets/string_table.h"
#include "simkin_bindings/game_constants.h"
#include "simkin_bindings/item_executable.h"
#include "simkin_bindings/level_executable.h"
#include "simkin_bindings/menu_stack.h"
#include "simkin_bindings/player_executable.h"
#include "simkin_bindings/vitals.h"
#include "simkin_bindings/weapon_viewmodel.h"
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
    std::printf("%-76s %s\n", what.c_str(), ok ? "OK" : "FAILED");
    if (!ok) ++g_failures;
}

std::string Join(const std::vector<int>& v) {
    std::string s;
    for (size_t i = 0; i < v.size(); ++i) {
        if (i) s += ",";
        s += std::to_string(v[i]);
    }
    return s;
}

// One tick of the real loop for a player holding an attack key: the gate,
// and -- when it lets the tick through -- the swing that gate will then
// refuse on. Returns true if this tick actually attacked.
bool HeldAttackTick(sk_bindings::WeaponViewmodel& vm, sk_bindings::ItemExecutable* weapon,
                    int speed) {
    bool attacked = sk_bindings::CheckPlayerAttackGate(
                        vm, weapon, speed, sk_bindings::kViewmodelFrameDeltaUnits) ==
                    sk_bindings::PlayerAttackGate::Allowed;
    if (attacked) sk_bindings::StartWeaponSwing(vm, weapon);
    sk_bindings::TickWeaponViewmodel(vm, 0);
    return attacked;
}

}  // namespace

int main(int argc, char** argv) {
    using Item = sk_bindings::ItemExecutable;
    using VM = sk_bindings::WeaponViewmodel;
    using Gate = sk_bindings::PlayerAttackGate;

    const char* scriptRoot =
        argc > 1 ? argv[1]
                  : "The-Elder-Scrolls-Travels-Shadowkey_N-Gage_EN-FR-DE-ES-IT_USA-Europe-"
                    "EnFrDeEsIt-26102004/system/apps/6r51";

    sk::StringTable strings;
    strings.Load(std::string(scriptRoot) + "/stringtable.eng");
    sk::EntityTypeTable entityTypes;
    if (!entityTypes.Load(scriptRoot)) {
        std::printf("m78_player_attack_smoke: FAILED to load entities.txt\n");
        return 1;
    }

    skInterpreter interpreter;
    sk_bindings::MenuStack stack(scriptRoot, interpreter, &strings);
    stack.level().SetEntityTypes(&entityTypes);

    auto loadItem = [&](const std::string& rel) -> std::unique_ptr<Item> {
        skExecutableContext loadCtxt(&interpreter);
        try {
            auto it = std::make_unique<Item>(
                skString((std::string(scriptRoot) + "/" + rel).c_str()), loadCtxt, stack);
            skRValueArray args;
            args.append(skRValue(0));
            skRValue ret;
            skExecutableContext callCtxt(&interpreter);
            it->method(skString("Init"), args, ret, callCtxt);
            return it;
        } catch (skParseException& e) {
            std::printf("  (parse error loading %s: %s)\n", rel.c_str(), e.toString().ptr());
            return nullptr;
        } catch (skRuntimeException& e) {
            std::printf("  (runtime error loading %s: %s)\n", rel.c_str(), e.toString().ptr());
            return nullptr;
        }
    };

    std::printf("=== M78: the player's attack, wired to its own swing ===\n\n");

    std::unique_ptr<Item> club = loadItem("weapons/club.s");
    std::unique_ptr<Item> bow = loadItem("weapons/bandit_longbow.s");
    std::unique_ptr<Item> spell = loadItem("spells/blind.s");
    Check(club && bow && spell, "weapons/club.s, weapons/bandit_longbow.s, spells/blind.s load");
    if (!club || !bow || !spell) {
        std::printf("\nm78_player_attack_smoke: FAILED\n");
        return 1;
    }

    // ---- 1. The cadence (`player+0xf48`) ----
    //
    // Seeded from the Speed stat, decremented by the frame delta, and the
    // seed happens on the way *through* the gate, not on the way out.
    {
        VM vm;
        Check(vm.attackCooldown == 0, "a fresh player has no attack cooldown standing");
        Check(sk_bindings::CheckPlayerAttackGate(vm, club.get(), 50, 10) == Gate::Allowed,
              "the first attack is allowed");
        Check(vm.attackCooldown == 50, "...and seeds the cadence to the Speed stat (50)");
        Check(sk_bindings::CheckPlayerAttackGate(vm, club.get(), 50, 10) == Gate::CoolingDown,
              "the very next tick is refused by the cadence");
        Check(vm.attackCooldown == 40, "...which drains by the frame delta, not to zero");
        for (int i = 0; i < 4; ++i) sk_bindings::CheckPlayerAttackGate(vm, club.get(), 50, 10);
        Check(vm.attackCooldown == 0, "five drains empty it");
        Check(sk_bindings::CheckPlayerAttackGate(vm, club.get(), 50, 10) == Gate::Allowed,
              "the sixth tick attacks again -- Speed 50 is one attack per 6 ticks");
    }
    {
        // The stat is read live, so a Drain/Fortify on Speed retimes the
        // player's attacks. Speed 100 is *slower*, which is what the code
        // says: the seed is the attribute itself, not its reciprocal.
        VM vm;
        sk_bindings::CheckPlayerAttackGate(vm, club.get(), 100, 10);
        Check(vm.attackCooldown == 100, "Speed 100 seeds 100 -- a higher Speed attacks less often");
        VM vm2;
        sk_bindings::CheckPlayerAttackGate(vm2, club.get(), 10, 10);
        Check(vm2.attackCooldown == 10, "Speed 10 seeds 10");
    }
    {
        // `if (player->0x204 && player->0x204->0x1a7)` -- and `0x1a7` is
        // SetRange above 0x400, not the SetBow/SetThrowingWeapon pair the
        // swing seed tests. Every shipped bow/crossbow/thrown weapon is
        // SetRange(16384) and every melee weapon SetRange(384), so the two
        // agree in the corpus while being different fields.
        Check(!club->usesRangedPath() && bow->usesRangedPath(),
              "club is not on the ranged path (SetRange 384); the longbow is (16384)");
        VM vm;
        sk_bindings::CheckPlayerAttackGate(vm, bow.get(), 50, 10);
        Check(vm.attackCooldown == 300, "a ranged weapon multiplies the cadence by 6 (50 -> 300)");
        VM vm2;
        sk_bindings::CheckPlayerAttackGate(vm2, nullptr, 50, 10);
        Check(vm2.attackCooldown == 50, "an absent active weapon takes the unmultiplied seed");
    }

    // ---- 2. The two busy gates, and the order they sit in ----
    {
        VM vm;
        sk_bindings::StartWeaponSwing(vm, club.get());
        Check(sk_bindings::CheckPlayerAttackGate(vm, club.get(), 50, 10) == Gate::Swinging,
              "a swing in flight refuses the attack (`0 < +0x234`)");
        Check(vm.attackCooldown == 50,
              "...but the cadence was still re-seeded first -- the seed is above both tests");
    }
    {
        VM vm;
        sk_bindings::NotifyWeaponChanged(vm, club.get());
        Check(sk_bindings::CheckPlayerAttackGate(vm, club.get(), 50, 10) == Gate::Swapping,
              "a weapon swap on screen refuses the attack (`0 < +0x238`)");
    }

    // ---- 3. What a held attack key actually produces ----
    //
    // The whole of the reported "I can attack as fast as I can press it".
    // With the gate in place, holding the key through 300 ticks (12 seconds
    // at 25Hz) gives one attack per swing, and the swing is the limit --
    // the Speed-50 cadence is six ticks and never the binding constraint
    // for a melee weapon.
    {
        VM vm;
        std::vector<int> gaps;
        int last = -1;
        for (int tick = 0; tick < 300; ++tick) {
            if (HeldAttackTick(vm, club.get(), 50)) {
                if (last >= 0) gaps.push_back(tick - last);
                last = tick;
            }
        }
        bool spaced = !gaps.empty();
        for (int g : gaps) spaced = spaced && g == 48;
        Check(spaced, "holding attack with a club: one swing every 48 ticks (got " + Join(gaps) +
                          ")");
        // 45 ticks of swing (1792 / 40, rounded up), rounded up again to
        // the next multiple of the cadence: the re-seed sits *above* the
        // swing test, so every sixth tick spent waiting on the swing puts
        // another Speed's worth of cooldown back on the clock, and the
        // first tick that finds an empty accumulator has just re-armed it.
        // 6 * 8 = 48, or 1.92 seconds -- the player's real melee rate with
        // Speed 50.
        Check(gaps.size() == 6, "...6 swings in 12 seconds");
    }
    {
        // A bow's swing is only 1024 units (13 ticks of drawing, 26 to
        // drain), so the x6 cadence is what limits it instead: 300 units at
        // 10 a tick is 30 ticks, and the swing has run out by then.
        VM vm;
        std::vector<int> gaps;
        int last = -1;
        for (int tick = 0; tick < 300; ++tick) {
            if (HeldAttackTick(vm, bow.get(), 50)) {
                if (last >= 0) gaps.push_back(tick - last);
                last = tick;
            }
        }
        bool spaced = !gaps.empty();
        for (int g : gaps) spaced = spaced && g == 31;
        Check(spaced, "holding attack with a longbow: one shot every 31 ticks (got " + Join(gaps) +
                          ")");
        Check(gaps.size() == 9, "...a bow is cadence-limited, not swing-limited");
    }

    // ---- 4. The swing runs out before the attack is allowed again ----
    {
        VM vm;
        sk_bindings::StartWeaponSwing(vm, club.get());
        int drawn = 0, running = 0;
        while (vm.swinging() && running < 1000) {
            if (sk_bindings::ResolveViewmodelDraw(vm).visible) ++drawn;
            sk_bindings::TickWeaponViewmodel(vm, 0);
            ++running;
        }
        Check(running == 45, "a five-frame melee swing locks the attack out for 45 ticks");
        Check(drawn == 32, "...of which 32 draw a swing frame and the last 13 draw nothing");
        Check(!vm.swingVisible() && !vm.swinging(), "both queries agree once it has run out");
    }

    // ---- 5. The last frame is inside the weapon's own strip ----
    //
    // A melee weapon owns 16 consecutive global.spr slots: the shipped
    // bases are 0/33/72/88/104/120/136 for melee and 163/169/175 for
    // ranged, and 72 -> 88 -> 104 -> 120 -> 136 is a clean run of 16. The
    // club's are 88 idle plus three five-frame variants at 89-93, 94-98 and
    // 99-103; the artifact frame for variant 10 was 104, which is the *next
    // weapon's* idle pose.
    {
        const int variants[3] = {0, 5, 10};
        for (int v = 0; v < 3; ++v) {
            VM vm;
            sk_bindings::StartWeaponSwing(vm, club.get());
            vm.swingVariant = variants[v];
            std::vector<int> slots;
            for (int guard = 0; guard < 1000 && vm.swingAccum != 0; ++guard) {
                sk_bindings::ViewmodelDraw d = sk_bindings::ResolveViewmodelDraw(vm);
                if (d.visible && (slots.empty() || slots.back() != d.spriteSlot)) {
                    slots.push_back(d.spriteSlot);
                }
                sk_bindings::TickWeaponViewmodel(vm, 0);
            }
            bool inStrip = !slots.empty() && slots.front() >= 89 && slots.back() <= 103;
            Check(inStrip && slots.size() == 5,
                  "club variant " + std::to_string(variants[v]) +
                      " stays inside its own 16-slot strip: " + Join(slots));
        }
    }

    // ---- 6. An empty hand, and the item-type switch in front of it ----
    //
    // `FUN_1006d508` returns 1 for a weapon, 2 for a spell, 4 for a
    // consumable; `FUN_10042394` attacks only on 1, casts on 2, uses on 4,
    // and does nothing at all for a null item.
    {
        Check(club->itemType() == sk_bindings::kItemTypeWeapon,
              "weapons/club.s reports kItemTypeWeapon -- the only type that attacks");
        Check(spell->itemType() != sk_bindings::kItemTypeWeapon && spell->spellTypeId() != 0,
              "spells/blind.s is not a weapon and does have a spell type -- it casts instead");
        Check(spell->weaponSprite() < 0 && !sk_bindings::StartWeaponSwing(VM(), spell.get()),
              "...and has no viewmodel art, so it cannot swing");
    }
    {
        // The cast's own swing (`FUN_10042394` case 2): the melee seed,
        // from whatever weapon is on screen, with no gate in front of it.
        VM vm;
        sk_bindings::StartSpellSwing(vm);
        Check(vm.swingAccum == 0, "a cast with no weapon on screen starts no swing");
        vm.item = club.get();
        sk_bindings::StartSpellSwing(vm);
        Check(vm.swingAccum == ((club->animationFrames() + 2) << 8),
              "a cast with a club on screen swings it -- (5+2)<<8, the melee seed");
        int mid = vm.swingAccum;
        sk_bindings::TickWeaponViewmodel(vm, 0);
        sk_bindings::StartSpellSwing(vm);
        Check(vm.swingAccum == mid,
              "...and a second cast restarts it mid-flight -- the cast path has no swing gate");
    }

    // ---- 7. The fatigue is behind the gate ----
    //
    // 4 points, spent on the far side of the three tests, so a refused
    // press is free. This port used to bill every keypress.
    {
        VM vm;
        int fatigue = 100;
        int attacks = 0;
        for (int tick = 0; tick < 49; ++tick) {
            if (HeldAttackTick(vm, club.get(), 50)) {
                ++attacks;
                fatigue -= sk_bindings::kAttackFatigueCost;
            }
        }
        Check(attacks == 2 && fatigue == 92,
              "holding attack for 49 ticks swings twice and costs 8 fatigue, not 196");
    }

    std::printf("\nm78_player_attack_smoke: %s (%d failure(s))\n", g_failures == 0 ? "OK" : "FAILED",
                g_failures);
    return g_failures == 0 ? 0 : 1;
}
