#include "simkin_bindings/item_executable.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>

#include "assets/string_table.h"
#include "simkin_bindings/combat.h"
#include "simkin_bindings/game_constants.h"
#include "simkin_bindings/level_executable.h"
#include "simkin_bindings/menu_stack.h"
#include "simkin_bindings/monster_executable.h"
#include "simkin_bindings/native_binding_common.h"
#include "simkin_bindings/player_executable.h"
#include "skExecutableContext.h"
#include "skParseException.h"
#include "skRValue.h"
#include "skRValueArray.h"
#include "skRuntimeException.h"

namespace sk_bindings {

ItemExecutable::ItemExecutable(const skString& filename, skExecutableContext& ctxt,
                                MenuStack& stack)
    : skScriptedExecutable(filename, ctxt), m_Stack(stack), m_Interpreter(ctxt.getInterpreter()) {
    m_ScriptPath = ToStdString(filename);
    for (char& c : m_ScriptPath) {
        if (c == '\\') c = '/';
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
}

ItemExecutable::StatusEffect ItemExecutable::statusEffect() const {
    // M37: when the script declared its own typeId, use it -- that is
    // literally what FUN_100458e4 switches on, and it is the only way to
    // separate the two typeIds that share blaze.s (50 scales with the
    // caster, 4006 is a flat 45..50) and the two that share DoomHammer.s.
    switch (m_SpellType) {
        case 50: return kEffectBlaze;
        case 4002: return kEffectDeadToDust;
        case 4006: return kEffectBlazeGreater;
        case 4009: return kEffectAbsorb;
        case 4010: return kEffectBlind;
        case 4012:
        case 4017: return kEffectDoomHammer;
        case 4018: return kEffectDrain;
        case 4020: return kEffectFear;
        case 4023: return kEffectHarmArmor;
        case 4024: return kEffectIgniteFoe;
        case 4025: return kEffectParalyze;
        case 4033: return kEffectDisease;
        case 4034: return kEffectPoison;
        case 4035: return kEffectDeathHowl;
        default: break;
    }

    // Otherwise fall back on the script path: the real engine keys this
    // off the spell entity's entities.txt typeId, and entities.txt maps
    // those same typeIds to exactly these script files.
    auto endsWith = [&](const char* suffix) {
        std::string s(suffix);
        return m_ScriptPath.size() >= s.size() &&
               m_ScriptPath.compare(m_ScriptPath.size() - s.size(), s.size(), s) == 0;
    };
    if (endsWith("spells/fear.s")) return kEffectFear;
    if (endsWith("spells/paralyze.s")) return kEffectParalyze;
    if (endsWith("spells/poison.s")) return kEffectPoison;
    if (endsWith("spells/disease.s")) return kEffectDisease;
    if (endsWith("spells/drain.s")) return kEffectDrain;
    if (endsWith("spells/blind.s")) return kEffectBlind;
    if (endsWith("spells/harmarmor.s")) return kEffectHarmArmor;
    if (endsWith("spells/absorb.s")) return kEffectAbsorb;
    // M34: IgniteFoe has two real aliases. spells\IgniteScroll.s and
    // spells\U_Ignite_Foe_8_lvl8.s are both three-line scripts whose whole
    // Init() is `SetSpellType(4024); ... RunScript("spells\\IgniteFoe")` --
    // they set a different cost/level and then *become* IgniteFoe. This
    // port has no RunScript-style script swap, so matching the filenames
    // here is the same relation by a shorter road, and it is what keeps a
    // real found scroll from casting nothing at all.
    if (endsWith("spells/ignitefoe.s") || endsWith("spells/ignitescroll.s") ||
        endsWith("spells/u_ignite_foe_8_lvl8.s")) {
        return kEffectIgniteFoe;
    }
    // M37: the four damage-only branches. Blaze is the one spell the game
    // hands out at the very start (menus/newgamemenu.s gives it to every
    // new character), and until now it was the *only* spell falling
    // through to the from-scratch RollSpellDamage() path.
    if (endsWith("blaze.s") || endsWith("spells/u_blaze_lvl5.s") ||
        endsWith("spells/u_blaze_lvl10.s")) {
        return kEffectBlaze;
    }
    if (endsWith("spells/deadtodust.s")) return kEffectDeadToDust;
    if (endsWith("spells/doomhammer.s") || endsWith("spells/u_doomhammer_lvl10.s")) {
        return kEffectDoomHammer;
    }
    if (endsWith("spells/deathhowl.s")) return kEffectDeathHowl;
    return kEffectNone;
}

bool ItemExecutable::InvokeOnUse() {
    if (!m_Interpreter) return false;
    skRValueArray args;
    args.append(skRValue(0));  // placeholder for OnUse's "(s)" parameter, same convention every
                                // other InvokeOnUse() in this codebase already uses.
    skRValue ret;
    skExecutableContext ctxt(m_Interpreter);
    try {
        // M35: returns whether the script actually *defined* OnUse. Calls
        // the base class directly, the same way MenuExecutable::TryInvoke
        // does, so a script without the handler answers false instead of
        // logging an unresolved-native soft-fail -- for a world item,
        // having no OnUse is the normal case, not an error. See main.cpp's
        // Use handling for the native default that then runs.
        return skScriptedExecutable::method(skString("OnUse"), args, ret, ctxt);
    } catch (skParseException& e) {
        std::printf("ItemExecutable: PARSE ERROR in OnUse(): %s\n", e.toString().ptr());
    } catch (skRuntimeException& e) {
        std::printf("ItemExecutable: RUNTIME ERROR in OnUse(): %s\n", e.toString().ptr());
    }
    return false;
}

void ItemExecutable::InvokeHitTarget(skiExecutable* target) {
    if (!m_Interpreter) return;
    skRValueArray args;
    args.append(skRValue(target, false));
    skRValue ret;
    skExecutableContext ctxt(m_Interpreter);
    try {
        method(skString("HitTarget"), args, ret, ctxt);
    } catch (skParseException& e) {
        std::printf("ItemExecutable: PARSE ERROR in HitTarget(): %s\n", e.toString().ptr());
    } catch (skRuntimeException& e) {
        std::printf("ItemExecutable: RUNTIME ERROR in HitTarget(): %s\n", e.toString().ptr());
    }
}

std::string ItemExecutable::name() const {
    if (m_Stack.strings() && m_NameId >= 0) return m_Stack.strings()->Get(m_NameId);
    return m_Id.empty() ? std::string("?") : m_Id;
}

bool ItemExecutable::method(const skString& methodName, skRValueArray& args,
                             skRValue& returnValue, skExecutableContext& context) {
    if (methodName == skString("SetName") && args.entries() == 1) {
        m_NameId = args[0].intValue();
        return true;
    }
    if (methodName == skString("SetShortName") && args.entries() == 1) {
        m_ShortNameId = args[0].intValue();
        return true;
    }
    if (methodName == skString("SetItemDescription") && args.entries() == 1) {
        m_DescriptionId = args[0].intValue();
        return true;
    }
    if (methodName == skString("SetUseText") && args.entries() == 1) {
        m_UseTextId = args[0].intValue();
        return true;
    }
    if (methodName == skString("SetID") && args.entries() == 1) {
        m_Id = ToStdString(args[0].str());
        return true;
    }
    if (methodName == skString("SetCost") && args.entries() == 1) {
        m_Cost = args[0].intValue();
        return true;
    }
    if (methodName == skString("SetMarketValue") && args.entries() == 1) {
        m_MarketValue = args[0].intValue();
        return true;
    }
    if (methodName == skString("SetIcon") && args.entries() == 1) {
        m_Icon = args[0].intValue();
        return true;
    }
    if (methodName == skString("SetUsable") && args.entries() == 1) {
        m_Usable = args[0].boolValue();
        if (m_Usable) m_ItemType = kItemTypeConsumable;
        return true;
    }
    if (methodName == skString("SetArmorValue") && args.entries() == 1) {
        m_ArmorValue = args[0].intValue();
        m_ItemType = kItemTypeArmor;
        return true;
    }
    if (methodName == skString("SetArmorType") && args.entries() == 1) {
        m_ArmorType = args[0].intValue();
        m_ItemType = kItemTypeArmor;
        return true;
    }
    if (methodName == skString("SetArmorConstraint") && args.entries() == 1) {
        m_ArmorConstraint = args[0].intValue();
        m_ItemType = kItemTypeArmor;
        return true;
    }
    if (methodName == skString("SetDamageMin") && args.entries() == 1) {
        m_DamageMin = args[0].intValue();
        m_ItemType = kItemTypeWeapon;
        return true;
    }
    if (methodName == skString("SetDamageMax") && args.entries() == 1) {
        m_DamageMax = args[0].intValue();
        m_ItemType = kItemTypeWeapon;
        return true;
    }
    if (methodName == skString("SetWeaponSprite") && args.entries() == 1) {
        m_WeaponSprite = args[0].intValue();
        m_ItemType = kItemTypeWeapon;
        return true;
    }
    if (methodName == skString("SetAnimationFrames") && args.entries() == 1) {
        m_AnimationFrames = args[0].intValue();
        return true;
    }
    if (methodName == skString("SetRange") && args.entries() == 1) {
        m_Range = args[0].intValue();
        m_ItemType = kItemTypeWeapon;
        return true;
    }
    if (methodName == skString("SetWeaponType") && args.entries() == 1) {
        m_WeaponType = args[0].intValue();
        m_ItemType = kItemTypeWeapon;
        return true;
    }
    // M20: real ranged-weapon markers -- SetClipSize/SetFireRate/
    // SetReloadFrames are deliberately NOT handled here (soft-fail is
    // correct): a full corpus grep found zero real call sites for any of
    // the three anywhere in the whole game, matching docs/
    // INPUT_HANDLING.md's separate finding that "Shoot"/"Reload" exist as
    // logical actions but were never wired into the real default control
    // scheme -- ammo/rate-of-fire is dead weight in the shipped game, not
    // a gap this port is missing.
    if (methodName == skString("SetBow") && args.entries() == 1) {
        m_Ranged = args[0].boolValue();
        m_ItemType = kItemTypeWeapon;
        return true;
    }
    if (methodName == skString("SetCrossbow") && args.entries() == 1) {
        m_Ranged = args[0].boolValue();
        m_ItemType = kItemTypeWeapon;
        return true;
    }
    if (methodName == skString("SetThrowingWeapon") && args.entries() == 1) {
        m_Ranged = args[0].boolValue();
        m_ItemType = kItemTypeWeapon;
        return true;
    }
    if (methodName == skString("SetRating") && args.entries() == 1) {
        // M22: deliberately does NOT set m_ItemType -- see rating()'s
        // header comment for the real corpus counter-example
        // (misc/ring_of_fangs.s) that ruled this out as a category
        // signal.
        m_Rating = args[0].intValue();
        return true;
    }
    // --- M37: the Spell class's own three fields (see the header) ---
    if (methodName == skString("SetSpellType") && args.entries() == 1) {
        // Previously soft-failed. The value is the entities.txt typeId
        // FUN_100458e4 switches on, so this is the most direct possible
        // statement of "which spell is this" -- and the shipped scroll
        // wrappers are the scripts that carry it.
        m_SpellType = args[0].intValue();
        return true;
    }
    if (methodName == skString("SetLevel") && args.entries() == 1) {
        m_SpellLevel = args[0].intValue();
        return true;
    }
    if (methodName == skString("GetLevel") && args.entries() == 0) {
        returnValue = skRValue(m_SpellLevel);
        return true;
    }
    if (methodName == skString("SetScroll") && args.entries() == 1) {
        // Set by the loot generators, not by the spell script itself
        // (broken1/loot_random_a.s's `Scroll.SetScroll(true)` and ~20 more)
        // -- a found scroll casts at the scroll's own level, not the
        // reader's, which is exactly the branch this flag selects.
        m_Scroll = args[0].boolValue();
        return true;
    }
    if (methodName == skString("SetMPUsable") && args.entries() == 1) {
        // M34: "usable in multiplayer", called by 49 real scripts
        // (absorb.s among them). The real engine's own multiplayer paths
        // are the ones guarded by the session flag this port has no
        // equivalent of -- there is nothing here for the answer to gate,
        // so it's accepted as a no-op rather than left logging soft-fail
        // noise on every spell load. Same treatment RestrictUse() below
        // already gets, for the same reason.
        return true;
    }
    if (methodName == skString("RestrictUse")) {
        // M22: a real class/race restriction list, never read back by any
        // script and with no restriction system in this port to apply it
        // to (PlayerExecutable::IsItemEnabledFor()'s own comment) --
        // accepted as a no-op so its 100+ real call sites don't log soft-
        // fail noise.
        return true;
    }
    if (methodName == skString("DoAttackRoll") && args.entries() >= 1) {
        // M22: called bare (self-receiver) from within a real spell
        // script's own HitTarget(target) handler (InvokeHitTarget() above)
        // -- args[0] is the target HitTarget() itself received; args[1],
        // when present, is a real per-spell status-effect id (e.g.
        // blaze.s's own DoAttackRoll(target,1)) this port doesn't model
        // (RollSpellDamage()'s own comment) -- only damage applies.
        auto* target = static_cast<MonsterExecutable*>(args[0].obj());
        if (target && target->alive()) {
            StatusEffect effect = statusEffect();
            PlayerExecutable& caster = m_Stack.player();

            // M33: the effect's magnitude is NOT args[1] -- FUN_100458e4
            // never looks at the script's argument. The proof is in the
            // scripts: real Poison.s and Disease.s call
            // `DoAttackRoll(target)` with no second argument at all, yet
            // both apply a fully-parameterised effect.
            //
            // M37 recovers what it *is*. The dispatcher reads the caster's
            // stats block `+0x34`, which M37's stats-layout pass identifies
            // as the **character level** (FUN_10048244's GetLevel/SetLevel
            // bindings read and write exactly that halfword) -- not the
            // "spell power" earlier notes guessed at, and not the spell's
            // SetRating() this port used to substitute (see rating()'s
            // header comment for why that substitution had to go).
            //
            // The full real rule, including its fallback:
            //
            //   if (!scroll && (caster == null || casterIsCharacter))
            //        magnitude = casterLevel;
            //   else magnitude = spell->level;   // the Spell SetLevel
            //
            // That fallback is why the shipped scroll/unique wrappers
            // (spells\IgniteScroll.s SetLevel(8), U_Blaze_lvl5.s
            // SetLevel(5), ...) are precisely the spell scripts that call
            // SetLevel and no plain spell does.
            int magnitude = m_Scroll ? m_SpellLevel : caster.level();
            // The real clamp, transcribed literally: `if (0x18 < mag) mag =
            // 0x19` -- a ceiling of 25, and the same 25 that divides
            // Absorb's heal, so "magnitude / 25" is a fraction of the level
            // cap.
            if (magnitude > 24) magnitude = 25;

            // ---- M37: the shared damage pair. FUN_100458e4 picks `max`
            // first and `min` second for every branch that carries damage,
            // then clamps `max` up to `min`; the status switch further down
            // is a *separate* switch on the same typeId, which is why
            // several spells do both and four do damage only.
            int dmgMax = 0;
            int dmgMin = 0;
            switch (effect) {
                case kEffectBlaze:
                    // `local_250 = 3; iVar11 = casterStats ? mag*3+3 : 6`.
                    dmgMin = 3;
                    dmgMax = magnitude * 3 + 3;
                    break;
                case kEffectBlazeGreater:
                    // The one branch with no magnitude term at all.
                    dmgMin = 45;
                    dmgMax = 50;
                    break;
                case kEffectDeadToDust:
                    // Guarded in the real code by "target is undead"
                    // (`vtable+0xe4` and FUN_10086f88) -- it returns
                    // without doing anything at all otherwise. This port
                    // has SetUndead() stored on MonsterExecutable, so the
                    // guard is real, see undead() below.
                    dmgMin = (magnitude + 1) * 2;
                    dmgMax = (magnitude + 1) * 5;
                    break;
                case kEffectAbsorb:
                    // Equal bounds are load-bearing: the roll only happens
                    // when min != max, so Absorb has no variance.
                    dmgMax = magnitude + 12;
                    dmgMin = dmgMax;
                    break;
                case kEffectDoomHammer: {
                    // `r = rand(1,10); max = mag*r + r; min = max` -- the
                    // variance is in `r`, not in a min..max roll. (Typeid
                    // 4012 draws `r` twice and throws the first away; 4017
                    // draws once. Same distribution, so not reproduced.)
                    int r = 1 + std::rand() % 10;
                    dmgMax = magnitude * r + r;
                    dmgMin = dmgMax;
                    break;
                }
                case kEffectIgniteFoe:
                    dmgMin = (magnitude + 1) * 2;
                    dmgMax = (magnitude + 1) * 5;
                    break;
                case kEffectDeathHowl: {
                    int r = 1 + std::rand() % 10;
                    dmgMax = r * 8 + 1 + magnitude;
                    dmgMin = dmgMax;
                    break;
                }
                default:
                    // Every pure-status branch leaves the pair at 0/0.
                    break;
            }
            if (dmgMax <= dmgMin) dmgMax = dmgMin;

            // ---- M37: the hit gate. This is the real resistance model,
            // and it gates the **whole** effect -- damage and status alike
            // live inside this branch in FUN_100458e4, so a resisted Poison
            // applies no poison at all rather than a weaker one. It
            // replaces the flat resistance *subtraction* this port used
            // until now, which had no basis in the decompile.
            if (effect != kEffectNone &&
                !RollSpellHit(caster.spellToHit(), target->spellResistance())) {
                return true;
            }
            // DeadToDust's own target guard, which the real branch applies
            // before anything else.
            if (effect == kEffectDeadToDust && !target->undead()) return true;

            // The roll, then the damage -- `if (0 < max)` is the real
            // guard, which is exactly what keeps the pure-status branches
            // from dealing 0 damage and printing a damage message.
            int rolled = dmgMin;
            if (dmgMin != dmgMax) rolled = dmgMin + std::rand() % (dmgMax - dmgMin + 1);
            if (dmgMax > 0) target->ApplyDamage(rolled);

            switch (effect) {
                case kEffectFear:
                    // FUN_100458e4's Fear branch:
                    // FUN_10086b98(target, 4, magnitude * 5).
                    target->SetAiPackageTimed(MonsterExecutable::kAiFlee, magnitude * 5);
                    break;
                case kEffectParalyze:
                    // The Paralyze branch arms the target's action lockout
                    // (monster+0x294) through a vtable call; the same
                    // magnitude-scaled duration shape is used here.
                    target->SetParalyzed(magnitude * 5 * 256);
                    break;
                // ---- M33: the remaining real branches, transcribed from
                // FUN_100458e4. Each is a combination of the two decompiled
                // primitives: a timed stat modifier (FUN_1004aa28) and/or a
                // timed effect flag (FUN_1004bae8).
                case kEffectDrain:
                    // `iVar3 = -10; duration = magnitude + 8; stat = 1`
                    // falling into the shared FUN_1004aa28 tail.
                    target->ApplyStatModifier(MonsterExecutable::kStatAttack, -10,
                                               magnitude + 8);
                    break;
                case kEffectHarmArmor:
                    // `iVar3 = -magnitude; duration = magnitude + 8; stat = 7`.
                    target->ApplyStatModifier(MonsterExecutable::kStatArmor, -magnitude,
                                               magnitude + 8);
                    break;
                case kEffectBlind: {
                    // Two -10 modifiers (attack and defense) for
                    // `magnitude + 5`, plus effect flag 4 -- which the real
                    // code applies only when the target is not the player.
                    // Every target this port can cast at is a creature, so
                    // the flag always applies here.
                    int blindDuration = magnitude + 5;
                    target->ApplyStatModifier(MonsterExecutable::kStatAttack, -10, blindDuration);
                    target->ApplyStatModifier(MonsterExecutable::kStatDefense, -10, blindDuration);
                    target->ApplyEffectFlag(MonsterExecutable::kEffectFlagBlind, 0, blindDuration);
                    break;
                }
                case kEffectDisease:
                    // `delta = (magnitude < 7) ? -2 : -3` on attack, a flat
                    // -3 on defense, both for a fixed 0x1e (30) -- the only
                    // effect with a duration that ignores magnitude.
                    target->ApplyStatModifier(MonsterExecutable::kStatAttack,
                                               magnitude < 7 ? -2 : -3, 30);
                    target->ApplyStatModifier(MonsterExecutable::kStatDefense, -3, 30);
                    break;
                case kEffectPoison:
                    // `FUN_1004bae8(target, 8, magnitude)` -- effect flag 8
                    // with damage-over-time kind 3, which the DoT tick
                    // passes straight to DoDamage: 3 points every 256 delta
                    // units for `magnitude` seconds.
                    target->ApplyEffectFlag(MonsterExecutable::kEffectFlagPoison, 3, magnitude);
                    break;
                // ---- M34: the two branches that carry their own damage ----
                //
                // Every effect above is pure status: FUN_100458e4 leaves
                // the shared `min`/`max` damage pair at 0/0 for them, so
                // the `if (0 < max)` guarding the DoDamage call never
                // fires and only the effect lands. Absorb and IgniteFoe
                // are the two that set both -- they deal damage *and* do
                // something else with it.
                case kEffectAbsorb: {
                    // The damage itself is dealt above, from the shared
                    // pair (M37); this branch is only the heal.
                    //
                    // The caster is healed from the *pre-mitigation*
                    // roll -- the real code reads the same `local_250` it
                    // passed to DoDamage, before DoDamage's own reductions:
                    //
                    //   SetHealth(health
                    //             + ((rolled * 0x100 * (magnitude<<16)/6400) >> 16)
                    //             + 6)
                    //
                    // That divisor is not written as a divisor in the
                    // binary -- it is a magic multiply, 0x51EB851F with a
                    // 43-bit shift, which is the standard sequence for
                    // signed division by 6400 (the /25 magic 0x51EB851F
                    // with its shift of 3, plus 8 more for the 256). And
                    // 6400 is 25 * 256, with 25 being exactly the cap
                    // applied to `magnitude` at the top of the function.
                    // So the whole expression collapses to
                    //
                    //   heal = damage * magnitude / 25 + 6
                    //
                    // i.e. absorb a share of the damage proportional to
                    // how far the caster's **level** is toward the level
                    // cap (M37 identified stats+0x34; the earlier "spell
                    // power" reading was a guess), plus a flat 6. Kept in
                    // the real integer order below so the truncation
                    // matches step for step rather than only in the
                    // algebra.
                    int q = (magnitude << 16) / 6400;
                    int drained = (rolled * 256 * q) >> 16;
                    caster.SetHealth(caster.health() + drained + 6);
                    break;
                }
                case kEffectIgniteFoe: {
                    // The damage (an actual `(mag+1)*2 .. (mag+1)*5` roll,
                    // unlike Absorb's fixed one) is dealt above from the
                    // shared pair; this branch is only the burn.
                    //
                    // Set the target on fire: 1 point per second for
                    // `magnitude * 2` seconds, on the second periodic
                    // channel (MonsterExecutable::ApplyBurn). The real
                    // branch also spawns the flame itself -- FUN_10067f84
                    // builds a particle emitter at the target's own x/y/z,
                    // sprite range 62..68, and gives it a lifetime of
                    // `magnitude * 2 << 8`, exactly the burn duration.
                    // That is an independent confirmation of the duration
                    // reading, from a completely different write; the
                    // visual itself has no emitter system here to attach
                    // to and is not reproduced.
                    target->ApplyBurn(1, magnitude * 2);
                    break;
                }
                // ---- M37: the four damage-only branches. Their whole
                // effect is the shared damage pair above; FUN_100458e4's
                // status switch has no arm for any of them and simply
                // falls off the end. Listed explicitly rather than left to
                // `default:` so the compiler keeps flagging this switch if
                // a new typeId is ever added.
                case kEffectBlaze:
                case kEffectBlazeGreater:
                case kEffectDeadToDust:
                case kEffectDoomHammer:
                case kEffectDeathHowl:
                    break;
                case kEffectNone: {
                    // Not a real dispatcher branch at all: in the shipped
                    // engine an unrecognised typeId leaves the damage pair
                    // at 0/0 and does nothing whatsoever. This is the
                    // port's own fallback for a spell script it could not
                    // resolve to a typeId, so it keeps the from-scratch
                    // rating-scaled formula (and stays outside the real hit
                    // gate, which has no meaning without a real branch).
                    int dmg = RollSpellDamage(m_Rating, target->magicResistance());
                    target->ApplyDamage(dmg);
                    break;
                }
            }
        }
        return true;
    }
    if (methodName == skString("GetName") && args.entries() == 0) {
        returnValue = skRValue(skString(name().c_str()));
        return true;
    }
    if (methodName == skString("GetItemDescription") && args.entries() == 0) {
        std::string desc = m_Stack.strings() && m_DescriptionId >= 0
                                ? m_Stack.strings()->Get(m_DescriptionId)
                                : "";
        returnValue = skRValue(skString(desc.c_str()));
        return true;
    }
    if (methodName == skString("GetIcon") && args.entries() == 0) {
        returnValue = skRValue(m_Icon);
        return true;
    }
    if (methodName == skString("GetCost") && args.entries() == 0) {
        returnValue = skRValue(m_Cost);
        return true;
    }
    if (methodName == skString("GetItemType") && args.entries() == 0) {
        returnValue = skRValue(m_ItemType);
        return true;
    }
    if (methodName == skString("CanDrop") && args.entries() == 0) {
        // No quest-critical/undroppable item flag exists in this port's
        // model -- every real, owned item can be dropped.
        returnValue = skRValue(true);
        return true;
    }
    if (methodName == skString("CanTravel") && args.entries() == 0) {
        // Travel-document-style misc items aren't part of this milestone's
        // curated starting inventory (see PlayerExecutable::
        // LoadStartingInventory) -- correctly false for every item this
        // port can currently own.
        returnValue = skRValue(false);
        return true;
    }
    if (methodName == skString("GetArmorText") && args.entries() == 0) {
        // Real armor-rating display text format was never RE'd -- a plain
        // number is the simplest thing that reads correctly in the
        // Equip/UnEquip popup line ("Equip 18"), see inventory.s.
        returnValue = skRValue(skString(std::to_string(m_ArmorValue).c_str()));
        return true;
    }
    if (methodName == skString("GetOwner") && args.entries() == 0) {
        if (m_Owner) returnValue = skRValue(m_Owner, false);
        return true;
    }
    if (methodName == skString("OnUsedBy") && args.entries() == 1) {
        // Native entry point inventory.s's UseItem() calls
        // ("inv.OnUsedBy(GetPlayer())") -- binds the passed-in player as
        // this item's GetOwner() and then runs the script's own OnUse
        // handler (items/bread.s etc.), if it defines one. Marks the item
        // for removal so the host (PlayerExecutable::PurgeRemovedItems)
        // erases it from the inventory once this tick's script calls have
        // fully returned -- matches items/bread.s's own
        // DestroyObject(self) call, which this port doesn't model as a
        // generic native (no general object-destruction registry exists),
        // just this one well-understood consumable-item case.
        m_Owner = args[0].obj();
        skRValueArray noArgs;
        skRValue ret;
        skScriptedExecutable::method(skString("OnUse"), noArgs, ret, context);
        m_MarkedForRemoval = true;
        return true;
    }
    if (methodName == skString("DestroyObject") && args.entries() == 1) {
        // Called as "DestroyObject(self)" from within the item's own
        // OnUse handler -- already covered by OnUsedBy() marking
        // m_Consumed above, this just needs to not throw.
        return true;
    }
    if (methodName == skString("GetPlayer") && args.entries() == 0) {
        // M19: a world pickup's real OnUse() calls this bare (self-
        // receiver) -- same handler shape Door/Monster/Menu already have.
        returnValue = skRValue(static_cast<skiExecutable*>(&m_Stack.player()), false);
        return true;
    }
    if (TryHandleRandom(methodName, args, returnValue)) {
        // M21: loot_gold6-10.s's own Init() calls
        // `Item.SetQuantity(Random(6,10))` -- bare, so resolved as a call
        // on whichever ItemExecutable is running (native_binding_common.h's
        // TryHandleRandom() comment has the full real-corpus justification).
        return true;
    }
    if (methodName == skString("SetQuantity") && args.entries() == 1) {
        m_Quantity = args[0].intValue();
        return true;
    }
    if (methodName == skString("GetQuantity") && args.entries() == 0) {
        returnValue = skRValue(m_Quantity);
        return true;
    }
    if (methodName == skString("AddObject") && args.entries() == 1) {
        // M21: called as "AddObject(Item)" from a real loot-bag script's
        // own Init() (loot_ratseye.s etc.), where `Item` is exactly what
        // `Level.CreateEntity(...)` (LevelExecutable::method()'s own
        // handler) just returned -- takes real ownership of it out of
        // Level's one-slot pending holder (see
        // LevelExecutable::TakePendingCreatedEntity()'s comment) and into
        // this bag's own Collection, only if the passed reference actually
        // matches (defends against a script somehow passing something
        // else, though no real corpus script does).
        std::unique_ptr<ItemExecutable> created = m_Stack.level().TakePendingCreatedEntity();
        if (created && static_cast<skiExecutable*>(created.get()) == args[0].obj()) {
            m_Contents.push_back(std::move(created));
        }
        return true;
    }
    if (methodName == skString("GetFirst") && args.entries() == 0) {
        // M21: lootmenu.s's own Collection-walk convention (GetFirst() then
        // repeated GetNext() until null) -- see docs/SIMKIN_NATIVE_API.md's
        // "Collection" class research.
        m_ContentsIter = 0;
        if (!m_Contents.empty()) {
            returnValue = skRValue(static_cast<skiExecutable*>(m_Contents[0].get()), false);
        }
        // else: leave returnValue at its default blank skRValue() -- the
        // "null" global (game_constants.cpp's own comment) -- lootmenu.s's
        // own `if (Opener.GetFirst() = null)` compares against exactly
        // this. Correct as long as a real found ItemExecutable's own
        // strValue() is never blank -- see this class's strValue()
        // override below.
        return true;
    }
    if (methodName == skString("GetNext") && args.entries() == 0) {
        ++m_ContentsIter;
        if (m_ContentsIter < m_Contents.size()) {
            returnValue =
                skRValue(static_cast<skiExecutable*>(m_Contents[m_ContentsIter].get()), false);
        }
        return true;
    }
    if (methodName == skString("SetDestroy") && args.entries() == 1) {
        m_DestroyWhenEmpty = args[0].boolValue();  // M36, see destroyWhenEmpty()
        return true;
    }
    if (methodName == skString("GetDestroy") && args.entries() == 0) {
        returnValue = skRValue(m_DestroyWhenEmpty);
        return true;
    }
    if (methodName == skString("GetTemplate") && args.entries() == 0) {
        returnValue = skRValue(m_TemplateId);  // M36, see templateId()
        return true;
    }
    if (methodName == skString("RemoveObject") && args.entries() == 1) {
        // M21: lootmenu.s's SelectItem() calls
        // "GetPlayer().PickupItem(Object); GetOpener().RemoveObject(Object);"
        // in that order -- PickupItem() (PlayerExecutable's own handler)
        // only records which object asked (see its comment), so this is
        // where the real ownership transfer actually completes: unlike
        // M19's world-pickup flow (deferred to main.cpp, because that real
        // call site is still inside a host-triggered InvokeOnUse() frame
        // holding the item in a different container), a loot-menu
        // selection's whole round trip happens within one ordinary script-
        // to-script call chain, so the transfer can finish synchronously
        // right here, with no dangling-pointer window to defer past.
        skiExecutable* target = args[0].obj();
        auto it = std::find_if(m_Contents.begin(), m_Contents.end(),
                                [&](const std::unique_ptr<ItemExecutable>& item) {
                                    return static_cast<skiExecutable*>(item.get()) == target;
                                });
        if (it != m_Contents.end()) {
            std::unique_ptr<ItemExecutable> removed = std::move(*it);
            m_Contents.erase(it);
            if (m_Stack.player().TakePendingPickupItem() ==
                static_cast<skiExecutable*>(removed.get())) {
                m_Stack.player().AddItem(std::move(removed));
            }
            // else: no real corpus script calls RemoveObject() without a
            // matching PickupItem() first -- the removed item is simply
            // dropped (freed) rather than silently kept somewhere, same
            // "soft-fail rather than guess" spirit as everywhere else.
        }
        return true;
    }
    if (methodName == skString("OpenMenu") && args.entries() == 1) {
        // M21: a real loot bag's OnUse() calls this bare (self-receiver,
        // loot_ratseye.s's own `OpenMenu("LootMenu")`) -- ReopenMenu(), not
        // OpenMenu(), for the same M17 reason NPC dialogue needs it
        // (lootmenu.s's own UpdateMenu() re-walks this bag's live Collection
        // in Init(), which has to rerun on every visit as the bag's
        // contents actually change, not just the first). Passes `this` as
        // the new menu's opener so lootmenu.s's real GetOpener() calls
        // resolve back to this exact bag, not some other one.
        m_Stack.ReopenMenu(ToStdString(args[0].str()), static_cast<skiExecutable*>(this));
        return true;
    }
    if (methodName == skString("MirrorDestroyObject") && args.entries() == 1) {
        // M19: called as "MirrorDestroyObject(self)" from a real world
        // pickup's own OnUse() (snowline/foxglove.s etc.), right after
        // GetPlayer().PickupItem(self) -- network/replication bookkeeping
        // in the original (this port has no multiplayer, same DoorOpened()
        // precedent), but the removal-from-world intent is real: reuses
        // the same markedForRemoval flag OnUsedBy() sets for a consumed
        // inventory item -- main.cpp's Action::Use handling reads it to
        // erase this instance from gamePickups after InvokeOnUse() returns.
        m_MarkedForRemoval = true;
        return true;
    }
    if (skScriptedExecutable::method(methodName, args, returnValue, context)) {
        return true;
    }
    return SoftFailNativeCall("Item", methodName, args, returnValue);
}


// M38: see native_binding_common.h's StoreScriptObjectField().
bool ItemExecutable::setValue(const skString& fieldName, const skString& attribute,
                     const skRValue& value) {
    if (StoreScriptObjectField(m_ObjectFields, fieldName, value)) return true;
    return skScriptedExecutable::setValue(fieldName, attribute, value);
}

bool ItemExecutable::getValue(const skString& fieldName, const skString& attribute, skRValue& value) {
    if (LoadScriptObjectField(m_ObjectFields, fieldName, value)) return true;
    return skScriptedExecutable::getValue(fieldName, attribute, value);
}

}  // namespace sk_bindings
