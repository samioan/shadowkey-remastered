#include "simkin_bindings/weapon_viewmodel.h"

#include <algorithm>

namespace sk_bindings {

void StartWeaponSwing(WeaponViewmodel& vm, ItemExecutable* item) {
    if (!item || item->weaponSprite() < 0) return;
    // M35: a swing already in flight is not restarted. The real draw
    // function's swing branch is gated on its progress accumulator
    // (WeaponViewState +0x234) being non-zero, and the branch only
    // *advances* that accumulator -- there is no path through it that
    // resets the pose to frame 0 while it is still running. Without this
    // guard, holding or tapping the attack key re-entered frame 0 every
    // press, so the animation restarted continuously and never visibly
    // played. The post-swing Hold is deliberately still interruptible:
    // that is the recovery pose, and cutting it short to begin the next
    // swing is what makes repeated attacks feel connected.
    if (vm.item == item && vm.phase == WeaponViewmodel::Phase::Swinging) return;
    vm.item = item;
    vm.phase = WeaponViewmodel::Phase::Swinging;
    vm.frame = 0;
    vm.ticksInPhase = kViewmodelFrameTicks;
}

void TickWeaponViewmodel(WeaponViewmodel& vm) {
    if (!vm.item) return;
    switch (vm.phase) {
        case WeaponViewmodel::Phase::Swinging:
            if (--vm.ticksInPhase <= 0) {
                ++vm.frame;
                vm.ticksInPhase = kViewmodelFrameTicks;
                if (vm.frame >= (std::max)(1, vm.item->animationFrames())) {
                    vm.phase = WeaponViewmodel::Phase::Hold;
                    vm.ticksInPhase = kViewmodelHoldTicks;
                }
            }
            break;
        case WeaponViewmodel::Phase::Hold:
            if (--vm.ticksInPhase <= 0) vm.phase = WeaponViewmodel::Phase::Idle;
            break;
        case WeaponViewmodel::Phase::Idle:
            ++vm.idleSwayTick;
            break;
    }
}

}  // namespace sk_bindings
