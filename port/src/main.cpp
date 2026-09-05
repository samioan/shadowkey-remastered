// M5: deepens the menu chain from M4's main-menu-only milestone --
// New Game -> character creation (choose character/race/portrait, name
// entry) is now playable end to end through the menus, plus Load/Save/
// Delete (a simulated in-memory save system, no real save-file format
// RE'd) and Credits (a native-only screen reading credits.txt directly,
// with no script-side handler in the whole corpus) and the Quit
// confirmation popup actually closing the app. See
// C:\Users\Admin\.claude\plans\vast-wandering-summit.md for the original
// M0-M4 plan; M5 continues past it in the same "deepen the menu chain"
// direction the user chose. M6 added the 3D zone renderer, M7 tile-grid
// collision, M8 placed-entity rendering -- see docs/PORT_ROADMAP.md.
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "assets/game_config.h"
#include "assets/sound_archive.h"
#include "assets/sprite_archive.h"
#include "assets/string_table.h"
#include "assets/zone_display_names.h"
#include "audio/audio_engine.h"
#include "engine/game_clock.h"
#include "engine/input_state.h"
#include "engine/pc_key_map.h"
#include "engine/screen_mode.h"
#include "graphics/backbuffer.h"
#include "graphics/bitmap_font.h"
#include "platform/win32/console_tee.h"
#include "platform/win32/crash_report.h"
#include "platform/win32/exe_dir.h"
#include "platform/win32/window.h"
#include "render3d/camera.h"
#include "render3d/zone_renderer.h"
#include "simkin_bindings/combat.h"
#include "simkin_bindings/combo_box_executable.h"
#include "simkin_bindings/door_executable.h"
#include "simkin_bindings/floating_sprite_executable.h"
#include "simkin_bindings/game_constants.h"
#include "simkin_bindings/item_button_executable.h"
#include "simkin_bindings/item_executable.h"
#include "simkin_bindings/level_executable.h"
#include "simkin_bindings/menu_executable.h"
#include "simkin_bindings/menu_stack.h"
#include "simkin_bindings/monster_executable.h"
#include "simkin_bindings/player_executable.h"
#include "simkin_bindings/popup_menu_executable.h"
#include "simkin_bindings/slider_executable.h"
#include "simkin_bindings/arrow_projectile.h"
#include "simkin_bindings/spell_cast.h"
#include "simkin_bindings/spell_projectile.h"
#include "simkin_bindings/table_executable.h"
#include "simkin_bindings/text_area_executable.h"
#include "simkin_bindings/weapon_viewmodel.h"
#include "simkin_bindings/zone_script_executable.h"
#include "skExecutableContext.h"
#include "skInterpreter.h"
#include "skParseException.h"
#include "skRuntimeException.h"
#include "world/entity_types.h"
#include "world/model_archive.h"
#include "world/zone.h"

namespace {

// kBackgroundColor is a placeholder -- only the flat fallback fill for
// when a real MenuBackground() sprite fails to load (docs/
// GRAPHICS_FORMAT.md); the real backgrounds themselves are decoded
// sprite art, not a color. The menu text colors below, though, are
// real values sampled directly off a real screenshot (unselected =
// dark red, selected = near-white, static/disabled kept as a muted
// tone distinct from both -- not confirmed against a real disabled
// row, no screenshot of one on hand, but at minimum no longer
// identical-looking to the enabled colors).
constexpr uint16_t kBackgroundColor = sk::PackRGB565(16, 16, 32);
constexpr uint16_t kTextColor = sk::PackRGB565(140, 40, 40);
constexpr uint16_t kSelectedTextColor = sk::PackRGB565(235, 235, 235);
constexpr uint16_t kStaticTextColor = sk::PackRGB565(110, 95, 75);
constexpr uint16_t kTitleColor = sk::PackRGB565(140, 180, 255);
constexpr uint16_t kPopupBgColor = sk::PackRGB565(40, 40, 60);
constexpr uint16_t kPopupBorderColor = sk::PackRGB565(90, 90, 130);

// Word-wraps `text` at `maxChars` per line, drawing each line starting at
// (x,y). Returns the number of lines drawn, so callers can advance their
// own layout cursor by that many line-heights.
int DrawWrappedText(sk::Backbuffer& bb, int x, int y, const std::string& text, int maxChars,
                     uint16_t color) {
    if (maxChars <= 0) maxChars = 27;
    const int lineHeight = sk::BitmapFont::kGlyphHeight + 3;
    std::vector<std::string> lines;
    std::string current;
    std::istringstream words(text);
    std::string word;
    while (words >> word) {
        std::string candidate = current.empty() ? word : current + " " + word;
        if (static_cast<int>(candidate.size()) > maxChars && !current.empty()) {
            lines.push_back(current);
            current = word;
        } else {
            current = candidate;
        }
    }
    if (!current.empty() || lines.empty()) lines.push_back(current);
    for (size_t i = 0; i < lines.size(); ++i) {
        sk::BitmapFont::DrawString(bb, x, y + static_cast<int>(i) * lineHeight, lines[i], color);
    }
    return static_cast<int>(lines.size());
}

// A row's display text: a literal (already-resolved) string if one was
// set (AddButton("Cymric",...), AddFloatingText(healthText,...)) takes
// priority; otherwise resolves textId through the real stringtable, same
// as every row kind before M10. See menu_executable.h's class comment.
std::string RowText(int textId, const std::string& literalText, const sk::StringTable& strings) {
    if (!literalText.empty()) return literalText;
    if (textId < 0) return "";
    return strings.Get(textId);
}

// M15/M16: entities.txt's `name` column is a real loadable script path
// only for a minority of placements in any given category -- the rest
// use a "!label"-only convention for generic/decorative instances that
// share their category's appearance but have no unique per-instance
// script/behavior (docs/ZONE_FORMAT.md's category table; confirmed
// separately for category 11/doors and category 2/monsters this
// session). Shared by both the door and monster/NPC branches of the
// zone-load loop below.
// The real .ent per-placement heading (Zone::EntPlacement::yawRaw, a raw
// 16-bit angle with 65536 == one full turn) as radians. Same units and
// convention as DoorExecutable's AddRotationTurn() accumulator, so the two
// simply add.
float PlacementYawRadians(uint16_t yawRaw) {
    constexpr float kTwoPi = 6.28318530718f;
    // M35: the trailing `- kModelForwardYawOffset` holds every *placed*
    // entity (doors, world pickups) exactly where it already rendered.
    //
    // The renderer now takes a quarter turn out of every heading, because
    // a model's forward axis is its local +Z (see kModelForwardYawOffset
    // -- decompiled from the real actor transform). Creature headings are
    // computed here from atan2(dy, dx) and were genuinely a quarter turn
    // wrong, which that fixes. This raw .ent angle is a different case:
    // its own zero-reference was never derived from the binary, it was
    // fitted by eye until doors stopped reading as permanently open. That
    // fit silently absorbed the same quarter turn, so cancelling it here
    // keeps the one thing that *was* verified -- how these actually look
    // in the world -- rather than rotating it by an angle the fit already
    // accounted for.
    return static_cast<float>(yawRaw) / 65536.0f * kTwoPi - sk::kModelForwardYawOffset;
}

// M30: the entities.txt categories that are *item-shaped* -- a placement
// the player walks up to and uses, whose script is an ordinary item
// script (docs/ZONE_FORMAT.md's category table):
//   3  misc loot/quest objects      4  weapons        5  spells
//   6  armor pieces                 8  containers     9  consumables
//   12 trapped container/door       14 spell scrolls  15 shields
//   16 the one unique weapon
// Before this only category 3 was loaded, so a weapon, spell, scroll,
// shield, potion or **chest** lying in the world was an inert prop with no
// interact prompt -- which is what "the prompt doesn't appear for a
// lootable object" was. Categories 2/7 (monsters/merchants) and 11 (doors)
// keep their own dedicated branches.
bool IsPickupCategory(int category) {
    switch (category) {
        case 3: case 4: case 5: case 6:
        case 8: case 9: case 12: case 14:
        case 15: case 16:
            return true;
        default:
            return false;
    }
}

// Containers (and their trapped variant) open a real loot menu from
// OnUse() rather than transferring themselves -- see PickupInstance.
bool IsContainerCategory(int category) { return category == 8 || category == 12; }

bool HasRealScript(const std::string& entityTypeName) {
    return entityTypeName.size() > 2 &&
           entityTypeName.compare(entityTypeName.size() - 2, 2, ".s") == 0;
}

// "ChooseMale" -> "Male", "ChooseFemale" -> "Female" -- strips the
// convention these callbacks use; falls back to the raw name otherwise.
// Real portrait sprites are out of scope here (see
// FloatingSpriteExecutable's header comment), so this text label stands
// in for the picker entirely.
std::string SpriteLabel(const std::string& callback) {
    const std::string prefix = "Choose";
    if (callback.size() > prefix.size() && callback.compare(0, prefix.size(), prefix) == 0) {
        return callback.substr(prefix.size());
    }
    return callback;
}

// Combat vertical-slice (docs/PORT_ROADMAP.md): a live, AI-driven
// azra_rat instance -- unlike gameEntities' static props, these move,
// fight, and can die, so they carry their own MonsterExecutable (real
// stats/OnKilled pulled from monsters/Azra_Rat.s, see
// simkin_bindings/monster_executable.h) plus mutable world position and
// AI state, separate from the PlacedEntity list the renderer otherwise
// draws untouched every frame. Idle/Chasing/Attacking is this port's
// own from-scratch AI design -- no real monster script ever calls
// AiAttack/AiPursue itself (only AiDetect(), confirmed by grepping the
// whole monsters/*.s corpus), so there's no real state machine to
// match, just parameters (chaseRadius etc.) to honor.
// M44: the host side of LevelExecutable::ZoneRegions -- see that class for
// why the Level global reaches the zone through an interface rather than
// including world/zone.h (sk_bindings deliberately does not link sk_world).
// Points at whatever zone is live; harmlessly inert between zones.
class LiveZoneRegions : public sk_bindings::LevelExecutable::ZoneRegions {
public:
    void SetZone(sk::Zone* zone) { m_Zone = zone; }
    bool HasRegion(const std::string& name) const override {
        return m_Zone && !m_Zone->RegionsNamed(name).empty();
    }
    int LockRegion(const std::string& name) override {
        return m_Zone ? m_Zone->LockRegion(name) : 0;
    }
    int UnlockRegion(const std::string& name) override {
        return m_Zone ? m_Zone->UnlockRegion(name) : 0;
    }
    void LightRect(int x0, int y0, int x1, int y1, int level) override {
        if (m_Zone) m_Zone->LightRect(x0, y0, x1, y1, level);
    }

private:
    sk::Zone* m_Zone = nullptr;
};

struct MonsterInstance {
    std::unique_ptr<sk_bindings::MonsterExecutable> script;
    float x = 0, y = 0, z = 0;
    int modelArchiveIndex = -1;
    // Real per-placement heading from the .ent record (Zone::EntPlacement::
    // yawRaw), radians. Every live instance carries one now -- see the
    // door struct's comment.
    float placementYaw = 0.0f;
    // Which way the creature is currently drawn facing, radians. Starts at
    // its real .ent placement heading and turns to face where it is moving
    // (or the player, while attacking). No RE ground truth for a model's
    // own forward axis, so this may carry a constant offset per model --
    // it is still far better than every creature staring in one fixed
    // direction regardless of where it is walking.
    float facingYaw = 0.0f;
    enum class AiState { Idle, Chasing, Attacking } aiState = AiState::Idle;
    // Ticks since this monster last actually had the player in sight.
    // Chasing survives brief losses of sight (the player ducking round a
    // pillar) but not indefinitely -- see the AI block in the tick loop.
    int ticksSinceSeen = 0;
    // Where the player last was when seen; a chaser steers toward this
    // rather than freezing the instant sight breaks.
    float lastSeenX = 0, lastSeenY = 0;
    // M28: vertex-animation playback state. `animClip` is the clip index
    // the creature's own script named (idle/walk/swing/death); `animTime`
    // counts seconds into it, and the death clip latches (plays once and
    // holds its last frame) instead of looping.
    int animClip = -1;
    float animTime = 0.0f;
    bool animHoldLastFrame = false;
    // M24: the real entities.txt typeId this placement resolved from --
    // ZoneScriptExecutable::NotifyKilled() (a real zone-root script's own
    // AddTrigger()...SetEntityID(id) kill-count trigger, e.g. ghstpass.s's
    // zombieTrigger.SetEntityID(104)) matches kills against this.
    int typeId = -1;
    // M45: the real `actor+0x2e4` backlink -- which encounter region
    // spawned this creature, so its death can decrement that region's
    // live count (`FUN_10083c04` calls `FUN_1008b118(actor->region)`).
    // That count is what lets a cleared region spawn again. Null for
    // everything placed by the zone's own `.ent`.
    sk_bindings::EncounterExecutable* encounter = nullptr;
    size_t encounterRegion = 0;
};

// M28: advances one live creature's vertex animation by a tick and
// returns the model frame to draw.
//
// The clip table itself is real, decoded data (world/model_archive.h's
// AnimationClip): a monster script's SetIdleAnimation/SetWalkAnimation/
// SetSwingAnimation/SetDeathAnimation numbers index straight into it. Two
// things here are this port's own choices, not recovered behaviour:
// reading the record's third field as frames-per-second (its units were
// never confirmed), and looping every clip except the death one, which
// holds its final frame so a corpse stays down.
int AdvanceMonsterAnimation(MonsterInstance& m, sk::ModelArchive& models) {
    const sk::Model* model = models.GetModel(m.modelArchiveIndex);
    if (!model || model->frameCount <= 1) return 0;
    // Fall back to whatever pose the script's own PlayAnimation() asked
    // for at Init() before the AI has picked a clip.
    int clipIndex = m.animClip >= 0 ? m.animClip : m.script->currentAnimation();
    const sk::AnimationClip* clip = model->clip(clipIndex);
    if (!clip || clip->frameCount() <= 0) return 0;

    constexpr float kTickSeconds = 0.04f;  // the fixed 25Hz tick, engine/game_clock.h
    m.animTime += kTickSeconds;
    // M52: `rate` is in units of 1.5 fps, not fps -- the engine multiplies
    // it by 384 into an 8.8-per-second cursor rate. See AnimationClip.
    float rate = clip->rate > 0 ? clip->fps() : 10.0f * sk::kRateToFps;
    int advanced = static_cast<int>(m.animTime * rate);
    int frameInClip;
    if (m.animHoldLastFrame) {
        frameInClip = (std::min)(advanced, clip->frameCount() - 1);
    } else {
        frameInClip = advanced % clip->frameCount();
        // Keep animTime from growing without bound over a long session.
        float clipSeconds = static_cast<float>(clip->frameCount()) / rate;
        if (clipSeconds > 0.0f && m.animTime >= clipSeconds) m.animTime -= clipSeconds;
    }
    return clip->startFrame + frameInClip;
}

// M30: the world-space height of a creature's centre of mass, from its
// real model's own vertical extent scaled by its script's SetScale(). Used
// to aim the camera at whatever the player is fighting -- a rat's centre
// sits far below eye level, a person's does not, so "is this a small
// enemy" is measured from the real art rather than hardcoded per monster.
// Falls back to eye height (i.e. no pitch) if the model can't be resolved.
float MonsterCenterZ(const MonsterInstance& m, sk::ModelArchive& models) {
    const sk::Model* model = models.GetModel(m.modelArchiveIndex);
    if (!model || model->localHeight() <= 0) return m.z + sk::kEyeHeightOffset;
    float scale = m.script->scale();
    return m.z + (static_cast<float>(model->minLocalY) +
                  static_cast<float>(model->localHeight()) * 0.5f) * scale;
}

// M15: Action::Use interact binding, first (narrow) slice -- doors only,
// same "one real category, not the whole native surface" precedent M12
// set for combat (docs/PORT_ROADMAP.md's "generic Action::Use interact
// binding (doors, pickups, NPC talk -- still unbound)" open item; pickups
// and NPC talk stay unbound, see the zone-load block below). Like
// MonsterInstance, a live door carries its own DoorExecutable (a real
// door.s/door02.s script's Init() actually runs) plus mutable world
// state (yaw, for the open/close swing) separate from gameEntities'
// untouched static-prop list.
struct DoorInstance {
    std::unique_ptr<sk_bindings::DoorExecutable> script;
    float x = 0, y = 0, z = 0;
    int modelArchiveIndex = -1;
    // Real per-placement heading from the .ent record, radians. Composed
    // with the script's own accumulated AddRotationTurn() so a door both
    // sits in its wall correctly *and* swings when opened. Without this
    // every door rendered face-on regardless of which wall it was in,
    // which is what made them all look permanently open.
    float placementYaw = 0.0f;
    // M38: the two ways a zone trigger identifies the entity it guards --
    // its entities.txt typeId and its .ent placement name. crypt1.s's five
    // trapped doors are matched purely by name.
    int typeId = -1;
    std::string name;
};

// M38: a placed entity a zone-root script's own AddTrigger() watches as a
// physical trap. Built once per zone, right after the zone script's Init()
// has registered its triggers, by filtering every placement through
// ZoneScriptExecutable::AnyTrapWatches() -- so the per-tick proximity test
// walks a handful of real trap placements rather than every prop.
//
// The real engine has no such list: each trap entity ticks itself
// (FUN_1008ff14) and asks the whole trigger list. Same outcome, one less
// object to model.
struct TrapInstance {
    float x = 0, y = 0;
    int typeId = -1;
    std::string name;
    // The real trap holds a "already sprung" flag (entity+0x225) and a
    // re-arm timer (+0x228); this is the same latch, cleared when the
    // player leaves the trap's box, so standing on a spike trap costs one
    // hit rather than one per tick.
    bool inside = false;
};

// M19: Action::Use interact binding's last remaining slice -- pickups.
// Same "one real category" precedent (docs/PORT_ROADMAP.md) -- category 3
// (misc loot) real .s scripts like snowline/foxglove.s, not the broader
// loot-menu/container pattern (category 8, still unbound -- see
// item_executable.h's class comment). Like DoorInstance/MonsterInstance,
// a live world pickup carries its own real ItemExecutable (Init() genuinely
// runs), separate from gameEntities' untouched static-prop list -- but
// unlike a door/monster, a pickup's script instance doesn't live for the
// zone's whole lifetime: a successful real OnUse() (PickupItem(self) +
// MirrorDestroyObject(self)) moves script's ownership into the player's
// inventory and erases this entry (see the Action::Use handling below).
struct PickupInstance {
    std::unique_ptr<sk_bindings::ItemExecutable> script;
    float x = 0, y = 0, z = 0;
    int modelArchiveIndex = -1;
    float placementYaw = 0.0f;  // real .ent heading, radians
    // M30: true for a container/loot category (8/12) rather than a
    // directly-collectable world item -- its OnUse() opens a real loot
    // menu instead of transferring itself. Kept explicitly rather than
    // inferred, so an empty container still prompts correctly.
    bool isContainer = false;
};

// M25: first-person weapon viewmodel -- state machine (WeaponViewmodel/
// StartWeaponSwing/TickWeaponViewmodel) lives in simkin_bindings/
// weapon_viewmodel.h so it's unit-testable without the windowed game loop
// (see that header's comment for the real RE ground truth it recreates
// and its one unresolved gap). Only the actual draw call is here, since it
// needs main.cpp's own sk::Backbuffer/sk::SpriteArchive render-layer types.
//
// Draws the equipped item's own real global.spr sprite -- weaponSprite()
// as a base slot, offset by the current swing frame (SetAnimationFrames()'s
// real meaning, confirmed via the Weapon class dispatcher's SetWeaponSprite/
// SetAnimationFrames handlers) -- bottom-right of the 3D view, matching a
// first-person viewmodel's conventional screen position (not decompiled,
// see weapon_viewmodel.h's comment). Falls back to the base (unoffset) slot
// if a specific animation frame's slot doesn't decode, and is a silent
// no-op if the base slot itself doesn't (missing/incomplete global.spr,
// same tolerance every other optional sprite draw in this port already has).
// M47: the real draw, from FUN_1002b1b0. Every weapon viewmodel sprite in
// global.spr is a full 176x208 frame and the real blit puts it at (0,0) --
// these are full-screen overlays of a hand holding the weapon, not a small
// icon tucked into a corner, which is where this port used to draw them.
// The only non-zero positions the real function produces are the bob (0..10
// px on each axis while idle/walking) and the 124px drop while a weapon
// swap is raising the new weapon into view.
void RenderWeaponViewmodel(sk::Backbuffer& backbuffer, const sk_bindings::WeaponViewmodel& vm,
                            sk::SpriteArchive& sprites) {
    sk_bindings::ViewmodelDraw draw = sk_bindings::ResolveViewmodelDraw(vm);
    if (!draw.visible || draw.spriteSlot < 0) return;
    const sk::Sprite* sprite = sprites.GetSprite(draw.spriteSlot);
    // A strip slot the per-zone sprite manifest didn't pull in falls back
    // to the weapon's own base frame rather than blinking out.
    if (!sprite) sprite = sprites.GetSprite(vm.item->weaponSprite());
    if (!sprite) return;
    backbuffer.Blit(draw.x, draw.y, *sprite);
}

// M25: a plain outline rectangle -- ButtonExecutable's real ShowBorder
// (true) (charactermanager.s's Stats/Equip/Quest buttons, docs/
// PORT_ROADMAP.md's M25 entry) drawn using the row's own real w/h.
void DrawRectOutline(sk::Backbuffer& backbuffer, int x0, int y0, int w, int h, uint16_t color) {
    int x1 = x0 + w, y1 = y0 + h;
    for (int x = x0; x < x1; ++x) {
        backbuffer.SetPixel(x, y0, color);
        backbuffer.SetPixel(x, y1 - 1, color);
    }
    for (int y = y0; y < y1; ++y) {
        backbuffer.SetPixel(x0, y, color);
        backbuffer.SetPixel(x1 - 1, y, color);
    }
}

void RenderPopup(sk::Backbuffer& backbuffer, sk_bindings::PopupMenuExecutable& popup,
                  const sk::StringTable& strings) {
    int x0 = 10, y0 = 60, x1 = sk::Backbuffer::kWidth - 10, y1 = 150;
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            bool border = (x == x0 || x == x1 - 1 || y == y0 || y == y1 - 1);
            backbuffer.SetPixel(x, y, border ? kPopupBorderColor : kPopupBgColor);
        }
    }
    int y = y0 + 6;
    for (size_t i = 0; i < popup.items().size(); ++i) {
        const auto& item = popup.items()[i];
        if (item.blanked) continue;  // M10: UpdatePopupItem(index, "") hides this row entirely
        bool selectable = sk_bindings::PopupMenuExecutable::IsSelectable(item);
        // popup.selectedItem() indexes ALL items, blanked/static ones
        // included -- see PopupMenuExecutable::IsItemSelected(). This used
        // to recount only the selectable, non-blanked items, which put the
        // highlight on the wrong line for every confirm popup in the game
        // (they all open with a non-selectable message line).
        bool isSelected = selectable && popup.IsItemSelected(i);
        uint16_t color = isSelected ? kSelectedTextColor : kTextColor;
        std::string text = !item.literalText.empty() ? item.literalText : strings.Get(item.textId);
        int lines = DrawWrappedText(backbuffer, x0 + 6, y, text, 22, color);
        y += lines * (sk::BitmapFont::kGlyphHeight + 3) + 2;
    }
}

void RenderMenu(sk::Backbuffer& backbuffer, sk_bindings::MenuExecutable& menu,
                 sk_bindings::PlayerExecutable& player, const sk::StringTable& strings,
                 sk::SpriteArchive& sprites) {
    using RowKind = sk_bindings::MenuExecutable::RowKind;
    // Real background (docs/GRAPHICS_FORMAT.md) when the real script's
    // MenuBackground(id) call resolved to a decodable global.spr slot
    // (id *is* the slot index directly, no translation -- confirmed
    // against every real MenuBackground() call in the corpus); falls
    // back to the flat placeholder color otherwise (asset missing, or a
    // menu that never calls MenuBackground() at all).
    const sk::Sprite* background =
        menu.backgroundId() >= 0 ? sprites.GetSprite(menu.backgroundId()) : nullptr;
    if (background) {
        backbuffer.Blit(0, 0, *background);
    } else {
        backbuffer.Fill(kBackgroundColor);
    }

    // Slot 69 (the main menu's background, MenuBackground(69) in
    // mainmenu.s) has "The Elder Scrolls Travels / SHADOWKEY" logo art
    // baked into its top ~50px -- unlike every other menu background
    // (20, 174), which are plain parchment. The real game's item list
    // starts below it; nothing in mainmenu.s sets this explicitly (no
    // native y-offset call exists), so this is a fixed native constant
    // measured against a real screenshot, not a decompiled value.
    int y = menu.backgroundId() == 69 ? 50 : 8;
    const int lineHeight = sk::BitmapFont::kGlyphHeight + 4;

    if (menu.titleTextId() >= 0) {
        sk::BitmapFont::DrawString(backbuffer, 4, y, strings.Get(menu.titleTextId()), kTitleColor);
        y += lineHeight + 2;
    }

    for (size_t rowIndex = 0; rowIndex < menu.rows().size(); ++rowIndex) {
        const auto& row = menu.rows()[rowIndex];
        // menu.selectedItem() is a 1-based index into ALL rows (see
        // MenuExecutable::IsRowSelected() -- it's the number
        // AddMenuItem()/AddStaticItem() hand back to scripts). The old code
        // here compared it against a counter that only advanced on
        // selectable rows, so every screen mixing static and selectable
        // rows -- character creation, the character manager, inventory --
        // drew the highlight on a different row than the one Enter would
        // actually activate.
        bool isSelected = row.selectable && menu.IsRowSelected(rowIndex);
        uint16_t color = !row.selectable      ? kStaticTextColor
                          : isSelected         ? kSelectedTextColor
                                                : kTextColor;

        switch (row.kind) {
            case RowKind::MenuItem:
            case RowKind::StaticItem: {
                std::string label = RowText(row.textId, row.literalText, strings);
                int textW = sk::BitmapFont::TextWidth(label);
                // M36: a real AddButton() that carries art draws that art
                // rather than its (usually empty) label -- the
                // inventory/equip screen's five category tabs are exactly
                // this shape. The two slot ids are the button's normal and
                // highlighted states; which of the pair is which isn't
                // recorded anywhere, so the selected tab additionally gets
                // the outline the real screen shows around it, making the
                // selection unambiguous either way round.
                if (row.x >= 0 && row.spriteNormal >= 0) {
                    int slot = isSelected && row.spriteSelected >= 0 ? row.spriteSelected
                                                                      : row.spriteNormal;
                    const sk::Sprite* art = sprites.GetSprite(slot);
                    if (!art) art = sprites.GetSprite(row.spriteNormal);
                    if (art) {
                        backbuffer.Blit(row.x, row.y, *art);
                        if (isSelected) {
                            DrawRectOutline(backbuffer, row.x - 1, row.y - 1, art->width + 2,
                                             art->height + 2, kSelectedTextColor);
                        }
                        if (!label.empty()) {
                            sk::BitmapFont::DrawString(backbuffer, row.x, row.y + art->height,
                                                        label, color);
                        }
                        break;
                    }
                }
                if (row.isQuitButton) {
                    // M36: the real screens put this softkey label at the
                    // bottom of the page (see the shipped equip screen),
                    // not inline in the vertical flow -- which is where it
                    // used to land, printing "Back" straight over the
                    // category icons.
                    sk::BitmapFont::DrawString(backbuffer, (sk::Backbuffer::kWidth - textW) / 2,
                                                sk::Backbuffer::kHeight - lineHeight - 4, label,
                                                color);
                    break;
                }
                if (row.x >= 0) {
                    // M25: a real AddButton()/AddFloatingText() position
                    // (charactermanager.s's whole real layout) -- centered
                    // within the row's own real width when it has one
                    // (a button box, e.g. Stats/Equip/Quest), else drawn
                    // left-anchored at its own real x (a plain stat-text
                    // line, e.g. health/gold, which never sets a width).
                    int textX = row.w > 0 ? row.x + (row.w - textW) / 2 : row.x;
                    sk::BitmapFont::DrawString(backbuffer, textX, row.y, label, color);
                    if (row.showBorder && row.w > 0 && row.h > 0) {
                        DrawRectOutline(backbuffer, row.x, row.y, row.w, row.h, kPopupBorderColor);
                    }
                } else {
                    // Real menu item rows (mainmenu.s etc., no real x/y of
                    // their own -- AddMenuItem never takes one) are
                    // centered on screen -- confirmed against a real
                    // screenshot, which also shows no left-margin arrow
                    // glyph on the selected row (selection is color-only,
                    // kSelectedTextColor).
                    int textX = (sk::Backbuffer::kWidth - textW) / 2;
                    sk::BitmapFont::DrawString(backbuffer, textX, y, label, color);
                    y += lineHeight;
                }
                break;
            }
            case RowKind::ItemButton: {
                auto* item = static_cast<sk_bindings::ItemButtonExecutable*>(row.widget.get());
                if (item->visible()) {
                    // M25: real AddItemButton(x,y,w,h) position
                    // (charactermanager.s's left/right-hand equip-slot
                    // boxes) when given one; the icon is drawn at the
                    // box's own top-left, falling back to the old
                    // vertical-list placement (icon+label on one running-
                    // cursor line) for any other real AddItemButton()
                    // call site this port doesn't otherwise position.
                    int boxX = row.x >= 0 ? row.x : 12;
                    int boxY = row.x >= 0 ? row.y : y;
                    // Real icon (docs/GRAPHICS_FORMAT.md) when the
                    // real ItemExecutable::SetIcon() id resolved to a
                    // decoded, row-sized slot -- global.spr's icon ids
                    // aren't all small row icons (some of the same
                    // ids item scripts use are full-screen 176x208
                    // panels, presumably for a detail/examine view
                    // this port doesn't have), so only draw ones that
                    // actually fit a list row; anything bigger falls
                    // back to the label-only rendering below, same as
                    // a missing/undecoded slot.
                    int textX = boxX;
                    int textY = boxY;
                    const sk::Sprite* icon = sprites.GetSprite(item->icon());
                    if (icon && icon->width <= 40 && icon->height <= 40) {
                        backbuffer.Blit(textX, textY, *icon);
                        textY += icon->height + 2;
                    }
                    std::string label = RowText(item->textId(), item->itemText(), strings);
                    sk::BitmapFont::DrawString(backbuffer, textX, textY, label, color);
                    if (row.x < 0) {
                        y += lineHeight;
                    }
                }
                break;
            }
            case RowKind::Table: {
                auto* table = static_cast<sk_bindings::TableExecutable*>(row.widget.get());
                // Only a handful of rows fit on a 208px-tall screen
                // alongside everything else on the page -- show a window
                // centered on the current selection rather than the whole
                // table (a real scrollable viewport, docs/PORT_ROADMAP.md
                // flags this table widget's layout as simplified overall).
                constexpr int kVisibleRows = 6;
                // M36: AddTable's own real x/y (now recorded -- see the
                // AddTable handler) instead of a hardcoded x with the
                // shared vertical cursor. On the equip screen that is the
                // difference between the item list starting below the
                // category icons, where the script put it, and starting on
                // top of them.
                int tableX = row.x >= 0 ? row.x : 16;
                if (row.y >= 0) y = row.y;
                int selected = table->selectedRow();
                // Parenthesized -- this file transitively includes the
                // vendored Simkin headers (skGeneral.h), which '#define
                // max(a,b)'/'min(a,b)' as plain macros; (std::max)(...)
                // defeats the function-like-macro expansion without
                // touching that vendored file.
                int start = (std::max)(0, selected - kVisibleRows / 2);
                int end = (std::min)(table->rowCount(), start + kVisibleRows);
                for (int r = start; r < end; ++r) {
                    std::string line = table->CellText(r, 0);
                    for (int c = 1; c < table->columnCount(); ++c) {
                        std::string cell = table->CellText(r, c);
                        if (!cell.empty()) line += "  " + cell;
                    }
                    uint16_t rowColor = (isSelected && r == selected) ? kSelectedTextColor
                                                                       : kTextColor;
                    sk::BitmapFont::DrawString(backbuffer, tableX, y, line, rowColor);
                    y += lineHeight;
                }
                if (table->rowCount() == 0) {
                    sk::BitmapFont::DrawString(backbuffer, tableX, y, "(empty)", kStaticTextColor);
                    y += lineHeight;
                }
                break;
            }
            case RowKind::Slider: {
                // M28: the real Options screen's volume rows. Drawn as
                // "< Label ####------ >" -- the angle brackets match the
                // combo-box row's own convention for "Left/Right adjusts
                // this", and the bar is a plain character meter (no real
                // slider art was found in global.spr; the two slots
                // options.s references are text ids, not sprite ids).
                auto* slider = static_cast<sk_bindings::SliderExecutable*>(row.widget.get());
                std::string name = RowText(row.textId, row.literalText, strings);
                // Shrink the meter until the whole row fits the real
                // 176px screen -- the font is proportional and the real
                // labels ("Sound Volume", "Music Volume") are long, so a
                // fixed cell count runs off the right edge.
                constexpr int kLeftMargin = 6;
                int maxWidth = sk::Backbuffer::kWidth - kLeftMargin * 2;
                std::string label;
                for (int cells = 10; cells >= 3; --cells) {
                    int filled = slider->maxValue() > 0
                                      ? slider->value() * cells / slider->maxValue()
                                      : 0;
                    filled = std::clamp(filled, 0, cells);
                    std::string bar(static_cast<size_t>(filled), '#');
                    bar += std::string(static_cast<size_t>(cells - filled), '-');
                    label = "<" + name + " " + bar + ">";
                    if (sk::BitmapFont::TextWidth(label) <= maxWidth) break;
                }
                sk::BitmapFont::DrawString(backbuffer, kLeftMargin, y, label, color);
                y += lineHeight;
                break;
            }
            case RowKind::ComboBox: {
                auto* combo = static_cast<sk_bindings::ComboBoxExecutable*>(row.widget.get());
                int value = combo->currentOptionValue();
                std::string label = value >= 0 ? ("< " + strings.Get(value) + " >") : "< -- >";
                sk::BitmapFont::DrawString(backbuffer, 12, y, label, color);
                y += lineHeight;
                break;
            }
            case RowKind::TextArea: {
                auto* textArea = static_cast<sk_bindings::TextAreaExecutable*>(row.widget.get());
                int lines = DrawWrappedText(backbuffer, 12, y, strings.Get(row.textId),
                                             textArea->textWidth(), color);
                y += lines * lineHeight;
                break;
            }
            case RowKind::FloatingSprite: {
                auto* fsprite = static_cast<sk_bindings::FloatingSpriteExecutable*>(row.widget.get());
                // M25: charactermanager.s's own real player portrait
                // (`portrait.SetSprite(GetPlayer().GetPortraitID())`) --
                // a real global.spr slot, drawn for real at its own real
                // position instead of a bracketed text label. Falls back
                // to the old placeholder label for a FloatingSprite that
                // never got a real sprite id (ChoosePortraitMenu.s's own
                // rows, which are chosen by callback, not by rendering a
                // sprite) or whose id doesn't decode.
                const sk::Sprite* portrait =
                    fsprite->spriteId() >= 0 ? sprites.GetSprite(fsprite->spriteId()) : nullptr;
                if (portrait && row.x >= 0) {
                    backbuffer.Blit(row.x, row.y, *portrait);
                } else {
                    std::string label = "[ " + SpriteLabel(fsprite->callback()) + " ]";
                    if (row.x >= 0) {
                        sk::BitmapFont::DrawString(backbuffer, row.x, row.y, label, color);
                    } else {
                        sk::BitmapFont::DrawString(backbuffer, 12, y, label, color);
                        y += lineHeight;
                    }
                }
                break;
            }
            case RowKind::TextEntry: {
                std::string label = "NAME: " + player.charNameBuffer() + "_";
                sk::BitmapFont::DrawString(backbuffer, 12, y, label, kSelectedTextColor);
                y += lineHeight;
                break;
            }
        }
    }

    if (auto* popup = menu.activePopup()) {
        RenderPopup(backbuffer, *popup, strings);
    }
}

// M10/M14: the always-on HUD during the 3D view. M10 first placed a
// hand-drawn vitals HUD bottom-left (a real screenshot comparison had
// shown that's where the real bars sit, inside ornate art this port
// couldn't yet draw). M14 (this session) decompiled the real HUD draw
// functions and wires up the real assets docs/GRAPHICS_FORMAT.md's HUD
// section documents in full -- traced by finding the *fixed* (not
// variable-indexed) 384-slot cache addresses the compiler folds a
// compile-time-constant slot index into (engine+0x4460+slot*4 with slot
// a literal collapses to one constant address, e.g. slot 1 ->
// engine+0x4464 -- invisible to a plain "0x4460" text search, which is
// why this took a second pass; shadowkey/ghidra/scripts/
// pyghidra_grep_decompiled.py found the handful of functions using
// those specific fixed addresses).
//
// **Compass banner** (`FUN_1002ba64`, decompiled in full): draws
// global.spr slot 0 (a 322x13 strip repeating "N...E...S...W...", wider
// than the screen on purpose) at screen (52,5), showing only a 68px
// window starting at a heading-derived source X offset, then draws slot
// 1 (176x31, dragon-head-flanked frame with a transparent center
// window) on top at (0,0) -- the frame's transparent gap is exactly
// where the scrolled tape shows through. The real source-offset formula
// reads the *high byte* of a 16-bit heading field at player+0xb6 (an
// 8-bit angle, 0-255 across a full turn) as a signed value, wrapped into
// [0,255] and capped at 254. This port has no equivalent 16-bit fixed-
// point heading field (Camera::yaw is a float radian, see camera.h) and
// no real screenshot to confirm which turn direction should scroll the
// tape which way -- HeadingToCompassOffset() below is a best-effort,
// unverified-direction mapping from yaw to that same [0,254] range, not
// a decompiled formula.
//
// **Vitals bar cluster** (`FUN_1002ae88`, decompiled in full,
// corrected post-M14): this -- not `FUN_1002c010` below -- is the real
// always-on vitals HUD. Found via `FUN_1002ae88`'s actual (direct,
// non-vtable) caller `FUN_10029cb0`, which is itself vtable slot +0x1c
// on the same `ScreenModeController` secondary vtable, confirmed called
// unconditionally every tick from `GameTick_UpdateAndPresent`; inside
// it, the gameplay-screen-mode branch (state == 5) calls exactly
// `FUN_1002ae88(); FUN_1002ba64(); FUN_1002bb54();` -- vitals, compass,
// hand icons, together, every frame. `FUN_1002ae88` draws THREE 39x9
// bars (global.spr slots 162=red, 160=blue/white, 161=green, each 39x5)
// at (10,182)/(10,190)/(10,196), each width-clipped to its own
// percentage fill, then ONE shared frame (slot 180, 57x46, dragon-head/
// wing art) on top at (0,162) masking all three. Field-order in the
// underlying stat struct (max fields at +0x24/+0x26/+0x28, matching
// red/green/blue in that address order -- i.e. health/fatigue/magicka,
// not health/magicka/fatigue) plus standard health=red/magicka=blue/
// fatigue=green color convention fixes the three bars as health (top),
// magicka (middle), fatigue (bottom).
//
// A live user-provided screenshot from real gameplay confirms this
// exact 3-bar cluster (docs/GRAPHICS_FORMAT.md's HUD section has the
// verification writeup).
//
// **Open item**: a real screenshot *also* shows a second, different
// vitals widget -- `FUN_1002c010`'s single big bar (global.spr slot 205,
// 79x9 red gradient, at (44,182); slot 206, 94x42 dragon-wing frame, at
// (40,166)) -- in actual play. Exhaustively searched (whole-memory scan
// for both the function's own address and its containing vtable's base
// address as raw 4-byte words): `FUN_1002c010` is referenced exactly
// once anywhere in the program, at its one static vtable slot
// (`ScreenModeController`+0x44) -- there's no second static caller to
// find, meaning the real trigger is a fully dynamic/computed dispatch
// this pass couldn't resolve (not this project's usual "just search
// harder" case). Left unimplemented rather than guessing at a trigger
// condition; a real research lead if this HUD is revisited: whatever
// game state raises it is probably rare/conditional (an enemy
// lock-on/target health bar is the leading guess, given it's a single
// bar with no magicka/fatigue counterpart -- but unconfirmed).
//
// **Equipped-item icons** (`FUN_1002bb54`, decompiled in full): the
// real function draws the left/right hand's equipped item icon at
// (5,5)/(139,5) -- flanking the compass banner in the same top HUD
// row. Ties directly into this port's own PlayerExecutable::leftItem()/
// rightItem() (the real hand-equip system decompiled two sessions ago).
void RenderHud(sk::Backbuffer& backbuffer, const sk_bindings::PlayerExecutable& player,
                sk::SpriteArchive& sprites, float cameraYaw) {
    const sk::Sprite* compassTape = sprites.GetSprite(0);
    const sk::Sprite* compassFrame = sprites.GetSprite(1);
    if (compassTape && compassFrame) {
        constexpr float kTwoPi = 6.28318530718f;
        float turns = cameraYaw / kTwoPi;
        turns -= std::floor(turns);  // wrap to [0,1)
        int offset = static_cast<int>(turns * 255.0f);
        offset = (std::max)(0, (std::min)(253, offset));
        backbuffer.BlitRegion(52, 5, *compassTape, offset, 68);
        backbuffer.Blit(0, 0, *compassFrame);
    }

    auto drawHandIcon = [&](sk_bindings::ItemExecutable* handItem, int x) {
        if (!handItem) return;
        const sk::Sprite* icon = sprites.GetSprite(handItem->icon());
        if (icon && icon->width <= 32 && icon->height <= 32) backbuffer.Blit(x, 5, *icon);
    };
    drawHandIcon(player.leftItem(), 5);
    drawHandIcon(player.rightItem(), 139);

    const sk::Sprite* vitalsFrame = sprites.GetSprite(180);
    auto drawVitalBar = [&](int slot, int y, int value, int maxValue) {
        const sk::Sprite* fill = sprites.GetSprite(slot);
        if (!fill) return false;
        float fraction = maxValue > 0
                              ? (std::max)(0.0f, static_cast<float>(value)) / static_cast<float>(maxValue)
                              : 0.0f;
        int fillWidth = static_cast<int>(fill->width * (std::min)(1.0f, fraction));
        backbuffer.BlitRegion(10, y, *fill, 0, fillWidth);
        return true;
    };
    bool health = drawVitalBar(162, 182, player.health(), player.maxHealth());
    bool magicka = drawVitalBar(160, 190, player.magicka(), player.maxMagicka());
    bool fatigue = drawVitalBar(161, 196, player.fatigue(), player.maxFatigue());
    if (vitalsFrame && (health || magicka || fatigue)) {
        backbuffer.Blit(0, 162, *vitalsFrame);
    } else {
        // Fallback (missing asset): flat bars, same footprint the real
        // cluster occupies.
        constexpr int kBarWidth = 39, kBarHeight = 5, kBarGap = 3;
        int y = 182;
        auto drawFlatBar = [&](int value, int maxValue, uint16_t color) {
            int filled = maxValue > 0 ? (kBarWidth * (std::max)(0, value)) / maxValue : 0;
            filled = (std::min)(filled, kBarWidth);
            for (int dy = 0; dy < kBarHeight; ++dy) {
                for (int dx = 0; dx < kBarWidth; ++dx) {
                    backbuffer.SetPixel(10 + dx, y + dy,
                                         dx < filled ? color : kPopupBorderColor);
                }
            }
            y += kBarHeight + kBarGap;
        };
        drawFlatBar(player.health(), player.maxHealth(), sk::PackRGB565(200, 40, 40));
        drawFlatBar(player.magicka(), player.maxMagicka(), sk::PackRGB565(60, 80, 220));
        drawFlatBar(player.fatigue(), player.maxFatigue(), sk::PackRGB565(60, 180, 80));
    }
}

void RenderCredits(sk::Backbuffer& backbuffer, const std::vector<std::string>& lines,
                    int scrollOffset) {
    backbuffer.Fill(kBackgroundColor);
    const int lineHeight = sk::BitmapFont::kGlyphHeight + 4;
    int y = 8;
    int maxLines = sk::Backbuffer::kHeight / lineHeight;
    for (int i = 0; i < maxLines && (scrollOffset + i) < static_cast<int>(lines.size()); ++i) {
        sk::BitmapFont::DrawString(backbuffer, 4, y, lines[static_cast<size_t>(scrollOffset + i)],
                                    kTextColor);
        y += lineHeight;
    }
}

// M26: turns a real zone name into real display text -- the actual
// lookup table lives in assets/zone_display_names.h (testable without the
// windowed game loop, its own comment has the full writeup); ffarena, or
// anything else not in that real table, falls back to its own raw
// internal name (a documented gap, not a guess at a slot number that
// isn't actually there).
std::string ZoneDisplayName(const sk::StringTable& strings, const std::string& zoneName) {
    int id = sk::ZoneDisplayNameStringId(zoneName);
    return id >= 0 ? strings.Get(id) : zoneName;
}

// M26: the real zone-transition loading screen (`FUN_1002c010`,
// ScreenModeController's own secondary-vtable slot +0x44) -- per the
// user's real-gameplay correction, this is NOT a combat health bar (the
// prior "enemy lock-on" guess in docs/PORT_ROADMAP.md's "Next
// milestones"), it's a loading-progress bar shown during zone/level
// travel. Fresh RE this session confirmed: the bar's fill genuinely
// tracks `GameEngine_InitLevel`'s own real progress counter (a live
// percentage sequence -- 0,3,5,...,100 -- written as real loading stages
// complete on a real background thread), not a decorative animation; the
// full-screen splash behind it is `global.spr` slot 174 (visually
// confirmed against the user's own reference screenshot -- the glowing
// key / "The Elder Scrolls Travels SHADOWKEY" logo art); the bar itself
// is slot 205 (79x9 red gradient, drawn at (44,182), width-clipped to
// the real percentage) inside slot 206's frame (94x42 dragon-wing motif,
// at (40,166)); the "Travel to: <zone>" banner text is real too (string
// 3950 "Travel to: " concatenated with a real per-zone display name,
// ZoneDisplayName() above).
//
// M54 resolves the last of the four states that reach this draw. The gate
// really is `mode == 3 || mode == 4 || mode == 10 || mode == 0x1f`, and
// all four are now named: **3** = zone travel, **4** = saving, **10** =
// loading a saved game, **0x1f** = quit to the main menu. The banner is
// *not* drawn for all four -- the real code draws it under
// `mode == 10 || mode == 3` only, i.e. exactly the two that travel to a
// named zone -- so callers pass an empty string for the other two, which
// is what the quit-to-menu screen below does. The percentage the bar
// samples is the one counter both background threads write
// (`appview+0x408`), so a save and a quit fill the same bar a zone load
// does, just with their own stage lists.
//
// **Not decompiled, this port's own choice**: this port's zone loading is
// synchronous (main.cpp's zone-load block runs to completion in a single
// tick, not on a real background thread), so there's no live progress to
// sample -- `percent` here steps through the real documented stage list
// on a fixed per-tick schedule purely for visual continuity with the real
// screen, not a measurement of actual work done. The exact screen
// position of the "Travel to" text is likewise undetermined; this port
// shows the banner for every zone change after the first (the first uses
// "Loading..." instead, string 3820) as the best-evidenced substitute.
void RenderLoadingScreen(sk::Backbuffer& backbuffer, sk::SpriteArchive& sprites,
                          const std::string& text, int percent) {
    const sk::Sprite* splash = sprites.GetSprite(174);
    if (splash) {
        backbuffer.Blit(0, 0, *splash);
    } else {
        backbuffer.Fill(kBackgroundColor);
    }
    int textX = (sk::Backbuffer::kWidth - sk::BitmapFont::TextWidth(text)) / 2;
    sk::BitmapFont::DrawString(backbuffer, (std::max)(0, textX), 4, text, kTitleColor);
    const sk::Sprite* frame = sprites.GetSprite(206);
    const sk::Sprite* fill = sprites.GetSprite(205);
    if (fill) {
        int fillWidth = fill->width * (std::max)(0, (std::min)(100, percent)) / 100;
        backbuffer.BlitRegion(44, 182, *fill, 0, fillWidth);
    }
    if (frame) backbuffer.Blit(40, 166, *frame);
}

// M44: `Level.Vignette(n)` -- the full-screen story slideshow. See
// LevelExecutable::VignetteDefinition for the recovered table and for the
// real screen-mode machinery this stands in for.
//
// The real one advances one screen mode per 0x700 engine time units
// (0x700 / 0x100 = 7 seconds at the engine's own 1/256s clock), draws the
// slide's sprite at (0xe, 5) and its caption underneath, and lets any key
// skip straight out. All three are reproduced; what is not is the fade
// between slides, which lives in the screen-mode controller this port
// does not have.
constexpr int kVignetteSlideUnits = 0x700;
constexpr int kVignetteSpriteX = 0xe;
constexpr int kVignetteSpriteY = 5;

void RenderVignette(sk::Backbuffer& backbuffer, sk::SpriteArchive& sprites,
                     const sk::StringTable& strings,
                     const sk_bindings::LevelExecutable::VignetteDefinition& def, int slide) {
    backbuffer.Fill(kBackgroundColor);
    const sk::Sprite* art = sprites.GetSprite(def.firstSprite + slide);
    if (art) backbuffer.Blit(kVignetteSpriteX, kVignetteSpriteY, *art);
    // The real draw puts the caption at y=0x73 with a fixed wrap width;
    // this port's font helper has no wrapper, so the string is drawn
    // centred on one line -- the same simplification the loading screen's
    // own banner already makes.
    std::string caption = strings.Get(def.firstTextId + slide);
    int textX = (sk::Backbuffer::kWidth - sk::BitmapFont::TextWidth(caption)) / 2;
    sk::BitmapFont::DrawString(backbuffer, (std::max)(0, textX), 0x73, caption, kTitleColor);
}

}  // namespace

int main(int argc, char** argv) {
    // User-requested: every std::printf this whole codebase already does
    // (action traces, SoftFailNativeCall/"not implemented" logs, load
    // errors, ...) also lands in a plain text file, unchanged, so a play
    // session's unimplemented-call traces can be reviewed afterward
    // without copying them out of the console by hand. See platform/
    // win32/console_tee.h's own comment for why this is a real OS-level
    // tee (keeps the live console working too) rather than a plain
    // `freopen`. As early as possible -- anything printed before this
    // call only reaches the console, not the log.
    sk::StartConsoleTeeLog("shadowkey_port.log");
    // M36: right after the tee, so a crash report lands in the log too.
    sk::InstallCrashReporter();

    // Both this and fontPath's default below are resolved relative to the
    // executable's own location (exe_dir.h), not the process's current
    // working directory -- that varies by launch method (double-click in
    // Explorer, running from port/build/, running from the repo root)
    // and previously left the .gdr font unfindable whenever CWD wasn't
    // the repo root, even though the file was sitting right where
    // .gitignore says it should be (port/assets/fonts/). The build always
    // places this exe at <repo>/port/build/, so the repo root is two
    // directories up.
    const std::string repoRoot = sk::ExecutableDirectory() + "/../..";
    const std::string defaultScriptRoot =
        repoRoot +
        "/The-Elder-Scrolls-Travels-Shadowkey_N-Gage_EN-FR-DE-ES-IT_USA-Europe-"
        "EnFrDeEsIt-26102004/system/apps/6r51";
    const char* scriptRoot = argc > 1 ? argv[1] : defaultScriptRoot.c_str();

    sk::StringTable strings;
    if (!strings.Load(std::string(scriptRoot) + "/stringtable.eng")) {
        std::printf("shadowkey-port: failed to load stringtable.eng from %s, aborting.\n",
                    scriptRoot);
        return 1;
    }

    // M8: the global model archive (models.idx/.huge) and entity type
    // table (entities.txt) -- loaded once at startup, matching the real
    // engine's own GameEngine_FirstTickBootstrap timing for
    // EntityTypeConfig_Load (docs/ZONE_FORMAT.md). Not fatal to fail --
    // the game still runs, just without placed entities in the 3D view.
    sk::EntityTypeTable entityTypes;
    entityTypes.Load(scriptRoot);
    sk::ModelArchive modelArchive;
    modelArchive.Load(scriptRoot);

    // Real menu backgrounds/HUD icons (docs/GRAPHICS_FORMAT.md's "The
    // 384-slot image cache's real source format", docs/PORT_ROADMAP.md's
    // milestone) -- global.spr is the same global archive every menu
    // and zone icon set draws from; menu_sprites.txt prewarms exactly
    // the slots the main-menu/character-creation flow (running before
    // any zone is loaded) needs, including the real MenuBackground()
    // ids (20/69/174, confirmed by grepping every real script's call).
    // Not fatal to fail -- RenderMenu() falls back to a flat color per
    // menu when a slot isn't available, same "optional asset" spirit as
    // modelArchive/entityTypes above.
    sk::SpriteArchive spriteArchive;
    if (spriteArchive.Load(scriptRoot)) {
        spriteArchive.LoadCategory(scriptRoot, "menu");
    }

    // M27: real audio -- see assets/sound_archive.h's class comment (the
    // real per-zone <zone>_sounds.txt manifest convention, corpus-
    // verified against real PlaySound()/PlayAmbient() call sites) and
    // audio/audio_engine.h (XAudio2 playback). Init() failing (no audio
    // device, XAudio2 unavailable) is non-fatal -- every PlaySound/
    // PlayAmbient call below just silently no-ops, same "optional
    // subsystem" tolerance as every other real asset in this port.
    sk::SoundArchive soundArchive;
    sk::AudioEngine audioEngine;
    audioEngine.Init();

    // M51: the engine's two ways of starting a sound.
    //
    // `FUN_1001b204` plays at the *listener's* own position -- it passes
    // the player's x/y as the source, so the distance term is always zero
    // and the sound is heard at full volume. Every UI and player-action
    // sound goes through it.
    auto playPlayerSound = [&](int slot, int volume = sk::kDefaultSoundVolume) {
        const sk::Sound* sfx = soundArchive.GetSound(slot);
        if (sfx) audioEngine.PlaySfx(*sfx, volume);
    };

    // The real N-Gage menu/UI font (bitmap_font.h/.cpp's class
    // comments, docs/GRAPHICS_FORMAT.md). IS the device ROM's own
    // Ceurope.gdr after all -- confirmed by decompiling shadowkey's
    // real text-draw call chain (DrawUIText -> FUN_1008f8a4 ->
    // FUN_10022b20, whose fontNum-gated branch calls genuine
    // EIKCORE::LegendFont() for ordinary menu text), then verifying
    // pixel-for-pixel: downscaling a real screenshot back to native
    // 176x208 (undoing a video capture's ~4.4x upscale) shows every
    // glyph in "New Game" matches Ceurope.gdr's LatinBold12 exactly.
    // An earlier pass in this same session wrongly concluded it was a
    // "Nokia Cellphone FC" TrueType lookalike -- that was comparing
    // against the *upscaled, video-compressed* screenshot, where
    // compression blur on this blocky bitmap font reads as "rounded"
    // to the eye. Like the retail game install, Ceurope.gdr is Nokia
    // device firmware, never committed to this repo (see .gitignore);
    // point argv[2] at wherever it's been extracted to locally.
    // Missing/bad file is non-fatal -- DrawString keeps using the
    // placeholder glyphs, same as every other optional real asset here.
    const std::string defaultFontPath = repoRoot + "/port/assets/fonts/Ceurope.gdr";
    const char* fontPath = argc > 2 ? argv[2] : defaultFontPath.c_str();
    sk::BitmapFont::LoadRealFont(fontPath, "LatinBold12");

    skInterpreter interpreter;
    sk_bindings::MenuStack stack(scriptRoot, interpreter, &strings, &soundArchive, &audioEngine);

    // M50: save slots are real files now (menu_stack.h). The real game
    // keeps them under `c:\systemppsR51\`; here they go beside the
    // executable, resolved the same way as every other asset path above
    // so a save does not land in whatever directory the game was launched
    // from.
    stack.SetSaveDirectory(sk::ExecutableDirectory());
    // M21: Level.CreateEntity() needs entities.txt to resolve a typeId --
    // see level_executable.h's class comment.
    stack.level().SetEntityTypes(&entityTypes);

    // M51: the front-end's own music, which M27 could only guess at.
    // `FUN_1002707c` -- the "back to the front end" path (it resets the
    // level, clears the multiplayer session and drops the player) --
    // loads the `menu` sound bank by name and then calls
    // `FUN_1001b180(engine, 0x46, 100, 0xff)`: slot 70 of
    // menu_sounds.txt, which is `battle3.ogg`, at volume 100, repeating
    // 255 times. That is the trigger no script has, and it is native
    // because returning to the menu is native.
    soundArchive.LoadCategory(scriptRoot, sk::kMenuSoundCategory);
    if (const sk::Sound* menuMusic = soundArchive.GetSound(sk::kSoundMenuMusic)) {
        audioEngine.PlayMusic(*menuMusic);
    }

    std::string mainMenuPath = std::string(scriptRoot) + "/mainmenu.s";
    try {
        // Registered under "MainMenu" -- the exact string every "back to
        // main menu" handler across the corpus (SaveGameMenuBack,
        // LoadGameMenuBack, OptionsBack, newgamemenu.s's MenuQuit, ...)
        // passes to OpenMenu(). Without this, each of those would
        // silently construct and switch to a *new*, separate MenuStack-
        // owned instance instead of coming back to this same one.
        stack.CreateRootMenu("MainMenu", mainMenuPath);
    } catch (skParseException& e) {
        std::printf("shadowkey-port: PARSE ERROR loading mainmenu.s: %s\n", e.toString().ptr());
        return 2;
    } catch (skRuntimeException& e) {
        std::printf("shadowkey-port: RUNTIME ERROR running mainmenu.s: %s\n", e.toString().ptr());
        return 2;
    }

    sk::Window window(sk::Backbuffer::kWidth * 3, sk::Backbuffer::kHeight * 3,
                       L"shadowkey-port (M6: 3D zone renderer)");

    sk::InputState input;

    // M52: `dragonstar.set` -- the one configuration file the real engine
    // writes (assets/game_config.h). Loading it here is the counterpart of
    // the real loader running before the front end: the action map, the
    // two volumes and the mute-on-call flag all come back, and anything
    // the file does not mention keeps the default the constructors gave
    // it. A missing file is not an error -- a first run has none.
    const std::string configPath =
        sk::ExecutableDirectory() + "/" + sk::GameConfig::kFileName;
    sk::GameConfig config;
    config.CaptureBindings(input);
    config.soundVolume = audioEngine.sfxVolumePercent();
    config.musicVolume = audioEngine.musicVolumePercent();
    if (config.Load(configPath)) {
        config.ApplyBindings(input);
        audioEngine.SetSfxVolumePercent(config.soundVolume);
        audioEngine.SetMusicVolumePercent(config.musicVolume);
        stack.SetMuteOnCall(config.muteOnCall);
        std::printf("loaded %s\n", configPath.c_str());
    }
    // The real `SaveConfig` is a script binding the options screen calls,
    // so writing on exit is not what the engine does -- but the engine
    // also cannot be closed by a window button. Capture-then-write keeps
    // the file in step with whatever the Options screen changed.
    struct ConfigWriter {
        sk::GameConfig& cfg;
        const sk::InputState& input;
        sk::AudioEngine& audio;
        sk_bindings::MenuStack& stack;
        std::string path;
        ~ConfigWriter() {
            cfg.CaptureBindings(input);
            cfg.soundVolume = audio.sfxVolumePercent();
            cfg.musicVolume = audio.musicVolumePercent();
            cfg.muteOnCall = stack.muteOnCall();
            cfg.Save(path);
        }
    } configWriter{config, input, audioEngine, stack, configPath};

    window.SetKeyCallback([&](int vkCode, bool down) {
        if (auto slot = sk::MapPcKeyToButtonSlot(vkCode)) {
            input.SetButton(*slot, down);
        }
    });
    window.SetCharCallback([&](wchar_t ch) {
        sk_bindings::MenuExecutable* menu = stack.currentMenu();
        if (!menu || !menu->textEntryActive()) return;
        if (ch == 0x08) {
            stack.player().BackspaceCharName();
        } else if (ch >= 0x20 && ch < 0x7F && stack.player().charNameBuffer().size() < 20) {
            stack.player().AppendCharNameChar(static_cast<char>(ch));
        }
        // 0x0D (Enter) is handled in the tick loop below, not here --
        // it needs to fire a script callback, which the char callback
        // (running off the message pump, not the tick loop) shouldn't do.
    });

    sk::Backbuffer backbuffer;
    sk::GameClock clock;

    std::printf(
        "shadowkey-port: menus -- Up/Down move selection, Left/Right cycle combo values or "
        "navigate horizontal screens, Enter confirms, Esc goes back.\n");
    std::printf(
        "shadowkey-port: New Game/Load Game enter the 3D zone (M6) -- Up/Down walk, "
        "Left/Right turn, Esc returns to the main menu.\n");
    std::printf(
        "shadowkey-port: '3' (real default Use binding, M15) opens/closes the nearest door "
        "you're facing -- '7'/'5' swing your left/right-hand weapon (M12).\n");
    std::printf(
        "shadowkey-port: '=' (stands in for the N-Gage's '#') opens the character manager "
        "(real inventory/stats/quest-log screens, M10) -- Esc there always returns straight "
        "to the 3D view.\n");

    sk_bindings::MenuExecutable* lastMenu = nullptr;
    int creditsScroll = 0;

    // M6: the 3D zone renderer, entered when a menu calls NewGame()/
    // LoadGame() (see MenuStack::RequestGameStart()). M7 added simple
    // circle-vs-wall-tile collision (Zone::CircleHitsWall); M8 added
    // placed-entity rendering (props/monsters/doors resolved through
    // entities.txt -> models.idx) -- see render3d/zone_renderer.h for
    // what each milestone does and doesn't reproduce from the real
    // engine.
    std::unique_ptr<sk::Zone> gameZone;
    // M23: the real zone-root script (e.g. azra.s) -- Init() runs once,
    // right after this zone's doors/monsters/pickups are loaded and
    // registered into the Level global (see the zone-load block below).
    // Kept alive for the whole time this zone is loaded even though
    // nothing currently calls anything else on it (EnterZone() isn't
    // wired up yet, see zone_script_executable.h's class comment) --
    // matches gameZone/gameDoors/etc.'s own lifetime.
    std::unique_ptr<sk_bindings::ZoneScriptExecutable> gameZoneScript;
    std::vector<sk::PlacedEntity> gameEntities;
    // Combat vertical-slice: live monsters, pulled out of gameEntities'
    // static-prop list at zone load (see below) -- see MonsterInstance's
    // comment.
    std::vector<MonsterInstance> gameMonsters;
    // M15: live doors, pulled out of gameEntities the same way gameMonsters
    // is -- see DoorInstance's comment.
    std::vector<DoorInstance> gameDoors;
    // M38: this zone's real trap placements -- see TrapInstance's comment.
    std::vector<TrapInstance> gameTraps;
    // M38: every placement's (position, typeId, name), kept only long
    // enough to build gameTraps once the zone script has registered its
    // triggers (which happens after the placement loop, because those
    // triggers' own Init() resolves Level.GetEntity() names).
    std::vector<TrapInstance> gameTrapCandidates;
    // M19: live world pickups, pulled out of gameEntities the same way --
    // see PickupInstance's comment. Shrinks as items are actually picked
    // up (unlike gameDoors/gameMonsters, which stay fixed-size for a
    // zone's whole lifetime).
    std::vector<PickupInstance> gamePickups;
    // M44: which named `.zon` regions the player is currently standing in,
    // by index into Zone::regions(). The real engine tracks this per
    // actor and fires `FUN_1002ef44` on a transition; here it is a set
    // diffed once per tick, which gives the same edge-triggered behaviour
    // without a room pointer on every entity. Names are not unique -- azra
    // has four rectangles called "YouSure" -- so this is indexed by
    // rectangle, and walking out of one "YouSure" into another really does
    // fire EnterZone("YouSure") again, exactly as the real per-room
    // tracking would.
    std::set<size_t> gameRegionsOccupied;
    // M44: the LockZone/UnlockZone/GetZone bridge -- see LiveZoneRegions.
    LiveZoneRegions gameZoneRegions;
    stack.level().SetZoneRegions(&gameZoneRegions);
    sk::Camera gameCamera;
    // M51: the other way -- `FUN_1001b198` takes a real world position and
    // attenuates by distance from the listener before playing. The curve
    // is linear in *squared* distance, so its 512 hardcodes an audible
    // radius of sqrt(512) ~ 22.6 tiles, and anything inside one tile is
    // at full volume. `directional` additionally asks for the emitter-
    // facing term (at most a 12.5% cut); no shipped call site this port
    // reaches uses it, so it is offered but defaulted off.
    auto playWorldSound = [&](int slot, float worldX, float worldY,
                              int volume = sk::kDefaultSoundVolume) {
        const sk::Sound* sfx = soundArchive.GetSound(slot);
        if (!sfx) return;
        const int dx = static_cast<int>(gameCamera.x - worldX);
        const int dy = static_cast<int>(gameCamera.y - worldY);
        const int attenuated = sk::PositionalVolume(volume, dx, dy);
        if (attenuated <= 0) return;
        audioEngine.PlaySfx(*sfx, attenuated);
    };
    // M25: first-person weapon viewmodel -- see simkin_bindings/
    // weapon_viewmodel.h's comment for the real RE ground truth this
    // recreates.
    sk_bindings::WeaponViewmodel gameWeaponViewmodel;
    // M48: live spell projectiles -- see simkin_bindings/spell_projectile.h.
    // Every offensive spell, the player's and a creature's alike, now goes
    // through one of these instead of touching its target directly.
    std::vector<sk_bindings::SpellProjectile> gameProjectiles;
    // M49: live arrows -- a *different* class from the spell projectile
    // above (0x16c bytes, FUN_10007da4, against 0x198 and FUN_1005f0b4).
    // Every bow, crossbow and thrown weapon on either side goes through one
    // of these; see simkin_bindings/arrow_projectile.h.
    std::vector<sk_bindings::ArrowProjectile> gameArrows;
    // M48: the player's own cast cooldown (FUN_10046680), which is the only
    // side of the cast the gate applies to.
    sk_bindings::SpellCastCooldown gameCastCooldown;
    // M48: the engine's 1/256-second clock (`level+0x460`), which the cast
    // cooldown is compared against. Advanced by the same per-frame delta
    // every other timer in this port uses.
    int gameClockUnits = 0;
    // M48: the player's second periodic channel, watched for a Sanctuary
    // channel's 4 -> 0 expiry, which is what stamps its own re-cast
    // cooldown (`player+0xfc8`).
    int gamePlayerPeriodicKind = 0;
    // M26: real loading-screen state -- see RenderLoadingScreen()'s own
    // comment. Active for a fixed run of ticks after stack.
    // gameStartRequested() fires (both the very first zone entered and
    // every later Level.LoadLevel() transition, MenuStack::
    // RequestZoneChange()'s own comment), covering both the initial
    // "Loading..." case (loadingScreenIsFirstZone, gameZone still null
    // when the request came in) and every later "Travel to: <zone>" one.
    bool loadingScreenActive = false;
    int loadingScreenTick = 0;
    // M54: the same screen for screen mode 0x1f -- a script's own
    // QuitToMenu(). Separate latch from the loading screen's because the
    // two mean opposite things (one is entering a session, the other is
    // ending one) and the real engine keeps them apart the same way, in
    // two different background threads.
    bool quitToMenuActive = false;
    int quitToMenuTick = 0;
    // M44: `Level.Vignette(n)` -- see RenderVignette().
    sk_bindings::LevelExecutable::VignetteDefinition vignetteDef;
    bool vignetteActive = false;
    int vignetteSlide = 0;
    int vignetteTimer = 0;
    bool loadingScreenIsFirstZone = false;
    std::string loadingScreenZoneName;
    sk::ZoneRenderer zoneRenderer;
    bool inGame = false;
    // Post-M11: real per-tick vertical physics (gravity/jump/ground-
    // and-ceiling clamp), see the tick loop below for the full writeup --
    // greenfield gameplay design (no RE ground truth exists for the
    // original's actor physics, docs/WORLD_MODEL.md), same spirit as
    // render3d/camera.h's kEyeHeightOffset.
    float gameVelZ = 0.0f;
    bool onGround = true;
    // M10: set while the character-manager screen chain is open *from*
    // the 3D view (see the CharacterManager action below) -- gameZone/
    // gameCamera stay alive so RightSelectionKey can resume gameplay
    // directly instead of falling through to charactermanager.s's own
    // OnRightSoftKey handler, which calls Quit()+OpenMainMenu() (correct
    // for reaching it from a menu, wrong for reaching it mid-game -- no
    // real in-game pause-menu entry point was ever found to disambiguate
    // the two contexts, so this is a deliberate host-side simplification:
    // RightSelectionKey always means "back to gameplay" here, even from a
    // nested Inventory/Stats/QuestLog screen, rather than backing out one
    // level at a time).
    bool gamePausedForMenu = false;

    window.RunMessageLoop([&]() {
        if (window.ShouldClose()) return;
        if (!clock.PollTick()) return;

        // Drives held-key auto-repeat for menu navigation only -- see
        // InputState::TickRepeats(). Must run before any Consume* call
        // this tick.
        input.TickRepeats();

        // M10: erase any inventory items marked for removal last tick
        // (UseItem/DropItem) -- safe here since any script call chain
        // that marked them has long since returned. See item_executable.h.
        //
        // M25: gameWeaponViewmodel.item is a raw, non-owning pointer into
        // the same inventory (whichever item last swung) -- drop/consume
        // the currently-mid-swing weapon and PurgeRemovedItems() below
        // would otherwise free it out from under that pointer, same
        // use-after-free shape PlayerExecutable::PurgeRemovedItems()
        // itself already guards for m_LeftItem/m_RightItem.
        if (gameWeaponViewmodel.item && gameWeaponViewmodel.item->markedForRemoval()) {
            gameWeaponViewmodel.item = nullptr;
            gameWeaponViewmodel.swingAccum = 0;
            gameWeaponViewmodel.swapTimer = 0;
            gameWeaponViewmodel.currentSprite = -1;
        }
        // M48: same hazard, same tick. A projectile holds the spell script
        // whose HitTarget() its impact runs -- and a *scroll* destroys
        // itself the moment it is read, while its projectile is still in
        // the air. Drop those shots rather than let the purge free the
        // script out from under them.
        if (!gameProjectiles.empty()) {
            gameProjectiles.erase(
                std::remove_if(gameProjectiles.begin(), gameProjectiles.end(),
                                [](const sk_bindings::SpellProjectile& shot) {
                                    return shot.spell && shot.spell->markedForRemoval();
                                }),
                gameProjectiles.end());
        }
        stack.player().PurgeRemovedItems();
        // M27: reaps one-shot SFX voices that finished playing -- see
        // AudioEngine::Update()'s own comment.
        audioEngine.Update();
        // M51: advance an in-flight FadeMusic()/UnFadeMusic(). The real
        // fade lives in the audio tick at 5 units per 256-sample buffer;
        // this drives it by wall-clock instead, so it takes the same
        // ~0.64s either way.
        audioEngine.TickMusicFade(sk::kMixBufferSamples * 1000 / sk::kSampleRateHz);

        if (stack.quitRequested()) {
            window.Close();
            return;
        }

        // M54: `QuitToMenu()` -- screen mode 0x1f, the last of the four
        // states that reach RenderLoadingScreen(). See
        // docs/RENDER_LOOP.md's "Screen mode 0x1f" for the full writeup.
        //
        // The real one hands the work to the `nGEN_Quitting` background
        // thread (`FUN_10026f40` spawns it and writes the mode straight
        // into `controller+0x78`, which is why the value never appears as
        // an argument anywhere), and that thread does, in order: stop the
        // music, free every loaded asset except the two sprite slots the
        // progress bar itself is drawn from, tell the world object to shut
        // down, load the `menu` pseudo-level's manifests, and open
        // MainMenu -- writing 4, 15, 22, 30, 40, 80, 100 into the shared
        // progress counter as it goes. Those seven really are the stages;
        // the fixed per-tick schedule they are stepped on here is this
        // port's own choice, exactly as for the zone-load list below,
        // since the teardown underneath is synchronous and instant.
        //
        // No banner: the real draw gates that on `mode == 3 || mode == 10`
        // and a quit travels to no named zone.
        if (stack.quitToMenuRequested() && !quitToMenuActive) {
            quitToMenuActive = true;
            quitToMenuTick = 0;
            stack.ClearQuitToMenuRequest();
        }
        if (quitToMenuActive) {
            RenderLoadingScreen(
                backbuffer, spriteArchive, std::string(),
                sk::kQuitProgressStages[(std::min)(quitToMenuTick,
                                                    sk::kQuitProgressStageCount - 1)]);
            window.Present(backbuffer);
            ++quitToMenuTick;
            if (quitToMenuTick <= sk::kQuitProgressStageCount) return;
            quitToMenuActive = false;

            // The teardown itself, in the real thread's own order. Scripts
            // first: a zone script, a monster and a pickup all hold raw
            // pointers into the zone and into each other, so nothing may
            // outlive it.
            gameZoneScript.reset();
            gameMonsters.clear();
            gameDoors.clear();
            gamePickups.clear();
            gameTraps.clear();
            gameTrapCandidates.clear();
            gameProjectiles.clear();
            gameArrows.clear();
            gameEntities.clear();
            stack.level().ClearEntities();
            gameRegionsOccupied.clear();
            gameZoneRegions.SetZone(nullptr);
            gameZone.reset();
            stack.SetCurrentLevelName(std::string());
            // Same reset the zone-load block does, for the same reason --
            // FUN_1002fca4 zeroes the script clock whenever the level goes.
            stack.gameClock().Reset();
            gameCastCooldown = sk_bindings::SpellCastCooldown{};
            gameClockUnits = 0;
            gamePlayerPeriodicKind = 0;
            gameWeaponViewmodel = sk_bindings::WeaponViewmodel{};
            gameVelZ = 0.0f;
            onGround = true;
            inGame = false;
            gamePausedForMenu = false;
            loadingScreenActive = false;

            // `FUN_10024c8c(this, "menu")` -- the quit thread reloads the
            // front end's own manifests by name. "menu" is a real
            // pseudo-level: it ships menu_sprites.txt/_models.txt/
            // _sounds.txt but no .zon or .ent, which is why nothing else
            // in this port has ever loaded it as a zone.
            spriteArchive.LoadCategory(scriptRoot, sk::kFrontEndLevelName);
            soundArchive.LoadCategory(scriptRoot, sk::kFrontEndLevelName);
            std::printf("shadowkey-port: QuitToMenu -- session torn down, back to MainMenu\n");
            stack.OpenMenu("MainMenu");
            return;
        }

        // M44: a vignette takes over the whole screen until it ends, the
        // same way the loading screen below does -- the real one is a
        // screen mode, and a screen mode is exclusive. Armed by
        // Level.Vignette(n) from inside an EnterZone handler.
        if (stack.level().TakePendingVignette(vignetteDef)) {
            vignetteActive = true;
            vignetteSlide = 0;
            vignetteTimer = kVignetteSlideUnits;
            std::printf("shadowkey-port: vignette -- %d slide(s), sprites %d..%d, text %d\n",
                        vignetteDef.slides(), vignetteDef.firstSprite, vignetteDef.lastSprite,
                        vignetteDef.firstTextId);
        }
        if (vignetteActive) {
            // Any key skips out, exactly as the real tick's own
            // InputState_GetButton2 sweep does.
            bool skip = false;
            for (int slot = 0; slot < static_cast<int>(sk::ButtonSlot::kCount); ++slot) {
                if (input.ConsumeJustPressed(static_cast<sk::ButtonSlot>(slot))) skip = true;
            }
            vignetteTimer -= sk_bindings::kAiFrameDeltaUnits;
            if (vignetteTimer <= 0) {
                ++vignetteSlide;
                vignetteTimer = kVignetteSlideUnits;
            }
            if (skip || vignetteSlide >= vignetteDef.slides()) {
                vignetteActive = false;
            } else {
                RenderVignette(backbuffer, spriteArchive, strings, vignetteDef, vignetteSlide);
                window.Present(backbuffer);
                return;
            }
        }

        // M26: real loading screen -- see RenderLoadingScreen()'s comment.
        // Latches loadingScreenActive the instant a request comes in
        // (initial game start or a real Level.LoadLevel() transition,
        // MenuStack::RequestZoneChange()) and renders a fixed run of
        // staged frames before the actual (synchronous, effectively
        // instant) zone-load work below ever runs.
        if (stack.gameStartRequested() && !loadingScreenActive) {
            loadingScreenActive = true;
            loadingScreenTick = 0;
            loadingScreenIsFirstZone = (gameZone == nullptr);
            loadingScreenZoneName = stack.requestedZone();
        }
        if (loadingScreenActive) {
            // M54: the stage list moved to engine/screen_mode.h, beside
            // the quit screen's own and the mode table both belong to.
            constexpr int kNumStages = sk::kLoadProgressStageCount;
            int percent = sk::kLoadProgressStages[(std::min)(loadingScreenTick, kNumStages - 1)];
            std::string bannerText =
                loadingScreenIsFirstZone
                    ? strings.Get(3820)  // "Loading..."
                    : strings.Get(3950) + ZoneDisplayName(strings, loadingScreenZoneName);  // "Travel to: <zone>"
            RenderLoadingScreen(backbuffer, spriteArchive, bannerText, percent);
            window.Present(backbuffer);
            ++loadingScreenTick;
            if (loadingScreenTick <= kNumStages) {
                return;
            }
            loadingScreenActive = false;
            // M50: SavedCharacter::levelName -- what a save records and a
            // load returns to. The real engine keeps it in the same place
            // (its app object's own buffer) and FUN_1001ea54 copies it
            // straight back out of a loaded record.
            stack.SetCurrentLevelName(stack.requestedZone());
            stack.ClearGameStartRequest();
            auto zone = std::make_unique<sk::Zone>();
            if (zone->Load(scriptRoot, stack.requestedZone())) {
                // Real per-zone icon set (docs/GRAPHICS_FORMAT.md) --
                // <zone>_sprites.txt includes the same item-icon cluster
                // (ItemExecutable::SetIcon() ids 209-218) every zone
                // ships, since the inventory/equip screens need them
                // available regardless of which zone the player is in.
                spriteArchive.LoadCategory(scriptRoot, stack.requestedZone());
                // M27: real per-zone sound manifest -- same idea as the
                // per-zone icon set above, see assets/sound_archive.h.
                // Loaded before the zone-root script's own Init() runs
                // (below), which is exactly where a real script's own
                // Level.PlayAmbient(id, volume) call (e.g. azra.s) needs
                // it already in place.
                soundArchive.LoadCategory(scriptRoot, stack.requestedZone());
                gameZone = std::move(zone);
                // M44: LockZone/UnlockZone write straight into the cell
                // grid, so the Level global needs the live zone. Repointed
                // on every zone load.
                gameZoneRegions.SetZone(gameZone.get());
                // M44: a fresh zone starts with nobody inside any region,
                // so the first tick fires EnterZone for wherever the
                // player spawns -- which is what azra's "start"/"help1"
                // regions are for.
                gameRegionsOccupied.clear();
                gameCamera.x = static_cast<float>(gameZone->playerStartX);
                gameCamera.y = static_cast<float>(gameZone->playerStartY);
                gameCamera.z = static_cast<float>(gameZone->playerStartZ) + sk::kEyeHeightOffset;
                gameCamera.yaw = 0.0f;
                gameCamera.pitch = 0.0f;
                gameCamera.fovY = 1.2f;
                gameVelZ = 0.0f;
                onGround = true;
                inGame = true;

                // Resolve every placed .ent record to a model archive
                // index via entities.txt -- same two-step chain the real
                // engine walks (docs/ZONE_FORMAT.md), just done eagerly
                // here instead of through the engine+0x6b38 zone-local
                // cache.
                //
                // Combat vertical-slice (M12) + interact binding (M15/
                // M16/M18/M19): category 2 (monster, including named NPCs
                // -- see monster_executable.h's class comment), category 7
                // (merchant, e.g. Gravel_Trothgar -- M18), category 11
                // (door), and category 3 (misc loot/world pickups, e.g.
                // snowline/foxglove.s -- M19) placements whose
                // entities.txt `name` is a real loadable script
                // (HasRealScript() above) are pulled out into
                // gameMonsters/gameDoors/gamePickups instead -- a live
                // MonsterExecutable/DoorExecutable/ItemExecutable actually
                // runs that real script's Init(), same load pattern
                // PlayerExecutable::LoadStartingInventory established for
                // real item scripts. Everything else (including the
                // "!label"-only majority of every category) still goes
                // into gameEntities as a static, unanimated prop,
                // unchanged.
                gameEntities.clear();
                gameMonsters.clear();
                gameDoors.clear();
                gameTraps.clear();
                gameTrapCandidates.clear();
                gamePickups.clear();
                // M48: a projectile holds raw pointers to its caster and to
                // the spell script that owns its HitTarget handler, both of
                // which die with the zone.
                gameProjectiles.clear();
                // M49: same reason -- an arrow holds a raw pointer to its
                // shooter.
                gameArrows.clear();
                // M18: every live object this loop is about to (re)create
                // is stale after this point -- drop any name -> object
                // registrations from the previous zone before repopulating
                // below, so Level.GetEntity() can never return a dangling
                // pointer into a destroyed zone's objects.
                stack.level().ClearEntities();
                // M53: and the script-timer clock restarts with the level,
                // exactly as the engine's does (FUN_1002fca4 zeroes it).
                // Every entity carrying a pending deadline is destroyed
                // right here anyway, so there is nothing left holding a
                // stale one.
                stack.gameClock().Reset();
                // M35: which script a placement actually runs.
                //
                // The .ent record carries its own script path (see
                // Zone::EntPlacement::scriptPath -- the second string in
                // the record tail, which this port used to read as part of
                // the name), and it *overrides* the entities.txt entry for
                // the typeId. That is how a chest gets its contents: every
                // container in the game is an entities.txt "!label" with no
                // script at all, and the loot script (Raiders\RT_A.s and
                // friends -- randomised `Level.CreateEntity`/`AddObject`
                // chains) is named per placement. It is also how a zone
                // gets its own variant of a shared NPC: azra's "tanyin"
                // placement is typeId 166, whose entities.txt script is
                // monsters\Tanyin_Aldwyr.s, but which names
                // monsters\Tanyin_Aldwyr_Azra.s for itself.
                //
                // Checked for existence rather than trusted, because for
                // pure scenery the same field is only a label ("rock",
                // "footlocker"). Falls back to the entities.txt name, i.e.
                // exactly what every branch below used to do.
                auto placementScript = [&](const sk::Zone::EntPlacement& e,
                                            const sk::EntityTypeDescriptor* desc) -> std::string {
                    auto toFullPath = [&](const std::string& rel) {
                        std::string p = rel;
                        std::replace(p.begin(), p.end(), '\\', '/');
                        return std::string(scriptRoot) + "/" + p;
                    };
                    if (!e.scriptPath.empty()) {
                        std::string full = toFullPath(e.scriptPath);
                        if (!HasRealScript(full)) full += ".s";
                        std::ifstream probe(full, std::ios::binary);
                        if (probe.good()) return full;
                    }
                    if (desc && HasRealScript(desc->name)) return toFullPath(desc->name);
                    return std::string();
                };

                for (const sk::Zone::EntPlacement& e : gameZone->entities()) {
                    const sk::EntityTypeDescriptor* desc = entityTypes.Lookup(e.typeId);
                    if (!desc) continue;
                    const std::string placementScriptPath = placementScript(e, desc);
                    // M38: every placement is a possible trigger target --
                    // matched by typeId (lothcav.s's `AddEntity(1013)`) or
                    // by name (crypt1.s's `AddTrigger("door1")`). Filtered
                    // down to the real ones below, once the zone script has
                    // actually registered its triggers.
                    {
                        TrapInstance cand;
                        cand.x = static_cast<float>(e.x);
                        cand.y = static_cast<float>(e.y);
                        cand.typeId = e.typeId;
                        cand.name = e.name;
                        gameTrapCandidates.push_back(std::move(cand));
                    }
                    if (desc->category == 11 && !placementScriptPath.empty()) {
                        const std::string& fullPath = placementScriptPath;
                        skExecutableContext loadCtxt(&interpreter);
                        try {
                            auto door = std::make_unique<sk_bindings::DoorExecutable>(
                                skString(fullPath.c_str()), loadCtxt, stack.player());
                            skRValueArray args;
                            args.append(skRValue(0));  // placeholder for Init's "(s)" parameter
                            skRValue ret;
                            skExecutableContext callCtxt(&interpreter);
                            door->method(skString("Init"), args, ret, callCtxt);
                            DoorInstance inst;
                            inst.placementYaw = PlacementYawRadians(e.yawRaw);
                            inst.x = static_cast<float>(e.x);
                            inst.y = static_cast<float>(e.y);
                            inst.z = static_cast<float>(e.z);
                            inst.modelArchiveIndex = desc->modelArchiveIndex;
                            inst.typeId = e.typeId;   // M38: zone-trigger identity
                            inst.name = e.name;
                            inst.script = std::move(door);
                            stack.level().RegisterEntity(e.name, inst.script.get());
                            gameDoors.push_back(std::move(inst));
                        } catch (skParseException& ex) {
                            std::printf("shadowkey-port: PARSE ERROR loading door %s: %s\n",
                                        fullPath.c_str(), ex.toString().ptr());
                        } catch (skRuntimeException& ex) {
                            std::printf("shadowkey-port: RUNTIME ERROR loading door %s: %s\n",
                                        fullPath.c_str(), ex.toString().ptr());
                        }
                        continue;
                    }
                    // M19/M30: every item-shaped category (see
                    // IsPickupCategory() -- loot, weapons, spells, armor,
                    // containers, consumables, scrolls, shields), same
                    // HasRealScript() gate as every other category this
                    // loop pulls a live object out for. M19 only covered
                    // category 3, leaving chests and every world-dropped
                    // weapon/potion as inert scenery with no prompt.
                    if (IsPickupCategory(desc->category) && !placementScriptPath.empty()) {
                        const std::string& fullPath = placementScriptPath;
                        skExecutableContext loadCtxt(&interpreter);
                        try {
                            auto item = std::make_unique<sk_bindings::ItemExecutable>(
                                skString(fullPath.c_str()), loadCtxt, stack);
                            skRValueArray args;
                            args.append(skRValue(0));  // placeholder for Init's "(s)" parameter
                            skRValue ret;
                            skExecutableContext callCtxt(&interpreter);
                            item->method(skString("Init"), args, ret, callCtxt);
                            PickupInstance inst;
                            inst.placementYaw = PlacementYawRadians(e.yawRaw);
                            inst.isContainer = IsContainerCategory(desc->category);
                            inst.x = static_cast<float>(e.x);
                            inst.y = static_cast<float>(e.y);
                            inst.z = static_cast<float>(e.z);
                            inst.modelArchiveIndex = desc->modelArchiveIndex;
                            inst.script = std::move(item);
                            // M53: pickups were the one loaded category not
                            // in GetEntity()'s registry, so a zone script
                            // could not reach one by name. crypt2.s needs
                            // exactly that -- `Star = GetEntity("star");
                            // Star.Delay(1, 0);` on the seventh crystal is
                            // what starts the Umbra arrival sequence, and
                            // "star" is a category-3 placement.
                            stack.level().RegisterEntity(e.name, inst.script.get());
                            gamePickups.push_back(std::move(inst));
                        } catch (skParseException& ex) {
                            std::printf("shadowkey-port: PARSE ERROR loading pickup %s: %s\n",
                                        fullPath.c_str(), ex.toString().ptr());
                        } catch (skRuntimeException& ex) {
                            std::printf("shadowkey-port: RUNTIME ERROR loading pickup %s: %s\n",
                                        fullPath.c_str(), ex.toString().ptr());
                        }
                        continue;
                    }
                    // M18: category 7 (merchant, e.g. monsters/
                    // Gravel_Trothgar.s) generalized in alongside category
                    // 2 -- same MonsterExecutable shape already covers it
                    // fully (Init()'s SetInvulnerable/SetAggressive(false)/
                    // AddProduct(...) calls all soft-fail or are already
                    // handled exactly like an NPC's, see monster_
                    // executable.cpp; OnUse() opens a real conversation menu
                    // the same way M16's NPC dialogue already does).
                    if ((desc->category == 2 || desc->category == 7) &&
                        !placementScriptPath.empty()) {
                        const std::string& fullPath = placementScriptPath;
                        skExecutableContext loadCtxt(&interpreter);
                        try {
                            auto monster = std::make_unique<sk_bindings::MonsterExecutable>(
                                skString(fullPath.c_str()), loadCtxt, &strings, stack.player(), stack);
                            skRValueArray args;
                            args.append(skRValue(0));  // placeholder for Init's "(s)" parameter
                            skRValue ret;
                            skExecutableContext callCtxt(&interpreter);
                            monster->method(skString("Init"), args, ret, callCtxt);
                            MonsterInstance inst;
                            inst.placementYaw = PlacementYawRadians(e.yawRaw);
                            inst.facingYaw = inst.placementYaw;
                            inst.x = static_cast<float>(e.x);
                            inst.y = static_cast<float>(e.y);
                            inst.z = static_cast<float>(e.z);
                            inst.modelArchiveIndex = desc->modelArchiveIndex;
                            inst.typeId = e.typeId;
                            inst.script = std::move(monster);
                            stack.level().RegisterEntity(e.name, inst.script.get());
                            gameMonsters.push_back(std::move(inst));
                        } catch (skParseException& ex) {
                            std::printf("shadowkey-port: PARSE ERROR loading monster %s: %s\n",
                                        fullPath.c_str(), ex.toString().ptr());
                        } catch (skRuntimeException& ex) {
                            std::printf("shadowkey-port: RUNTIME ERROR loading monster %s: %s\n",
                                        fullPath.c_str(), ex.toString().ptr());
                        }
                        continue;
                    }
                    // Static scenery gets its real .ent heading as well --
                    // barrels, urns, tables and wall fittings were all
                    // being drawn face-on regardless of how the level
                    // author placed them.
                    sk::PlacedEntity prop{static_cast<float>(e.x), static_cast<float>(e.y),
                                           static_cast<float>(e.z), desc->modelArchiveIndex};
                    prop.yaw = PlacementYawRadians(e.yawRaw);
                    gameEntities.push_back(prop);
                }
                // M36: opt-in test aid -- SK_DEBUG_EQUIP equips the first
                // real weapon in the starting inventory, so the weapon
                // viewmodel/swing path can be exercised without walking
                // the inventory UI first.
                if (std::getenv("SK_DEBUG_EQUIP")) {
                    for (const auto& item : stack.player().inventory()) {
                        if (item->itemType() != sk_bindings::kItemTypeWeapon) continue;
                        stack.player().UpdateEquipStatus(item.get(), true);
                        std::printf("shadowkey-port: SK_DEBUG_EQUIP equipped \"%s\"\n",
                                    item->name().c_str());
                        break;
                    }
                }
                // M36: opt-in test spawn. Set SK_DEBUG_SPAWN to a real
                // monster script path (relative to the script root, e.g.
                // "arat.s") to drop one two tiles in front of the player
                // start, facing them. Purely a development aid -- nothing
                // reads this variable unless it is set, so a normal run is
                // byte-identical. It exists because the in-world bugs in
                // this port are otherwise reachable only by playing to
                // wherever a creature happens to be, which makes verifying
                // a fix to creature facing/attacking slow and unreliable.
                if (const char* spawnScript = std::getenv("SK_DEBUG_SPAWN")) {
                    std::string rel = spawnScript;
                    std::replace(rel.begin(), rel.end(), '\\', '/');
                    std::string fullPath = std::string(scriptRoot) + "/" + rel;
                    skExecutableContext loadCtxt(&interpreter);
                    try {
                        auto monster = std::make_unique<sk_bindings::MonsterExecutable>(
                            skString(fullPath.c_str()), loadCtxt, &strings, stack.player(), stack);
                        skRValueArray args;
                        args.append(skRValue(0));
                        skRValue ret;
                        skExecutableContext callCtxt(&interpreter);
                        monster->method(skString("Init"), args, ret, callCtxt);
                        MonsterInstance inst;
                        // Far enough to be fully in frame on arrival, and
                        // to be watched walking in.
                        // Inside a default attack range (660 world units,
                        // MonsterExecutable's own SetAttackRange default),
                        // so the creature is swinging from the first tick
                        // instead of having to path its way in.
                        constexpr float kDebugSpawnDistance = 300.0f;
                        inst.x = gameCamera.x + std::cos(gameCamera.yaw) * kDebugSpawnDistance;
                        inst.y = gameCamera.y + std::sin(gameCamera.yaw) * kDebugSpawnDistance;
                        inst.z = gameZone->FloorHeightAt(inst.x, inst.y);
                        inst.placementYaw = gameCamera.yaw + 3.14159265f;  // looking back at us
                        inst.facingYaw = inst.placementYaw;
                        inst.modelArchiveIndex = 18;  // Azra_Rat's own models.idx index
                        inst.typeId = 0;
                        inst.script = std::move(monster);
                        std::printf("shadowkey-port: SK_DEBUG_SPAWN %s at (%.0f,%.0f)\n", rel.c_str(),
                                    inst.x, inst.y);
                        gameMonsters.push_back(std::move(inst));
                    } catch (skParseException& ex) {
                        std::printf("shadowkey-port: SK_DEBUG_SPAWN parse error: %s\n",
                                    ex.toString().ptr());
                    } catch (skRuntimeException& ex) {
                        std::printf("shadowkey-port: SK_DEBUG_SPAWN runtime error: %s\n",
                                    ex.toString().ptr());
                    }
                }
                std::printf("shadowkey-port: %zu live monster(s), %zu live door(s), %zu live "
                            "pickup(s) loaded\n",
                            gameMonsters.size(), gameDoors.size(), gamePickups.size());

                // M23: the real zone-root script's own Init() -- run last,
                // now that every real named door/monster/merchant this
                // zone places is loaded and registered into the Level
                // global (above), so its many real Level.GetEntity(...)
                // calls (azra.s alone references "m1".."m20", "trinket",
                // "birg", "skelos", "azra", "vil1".."vil4", "heather",
                // "tanyin" -- every one a real, named .ent placement,
                // confirmed this session) can actually resolve.
                gameZoneScript.reset();
                std::string zoneScriptPath = std::string(scriptRoot) + "/" + stack.requestedZone() +
                                              ".s";
                skExecutableContext zoneScriptCtxt(&interpreter);
                try {
                    auto zoneScript = std::make_unique<sk_bindings::ZoneScriptExecutable>(
                        skString(zoneScriptPath.c_str()), zoneScriptCtxt, stack);
                    skRValueArray args;
                    args.append(skRValue(0));  // placeholder for Init's "(s)" parameter
                    skRValue ret;
                    skExecutableContext callCtxt(&interpreter);
                    zoneScript->method(skString("Init"), args, ret, callCtxt);
                    gameZoneScript = std::move(zoneScript);
                    // M39: SetZone(zoneId, totalExperience) -- the real
                    // handler divides that total evenly across every
                    // creature in the zone whose script called SetMob(),
                    // overwriting each one's own SetExpWorth(). Applied
                    // here because the creature list lives on this side.
                    int zoneId = 0, zoneXp = 0;
                    if (gameZoneScript->TakePendingZoneExperience(zoneId, zoneXp)) {
                        int share = 0;
                        size_t counted = 0;
                        for (const MonsterInstance& m : gameMonsters) {
                            if (m.script && m.script->countsForZoneExperience()) ++counted;
                        }
                        if (counted) share = zoneXp / static_cast<int>(counted);
                        for (MonsterInstance& m : gameMonsters) {
                            if (m.script && m.script->countsForZoneExperience()) {
                                m.script->SetExpWorth(share);
                            }
                        }
                        std::printf("shadowkey-port: SetZone(%d, %d) -> %zu creature(s) worth %d "
                                    "each\n",
                                    zoneId, zoneXp, counted, share);
                    }
                    // M38: now that AddTrigger()/SetTrap()/AddEntity() have
                    // run, keep only the placements a trap trigger actually
                    // watches.
                    for (TrapInstance& cand : gameTrapCandidates) {
                        if (gameZoneScript->AnyTrapWatches(cand.typeId, cand.name)) {
                            gameTraps.push_back(cand);
                        }
                    }
                    if (!gameTraps.empty()) {
                        std::printf("shadowkey-port: %zu trap placement(s) armed in '%s'\n",
                                    gameTraps.size(), stack.requestedZone().c_str());
                    }
                } catch (skParseException& ex) {
                    std::printf("shadowkey-port: PARSE ERROR loading zone script %s: %s\n",
                                zoneScriptPath.c_str(), ex.toString().ptr());
                } catch (skRuntimeException& ex) {
                    std::printf("shadowkey-port: RUNTIME ERROR loading zone script %s: %s\n",
                                zoneScriptPath.c_str(), ex.toString().ptr());
                }
            } else {
                std::printf("shadowkey-port: failed to load zone '%s', staying in menu\n",
                            stack.requestedZone().c_str());
            }
        }

        if (inGame && gameZone) {
            // M53: the engine's script timer. `FUN_1006410c` runs once per
            // frame per entity: if the entity's deadline has passed against
            // the engine's own clock, it clears the armed flag and calls
            // the script's `DelayReached(tag)`. Reproduced here over the
            // two hosts every shipped `DelayReached` script lands in --
            // monsters (category 2) and items/containers (categories 3 and
            // 8) -- so azra_rat.s's eighth-kill congratulation, crypt1's
            // and crypt2's sarcophagi, drgnfld's loot chests,
            // crypt2/controller.s's seven-step Umbra arrival, and
            // umbra_keth.s's whole phase cycle all run. See
            // simkin_bindings/script_delay.h.
            //
            // The armed flag is cleared before the callback, so a handler
            // that re-arms (which is exactly how the chained sequences
            // work) is not re-fired on the same tick.
            stack.gameClock().Advance(
                std::chrono::duration<float>(sk::GameClock::kTickInterval).count());
            {
                auto fireDelay = [&](sk_bindings::ScriptDelay& delay,
                                     skScriptedExecutable& script, const char* what) {
                    int tag = 0;
                    if (!delay.Fire(stack.gameClock(), &tag)) return;
                    skRValueArray args;
                    args.append(skRValue(tag));
                    skRValue ret;
                    skExecutableContext ctxt(&interpreter);
                    try {
                        script.method(skString(sk_bindings::kDelayReachedMethod), args, ret, ctxt);
                    } catch (skParseException& ex) {
                        std::printf("shadowkey-port: PARSE ERROR in %s DelayReached: %s\n",
                                    what, ex.toString().ptr());
                    } catch (skRuntimeException& ex) {
                        std::printf("shadowkey-port: RUNTIME ERROR in %s DelayReached: %s\n",
                                    what, ex.toString().ptr());
                    }
                };
                for (MonsterInstance& m : gameMonsters) {
                    if (m.script) fireDelay(m.script->delay(), *m.script, "monster");
                }
                for (PickupInstance& p : gamePickups) {
                    if (p.script) fireDelay(p.script->delay(), *p.script, "pickup");
                }
            }

            // M47: how far the player actually moves this tick, which is
            // what drives the weapon viewmodel's walk bob (FUN_1001f230
            // takes the movement speed as its second argument, and a
            // stationary player freezes the phase). Measured rather than
            // inferred from the keys, so a move blocked by a wall correctly
            // counts as standing still.
            float playerPrevX = gameCamera.x, playerPrevY = gameCamera.y;
            // M10: the real default control scheme's own CharacterManager
            // action (docs/INPUT_HANDLING.md, KeyHash by default) opens
            // the real charactermanager.s screen chain, pausing the 3D
            // view -- RightSelectionKey (Esc) still returns straight to
            // the main menu when not paused for a menu.
            if (input.ConsumeBoundJustPressed(sk::Action::CharacterManager)) {
                stack.OpenMenu("charactermanager");
                inGame = false;
                gamePausedForMenu = true;
            } else if (input.ConsumeJustPressed(sk::ButtonSlot::RightSelectionKey)) {
                inGame = false;
            } else {
                constexpr float kMoveSpeed = 40.0f;    // world units/tick (256 units/tile)
                constexpr float kTurnSpeed = 0.06f;    // radians/tick
                constexpr float kPlayerRadius = 48.0f;  // world units, M7 collision
                // yaw's forward vector rotates toward -right as yaw increases
                // (see render3d/zone_renderer.cpp's forward/right basis), so
                // turning right means *decreasing* yaw.
                if (input.GetButton(sk::ButtonSlot::Left)) gameCamera.yaw += kTurnSpeed;
                if (input.GetButton(sk::ButtonSlot::Right)) gameCamera.yaw -= kTurnSpeed;
                // The real default control scheme's own LookUp/LookDown
                // (Key2/Key8, docs/INPUT_HANDLING.md) -- decoded long ago
                // but never wired to anything, because the renderer had no
                // pitch at all until now (see render3d/camera.h). Holding
                // either overrides the automatic aim-assist below.
                constexpr float kPitchSpeed = 0.05f;  // radians/tick
                bool manualLook = false;
                if (input.GetBoundButton(sk::Action::LookUp)) {
                    gameCamera.pitch -= kPitchSpeed;
                    manualLook = true;
                }
                if (input.GetBoundButton(sk::Action::LookDown)) {
                    gameCamera.pitch += kPitchSpeed;
                    manualLook = true;
                }
                float dx = std::cos(gameCamera.yaw) * kMoveSpeed;
                float dy = std::sin(gameCamera.yaw) * kMoveSpeed;
                // right = (sinYaw, -cosYaw), matching zone_renderer.cpp's
                // own forward/right basis comment -- used for strafing.
                float rx = std::sin(gameCamera.yaw) * kMoveSpeed;
                float ry = -std::cos(gameCamera.yaw) * kMoveSpeed;
                // Axis-separated collision (try X, then Y, independently)
                // gives a simple wall-slide instead of a hard stop the
                // instant either component would clip a wall.
                // Live monsters are solid too -- the player used to walk
                // straight through them, which (together with the aggro
                // fixes below) is the other half of "enemies ignore
                // collision". Vertical separation is honoured so a
                // creature on a different floor of the same tile column
                // doesn't block. No RE ground truth for the real actor-vs-
                // actor radius (docs/WORLD_MODEL.md), so this reuses the
                // same bounding-circle constants the rest of this port's
                // collision already uses.
                constexpr float kMonsterBodyRadius = 40.0f;
                constexpr float kBlockHeightDelta = sk::kEyeHeightOffset * 2.0f;
                // M30: a closed door is solid. door.s starts every door
                // closed (`saved_Open [0]`) and only calls SetPassable(true)
                // from OnUse(); the port stored that flag but never acted on
                // it, so every door in the level was walk-through -- which,
                // together with them being drawn at the wrong heading, is
                // why they all read as permanently open.
                constexpr float kDoorBodyRadius = 90.0f;  // a door leaf spans most of its tile
                auto blockedByDoor = [&](float wx, float wy) {
                    for (const DoorInstance& d : gameDoors) {
                        if (d.script->passable()) continue;
                        float dx = wx - d.x, dy = wy - d.y;
                        float r = kPlayerRadius + kDoorBodyRadius;
                        if (dx * dx + dy * dy < r * r) return true;
                    }
                    return false;
                };
                auto blockedByMonster = [&](float wx, float wy) {
                    for (const MonsterInstance& m : gameMonsters) {
                        if (!m.script->alive() || m.script->destroyed()) continue;
                        if (std::fabs(gameCamera.z - (m.z + sk::kEyeHeightOffset)) >
                            kBlockHeightDelta) {
                            continue;
                        }
                        float dx = wx - m.x, dy = wy - m.y;
                        float r = kPlayerRadius + kMonsterBodyRadius;
                        if (dx * dx + dy * dy < r * r) return true;
                    }
                    return false;
                };
                auto tryMove = [&](float mx, float my) {
                    float nx = gameCamera.x + mx;
                    if (!gameZone->CircleHitsWall(nx, gameCamera.y, kPlayerRadius) &&
                        !blockedByMonster(nx, gameCamera.y) &&
                        !blockedByDoor(nx, gameCamera.y)) {
                        gameCamera.x = nx;
                    }
                    float ny = gameCamera.y + my;
                    if (!gameZone->CircleHitsWall(gameCamera.x, ny, kPlayerRadius) &&
                        !blockedByMonster(gameCamera.x, ny) &&
                        !blockedByDoor(gameCamera.x, ny)) {
                        gameCamera.y = ny;
                    }
                };
                if (input.GetButton(sk::ButtonSlot::Up)) tryMove(dx, dy);
                if (input.GetButton(sk::ButtonSlot::Down)) tryMove(-dx, -dy);
                // The real default scheme binds these to Key4/Key6
                // (docs/INPUT_HANDLING.md) -- previously decoded but never
                // wired up; Up/Down/Left/Right above stayed on the raw
                // ButtonSlot layer since Left/Right double as turning, not
                // an Action.
                if (input.GetBoundButton(sk::Action::SideStepLeft)) tryMove(-rx, -ry);
                if (input.GetBoundButton(sk::Action::SideStepRight)) tryMove(rx, ry);

                // M39: scripted teleports. azra.s's `Birg.SummonMe()` and
                // friends are ordinary script handlers whose whole body is
                // a SetPosition (see MonsterExecutable's handler), so the
                // move has to be mirrored onto the live instance -- drained
                // here rather than inside the script call, same
                // defer-to-a-safe-point convention as PickupItem().
                for (MonsterInstance& m : gameMonsters) {
                    float nx = 0, ny = 0, nz = 0;
                    if (m.script && m.script->TakePendingPosition(nx, ny, nz)) {
                        m.x = nx;
                        m.y = ny;
                        m.z = nz;
                    }
                }

                // M38: the real trap proximity check, once the player's
                // position for this tick is settled.
                //
                // FUN_1008ff14 (the trap entity's own tick) builds a box of
                // +/-0x100 around the trap's own x/y and the player's box
                // from the player's SetRadius/SetRadius2 half-extents, then
                // notifies the zone's trigger list with mode 2. Both
                // numbers below are those: 256 world units is exactly one
                // tile, and kPlayerRadius is this port's own collision
                // radius standing in for the real half-extents.
                if (gameZoneScript && !gameTraps.empty()) {
                    constexpr float kTrapHalfExtent = 256.0f;  // the real +/-0x100
                    for (TrapInstance& trap : gameTraps) {
                        const float reach = kTrapHalfExtent + kPlayerRadius;
                        const bool overlapping = std::fabs(gameCamera.x - trap.x) <= reach &&
                                                  std::fabs(gameCamera.y - trap.y) <= reach;
                        // Edge-triggered: the real trap latches itself
                        // sprung (entity+0x225) and re-arms on a timer, so
                        // standing on a spike trap must not bill the player
                        // once per tick.
                        if (overlapping && !trap.inside) {
                            gameZoneScript->Notify(
                                sk_bindings::TriggerExecutable::kNotifyTrapProximity, trap.typeId,
                                trap.name);
                        }
                        trap.inside = overlapping;
                    }
                }

                // M44: `Level.CreateEntity(typeId, x, y, z)` -- the
                // creature form (crypt1.s's EnterZone("UmbraHere") spawns
                // Umbra Keth with it). LevelExecutable built and Init'd the
                // script object already, since the calling script needs it
                // back immediately; what is left is the world instance,
                // which only exists on this side. Drained here rather than
                // at the call site because the call comes from inside a
                // live script frame, the same reason PickupItem() and
                // SummonMe()'s SetPosition() are both deferred.
                {
                    sk_bindings::LevelExecutable::PendingCreature req;
                    std::unique_ptr<sk_bindings::MonsterExecutable> script;
                    while (stack.level().TakePendingCreature(req, script)) {
                        if (!script) continue;
                        const sk::EntityTypeDescriptor* desc = entityTypes.Lookup(req.typeId);
                        MonsterInstance inst;
                        inst.x = static_cast<float>(req.x);
                        inst.y = static_cast<float>(req.y);
                        inst.z = static_cast<float>(req.z);
                        inst.modelArchiveIndex = desc ? desc->modelArchiveIndex : -1;
                        inst.typeId = req.typeId;
                        inst.script = std::move(script);
                        std::printf("shadowkey-port: CreateEntity(%d) spawned \"%s\" at "
                                    "(%d, %d, %d)\n",
                                    req.typeId, inst.script->name().c_str(), req.x, req.y, req.z);
                        gameMonsters.push_back(std::move(inst));
                    }
                }

                // M45: `FUN_1008a76c` -- the encounter spawner. Placed
                // here so it can use the same freshly-settled positions
                // the region check below does, and so a spawn requested by
                // a script (`Level.fight41.SpawnEncounter("battle41")`)
                // and one triggered by walking into a region both go
                // through one code path.
                auto spawnEncounter = [&](sk_bindings::EncounterExecutable& encounter,
                                          size_t regionIndex) {
                    if (!gameZone) return;
                    if (regionIndex >= encounter.regionCount()) return;
                    if (!encounter.CanSpawnInRegion(regionIndex)) return;
                    // The region's rectangle comes from `.zon` (M44); the
                    // encounter only knows its name.
                    std::vector<const sk::Zone::Region*> rects =
                        gameZone->RegionsNamed(encounter.regionName(regionIndex));
                    if (rects.empty()) return;
                    const sk::Zone::Region& rect = *rects.front();
                    int setIndex = encounter.PickSetIndex();
                    if (setIndex < 0) return;
                    const std::vector<sk_bindings::EncounterExecutable::SpawnEntry>& set =
                        encounter.sets()[static_cast<size_t>(setIndex)];

                    // `FUN_1008ae94` -- the placement search, in
                    // Zone::FindFreeTileInRegion(). The occupancy
                    // predicate stands in for the real per-tile entity
                    // grid this port has no equivalent of.
                    auto tileOccupied = [&](int tx, int ty) {
                        for (const MonsterInstance& other : gameMonsters) {
                            if (!other.script || !other.script->alive()) continue;
                            if (static_cast<int>(std::floor(other.x / sk::kTileScale)) == tx &&
                                static_cast<int>(std::floor(other.y / sk::kTileScale)) == ty) {
                                return true;
                            }
                        }
                        return false;
                    };
                    auto findSpawnTile = [&](int& outTx, int& outTy) {
                        return gameZone->FindFreeTileInRegion(rect, tileOccupied, outTx, outTy);
                    };

                    for (const sk_bindings::EncounterExecutable::SpawnEntry& entry : set) {
                        for (int n = 0; n < entry.count; ++n) {
                            std::unique_ptr<sk_bindings::MonsterExecutable> script =
                                stack.level().CreateCreature(entry.typeId);
                            if (!script) continue;
                            // The real order: the creature is created and
                            // counted first, and only then does it look
                            // for somewhere to put it -- so a failed
                            // placement still consumes a slot. Reproduced.
                            encounter.NoteSpawned(regionIndex);
                            int tx = 0, ty = 0;
                            if (findSpawnTile(tx, ty)) {
                                MonsterInstance inst;
                                inst.x = (static_cast<float>(tx) + 0.5f) * sk::kTileScale;
                                inst.y = (static_cast<float>(ty) + 0.5f) * sk::kTileScale;
                                // `FUN_100686e0` snaps the new actor to the
                                // floor of the tile it landed on.
                                inst.z = static_cast<float>(
                                    gameZone->FloorHeightAt(inst.x, inst.y));
                                const sk::EntityTypeDescriptor* desc =
                                    entityTypes.Lookup(entry.typeId);
                                inst.modelArchiveIndex = desc ? desc->modelArchiveIndex : -1;
                                inst.typeId = entry.typeId;
                                inst.encounter = &encounter;
                                inst.encounterRegion = regionIndex;
                                inst.script = std::move(script);
                                std::printf("shadowkey-port: encounter spawned typeId %d \"%s\" in "
                                            "region \"%s\" at tile (%d, %d)\n",
                                            entry.typeId, inst.script->name().c_str(),
                                            encounter.regionName(regionIndex).c_str(), tx, ty);
                                gameMonsters.push_back(std::move(inst));
                            } else {
                                std::printf("shadowkey-port: encounter found no free tile in "
                                            "region \"%s\" for typeId %d\n",
                                            encounter.regionName(regionIndex).c_str(),
                                            entry.typeId);
                            }
                            if (encounter.RegionAtLimit(regionIndex)) return;
                        }
                    }
                };

                // A script's own `SpawnEncounter(region)`. The real handler
                // spawns inline; this port defers to here for the same
                // reason CreateEntity does.
                if (gameZoneScript) {
                    for (const auto& encounter : gameZoneScript->encounters()) {
                        int pending = encounter->TakePendingSpawnRegion();
                        if (pending >= 0) {
                            spawnEncounter(*encounter, static_cast<size_t>(pending));
                        }
                    }
                }

                // M44: named-region entry -- the `.zon` room list, decoded
                // this milestone (world/zone.h's Region). Same "once the
                // player's position for this tick is settled" placement as
                // the trap check above, and edge-triggered for the same
                // reason: azra's "YouSure" region opens a menu, and it must
                // do that on crossing the boundary, not 25 times a second.
                //
                // The real engine tracks the room per actor and calls
                // FUN_1002ef44 on a transition; this diffs the occupied set
                // instead, which behaves the same and needs no per-entity
                // room pointer. Leaving a region is deliberately silent --
                // the engine has no ExitZone, and no shipped script has a
                // handler for one.
                if (gameZone && gameZoneScript && !gameZone->regions().empty()) {
                    const int playerTx = static_cast<int>(std::floor(gameCamera.x / sk::kTileScale));
                    const int playerTy = static_cast<int>(std::floor(gameCamera.y / sk::kTileScale));
                    std::set<size_t> nowOccupied;
                    const std::vector<sk::Zone::Region>& regions = gameZone->regions();
                    for (size_t i = 0; i < regions.size(); ++i) {
                        if (!regions[i].Contains(playerTx, playerTy)) continue;
                        nowOccupied.insert(i);
                        if (gameRegionsOccupied.count(i)) continue;  // already inside
                        std::printf("shadowkey-port: entered zone region \"%s\"\n",
                                    regions[i].name.c_str());
                        sk_bindings::EncounterExecutable* encounter =
                            gameZoneScript->EnterRegion(regions[i].name);
                        if (encounter) {
                            // M45: the real FUN_1002ef44 spawns the
                            // encounter here and returns without calling
                            // EnterZone -- which EnterRegion() reproduces
                            // by returning the encounter instead of
                            // running the script handler.
                            int regionIndex = encounter->RegionIndexOf(regions[i].name);
                            if (regionIndex >= 0) {
                                spawnEncounter(*encounter, static_cast<size_t>(regionIndex));
                            }
                        }
                    }
                    gameRegionsOccupied.swap(nowOccupied);
                }

                // Post-M11: real gravity/jump/ground-and-ceiling physics,
                // replacing the M6-M11 fixed "camera.z set once at zone
                // load, never touched again" behavior -- the "floating
                // camera clipping through geometry" the port previously
                // had. No RE ground truth exists for the original's actor
                // physics (docs/WORLD_MODEL.md notes fixed-point actor
                // positions/collision exist but were never traced to the
                // byte level), so this is a deliberate, from-scratch
                // gameplay-feel design, same spirit as kEyeHeightOffset:
                // simple Euler integration against the per-tile
                // floor/ceiling heights Zone::FloorHeightAt/
                // CeilingHeightAt now expose (the same corner data
                // render3d/zone_renderer.cpp already draws the floor/
                // ceiling quads from). Grounded state auto-follows
                // slopes/steps of any height (snap-to-floor while
                // velZ<=0) rather than enforcing a max step height --
                // no data on what the original's real ledge/stair
                // behavior was, and CircleHitsWall's own tile-level wall
                // flag (not height) is what stops genuinely impassable
                // terrain.
                constexpr float kGravity = 4.0f;      // world units/tick^2
                constexpr float kJumpSpeed = 50.0f;   // world units/tick, initial upward velocity
                constexpr float kMaxFallSpeed = 200.0f;  // clamp, avoids tunneling through thin
                                                          // floors over one big tick step
                constexpr float kHeadroom = 60.0f;    // world units, eye-to-ceiling clearance
                if (onGround && input.ConsumeBoundJustPressed(sk::Action::Jump)) {
                    // M51: the real jump (FUN_10044400) is not free. It
                    // refuses outright unless current fatigue is above 5,
                    // spends 5, and plays pl_jump_female.wav (slot 91) or
                    // pl_jump_male.wav (92) chosen on player+0xfac -- the
                    // field SetSex writes (M50). So slot 91 is what a
                    // character with sex 0 uses, which makes 0 female.
                    if (stack.player().actorFatigue() > sk::kJumpFatigueCost) {
                        stack.player().SetActorFatigue(stack.player().actorFatigue() -
                                                        sk::kJumpFatigueCost);
                        playPlayerSound(stack.player().sex() == 0 ? sk::kSoundJumpFemale
                                                                  : sk::kSoundJumpMale);
                        gameVelZ = kJumpSpeed;
                        onGround = false;
                    }
                }
                gameVelZ -= kGravity;
                if (gameVelZ < -kMaxFallSpeed) gameVelZ = -kMaxFallSpeed;
                gameCamera.z += gameVelZ;

                float floorEyeZ = gameZone->FloorHeightAt(gameCamera.x, gameCamera.y) +
                                   sk::kEyeHeightOffset;
                if (gameVelZ <= 0.0f && gameCamera.z <= floorEyeZ) {
                    gameCamera.z = floorEyeZ;
                    gameVelZ = 0.0f;
                    onGround = true;
                } else {
                    onGround = false;
                }
                float ceilingZ = gameZone->CeilingHeightAt(gameCamera.x, gameCamera.y);
                if (gameCamera.z + kHeadroom > ceilingZ) {
                    gameCamera.z = ceilingZ - kHeadroom;
                    if (gameVelZ > 0.0f) gameVelZ = 0.0f;
                }

                // Combat vertical-slice (docs/PORT_ROADMAP.md): from-
                // scratch AI loop -- Idle -> Chasing once the player
                // enters the monster's real SetChaseRadius(), Chasing ->
                // Attacking once within melee range. Never de-aggroes
                // once past Idle (no real data on leash/return-to-post
                // behavior -- see monster_executable.h's class comment).
                //
                // M16: gated on the real script's own SetAggressive()
                // flag -- gameMonsters now also holds non-hostile NPCs
                // (monster_executable.h's class comment), whose real
                // scripts already call SetAggressive(false) themselves;
                // skipping this whole block for them is the direct,
                // evidenced behavior (an NPC has no SetChaseRadius() call
                // either, so it would default to 0 and never trigger
                // anyway -- this makes the intent explicit rather than
                // relying on that coincidence).
                // M31: aggro and stand-off distances are now the real
                // decompiled ones -- MonsterExecutable::chaseRadius() and
                // attackRange() convert the script's scaled-squared value
                // to raw world units (see monster_executable.h). The
                // dominant SetChaseRadius(18000) is 8.4 tiles, not the 70
                // the port previously computed by treating it as a linear
                // radius, which is what made aggro look unlimited. The
                // earlier 8-tile cap this replaces was a guess that landed
                // close by luck; it is gone.
                //
                // kMeleeRange survives only as the reach for the *player's*
                // bare fists (tryAttack below) -- monsters use their own
                // real attackRange().
                constexpr float kMeleeRange = 110.0f;      // world units
                // M22: no real spell script ever calls SetRange() (only
                // weapon scripts do -- M20's own corpus-verified bimodal
                // 384/16384 split), so there's no real value to read for
                // casting range -- an invented, documented constant, same
                // "no RE ground truth" footing as kEyeHeightOffset
                // (render3d/camera.h) or this port's gravity constants.
                constexpr float kSpellRange = 300.0f;      // world units
                // World units/tick at the fixed 25Hz tick. The player
                // walks at 40 (~3.9 tiles/s); 14 puts a creature at about a
                // third of that (~1.4 tiles/s), which is enough to close on
                // a player who stands and fights but leaves retreating a
                // real option. The previous 22 read as "too fast" in play.
                // No real constant exists to match: the corpus never calls
                // SetSpeed with a literal (only `SetSpeed(GetPlayer()...)`,
                // i.e. relative to the player), so this is a tuned
                // port-side value.
                constexpr float kMonsterMoveSpeed = 14.0f;
                constexpr float kMonsterRadius = 40.0f;     // world units, wall-collision only
                // How far above/below a monster the player can be and
                // still be considered "on the same level". Real zones
                // stack rooms vertically at very different floor heights
                // (docs/RENDERER_3D.md's ~6800-raw-unit room heights), so
                // without this a monster two storeys below aggroes on a
                // player it can never reach. A generous 2x the eye-height
                // constant -- enough to cover stairs and slopes within one
                // room, far short of a whole floor.
                constexpr float kAggroMaxHeightDelta = sk::kEyeHeightOffset * 2.0f;
                // Chasing survives this many ticks of lost sight before
                // the monster gives up and returns to Idle (~2s at the
                // fixed 40ms tick). Previously monsters never de-aggroed
                // at all, by design ("no real data on leash behaviour") --
                // but combined with an un-gated 70-tile radius that meant
                // the whole level permanently converging on the player.
                constexpr int kLoseInterestTicks = 50;
                // Monsters push apart at this range so a pack doesn't
                // collapse into one shared point on top of the player.
                constexpr float kMonsterSeparation = 70.0f;

                // Straight ahead first, then progressively wider turns to
                // either side -- shared by the chase and flee paths.
                static const float kSteerAngles[] = {0.0f, 0.6f, -0.6f, 1.2f, -1.2f, 1.9f, -1.9f};

                // M28: picks the clip a creature should be playing from
                // its own AI state, and latches the death clip so a corpse
                // holds its final pose instead of looping.
                auto setAnimClip = [](MonsterInstance& m, int clip, bool holdLastFrame) {
                    if (clip < 0 || m.animClip == clip) return;
                    m.animClip = clip;
                    m.animTime = 0.0f;
                    m.animHoldLastFrame = holdLastFrame;
                };

                // M36: world objects a script has condemned -- currently
                // only an emptied loot bag whose own SetDestroy(true) made
                // lootmenu.s call QuitAndDestroyOpener() on it. Erased here
                // at the top of the world tick rather than inside that
                // handler, so the whole script call chain (and the menu
                // frame it ran under) has fully returned first -- the same
                // deferred-removal rule PlayerExecutable::PurgeRemovedItems
                // already follows for inventory items, and for the same
                // use-after-free reason.
                for (size_t i = 0; i < gamePickups.size();) {
                    if (gamePickups[i].script && gamePickups[i].script->markedForRemoval()) {
                        gamePickups.erase(gamePickups.begin() + static_cast<long>(i));
                    } else {
                        ++i;
                    }
                }

                // M34: creatures that died to a damage-over-time this tick
                // (poison, or IgniteFoe's burn) rather than to a player
                // swing. The real burn branch calls the actor's kill vtable
                // slot directly when its health reaches 0, so these deaths
                // are just as real as combat ones and owe the same
                // OnKilled()/loot/kill-trigger handling -- which they were
                // silently not getting, because that handling lives with
                // spawnLoot() further down this same block. Collected by
                // index (not pointer) and drained the moment spawnLoot() is
                // in scope, since gameMonsters can be appended to in
                // between.
                std::vector<size_t> gameDiedFromEffect;

                // M43: the player's own stats block ticks on exactly the
                // same schedule and through exactly the same code (see
                // simkin_bindings/actor_stats.h) -- it is one class with two
                // owners in the real engine, not two parallel systems. This
                // is what makes a creature's Poison, IgniteFoe, Blind,
                // Disease, Drain, Weakness, FeebleBlade and HarmArmor
                // actually do something to the player rather than land and
                // vanish.
                stack.player().TickStatusEffects(sk_bindings::kAiFrameDeltaUnits);

                // M48: the engine's own 1/256s clock (`level+0x460`), which
                // the cast cooldown is compared against.
                gameClockUnits += sk_bindings::kAiFrameDeltaUnits;
                // M48: a Sanctuary channel expiring is what stamps its own
                // re-cast cooldown -- the real write is at the tail of
                // FUN_10049780, guarded on the owner being the player and
                // the equipped item being typeId 4028.
                {
                    const int kind = stack.player().actorStats().periodicKind();
                    if (gamePlayerPeriodicKind ==
                            sk_bindings::ActorStats::kPeriodicSanctuaryTimer &&
                        kind != sk_bindings::ActorStats::kPeriodicSanctuaryTimer) {
                        gameCastCooldown.sanctuaryTime = gameClockUnits;
                    }
                    gamePlayerPeriodicKind = kind;
                }

                // M48: the world-side half of a real cast. Everything that
                // happens to the *caster* has already happened inside
                // CastSpell() by the time this runs; what is left is the
                // projectile, AzraWrath's area damage, DaedricWeapon's
                // conjured sword and a scroll's own destruction.
                //
                // The one behavioural change worth calling out: casting no
                // longer needs a target. The real cast never looks for one
                // -- it fires a projectile down the caster's facing and
                // lets the flight find something -- which is why this is
                // the fix for spells having been hitscan.
                auto applyCastResult = [&](sk_bindings::ItemExecutable* spell,
                                           sk_bindings::SpellActor* caster,
                                           const sk_bindings::SpellCastResult& cast, float castX,
                                           float castY, float castZ, float castYaw,
                                           float castPitch) {
                    if (cast.soundId >= 0) {
                        // The two slots every shipped `*_sounds.txt` names:
                        // 0x54 pl_cast_fire.wav, 0x57 pl_cast_powerup.wav.
                        const sk::Sound* sfx = soundArchive.GetSound(cast.soundId);
                        if (sfx) audioEngine.PlaySfx(*sfx);
                    }
                    if (cast.projectileSprite != 0) {
                        gameProjectiles.push_back(sk_bindings::SpawnSpellProjectile(
                            spell, caster, static_cast<int>(castX), static_cast<int>(castY),
                            static_cast<int>(castZ),
                            sk_bindings::EngineYawFromPortYaw(castYaw),
                            sk_bindings::EngineAngleFromRadians(castPitch), cast.projectileSprite,
                            cast.impactDamage));
                    }
                    if (cast.areaDamage > 0) {
                        // FUN_1004720c: every creature in the level within
                        // 12000 units, the caster excluded. No sightline
                        // test and no facing test -- the real loop walks
                        // the level's whole creature list.
                        for (MonsterInstance& victim : gameMonsters) {
                            if (victim.script.get() == caster) continue;
                            if (!victim.script->alive() || victim.script->destroyed()) continue;
                            const float adx = victim.x - castX, ady = victim.y - castY;
                            if (std::sqrt(adx * adx + ady * ady) >
                                static_cast<float>(sk_bindings::kAreaSpellRange)) {
                                continue;
                            }
                            victim.script->ApplyDamage(cast.areaDamage);
                        }
                    }
                    if (cast.conjureTypeId != 0) {
                        // DaedricWeapon. The real branch searches the
                        // caster's inventory for typeId 4037 first and only
                        // creates one if it is missing -- reproduced
                        // through the same Level.CreateEntity path a script
                        // would use.
                        if (!stack.player().HasItemOfTemplate(cast.conjureTypeId)) {
                            std::unique_ptr<sk_bindings::ItemExecutable> conjured =
                                stack.level().CreateItem(cast.conjureTypeId, false);
                            if (conjured) stack.player().AddItem(std::move(conjured));
                        }
                    }
                    if (cast.consumeScroll) spell->MarkForRemoval();
                };

                // The cast itself, for the player's side: the cooldown gate,
                // FUN_10046764, and then the world half above.
                auto runSpellCast = [&](sk_bindings::ItemExecutable* spell,
                                        sk_bindings::SpellActor* caster, float castX, float castY,
                                        float castZ, float castYaw, float castPitch) -> bool {
                    if (!spell || !caster) return false;
                    if (spell->spellTypeId() == 0) return false;  // not a spell at all
                    if (caster->isPlayerActor() &&
                        !sk_bindings::SpellCastAllowed(*spell, gameCastCooldown, gameClockUnits)) {
                        return false;
                    }
                    sk_bindings::SpellCastResult cast = sk_bindings::CastSpell(*spell, *caster);
                    if (!cast.cast) return false;
                    if (caster->isPlayerActor()) {
                        sk_bindings::NoteSpellCast(gameCastCooldown, gameClockUnits);
                    }
                    applyCastResult(spell, caster, cast, castX, castY, castZ, castYaw, castPitch);
                    return true;
                };

                for (size_t monsterIndex = 0; monsterIndex < gameMonsters.size(); ++monsterIndex) {
                    MonsterInstance& m = gameMonsters[monsterIndex];
                    const bool wasAliveBeforeTick = m.script->alive();
                    // M32: the real per-frame AI timers -- the flee/Fear
                    // countdown (which restores the previous package when
                    // it expires) and the paralysis lockout. Runs even for
                    // the dead/destroyed so an effect can't outlive them.
                    m.script->TickAi(sk_bindings::kAiFrameDeltaUnits);
                    // M34: TickAi() is now a path that can actually kill.
                    if (wasAliveBeforeTick && !m.script->alive()) {
                        gameDiedFromEffect.push_back(monsterIndex);
                    }
                    // M23: destroyed() (a real zone-root script's
                    // DestroyObjectMirror()) removes an entity from play
                    // as fully as death does, everywhere alive() is
                    // already checked.
                    if (!m.script->alive()) {
                        setAnimClip(m, m.script->deathAnimation(), true);
                        continue;
                    }
                    if (m.script->destroyed() || !m.script->aggressive()) {
                        // Idle NPCs and merchants still breathe.
                        setAnimClip(m, m.script->idleAnimation(), false);
                        continue;
                    }
                    // M32: the real AI package gates everything below.
                    //
                    // Package 6 (AiSpellAssistTarget) is faithfully inert:
                    // nothing anywhere in the real binary reads it, so a
                    // creature left in it matches neither branch of the
                    // engine's own tick and simply stops acting. Package
                    // -1 (AiSleep) is the same. Reproduced, not invented.
                    int pkg = m.script->aiPackage();
                    if (pkg == sk_bindings::MonsterExecutable::kAiAsleep ||
                        pkg == sk_bindings::MonsterExecutable::kAiSpellAssist) {
                        setAnimClip(m, m.script->idleAnimation(), false);
                        continue;
                    }

                    float mdx = gameCamera.x - m.x, mdy = gameCamera.y - m.y;
                    float dist = std::sqrt(mdx * mdx + mdy * mdy);

                    // M32: package 4 -- flee. Reached in the real game only
                    // through the Fear spell (FUN_100458e4's typeId-4020
                    // branch calls FUN_10086b98(target, 4, magnitude*5)),
                    // never from a creature's own morale: SetWimpy is
                    // stored but the AI never reads it. The real helper
                    // sets a one-shot move goal and a countdown, and the
                    // tick restores the previous package when it expires --
                    // there is no per-tick flee steering in the original,
                    // so running directly away for the duration is this
                    // port's reading of that one-shot goal.
                    if (pkg == sk_bindings::MonsterExecutable::kAiFlee) {
                        setAnimClip(m, m.script->walkAnimation(), false);
                        if (dist > 1.0f && !m.script->paralyzed()) {
                            float awayX = -mdx / dist, awayY = -mdy / dist;
                            for (float steer : kSteerAngles) {
                                float cs = std::cos(steer), sn = std::sin(steer);
                                float sx = awayX * cs - awayY * sn;
                                float sy = awayX * sn + awayY * cs;
                                float nx = m.x + sx * kMonsterMoveSpeed;
                                float ny = m.y + sy * kMonsterMoveSpeed;
                                if (!gameZone->CircleHitsWall(nx, ny, kMonsterRadius)) {
                                    m.x = nx;
                                    m.y = ny;
                                    m.facingYaw = std::atan2(sy, sx);
                                    break;
                                }
                            }
                        }
                        m.z = gameZone->FloorHeightAt(m.x, m.y);
                        continue;
                    }

                    // A paralysed creature neither swings nor turns -- the
                    // real attack function's first test is
                    // `monster+0x294 < 1`, and the tick only steers toward
                    // a target while that timer is exactly 0.
                    if (m.script->paralyzed()) {
                        setAnimClip(m, m.script->idleAnimation(), false);
                        continue;
                    }

                    // Can this monster actually perceive the player right
                    // now? Real chase radii are enormous (see
                    // Zone::HasLineOfSight()'s comment), so sight and
                    // vertical separation -- not the radius -- are what
                    // really bound aggro.
                    bool inRadius = dist <= m.script->chaseRadius();
                    bool sameLevel =
                        std::fabs(gameCamera.z - (m.z + sk::kEyeHeightOffset)) <=
                        kAggroMaxHeightDelta;
                    // M33: the Blind effect (real effect flag 4) is what
                    // its stat penalties imply -- the creature can't pick
                    // the player out. It keeps whatever target it already
                    // had (the real flag doesn't clear +0x20c) but stops
                    // acquiring, so a blinded creature loses track once
                    // the player moves.
                    bool canSee = inRadius && sameLevel && !m.script->blinded() &&
                                   gameZone->HasLineOfSight(m.x, m.y, gameCamera.x, gameCamera.y);
                    if (canSee) {
                        m.ticksSinceSeen = 0;
                        m.lastSeenX = gameCamera.x;
                        m.lastSeenY = gameCamera.y;
                    } else {
                        ++m.ticksSinceSeen;
                    }

                    if (m.aiState == MonsterInstance::AiState::Idle) {
                        if (canSee) m.aiState = MonsterInstance::AiState::Chasing;
                    } else if (m.ticksSinceSeen > kLoseInterestTicks) {
                        // Lost it -- back to standing post rather than
                        // homing on the player forever through geometry.
                        m.aiState = MonsterInstance::AiState::Idle;
                    }
                    if (m.aiState == MonsterInstance::AiState::Idle) {
                        setAnimClip(m, m.script->idleAnimation(), false);
                        continue;
                    }

                    // The real stand-off: the AI tick zeroes the creature's
                    // velocity and swings as soon as the distance drops
                    // below `monster+0x2dc` (SetAttackRange, default 660
                    // world units). Attacking whether or not the
                    // centre-to-centre sightline clips a wall corner is
                    // deliberate -- otherwise a monster in a doorway keeps
                    // walking into the player instead of stopping to swing,
                    // which is exactly the reported behaviour.
                    float standAndAttack = m.script->attackRange();
                    if (dist <= standAndAttack) {
                        m.aiState = MonsterInstance::AiState::Attacking;
                        setAnimClip(m, m.script->swingAnimation(), false);
                        if (dist > 1.0f) m.facingYaw = std::atan2(mdy, mdx);
                        // M32: the real attack cadence (monster+0x2c4).
                        // The engine accumulates the frame delta every
                        // frame, only lets a creature act once the total
                        // passes 0x100, then resets it to `rand & 0x1f` so
                        // a pack doesn't swing in lockstep. That works out
                        // to roughly one attempt every 256ms, replacing
                        // this port's invented fixed ~1s cooldown.
                        if (m.script->ConsumeAttackCadence(
                                sk_bindings::kAiFrameDeltaUnits)) {
                            // M43: the real melee-vs-spell branch
                            // (FUN_100835b8). A creature with a spell in
                            // slot 0 rolls SetMeleeRoll against rand(0,100)
                            // and casts unless the roll reaches it -- so a
                            // script that adds spells but never calls
                            // SetMeleeRoll (bandit_mage.s, highwaymage.s,
                            // yelnicin.s) casts on every single attack, and
                            // one that sets 75 (every floater, every ghost)
                            // casts about three attacks in four.
                            if (!m.script->RollForMelee()) {
                                // M48: the real cast. The creature pays for
                                // it and applies any self-targeted half
                                // itself; an offensive spell now leaves the
                                // muzzle as a projectile aimed along the
                                // creature's facing, which is what makes a
                                // caster's spell miss when the player steps
                                // aside.
                                sk_bindings::MonsterExecutable::CastAttempt attempt =
                                    m.script->CastSpellAt(&stack.player());
                                if (attempt.spell && attempt.result.cast) {
                                    applyCastResult(attempt.spell, m.script.get(), attempt.result,
                                                     m.x, m.y, m.z, m.facingYaw, 0.0f);
                                }
                            } else if (m.script->shootsProjectile()) {
                                // M49: an archer. FUN_100835b8 puts its
                                // whole melee resolution inside `if
                                // (+0x2d8 == -1)`, so a creature that calls
                                // SetProjectile shoots *instead of*
                                // swinging -- it never lands a melee blow
                                // at all. The type is hardcoded 599 on this
                                // side (the script's own SetProjectile
                                // value goes to a draw parameter, not the
                                // art), the damage is `rand % damageMax`
                                // exactly as on the player's side, and the
                                // sound is the creature's own
                                // SetAttackNoise -- which every one of the
                                // twelve shipped archers sets to slot 1,
                                // the same bow-fire sample the player uses.
                                const int dmgMax = (std::max)(1, m.script->damageMax());
                                gameArrows.push_back(sk_bindings::SpawnArrowProjectile(
                                    m.script.get(), /*ownerIsPlayer=*/false,
                                    static_cast<int>(m.x), static_cast<int>(m.y),
                                    static_cast<int>(m.z), 0,
                                    sk_bindings::EngineYawFromPortYaw(m.facingYaw),
                                    /*pitch=*/0, sk_bindings::kBowProjectileTypeId,
                                    std::rand() % dmgMax, m.script->attack()));
                                playWorldSound(m.script->attackNoiseId(), m.x, m.y);
                            } else {
                                int dmg = sk_bindings::RollDamage(
                                    m.script->attack(), stack.player().defense(),
                                    stack.player().armorRating(), m.script->damageMin(),
                                    m.script->damageMax());
                                // M51: FUN_10044814 plays slot 80 on any
                                // positive incoming damage, before it
                                // applies it -- the same sample a
                                // connecting swing uses, so an impact is
                                // one sound whichever way it is going.
                                if (dmg > 0) playPlayerSound(sk::kSoundAttackHit);
                                stack.player().ApplyDamage(dmg);
                                playWorldSound(m.script->attackNoiseId(), m.x, m.y);
                            }
                        }
                    } else {
                        m.aiState = MonsterInstance::AiState::Chasing;
                        setAnimClip(m, m.script->walkAnimation(), false);
                        // Head for the player if currently visible, else
                        // for wherever they were last seen.
                        float goalX = canSee ? gameCamera.x : m.lastSeenX;
                        float goalY = canSee ? gameCamera.y : m.lastSeenY;
                        float gx = goalX - m.x, gy = goalY - m.y;
                        float goalDist = std::sqrt(gx * gx + gy * gy);
                        if (goalDist > 1.0f) {
                            float dirX = gx / goalDist, dirY = gy / goalDist;
                            // Crowd separation: push away from any other
                            // live monster that's too close, so a group
                            // spreads out instead of stacking in one spot.
                            for (const MonsterInstance& other : gameMonsters) {
                                if (&other == &m) continue;
                                if (!other.script->alive() || other.script->destroyed()) continue;
                                float ox = m.x - other.x, oy = m.y - other.y;
                                float od = std::sqrt(ox * ox + oy * oy);
                                if (od > 0.001f && od < kMonsterSeparation) {
                                    float push = (kMonsterSeparation - od) / kMonsterSeparation;
                                    dirX += (ox / od) * push;
                                    dirY += (oy / od) * push;
                                }
                            }
                            float dirLen = std::sqrt(dirX * dirX + dirY * dirY);
                            if (dirLen > 0.001f) {
                                dirX /= dirLen;
                                dirY /= dirLen;
                            }
                            // Try straight ahead; if a wall blocks it, try
                            // progressively wider turns to either side
                            // before giving up this tick. Not a real
                            // pathfinder (the original's own .pth
                            // spawn/patrol data is still undecoded past
                            // its header, docs/ZONE_FORMAT.md) -- but
                            // enough that a monster follows a corridor
                            // round a corner instead of grinding face-first
                            // into the wall between it and the player,
                            // which is what the previous straight-line
                            // beeline did.
                            // Never close past bodily contact: melee range
                            // is the trigger to swing, but the creature
                            // still must not walk *into* the player. Clamp
                            // this tick's step so it stops at the point
                            // where the two bounding circles touch.
                            // Stop where the real engine stops -- at the
                            // creature's own attackRange -- but never
                            // closer than bodily contact.
                            float standoff = (std::max)(m.script->attackRange(),
                                                         kPlayerRadius + kMonsterRadius);
                            float step = kMonsterMoveSpeed;
                            if (canSee) step = (std::min)(step, (std::max)(0.0f, dist - standoff));
                            for (float steer : kSteerAngles) {
                                float cs = std::cos(steer), sn = std::sin(steer);
                                float sx = dirX * cs - dirY * sn;
                                float sy = dirX * sn + dirY * cs;
                                float nx = m.x + sx * step;
                                float ny = m.y + sy * step;
                                if (!gameZone->CircleHitsWall(nx, ny, kMonsterRadius)) {
                                    m.x = nx;
                                    m.y = ny;
                                    if (step > 0.01f) m.facingYaw = std::atan2(sy, sx);
                                    break;
                                }
                            }
                        }
                    }
                    m.z = gameZone->FloorHeightAt(m.x, m.y);
                }

                // M30: automatic aim-assist pitch. Requested behaviour:
                // facing a small creature (a rat, a spider) the camera
                // should tilt down to it and level off again once it is out
                // of range.
                //
                // "Small" is measured, not listed: MonsterCenterZ() takes
                // the creature's real model height (scaled by its script's
                // own SetScale) so the camera aims at its actual centre of
                // mass. A rat's is far below eye level and produces a real
                // downward tilt; a humanoid's is near eye level and
                // produces almost none, which is why this needs no
                // per-creature special-casing.
                //
                // No RE ground truth: the original's own camera pitch is
                // real (render3d/camera.h) but nothing has been traced that
                // aims it automatically, so this is a port-side design,
                // documented as such. Manual LookUp/LookDown overrides it
                // while held.
                if (!manualLook) {
                    constexpr float kAutoAimRange = 3.0f * sk::kTileScale;
                    constexpr float kAutoAimEase = 0.18f;  // fraction closed per tick
                    const MonsterInstance* aimTarget = nullptr;
                    float aimBest = kAutoAimRange;
                    float fwdX = std::cos(gameCamera.yaw), fwdY = std::sin(gameCamera.yaw);
                    for (const MonsterInstance& m : gameMonsters) {
                        if (!m.script->alive() || m.script->destroyed()) continue;
                        if (!m.script->aggressive()) continue;
                        float dx = m.x - gameCamera.x, dy = m.y - gameCamera.y;
                        float d = std::sqrt(dx * dx + dy * dy);
                        if (d > aimBest || d < 1.0f) continue;
                        if ((fwdX * dx + fwdY * dy) / d < 0.5f) continue;  // roughly ahead
                        aimBest = d;
                        aimTarget = &m;
                    }
                    float desiredPitch = 0.0f;
                    if (aimTarget) {
                        float centerZ = MonsterCenterZ(*aimTarget, modelArchive);
                        desiredPitch = std::atan2(gameCamera.z - centerZ, (std::max)(1.0f, aimBest));
                    }
                    desiredPitch = std::clamp(desiredPitch, -sk::kMaxCameraPitch,
                                               sk::kMaxCameraPitch);
                    // Ease rather than snap, so acquiring or losing a target
                    // reads as the camera panning, not cutting.
                    gameCamera.pitch += (desiredPitch - gameCamera.pitch) * kAutoAimEase;
                }
                gameCamera.pitch =
                    std::clamp(gameCamera.pitch, -sk::kMaxCameraPitch, sk::kMaxCameraPitch);

                // M21: monster-death loot-bag spawning -- see docs/
                // PORT_ROADMAP.md's M21 entry for the full real-corpus
                // decode. A dead monster's real SetLoot() tag string
                // (lootTag(), MonsterExecutable) is, lowercased, a real
                // loadable script path (this port's filesystem being
                // case-insensitive, same convention every other path
                // lookup in this codebase already relies on) -- resolved
                // and loaded the same way M19's pickups are, then dropped
                // into gamePickups at the dead monster's own position so
                // it's immediately visible/usable, same Action::Use path
                // pickups already go through (a loot bag's own OnUse()
                // opens a real menu instead of directly transferring
                // itself -- see the Action::Use handling below for how
                // that's told apart from a direct M19-style pickup).
                auto spawnLoot = [&](const MonsterInstance& m) {
                    const std::string& tag = m.script->lootTag();
                    if (tag.empty()) return;
                    std::string relPath = tag;
                    for (char& c : relPath) {
                        if (c == '\\') c = '/';
                    }
                    std::string fullPath = std::string(scriptRoot) + "/" + relPath;
                    skExecutableContext loadCtxt(&interpreter);
                    try {
                        auto bag = std::make_unique<sk_bindings::ItemExecutable>(
                            skString(fullPath.c_str()), loadCtxt, stack);
                        skRValueArray initArgs;
                        initArgs.append(skRValue(0));  // placeholder for Init's "(s)" parameter
                        skRValue initRet;
                        skExecutableContext callCtxt(&interpreter);
                        bag->method(skString("Init"), initArgs, initRet, callCtxt);
                        PickupInstance inst;
                        inst.x = m.x;
                        inst.y = m.y;
                        inst.z = m.z;
                        inst.modelArchiveIndex = -1;  // no real model for a dropped loot bag found
                                                       // yet (typeId 300's own entities.txt entry
                                                       // is the "!bag_loot" label-only convention,
                                                       // docs/ZONE_FORMAT.md) -- ZoneRenderer
                                                       // already skips entities with no resolved
                                                       // model (M8), so this just doesn't render,
                                                       // same as any other undecoded visual.
                        inst.script = std::move(bag);
                        gamePickups.push_back(std::move(inst));
                    } catch (skParseException& ex) {
                        std::printf("shadowkey-port: PARSE ERROR loading loot bag %s: %s\n",
                                    fullPath.c_str(), ex.toString().ptr());
                    } catch (skRuntimeException& ex) {
                        std::printf("shadowkey-port: RUNTIME ERROR loading loot bag %s: %s\n",
                                    fullPath.c_str(), ex.toString().ptr());
                    }
                };

                // M34: the deaths the AI tick's damage-over-time channels
                // caused above, given exactly the same treatment a killing
                // blow gets below -- the creature's own OnKilled(), its
                // loot bag, and the zone script's kill-count trigger.
                for (size_t deadIndex : gameDiedFromEffect) {
                    if (deadIndex >= gameMonsters.size()) continue;
                    MonsterInstance& dead = gameMonsters[deadIndex];
                    dead.script->InvokeOnKilled();
                    spawnLoot(dead);
                    if (gameZoneScript) gameZoneScript->NotifyKilled(dead.typeId);
                    // M45: the `actor+0x2e4` backlink -- see
                    // MonsterInstance::encounter.
                    if (dead.encounter) dead.encounter->NoteDied(dead.encounterRegion);
                    dead.encounter = nullptr;
                }
                gameDiedFromEffect.clear();

                // M48: fly every live spell projectile one tick. This is
                // where a spell actually reaches its target -- the impact
                // runs the spell script's own HitTarget(), which is what
                // calls DoAttackRoll and therefore the whole status-effect
                // dispatcher. Before this the cast called HitTarget()
                // directly and spells were hitscan.
                if (!gameProjectiles.empty()) {
                    std::vector<sk_bindings::ProjectileTarget> projectileTargets;
                    projectileTargets.reserve(gameMonsters.size() + 1);
                    for (MonsterInstance& victim : gameMonsters) {
                        if (!victim.script->alive() || victim.script->destroyed()) continue;
                        sk_bindings::ProjectileTarget entry;
                        entry.actor = victim.script.get();
                        entry.script = victim.script.get();
                        entry.x = static_cast<int>(victim.x);
                        entry.y = static_cast<int>(victim.y);
                        // The real half-extents come off the target's own
                        // vtable (+0x44 / +0x48). This port has no per-model
                        // bounds, so both use the projectile's own radius --
                        // the same 200 the engine gives the projectile.
                        entry.halfWidth = sk_bindings::kProjectileRadius;
                        entry.halfDepth = sk_bindings::kProjectileRadius;
                        projectileTargets.push_back(entry);
                    }
                    {
                        // The player is a legal target throughout -- the
                        // sweep's filter is "a creature *or* the player",
                        // which is what lets a caster's fireball hit you.
                        sk_bindings::ProjectileTarget self;
                        self.actor = &stack.player();
                        self.script = &stack.player();
                        self.x = static_cast<int>(gameCamera.x);
                        self.y = static_cast<int>(gameCamera.y);
                        self.halfWidth = sk_bindings::kProjectileRadius;
                        self.halfDepth = sk_bindings::kProjectileRadius;
                        projectileTargets.push_back(self);
                    }
                    auto projectileBlocked = [&](int tileX, int tileY) {
                        if (!gameZone || !gameZone->InBounds(tileX, tileY)) return false;
                        const sk::ZmpCell& cell = gameZone->CellAt(tileX, tileY);
                        // `((tile[1] & 0x1c) == 4) || (tile[0] & 2)`.
                        return cell.IsWall() || (cell.blockFlags & 0x1c) == 0x04;
                    };
                    for (sk_bindings::SpellProjectile& shot : gameProjectiles) {
                        sk_bindings::ProjectileImpact impact = sk_bindings::TickSpellProjectile(
                            shot, gameZone->width(), gameZone->height(), projectileBlocked,
                            projectileTargets);
                        if (!impact.hit || !impact.target) continue;
                        // The impact may have killed a creature; that owes
                        // exactly the same handling a killing blow does.
                        for (MonsterInstance& victim : gameMonsters) {
                            if (victim.script.get() != impact.target) continue;
                            if (victim.script->alive()) break;
                            victim.script->InvokeOnKilled();
                            spawnLoot(victim);
                            if (gameZoneScript) gameZoneScript->NotifyKilled(victim.typeId);
                            if (victim.encounter) {
                                victim.encounter->NoteDied(victim.encounterRegion);
                                victim.encounter = nullptr;
                            }
                            break;
                        }
                    }
                    gameProjectiles.erase(
                        std::remove_if(gameProjectiles.begin(), gameProjectiles.end(),
                                        [](const sk_bindings::SpellProjectile& shot) {
                                            return !shot.alive;
                                        }),
                        gameProjectiles.end());
                }

                // M49: the same tick for arrows. Separate list, separate
                // class, and a genuinely different flight model -- an arrow
                // has no lifetime, travels 1.25 tiles a tick, and is stopped
                // by floor height rather than by any wall flag.
                if (!gameArrows.empty() && gameZone) {
                    std::vector<sk_bindings::ArrowTarget> arrowTargets;
                    arrowTargets.reserve(gameMonsters.size() + 1);
                    for (MonsterInstance& victim : gameMonsters) {
                        if (!victim.script->alive() || victim.script->destroyed()) continue;
                        arrowTargets.push_back({victim.script.get(), static_cast<int>(victim.x),
                                                 static_cast<int>(victim.y)});
                    }
                    arrowTargets.push_back({&stack.player(), static_cast<int>(gameCamera.x),
                                             static_cast<int>(gameCamera.y)});
                    auto arrowCellAt = [&](int worldX, int worldY, int worldZ) {
                        sk_bindings::ArrowCell cell;
                        const int tileX = worldX >> 8, tileY = worldY >> 8;
                        if (!gameZone->InBounds(tileX, tileY)) return cell;
                        cell.onMap = true;
                        cell.floorHeight = static_cast<int>(gameZone->CollisionFloorHeightAt(
                            static_cast<float>(worldX), static_cast<float>(worldY),
                            static_cast<float>(worldZ)));
                        return cell;
                    };
                    for (sk_bindings::ArrowProjectile& shot : gameArrows) {
                        sk_bindings::ArrowImpact impact =
                            sk_bindings::TickArrowProjectile(shot, arrowCellAt, arrowTargets);
                        if (!impact.hit || !impact.target) continue;
                        for (MonsterInstance& victim : gameMonsters) {
                            if (victim.script.get() != impact.target) continue;
                            if (victim.script->alive()) break;
                            victim.script->InvokeOnKilled();
                            spawnLoot(victim);
                            if (gameZoneScript) gameZoneScript->NotifyKilled(victim.typeId);
                            if (victim.encounter) {
                                victim.encounter->NoteDied(victim.encounterRegion);
                                victim.encounter = nullptr;
                            }
                            break;
                        }
                    }
                    gameArrows.erase(std::remove_if(gameArrows.begin(), gameArrows.end(),
                                                     [](const sk_bindings::ArrowProjectile& shot) {
                                                         return !shot.alive;
                                                     }),
                                      gameArrows.end());
                }

                // Player attack -- UseLeftAction/UseRightAction (Key7/
                // Key5, real decoded default bindings, previously unused)
                // swing/fire whichever hand's weapon is equipped (bare-
                // fists 1-3 damage at kMeleeRange if empty) at the nearest
                // alive monster within range and roughly in front of the
                // camera.
                //
                // M20: range is now the real equipped weapon's own
                // SetRange() value (item_executable.h's range() comment --
                // 384 for every real melee weapon, 16384 for every real
                // bow/crossbow/thrown weapon, corpus-verified bimodal) --
                // this is what actually makes a bow/crossbow attack reach
                // farther than a sword; no separate "fire" input exists in
                // the real default control scheme (docs/INPUT_HANDLING.md
                // -- "Shoot"/"Reload" are real logical actions but were
                // never bound), so reusing the same two attack keys with a
                // longer real range is the evidenced design, not a guess.
                // No line-of-sight/wall check (sk_bindings::InAttackRange's
                // own comment) -- a ranged shot can theoretically clip
                // through a thin wall corner at extreme range, a documented
                // simplification, same footing as every other from-scratch
                // combat constant in this port.
                auto tryAttack = [&](sk_bindings::ItemExecutable* handItem) {
                    // M22: an equipped item that isn't a real weapon
                    // (kItemTypeWeapon) is treated as a spell cast instead
                    // of a melee/ranged swing -- reuses these same two
                    // attack keys, matching the real default control
                    // scheme's own lack of a dedicated "cast" action, same
                    // reasoning M20 already established for ranged
                    // weapons reusing them over a "fire" key.
                    bool isWeapon = handItem && handItem->itemType() == sk_bindings::kItemTypeWeapon;
                    // M48: a spell leaves here entirely. The real cast
                    // (`FUN_10046764`) never looks for a target -- it
                    // charges the caster, applies its self-targeted half,
                    // and launches a projectile down the caster's facing.
                    // So casting no longer needs anything in range, and a
                    // spell that is all buff (Energize, Sanctuary,
                    // HealWound, ...) finally does something when there is
                    // nothing to shoot at, which is the larger half of the
                    // bug this closes. The invented kSpellRange targeting
                    // cone goes with it; a spell's real reach is its
                    // projectile's twelve-tile flight.
                    if (handItem && !isWeapon && handItem->spellTypeId() != 0) {
                        runSpellCast(handItem, &stack.player(), gameCamera.x, gameCamera.y,
                                      gameCamera.z, gameCamera.yaw, gameCamera.pitch);
                        return;
                    }
                    // M49: a ranged weapon leaves here too, for the same
                    // reason a spell does -- `FUN_100425bc`'s ranged branch
                    // never aims at anything. It plays the bow sound, rolls
                    // damage, and launches an arrow down the player's own
                    // facing; the five-pass target search a few lines above
                    // it in the real function feeds a bookkeeping call, not
                    // the shot. So a bow can now miss, and the invented
                    // 64-tile targeting cone this used to fire through
                    // (which was also how a shot reached through a wall
                    // corner) is gone with it.
                    if (isWeapon && handItem->usesRangedPath()) {
                        // Slot 1 -- barch_firebow.wav in 21 of the 22
                        // shipped sound tables, and the same slot every
                        // archer script's SetAttackNoise(1) names.
                        playPlayerSound(sk_bindings::kBowFireSound);
                        // `rand() % weapon->damageMax` -- the minimum is
                        // simply not consulted on the ranged path, unlike
                        // the melee RandomRange(min, max) below.
                        const int dmgMax = (std::max)(1, handItem->damageMax());
                        gameArrows.push_back(sk_bindings::SpawnArrowProjectile(
                            &stack.player(), /*ownerIsPlayer=*/true,
                            static_cast<int>(gameCamera.x), static_cast<int>(gameCamera.y),
                            static_cast<int>(gameCamera.z), static_cast<int>(gameCamera.z),
                            sk_bindings::EngineYawFromPortYaw(gameCamera.yaw),
                            sk_bindings::EngineAngleFromRadians(gameCamera.pitch),
                            handItem->projectileTypeId(), std::rand() % dmgMax,
                            stack.player().attack()));
                        return;
                    }
                    float range =
                        handItem ? (isWeapon ? static_cast<float>(handItem->range()) : kSpellRange)
                                 : kMeleeRange;
                    MonsterInstance* target = nullptr;
                    float bestDist = range + 1.0f;
                    for (MonsterInstance& m : gameMonsters) {
                        // M16: invulnerable() (essential quest NPCs, e.g.
                        // Tanyin Aldwyr's real SetInvulnerable(true))
                        // can't be targeted at all -- matches the real
                        // script's own intent, not just a damage-application
                        // no-op (ApplyDamage() already guards this too, but
                        // skipping targeting means the crosshair/prompt line
                        // never shows an NPC as attackable in the first place).
                        if (!m.script->alive() || m.script->destroyed() || m.script->invulnerable()) {
                            continue;
                        }
                        if (!sk_bindings::InAttackRange(gameCamera.x, gameCamera.y, gameCamera.yaw,
                                                         m.x, m.y, range)) {
                            continue;
                        }
                        // Closes the documented M20 gap: a bow/crossbow's
                        // real 16384-unit range (64 tiles) previously let
                        // a shot pass straight through walls. Same tile-
                        // grid sightline the AI now uses.
                        if (!gameZone->HasLineOfSight(gameCamera.x, gameCamera.y, m.x, m.y)) {
                            continue;
                        }
                        float ddx = m.x - gameCamera.x, ddy = m.y - gameCamera.y;
                        float dist = std::sqrt(ddx * ddx + ddy * ddy);
                        if (dist < bestDist) {
                            bestDist = dist;
                            target = &m;
                        }
                    }
                    if (!target) {
                        // M51: a swing that finds nothing in range plays
                        // pl_attack_sword.wav (slot 81) and a swing that
                        // finds something plays pl_attack_impale.wav (80).
                        // FUN_100425bc picks between exactly those two on
                        // exactly this condition -- whether the target
                        // search produced anything, not whether the damage
                        // roll landed -- so a connecting swing that rolls
                        // zero still sounds like a hit.
                        playPlayerSound(sk::kSoundAttackMiss);
                        return;
                    }
                    playPlayerSound(sk::kSoundAttackHit);
                    if (handItem && !isWeapon) {
                        // M22: a real spell's own HitTarget()/DoAttackRoll()
                        // (ItemExecutable) applies damage itself -- nothing
                        // else needed here. Harmless no-op if handItem
                        // isn't actually a spell (no real script's own
                        // HitTarget matches, soft-fails through).
                        handItem->InvokeHitTarget(target->script.get());
                    } else {
                        int dmgMin = isWeapon ? handItem->damageMin() : 1;
                        int dmgMax = isWeapon ? handItem->damageMax() : 3;
                        // M43: attack(), not baseAttack() -- a creature can
                        // Drain, Weaken, FeebleBlade, Disease or Blind the
                        // player now, and every one of those is a timed
                        // modifier on the player's own attack stat.
                        int dmg = sk_bindings::RollDamage(stack.player().attack(),
                                                           target->script->defense(),
                                                           target->script->armorValue(), dmgMin,
                                                           dmgMax);
                        target->script->ApplyDamage(dmg);
                    }
                    if (!target->script->alive()) {
                        target->script->InvokeOnKilled();
                        spawnLoot(*target);
                        // M24: a real zone-root script's own kill-count
                        // trigger (ghstpass.s's zombieTrigger etc.) --
                        // no-ops if this zone's real Init() never set one
                        // up watching this typeId.
                        if (gameZoneScript) gameZoneScript->NotifyKilled(target->typeId);
                        // M45: see MonsterInstance::encounter.
                        if (target->encounter) {
                            target->encounter->NoteDied(target->encounterRegion);
                            target->encounter = nullptr;
                        }
                    }
                };
                // Whichever hand actually holds a weapon provides the
                // visible swing, so attacking bare-handed with one hand
                // while the other holds the club still animates the club
                // rather than silently drawing nothing.
                auto swingItemFor = [&](sk_bindings::ItemExecutable* hand) {
                    if (hand && hand->weaponSprite() >= 0) return hand;
                    sk_bindings::ItemExecutable* other = stack.player().rightItem() == hand
                                                              ? stack.player().leftItem()
                                                              : stack.player().rightItem();
                    return (other && other->weaponSprite() >= 0) ? other : hand;
                };
                if (input.ConsumeBoundJustPressed(sk::Action::UseLeftAction)) {
                    // M25: the swing pose plays on the keypress itself, not
                    // only when tryAttack actually finds a target in range --
                    // matches a real weapon swing happening whether or not it
                    // connects (see WeaponViewmodel's comment; a no-op for a
                    // non-weapon/no weaponSprite() item, e.g. bare fists or a
                    // spell).
                    sk_bindings::StartWeaponSwing(gameWeaponViewmodel,
                                                   swingItemFor(stack.player().leftItem()));
                    tryAttack(stack.player().leftItem());
                }
                if (input.ConsumeBoundJustPressed(sk::Action::UseRightAction)) {
                    sk_bindings::StartWeaponSwing(gameWeaponViewmodel,
                                                   swingItemFor(stack.player().rightItem()));
                    tryAttack(stack.player().rightItem());
                }
                // M30: the viewmodel now shows the equipped weapon *all
                // the time*, not only for the moment after an attack key.
                // Before this, `vm.item` was assigned solely inside
                // StartWeaponSwing(), so equipping a weapon showed nothing
                // at all -- and if the weapon had landed in the hand whose
                // attack key you weren't pressing (UpdateEquipStatus fills
                // whichever hand is empty), the swing drew nothing either.
                // Every real weapon script carries the art for this:
                // weapons/club.s does SetWeaponSprite(88) +
                // SetAnimationFrames(5), and those global.spr slots are in
                // the per-zone manifest the port already loads.
                // M47: routed through the real weapon-swap entry
                // (FUN_1001d778, player vtable +0x194) instead of assigning
                // the pointer directly. Changing the item on screen now
                // plays the real transition -- old weapon, then the new one
                // raised from below -- rather than snapping.
                {
                    sk_bindings::ItemExecutable* shown = stack.player().rightItem();
                    if (!shown || shown->weaponSprite() < 0) shown = stack.player().leftItem();
                    if (shown && shown->weaponSprite() < 0) shown = nullptr;
                    sk_bindings::NotifyWeaponChanged(gameWeaponViewmodel, shown);
                }
                // The bob's rate is the player's real movement speed this
                // tick -- FUN_1001f230's own second argument. Standing
                // still freezes the phase, which is what stops the weapon
                // swaying while the player is stationary.
                float movedX = gameCamera.x - playerPrevX;
                float movedY = gameCamera.y - playerPrevY;
                sk_bindings::TickWeaponViewmodel(
                    gameWeaponViewmodel,
                    static_cast<int>(std::sqrt(movedX * movedX + movedY * movedY)));

                // M15/M16/M19: Action::Use (Key3, docs/INPUT_HANDLING.md's
                // default scheme) interact binding -- doors (M15), usable
                // NPCs (M16, e.g. Tanyin Aldwyr's real dialogue, see
                // monster_executable.h's class comment), and world pickups
                // (M19, e.g. snowline/foxglove.s -- the last remaining
                // unbound category, docs/PORT_ROADMAP.md). Same nearest-in-
                // range-and-facing-cone targeting tryAttack uses above,
                // reused here (and again below for the on-screen use-text
                // prompt).
                // Reach for the Use action. 384 raw world units is the
                // game's own melee reach: **every** real melee weapon in the
                // corpus calls SetRange(384) (64 of them; the other 16 are
                // bows/thrown at 16384), so it is the one corpus-verified
                // "arm's length" constant available rather than an invented
                // number. The previous 140 was barely half a tile -- you had
                // to stand almost exactly on an entity's placement point for
                // its prompt to appear, which is why doors only responded to
                // a very precise approach and chests/NPCs essentially never
                // did.
                constexpr float kInteractRange = 384.0f;
                // ~72 degrees off-centre, up from the old ~60. Entities sit
                // at a tile's centre while the player walks its edges, so a
                // tight cone misses things plainly on screen.
                constexpr float kInteractFacing = 0.30f;
                auto findNearbyDoor = [&]() -> DoorInstance* {
                    DoorInstance* nearest = nullptr;
                    float bestDist = kInteractRange + 1.0f;
                    for (DoorInstance& d : gameDoors) {
                        if (!sk_bindings::InInteractRange(gameCamera.x, gameCamera.y,
                                                           gameCamera.yaw, d.x, d.y,
                                                           kInteractRange, kInteractFacing)) {
                            continue;
                        }
                        float ddx = d.x - gameCamera.x, ddy = d.y - gameCamera.y;
                        float dist = std::sqrt(ddx * ddx + ddy * ddy);
                        if (dist < bestDist) {
                            bestDist = dist;
                            nearest = &d;
                        }
                    }
                    return nearest;
                };
                // M16: usable() is the real script's own SetUsable(true)
                // (monster_executable.h) -- an aggressive monster never
                // sets it, so this naturally only ever finds NPCs, not
                // hostile creatures the player is fighting.
                auto findNearbyUsableMonster = [&]() -> MonsterInstance* {
                    MonsterInstance* nearest = nullptr;
                    float bestDist = kInteractRange + 1.0f;
                    for (MonsterInstance& m : gameMonsters) {
                        if (!m.script->alive() || m.script->destroyed() || !m.script->usable()) {
                            continue;
                        }
                        if (!sk_bindings::InInteractRange(gameCamera.x, gameCamera.y,
                                                           gameCamera.yaw, m.x, m.y,
                                                           kInteractRange, kInteractFacing)) {
                            continue;
                        }
                        float ddx = m.x - gameCamera.x, ddy = m.y - gameCamera.y;
                        float dist = std::sqrt(ddx * ddx + ddy * ddy);
                        if (dist < bestDist) {
                            bestDist = dist;
                            nearest = &m;
                        }
                    }
                    return nearest;
                };
                // M19: same shape again for pickups.
                auto findNearbyPickup = [&]() -> PickupInstance* {
                    PickupInstance* nearest = nullptr;
                    float bestDist = kInteractRange + 1.0f;
                    for (PickupInstance& p : gamePickups) {
                        if (!sk_bindings::InInteractRange(gameCamera.x, gameCamera.y,
                                                           gameCamera.yaw, p.x, p.y,
                                                           kInteractRange, kInteractFacing)) {
                            continue;
                        }
                        float ddx = p.x - gameCamera.x, ddy = p.y - gameCamera.y;
                        float dist = std::sqrt(ddx * ddx + ddy * ddy);
                        if (dist < bestDist) {
                            bestDist = dist;
                            nearest = &p;
                        }
                    }
                    return nearest;
                };
                // Doors, usable NPCs, and pickups are three separate lists
                // -- pick whichever real placement is actually nearer when
                // more than one is in range at once, same "nearest wins"
                // rule each list already uses internally.
                auto distanceTo = [&](float x, float y) {
                    float dx = x - gameCamera.x, dy = y - gameCamera.y;
                    return std::sqrt(dx * dx + dy * dy);
                };
                if (input.ConsumeBoundJustPressed(sk::Action::Use)) {
                    DoorInstance* door = findNearbyDoor();
                    MonsterInstance* npc = findNearbyUsableMonster();
                    PickupInstance* pickup = findNearbyPickup();
                    // Nearest of the (up to) three candidates wins.
                    float doorDist = door ? distanceTo(door->x, door->y) : kInteractRange + 1.0f;
                    float npcDist = npc ? distanceTo(npc->x, npc->y) : kInteractRange + 1.0f;
                    float pickupDist =
                        pickup ? distanceTo(pickup->x, pickup->y) : kInteractRange + 1.0f;
                    if (pickup && pickupDist <= doorDist && pickupDist <= npcDist) {
                        // M19/M21: two different real OnUse() shapes share
                        // this one branch. A direct world item (snowline/
                        // foxglove.s) calls PickupItem(self)+
                        // MirrorDestroyObject(self) -- markedForRemoval()
                        // goes true, and (if TakePendingPickupItem()
                        // matches this exact instance -- a script could in
                        // principle destroy itself without ever granting
                        // the item, though no real corpus script does) its
                        // real ItemExecutable moves into the player's
                        // inventory before the world instance is erased. A
                        // loot bag (loot_ratseye.s etc., M21) instead calls
                        // OpenMenu("LootMenu") -- detected the same
                        // before/after currentMenu() way the NPC branch
                        // below already does -- and stays in the world
                        // (still possibly non-empty), so the 3D view pauses
                        // into the real loot-selection menu instead.
                        sk_bindings::MenuExecutable* beforeMenu = stack.currentMenu();
                        bool hadOnUse = pickup->script->InvokeOnUse();
                        if (!hadOnUse) {
                            // M35: the native default action, for the
                            // large majority of world items whose script
                            // defines no OnUse at all.
                            //
                            // Two shapes, and the corpus shows both by
                            // pairing scripts that differ *only* in having
                            // the handler. loot_ratseye.s is
                            // `SetUsable(true)` + a CreateEntity/AddObject
                            // chain + `OnUse { OpenMenu("LootMenu") }`;
                            // raiders\RT_A.s (a real chest's own loot
                            // script, named by its .ent placement) is the
                            // same CreateEntity/AddObject chain with
                            // SetUsable(true) and *no* OnUse. So a
                            // container's default is to open the loot menu
                            // on itself -- which is why no chest in the
                            // game could be opened before this.
                            //
                            // Otherwise the item is simply taken. Its own
                            // Init() already picked the prompt that says
                            // so: blaze.s sets use text 404 "Learn Blaze"
                            // when the player's class can use it and 405
                            // "Pickup Blaze Scroll" when it cannot. That
                            // prompt appearing with nothing behind it was
                            // the reported "interacting does nothing".
                            if (pickup->isContainer || !pickup->script->contents().empty()) {
                                // Same spelling every real bag script uses
                                // for it, so this shares MenuStack's cache
                                // entry rather than making a second one.
                                stack.OpenMenu("LootMenu", pickup->script.get());
                                inGame = false;
                                gamePausedForMenu = true;
                            } else {
                                stack.player().AddItem(std::move(pickup->script));
                                gamePickups.erase(gamePickups.begin() +
                                                   (pickup - gamePickups.data()));
                            }
                        } else if (pickup->script->markedForRemoval()) {
                            skiExecutable* pending = stack.player().TakePendingPickupItem();
                            if (pending == static_cast<skiExecutable*>(pickup->script.get())) {
                                stack.player().AddItem(std::move(pickup->script));
                            }
                            gamePickups.erase(gamePickups.begin() +
                                               (pickup - gamePickups.data()));
                        } else if (stack.currentMenu() != beforeMenu) {
                            inGame = false;
                            gamePausedForMenu = true;
                        }
                    } else if (npc && npcDist <= doorDist) {
                        // A real NPC's OnUse() (e.g. tanyinconvo.s) calls
                        // OpenMenu(...) -- detect that by comparing
                        // currentMenu() before/after (robust to whatever
                        // stale menu happened to be cached from before
                        // NewGame(), since a real conversation always
                        // resolves to a genuinely different MenuExecutable
                        // instance) and switch out of the 3D view into it,
                        // same mechanism the CharacterManager action above
                        // already uses -- RightSelectionKey (Esc) still
                        // returns straight to gameplay from there via
                        // gamePausedForMenu, bypassing tanyinconvo.s's own
                        // (soft-failed, native-only) Quit() handler.
                        sk_bindings::MenuExecutable* before = stack.currentMenu();
                        npc->script->InvokeOnUse();
                        if (stack.currentMenu() != before) {
                            inGame = false;
                            gamePausedForMenu = true;
                        }
                    } else if (door) {
                        // M38: mode 1 -- the real engine notifies the
                        // trigger list *before* running the door's own
                        // open (FUN_1002e6cc calls FUN_1007307c(level, 1,
                        // door) and only then the open itself), which is
                        // what makes crypt1.s's five trapped doors bite as
                        // you open them.
                        if (gameZoneScript) {
                            gameZoneScript->Notify(
                                sk_bindings::TriggerExecutable::kNotifyDoorOpened, door->typeId,
                                door->name, door->script.get());
                        }
                        door->script->InvokeOnUse();
                    }
                }

                // Per-frame render list: static props (gameEntities,
                // already excludes rats/doors -- see the zone-load block
                // above) plus one PlacedEntity per still-alive monster and
                // one per live door (its accumulated yaw() reflects any
                // real AddRotationTurn() calls OnUse() has made, so an
                // opened door visibly swings); PlacedEntity's shape
                // already covers both, no further renderer changes
                // needed. Dead monsters simply stop appearing here -- no
                // death animation/pose (M8's own "frame 0/skin 0 only"
                // simplification).
                std::vector<sk::PlacedEntity> frameEntities = gameEntities;
                for (MonsterInstance& m : gameMonsters) {
                    // M23: destroyed() (a real zone-root script's
                    // DestroyObjectMirror()) hides an entity from the
                    // world entirely -- unlike death, which now plays the
                    // creature's own real SetDeathAnimation() clip and
                    // leaves the body in its final pose. Before M28 a
                    // killed monster simply blinked out of existence,
                    // which is also why the death clip every real monster
                    // script sets had nothing to play it.
                    if (m.script->destroyed()) continue;
                    // Real per-instance appearance from the creature's own
                    // script (SetSkin/SetScale) -- see
                    // monster_executable.h.
                    sk::PlacedEntity pe{m.x, m.y, m.z, m.modelArchiveIndex, m.facingYaw};
                    pe.skinIndex = m.script->skin();
                    pe.scale = m.script->scale();
                    pe.frameIndex = AdvanceMonsterAnimation(m, modelArchive);
                    frameEntities.push_back(pe);
                    // M46: the creature's SetAttachedWeapon() model, drawn
                    // as a second whole model sharing the body's transform
                    // *and* its absolute animation frame -- which is
                    // exactly what the real monster render override
                    // (FUN_10083490) does, and is correct because the five
                    // weapon models are frame-aligned exports of the same
                    // 144-frame humanoid rig (identical 11-clip table; see
                    // monster_executable.h). Note the frame index is
                    // reused, not recomputed: running the clip lookup
                    // against the weapon's own table would be a different
                    // (if here identical) thing, and the engine plainly
                    // copies `actor+0x70` across.
                    if (m.script->attachedWeaponModel() !=
                        sk_bindings::MonsterExecutable::kNoAttachedWeapon) {
                        sk::PlacedEntity weapon = pe;
                        weapon.modelArchiveIndex = m.script->attachedWeaponModel();
                        // Every weapon model has skinCount 1, so skin 0 is
                        // its only skin -- and the real code does not copy
                        // the body's skin into the weapon's transform
                        // block either.
                        weapon.skinIndex = 0;
                        frameEntities.push_back(weapon);
                    }
                }
                for (const DoorInstance& d : gameDoors) {
                    // Real wall-facing heading from the .ent placement, plus
                    // whatever the script's own AddRotationTurn() has
                    // accumulated (the 90-degree swing door.s applies on
                    // open). Both use the same 65536-per-turn convention.
                    frameEntities.push_back({d.x, d.y, d.z, d.modelArchiveIndex,
                                              d.placementYaw + d.script->yawRadians()});
                }
                // M19: still-in-world pickups -- gamePickups shrinks as
                // items are actually picked up (see the Action::Use
                // handling above), so this naturally stops drawing one the
                // instant it's gone.
                for (const PickupInstance& p : gamePickups) {
                    frameEntities.push_back(
                        {p.x, p.y, p.z, p.modelArchiveIndex, p.placementYaw});
                }
                // M49: arrows in flight. Unlike the spell projectile --
                // whose `+0x134` art selector is still unidentified, so it
                // is simulated invisibly -- an arrow's model is completely
                // pinned down: entities.txt maps its typeId 599/598 to
                // models.idx 175/176, which models.txt names arrow.bin and
                // throw_dagger.bin. The heading is converted back from the
                // engine's own convention (zero along +y) to this port's.
                for (const sk_bindings::ArrowProjectile& shot : gameArrows) {
                    if (shot.modelIndex < 0) continue;
                    frameEntities.push_back(
                        {static_cast<float>(shot.x), static_cast<float>(shot.y),
                          static_cast<float>(shot.z), shot.modelIndex,
                          sk_bindings::PortYawFromEngineYaw(shot.yaw)});
                }
                zoneRenderer.Render(backbuffer, *gameZone, gameCamera, frameEntities, &modelArchive);
                RenderHud(backbuffer, stack.player(), spriteArchive, gameCamera.yaw);
                RenderWeaponViewmodel(backbuffer, gameWeaponViewmodel, spriteArchive);
                // Minimal combat/interact feedback -- name + HP of
                // whatever *aggressive* monster is currently in the
                // player's actual attack range/facing cone, else the
                // nearer of a usable NPC's real SetUseText() prompt (M16)
                // or a door's (M15) (combat takes priority when both are
                // in range at once; aggressive()==false already keeps an
                // NPC like Tanyin Aldwyr out of this first loop entirely --
                // see monster_executable.h's class comment). Drawn at
                // y=34, below the real compass banner (RenderHud now
                // occupies y=0..31 across the top).
                //
                // M20/M22: "in range" now means whichever hand's real
                // weapon reaches farthest (kMeleeRange for bare fists,
                // kSpellRange for a non-weapon item -- a spell) -- matches
                // tryAttack's own per-item range above, so equipping a bow
                // or a spell genuinely shows the HP label from farther
                // away too, not just landing the hit.
                auto handRange = [&](sk_bindings::ItemExecutable* item) -> float {
                    if (!item) return 0.0f;
                    return item->itemType() == sk_bindings::kItemTypeWeapon
                               ? static_cast<float>(item->range())
                               : kSpellRange;
                };
                float playerAttackRange = (std::max)(
                    kMeleeRange,
                    (std::max)(handRange(stack.player().leftItem()),
                               handRange(stack.player().rightItem())));
                const MonsterInstance* facingMonster = nullptr;
                for (const MonsterInstance& m : gameMonsters) {
                    if (!m.script->alive() || m.script->destroyed() || !m.script->aggressive()) {
                        continue;
                    }
                    if (!sk_bindings::InAttackRange(gameCamera.x, gameCamera.y, gameCamera.yaw, m.x,
                                                     m.y, playerAttackRange)) {
                        continue;
                    }
                    facingMonster = &m;
                    break;
                }
                if (facingMonster) {
                    std::string label = facingMonster->script->name() + "  " +
                                         std::to_string(facingMonster->script->currentHealth()) + "/" +
                                         std::to_string(facingMonster->script->maxHealth());
                    sk::BitmapFont::DrawString(backbuffer, 4, 34, label, kSelectedTextColor);
                } else {
                    DoorInstance* door = findNearbyDoor();
                    MonsterInstance* npc = findNearbyUsableMonster();
                    PickupInstance* pickup = findNearbyPickup();
                    float doorDist = door ? distanceTo(door->x, door->y) : kInteractRange + 1.0f;
                    float npcDist = npc ? distanceTo(npc->x, npc->y) : kInteractRange + 1.0f;
                    float pickupDist =
                        pickup ? distanceTo(pickup->x, pickup->y) : kInteractRange + 1.0f;
                    int useTextId = -1;
                    if (pickup && pickupDist <= doorDist && pickupDist <= npcDist) {
                        useTextId = pickup->script->useTextId();
                    } else if (npc && npcDist <= doorDist) {
                        useTextId = npc->script->useTextId();
                    } else if (door) {
                        useTextId = door->script->useTextId();
                    }
                    if (useTextId >= 0) {
                        sk::BitmapFont::DrawString(backbuffer, 4, 34, strings.Get(useTextId),
                                                    kSelectedTextColor);
                    }
                }
                window.Present(backbuffer);
                return;
            }
        }

        if (gamePausedForMenu && input.ConsumeJustPressed(sk::ButtonSlot::RightSelectionKey)) {
            // See gamePausedForMenu's declaration comment -- always
            // resumes gameplay directly, bypassing whatever menu screen
            // (charactermanager.s or a nested Inventory/Stats/QuestLog)
            // is currently open.
            gamePausedForMenu = false;
            inGame = true;
            zoneRenderer.Render(backbuffer, *gameZone, gameCamera, gameEntities, &modelArchive);
            RenderHud(backbuffer, stack.player(), spriteArchive, gameCamera.yaw);
            RenderWeaponViewmodel(backbuffer, gameWeaponViewmodel, spriteArchive);
            window.Present(backbuffer);
            return;
        }

        if (stack.creditsActive()) {
            if (input.ConsumeJustPressedOrRepeat(sk::ButtonSlot::Up) && creditsScroll > 0) {
                --creditsScroll;
            }
            if (input.ConsumeJustPressedOrRepeat(sk::ButtonSlot::Down)) ++creditsScroll;
            if (input.ConsumeJustPressed(sk::ButtonSlot::LeftSelectionKey) ||
                input.ConsumeJustPressed(sk::ButtonSlot::RightSelectionKey)) {
                stack.CloseCredits();
                creditsScroll = 0;
            }
            RenderCredits(backbuffer, stack.creditsLines(), creditsScroll);
            window.Present(backbuffer);
            return;
        }

        sk_bindings::MenuExecutable* menu = stack.currentMenu();
        if (menu) {
            // A screen whose OnDisplay() rebuilt its rows (ClearMenu +
            // repopulate -- the common pattern across the real corpus)
            // comes back with selectedItem() == 0, and the "new menu"
            // snap below only fires when the *identity* of the current
            // menu changes. Without this, reopening a screen you were
            // already on left it with nothing highlighted and Enter doing
            // nothing. See MenuExecutable::EnsureValidSelection().
            menu->EnsureValidSelection();
            sk_bindings::MenuExecutable* before = menu;
            try {
                if (sk_bindings::PopupMenuExecutable* popup = menu->activePopup()) {
                    // A visible confirmation popup captures input ahead of
                    // the underlying menu's own row navigation.
                    if (input.ConsumeJustPressedOrRepeat(sk::ButtonSlot::Up)) {
                        popup->MoveSelection(-1);
                    }
                    if (input.ConsumeJustPressedOrRepeat(sk::ButtonSlot::Down)) {
                        popup->MoveSelection(1);
                    }
                    if (input.ConsumeJustPressed(sk::ButtonSlot::LeftSelectionKey)) {
                        popup->ActivateSelected();
                    }
                    if (input.ConsumeJustPressed(sk::ButtonSlot::RightSelectionKey)) {
                        popup->GoBack();
                    }
                } else if (menu->textEntryActive()) {
                    // Typed characters land via SetCharCallback above; here
                    // just the two control actions matter -- confirm and
                    // back. MenuExecutable::GoBack() handles both real
                    // spellings of the back handler plus the SetPrevMenu()
                    // fallback (see its comment).
                    if (input.ConsumeJustPressed(sk::ButtonSlot::LeftSelectionKey)) {
                        menu->TryInvoke("Done");
                    }
                    if (input.ConsumeJustPressed(sk::ButtonSlot::RightSelectionKey)) {
                        menu->GoBack();
                    }
                } else {
                    // M10: Up/Down on a Table row (inventory/stats/quest-
                    // log screens) navigates within the table instead of
                    // to the next outer row -- TryMoveTableSelection()
                    // only does something (and returns true) when the
                    // current selection actually is a Table.
                    // M35: NavigateDirectional() first -- on any screen the
                    // script laid out by hand (the inventory/equip tab
                    // strip, charactermanager.s's 2x2 grid) it moves the
                    // way the layout reads, and lets a table release focus
                    // at its own first/last row instead of trapping it.
                    // It declines on the plain centred AddMenuItem lists,
                    // which then behave exactly as before.
                    if (input.ConsumeJustPressedOrRepeat(sk::ButtonSlot::Up)) {
                        if (!menu->NavigateDirectional(0, -1)) {
                            if (!menu->TryMoveTableSelection(-1)) menu->MoveSelection(-1);
                        }
                    }
                    if (input.ConsumeJustPressedOrRepeat(sk::ButtonSlot::Down)) {
                        if (!menu->NavigateDirectional(0, 1)) {
                            if (!menu->TryMoveTableSelection(1)) menu->MoveSelection(1);
                        }
                    }
                    if (input.ConsumeJustPressedOrRepeat(sk::ButtonSlot::Left)) {
                        if (!menu->NavigateDirectional(-1, 0)) {
                            // Portrait/name-entry screens (useHoriz) walk
                            // rows; everything else adjusts the selected
                            // combo/slider, if it has one.
                            if (menu->useHoriz()) {
                                menu->MoveSelection(-1);
                            } else {
                                menu->CycleSelectedCombo(-1);
                            }
                        }
                    }
                    if (input.ConsumeJustPressedOrRepeat(sk::ButtonSlot::Right)) {
                        if (!menu->NavigateDirectional(1, 0)) {
                            if (menu->useHoriz()) {
                                menu->MoveSelection(1);
                            } else {
                                menu->CycleSelectedCombo(1);
                            }
                        }
                    }
                    if (input.ConsumeJustPressed(sk::ButtonSlot::LeftSelectionKey)) {
                        // M10: Enter on a selected Table row fires its own
                        // SetCallback() handler (inventory.s's
                        // SelectedInventoryItem, statsscreen.s's
                        // OnTableSel, ...) instead of the outer row's
                        // usual ActivateSelected() path.
                        if (!menu->TryActivateTable()) menu->ActivateSelected();
                    }
                    if (input.ConsumeJustPressed(sk::ButtonSlot::RightSelectionKey)) {
                        menu->GoBack();
                    }
                }
            } catch (skRuntimeException& e) {
                std::printf("shadowkey-port: RUNTIME ERROR: %s\n", e.toString().ptr());
            }
            menu = stack.currentMenu();  // a callback may have opened a new one

            // M30: a script's own Quit() (MenuStack::closeMenuRequested()).
            // Handled here, after the whole callback chain has returned, so
            // a handler that does `Quit(); OpenMenu(...)` -- which several
            // real screens do -- still ends up on the menu it asked for
            // rather than being closed out from under itself.
            if (stack.closeMenuRequested()) {
                stack.ClearCloseMenuRequest();
                if (menu == before && menu) {
                    // Nothing else navigated: close for real. Back to
                    // gameplay if this screen was opened from the 3D view
                    // (an NPC conversation's "Goodbye"), otherwise to the
                    // screen's own SetPrevMenu target.
                    if (gamePausedForMenu && gameZone) {
                        gamePausedForMenu = false;
                        inGame = true;
                    } else if (!menu->prevMenuPath().empty()) {
                        stack.OpenMenu(menu->prevMenuPath());
                        menu = stack.currentMenu();
                    }
                }
            }
        }

        // A freshly opened menu (via OpenMenu()) starts with no selection
        // of its own -- snap to its first selectable row. (Also covered by
        // EnsureValidSelection() above from the next tick on; kept so the
        // very first frame a new menu is drawn already has a highlight.)
        if (menu && menu != lastMenu) {
            menu->EnsureValidSelection();
            lastMenu = menu;
        }

        if (menu) {
            RenderMenu(backbuffer, *menu, stack.player(), strings, spriteArchive);
        } else {
            backbuffer.Fill(kBackgroundColor);
        }
        window.Present(backbuffer);
    });

    return 0;
}
