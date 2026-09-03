#include "simkin_bindings/monster_executable.h"

#include <cstdio>

#include "assets/sound_archive.h"
#include "assets/string_table.h"
#include "audio/audio_engine.h"
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

void MonsterExecutable::PlayNoise(int soundId) {
    // M28: -1 covers both "SetXNoise() never called" (e.g. an NPC that
    // never sets one) and any script that genuinely passes a negative id
    // -- neither should reach SoundArchive::GetSound(), same defensive
    // shape the real slot-index convention already assumes elsewhere.
    if (soundId < 0 || !m_Stack.sounds() || !m_Stack.audio()) return;
    const sk::Sound* sound = m_Stack.sounds()->GetSound(soundId);
    if (sound) m_Stack.audio()->PlaySfx(*sound);
}

void MonsterExecutable::PlayAttackNoise() { PlayNoise(m_AttackNoiseId); }

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
        // M28: death noise wins over hit noise -- both fire from the same
        // single ApplyDamage() choke point (melee and spell HitTarget
        // both route through here), so there's no risk of ever double-
        // playing.
        PlayNoise(m_DeathNoiseId);
    } else {
        PlayNoise(m_IsHitNoiseId);
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
    // genuinely reaches 8 real rat kills. M18 (simkin_bindings/
    // level_executable.h) closed the remaining gap: a real `Level` global
    // now exists, and `GetEntity("trthgar")` resolves to a real live
    // `Gravel_Trothgar.s`-backed object whenever the host has registered
    // it (main.cpp's zone-load block does, for any zone with that real
    // placement) -- so `SetQuestSolved(0,true)` genuinely runs and quest
    // id 0 flips solved (verified end to end, `m18_level_smoke.cpp`). A
    // host that never registers "trthgar" (`quest_smoke`'s own isolated
    // setup, which doesn't load a full zone) still sees `GetEntity`
    // resolve gracefully to "not found," so
    // `Trthgar.SetPositionMirror(...)` throws its own clean "Cannot call
    // Method ... on a non object" instead -- same net effect (quest stays
    // unsolved there), just no longer because `Level` itself is missing.
    // `InvokeOnKilled()`'s existing try/catch already logs and swallows
    // either exception cleanly rather than crashing, same as any other
    // script error this host-triggered call might hit.
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
    if (methodName == skString("PlaySound") && args.entries() >= 1) {
        // M27: a real monster script's own bare self-call (e.g. monsters/
        // umbra_keth.s's `PlaySound(83)`) -- same real per-zone-manifest
        // slot-index convention PlayerExecutable::PlaySound() documents in
        // full (assets/sound_archive.h).
        if (m_Stack.sounds() && m_Stack.audio()) {
            const sk::Sound* sound = m_Stack.sounds()->GetSound(args[0].intValue());
            if (sound) m_Stack.audio()->PlaySfx(*sound);
        }
        return true;
    }
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
    // M28: the real animation clip numbers. Every monster script sets all
    // four; they index the model resource's own clip table (see
    // world/model_archive.h's AnimationClip -- decoded this session). All
    // of these soft-failed before, so creatures rendered as static
    // resting-pose statues sliding around the level.
    if (methodName == skString("SetIdleAnimation") && args.entries() == 1) {
        m_IdleAnim = args[0].intValue();
        return true;
    }
    if (methodName == skString("SetWalkAnimation") && args.entries() == 1) {
        m_WalkAnim = args[0].intValue();
        return true;
    }
    if (methodName == skString("SetSwingAnimation") && args.entries() == 1) {
        m_SwingAnim = args[0].intValue();
        return true;
    }
    if (methodName == skString("SetDeathAnimation") && args.entries() == 1) {
        m_DeathAnim = args[0].intValue();
        return true;
    }
    // `PlayAnimation(n)` / `PlayAnimationOffset(n)` -- both appear in real
    // Init() bodies to set the creature's starting pose (e.g. arat.s's
    // `PlayAnimation(0)`). Treated identically here: the host AI loop
    // overrides the clip every tick from the creature's own state anyway,
    // so this only decides what it looks like before it first acts.
    if ((methodName == skString("PlayAnimation") ||
         methodName == skString("PlayAnimationOffset")) &&
        args.entries() == 1) {
        m_CurrentAnim = args[0].intValue();
        return true;
    }
    // Real per-instance appearance, previously soft-failed and therefore
    // invisible in the port (every creature rendered as skin 0 at 1:1
    // scale). Both showed up in a real play session's own soft-fail log.
    if (methodName == skString("SetSkin") && args.entries() == 1) {
        // A models.idx resource genuinely carries several skins
        // (Model::skinCount, docs/MODEL_FORMAT.md) -- e.g. the real
        // corpus picks skin 9/10 for some creature variants, which the
        // port was drawing with skin 0's texture.
        m_Skin = args[0].intValue();
        return true;
    }
    if (methodName == skString("SetScale") && args.entries() == 1) {
        // 8.8 fixed point, so 256 == 1:1 -- consistent with every real
        // call site: literal values cluster at 230-306, and azra_rat.s
        // uses `SetScale(Random(206,306))` to vary rat size around 256.
        // A value of exactly 256 being the identity is what makes that
        // range read as "0.8x to 1.2x".
        m_Scale = args[0].intValue();
        return true;
    }
    if (methodName == skString("GetPlayer") && args.entries() == 0) {
        returnValue = skRValue(static_cast<skiExecutable*>(&m_Player), false);
        return true;
    }
    if (TryHandleRandom(methodName, args, returnValue)) {
        // M21: azra_rat.s's own `SetScale(Random(206,306))` -- bare, same
        // real gap native_binding_common.h's TryHandleRandom() comment
        // explains (previously soft-failed to 0 on every monster).
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
    if (methodName == skString("SetLoot") && args.entries() >= 2) {
        // M21: see lootTag()'s comment -- main.cpp's death handling
        // resolves and spawns this as a real world pickup.
        m_LootTag = ToStdString(args[1].str());
        return true;
    }
    if (methodName == skString("DestroyObjectMirror")) {
        // M23: see destroyed()'s comment -- azra.s's own
        // `M1.DestroyObjectMirror(M1)` (self-passed, network/replication
        // bookkeeping in the original, same `DoorOpened()`-style pattern
        // -- this port has no multiplayer) after a real save flag says
        // an entity is no longer relevant.
        m_Destroyed = true;
        return true;
    }
    if (methodName == skString("SetAttackNoise") && args.entries() == 1) {
        // M28: see PlayAttackNoise()'s comment.
        m_AttackNoiseId = args[0].intValue();
        return true;
    }
    if (methodName == skString("SetDeathNoise") && args.entries() == 1) {
        m_DeathNoiseId = args[0].intValue();
        return true;
    }
    if (methodName == skString("SetIsHitNoise") && args.entries() == 1) {
        m_IsHitNoiseId = args[0].intValue();
        return true;
    }
    // SetWalkAnimation/SetSwingAnimation/SetDeathAnimation/
    // SetIdleAnimation/PlayAnimationOffset/SetScale/AiDetect -- azra_rat.s
    // calls all of these, but this port has no skeletal animation and
    // renders every entity at a fixed scale/skin (consistent with every
    // prior milestone -- see docs/PORT_ROADMAP.md's M12 entry), so they
    // fall through to the soft-fail below rather than getting dedicated
    // no-op handlers -- there's nothing meaningful to store them into yet.
    if (skScriptedExecutable::method(methodName, args, returnValue, context)) {
        return true;
    }
    return SoftFailNativeCall("Monster", methodName, args, returnValue);
}

}  // namespace sk_bindings
