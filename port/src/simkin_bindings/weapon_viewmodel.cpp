#include "simkin_bindings/weapon_viewmodel.h"

#include <algorithm>

namespace sk_bindings {

void StartWeaponSwing(WeaponViewmodel& vm, ItemExecutable* item) {
    if (!item || item->weaponSprite() < 0) return;
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
