#include "simkin_bindings/monster_executable.h"

#include <cstdio>

#include "assets/string_table.h"
#include "simkin_bindings/menu_stack.h"
#include "simkin_bindings/native_binding_common.h"
#include "simkin_bindings/player_executable.h"
#include "skExecutableContext.h"
#include "skParseException.h"
#include "skRValue.h"
#include "skRValueArray.h"
#include "skRuntimeException.h"

namespace sk_bindings {

MonsterExecutable::MonsterExecutable(const skString& filename, skExecutableContext& ctxt,
                                      const sk::StringTable* strings, PlayerExecutable& player,
                                      MenuStack& stack)
    : skScriptedExecutable(filename, ctxt),
      m_Strings(strings),
      m_Player(player),
      m_Stack(stack),
      m_Interpreter(ctxt.getInterpreter()) {}

std::string MonsterExecutable::name() const {
    if (m_Strings && m_NameId >= 0) return m_Strings->Get(m_NameId);
    return m_Id.empty() ? std::string("?") : m_Id;
}

void MonsterExecutable::ApplyDamage(int amount) {
    // m_Invulnerable (M16): real essential-NPC scripts (Tanyin Aldwyr and
    // the other named quest NPCs) call SetInvulnerable(true) in Init() --
    // honoring it here means main.cpp's melee target selection doesn't
    // even need its own separate check for the common case, though it
    // still skips them too (see main.cpp) so they're never shown as a
    // combat target in the first place.
    if (!m_Alive || amount <= 0 || m_Invulnerable) return;
    m_CurrentHealth -= amount;
    if (m_CurrentHealth <= 0) {
        m_CurrentHealth = 0;
        m_Alive = false;
    }
}

void MonsterExecutable::InvokeOnUse() {
    if (!m_Interpreter) return;
    skRValueArray args;
    args.append(skRValue(0));  // placeholder for OnUse's "(s)" parameter, same convention every
                                // Init(s)/OnUse(s)/OnKilled(s) caller in this codebase already
                                // uses (main.cpp's zone-load block, etc.) -- M17 found this one
                                // and InvokeOnKilled() below had drifted from it (empty args),
                                // latent until a real script actually referenced its own `s`
                                // parameter on a path this port hadn't exercised before.
    skRValue ret;
    skExecutableContext ctxt(m_Interpreter);
    try {
        method(skString("OnUse"), args, ret, ctxt);
    } catch (skParseException& e) {
        std::printf("MonsterExecutable: PARSE ERROR in OnUse(): %s\n", e.toString().ptr());
    } catch (skRuntimeException& e) {
        std::printf("MonsterExecutable: RUNTIME ERROR in OnUse(): %s\n", e.toString().ptr());
    }
}

void MonsterExecutable::InvokeOnKilled() {
    // azra_rat.s's OnKilled body: `if (GetPlayer().QuestSolved(0)) {
    // return; } else { GetPlayer().AddMonsterKilled(203); if
    // (GetPlayer().MonstersKilled(203) >= 8) { ...if(not
    // GetPlayer().QuestSolved(0)) { if(s=false){Delay(2,0);} }
    // Trthgar=Level.GetEntity("trthgar"); Trthgar.SetPositionMirror(...);
    // ... SetQuestSolved(0,true); } }`. M17: QuestSolved/
    // AddMonsterKilled/MonstersKilled are now real state on
    // PlayerExecutable (see its class comment) -- the kill counter
    // genuinely reaches 8 real rat kills. But `SetQuestSolved(0,true)`
    // itself is still never reached (confirmed empirically, `quest_smoke`
    // test): `Level.GetEntity("trthgar")` throws first -- no `Level`/
    // `GameEngine`-root global object exists in this port at all yet (a
    // separate, much larger, pre-existing gap -- `docs/
    // SIMKIN_NATIVE_API.md`'s "Zone/Level" class, ~567 real scripts
    // reference it) -- and that exception aborts the rest of the
    // method's real script execution, including the `SetQuestSolved`
    // line 3 statements later. So quest id 0 stays unsolved even after 8
    // kills until `Level`/`GetEntity` exists; `InvokeOnKilled()`'s
    // existing try/catch already logs and swallows the exception cleanly
    // rather than crashing, same as any other script error this
    // host-triggered call might hit.
    if (!m_Interpreter) return;
    skRValueArray args;
    args.append(skRValue(0));  // placeholder for OnKilled's "(s)" parameter, see InvokeOnUse()'s
                                // comment above -- this call site had the same missing-argument
                                // bug, found by the same M17 test.
    skRValue ret;
    skExecutableContext ctxt(m_Interpreter);
    try {
        method(skString("OnKilled"), args, ret, ctxt);
    } catch (skParseException& e) {
        std::printf("MonsterExecutable: PARSE ERROR in OnKilled(): %s\n", e.toString().ptr());
    } catch (skRuntimeException& e) {
        std::printf("MonsterExecutable: RUNTIME ERROR in OnKilled(): %s\n", e.toString().ptr());
    }
}

bool MonsterExecutable::method(const skString& methodName, skRValueArray& args,
                                skRValue& returnValue, skExecutableContext& context) {
    if (methodName == skString("SetAggressive") && args.entries() == 1) {
        m_Aggressive = args[0].boolValue();
        return true;
    }
    if (methodName == skString("SetName") && args.entries() == 1) {
        m_NameId = args[0].intValue();
        return true;
    }
    if (methodName == skString("SetID") && args.entries() == 1) {
        m_Id = ToStdString(args[0].str());
        return true;
    }
    if (methodName == skString("SetExpWorth") && args.entries() == 1) {
        m_ExpWorth = args[0].intValue();
        return true;
    }
    if (methodName == skString("SetAttack") && args.entries() == 1) {
        m_Attack = args[0].intValue();
        return true;
    }
    if (methodName == skString("SetDefense") && args.entries() == 1) {
        m_Defense = args[0].intValue();
        return true;
    }
    if (methodName == skString("SetSpellcast") && args.entries() == 1) {
        m_Spellcast = args[0].intValue();
        return true;
    }
    if (methodName == skString("SetMagicResistance") && args.entries() == 1) {
        m_MagicResistance = args[0].intValue();
        return true;
    }
    if (methodName == skString("SetDamageMin") && args.entries() == 1) {
        m_DamageMin = args[0].intValue();
        return true;
    }
    if (methodName == skString("SetDamageMax") && args.entries() == 1) {
        m_DamageMax = args[0].intValue();
        return true;
    }
    if (methodName == skString("SetArmorValue") && args.entries() == 1) {
        m_ArmorValue = args[0].intValue();
        return true;
    }
    if (methodName == skString("SetMaxHealth") && args.entries() == 1) {
        m_MaxHealth = args[0].intValue();
        m_CurrentHealth = m_MaxHealth;
        return true;
    }
    if (methodName == skString("SetWimpy") && args.entries() == 1) {
        m_Wimpy = args[0].intValue();
        return true;
    }
    if (methodName == skString("SetChaseRadius") && args.entries() == 1) {
        m_ChaseRadius = args[0].intValue();
        return true;
    }
    if (methodName == skString("SetMob") && args.entries() == 1) {
        m_Mob = args[0].intValue();
        return true;
    }
    if (methodName == skString("GetPlayer") && args.entries() == 0) {
        returnValue = skRValue(static_cast<skiExecutable*>(&m_Player), false);
        return true;
    }
    // M16: NPC-mode fields/calls -- see class comment.
    if (methodName == skString("SetUsable") && args.entries() == 1) {
        m_Usable = args[0].boolValue();
        return true;
    }
    if (methodName == skString("SetUseText") && args.entries() == 1) {
        m_UseTextId = args[0].intValue();
        return true;
    }
    if (methodName == skString("SetInvulnerable") && args.entries() == 1) {
        m_Invulnerable = args[0].boolValue();
        return true;
    }
    if (methodName == skString("OpenMenu") && args.entries() == 1) {
        // M17: ReopenMenu(), not OpenMenu() -- an NPC's OnUse() call here
        // starts a fresh conversation each time, and the real quest-state
        // branching lives in the target menu's Init() (see menu_stack.h's
        // comment), which needs to rerun on every visit, not just the
        // first.
        m_Stack.ReopenMenu(ToStdString(args[0].str()));
        return true;
    }
    // SetAttackNoise/SetDeathNoise/SetIsHitNoise/SetWalkAnimation/
    // SetSwingAnimation/SetDeathAnimation/SetIdleAnimation/
    // PlayAnimationOffset/SetScale/AiDetect/SetLoot -- azra_rat.s calls
    // all of these, but this port has no audio and no skeletal
    // animation (consistent with every prior milestone) and no loot-
    // pickup-spawning system yet, so they fall through to the soft-fail
    // below rather than getting dedicated no-op handlers -- there's
    // nothing meaningful to store them into yet.
    if (skScriptedExecutable::method(methodName, args, returnValue, context)) {
        return true;
    }
    return SoftFailNativeCall("Monster", methodName, args, returnValue);
}

}  // namespace sk_bindings
