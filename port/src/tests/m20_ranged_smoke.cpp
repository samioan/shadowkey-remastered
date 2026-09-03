// M20 (ranged weapons) smoke test: proves ItemExecutable's real per-
// weapon SetRange() value (previously stored, never read back by
// anything -- see item_executable.h's range() comment) now genuinely
// drives attack range, and that a real bow's range is what actually lets
// it hit farther than a real melee weapon at the exact same geometry.
//
// Real scripts, read directly off the install image:
//   weapons/club.s:            SetRange(384);   SetWeaponType(WR_Blunt);
//   weapons/bandit_longbow.s:  SetRange(16384); SetBow(true);
//                              SetWeaponType(WR_LightBow);
// A corpus-wide grep (this session) found exactly two SetRange() values
// anywhere in the whole real weapon corpus -- 384 (65 melee weapons) and
// 16384 (16 bow/crossbow/thrown weapons) -- confirming this is a real,
// unambiguous signal, not an isolated pair of scripts.
#include <cstdio>
#include <memory>
#include <string>

#include "assets/string_table.h"
#include "simkin_bindings/combat.h"
#include "simkin_bindings/item_executable.h"
#include "simkin_bindings/menu_stack.h"
#include "skExecutableContext.h"
#include "skInterpreter.h"
#include "skParseException.h"
#include "skRValue.h"
#include "skRValueArray.h"
#include "skRuntimeException.h"

namespace {

std::unique_ptr<sk_bindings::ItemExecutable> LoadAndInit(const std::string& fullPath,
                                                           skInterpreter& interpreter,
                                                           sk_bindings::MenuStack& stack) {
    skExecutableContext loadCtxt(&interpreter);
    auto obj =
        std::make_unique<sk_bindings::ItemExecutable>(skString(fullPath.c_str()), loadCtxt, stack);
    skRValueArray args;
    args.append(skRValue(0));  // placeholder for Init's "(s)" parameter
    skRValue ret;
    skExecutableContext callCtxt(&interpreter);
    obj->method(skString("Init"), args, ret, callCtxt);
    return obj;
}

}  // namespace

int main(int argc, char** argv) {
    const char* scriptRoot =
        argc > 1 ? argv[1]
                  : "The-Elder-Scrolls-Travels-Shadowkey_N-Gage_EN-FR-DE-ES-IT_USA-Europe-"
                    "EnFrDeEsIt-26102004/system/apps/6r51";

    sk::StringTable strings;
    if (!strings.Load(std::string(scriptRoot) + "/stringtable.eng")) {
        std::printf("m20_ranged_smoke: FAILED to load stringtable.eng\n");
        return 1;
    }

    skInterpreter interpreter;
    sk_bindings::MenuStack stack(scriptRoot, interpreter, &strings);
    bool ok = true;

    // --- Part 1: real Init() values ---
    std::unique_ptr<sk_bindings::ItemExecutable> club, bow;
    try {
        club = LoadAndInit(std::string(scriptRoot) + "/weapons/club.s", interpreter, stack);
        bow = LoadAndInit(std::string(scriptRoot) + "/weapons/bandit_longbow.s", interpreter, stack);
    } catch (skParseException& e) {
        std::printf("m20_ranged_smoke: FAILED -- PARSE ERROR: %s\n", e.toString().ptr());
        return 1;
    } catch (skRuntimeException& e) {
        std::printf("m20_ranged_smoke: FAILED -- RUNTIME ERROR: %s\n", e.toString().ptr());
        return 1;
    }

    auto checkInt = [&](const char* label, int actual, int expected) {
        bool pass = actual == expected;
        std::printf("%s: %d (expected %d) %s\n", label, actual, expected, pass ? "OK" : "FAILED");
        if (!pass) ok = false;
    };
    auto checkBool = [&](const char* label, bool actual, bool expected) {
        bool pass = actual == expected;
        std::printf("%s: %s (expected %s) %s\n", label, actual ? "true" : "false",
                    expected ? "true" : "false", pass ? "OK" : "FAILED");
        if (!pass) ok = false;
    };
    checkInt("club.s range()", club->range(), 384);
    checkBool("club.s ranged()", club->ranged(), false);
    checkInt("bandit_longbow.s range()", bow->range(), 16384);
    checkBool("bandit_longbow.s ranged()", bow->ranged(), true);
    checkInt("bandit_longbow.s damageMin()", bow->damageMin(), 3);
    checkInt("bandit_longbow.s damageMax()", bow->damageMax(), 9);

    // --- Part 2: the payoff -- same geometry, different real range,
    // different in-range result. Attacker at origin facing +X (yaw=0), a
    // target 500 world units directly ahead: beyond club.s's real 384,
    // well within bandit_longbow.s's real 16384.
    bool clubMisses = !sk_bindings::InAttackRange(0.0f, 0.0f, 0.0f, 500.0f, 0.0f,
                                                   static_cast<float>(club->range()));
    std::printf(
        "target 500 units ahead, club.s's real range (384): out of range -- %s %s\n",
        clubMisses ? "true" : "false", clubMisses ? "OK" : "FAILED");
    if (!clubMisses) ok = false;

    bool bowHits = sk_bindings::InAttackRange(0.0f, 0.0f, 0.0f, 500.0f, 0.0f,
                                               static_cast<float>(bow->range()));
    std::printf("target 500 units ahead, bandit_longbow.s's real range (16384): in range -- %s %s\n",
                bowHits ? "true" : "false", bowHits ? "OK" : "FAILED");
    if (!bowHits) ok = false;

    // Facing cone still applies regardless of range -- a target directly
    // behind the attacker is never a valid target, bow or not.
    bool behindMisses = !sk_bindings::InAttackRange(0.0f, 0.0f, 0.0f, -500.0f, 0.0f,
                                                     static_cast<float>(bow->range()));
    std::printf("target 500 units directly behind, bow range: still out of the facing cone -- %s "
                "%s\n",
                behindMisses ? "true" : "false", behindMisses ? "OK" : "FAILED");
    if (!behindMisses) ok = false;

    std::printf("\nm20_ranged_smoke: %s\n", ok ? "OK" : "FAILED");
    return ok ? 0 : 1;
}
