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
#include <ctime>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
// M113: was reaching this file only through one of the debug suite's own
// headers, so every std::filesystem use below (DebugZoneNames,
// EnumerateMenuScripts, UserDirectory) compiled purely by luck -- and a
// build with SK_DEBUG_SUITE=OFF, which is what a shipping build is, failed
// outright. That configuration had evidently never been built before this
// milestone needed it.
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "skTreeNode.h"
#include "assets/game_config.h"
#include "assets/sound_archive.h"
#include "assets/sprite_archive.h"
#include "assets/string_table.h"
#include "assets/zone_display_names.h"
#include "audio/audio_engine.h"
#include "engine/frame_interpolation.h"
#include "engine/game_clock.h"
#include "engine/gamepad_map.h"
#include "engine/input_state.h"
#include "engine/pc_key_map.h"
#include "engine/pc_settings.h"
#include "engine/screen_mode.h"
#include "graphics/backbuffer.h"
#include "graphics/bitmap_font.h"
#include "graphics/scene_upscale.h"
#include "graphics/text_box.h"
#include "platform/win32/d3d11_renderer.h"
#include "net/mp_session.h"
#include "net/mp_world_fns.h"
#include "simkin_bindings/mp_hooks.h"
#include "render3d/gpu_frame.h"
#include "graphics/viewport.h"
#include "platform/win32/console_tee.h"
#include "platform/win32/crash_report.h"
#include "platform/win32/exe_dir.h"
#include "platform/win32/gamepad.h"
#include "engine/mouse_look_keys.h"
#include "engine/credits_roll.h"
#include "platform/win32/window.h"
#include "render3d/camera.h"
#include "render3d/zone_renderer.h"
#include "simkin_bindings/combat.h"
#include "simkin_bindings/combo_box_executable.h"
#include "simkin_bindings/door_executable.h"
#include "simkin_bindings/entity_base_ref.h"
#include "simkin_bindings/floating_sprite_executable.h"
#include "simkin_bindings/game_constants.h"
#include "simkin_bindings/ghost_mode.h"
#include "simkin_bindings/player_move.h"
#include "simkin_bindings/effects.h"
#include "simkin_bindings/use_prompt.h"
#include "simkin_bindings/button_executable.h"
#include "simkin_bindings/item_button_executable.h"
#include "simkin_bindings/item_executable.h"
#include "simkin_bindings/level_executable.h"
#include "simkin_bindings/native_binding_common.h"
#include "simkin_bindings/script_vars.h"
#include "assets/save_records.h"
#include "assets/save_stream.h"
#include "simkin_bindings/menu_executable.h"
#include "simkin_bindings/menu_stack.h"
#include "simkin_bindings/monster_executable.h"
#include "simkin_bindings/monster_ai.h"
#include "simkin_bindings/on_detect.h"
#include "simkin_bindings/player_executable.h"
#include "simkin_bindings/popup_menu_executable.h"
#include "simkin_bindings/quest_table.h"
#include "simkin_bindings/slider_executable.h"
#include "simkin_bindings/arrow_projectile.h"
#include "simkin_bindings/spell_cast.h"
#include "simkin_bindings/spell_projectile.h"
#include "simkin_bindings/table_executable.h"
#include "simkin_bindings/text_area_executable.h"
#include "simkin_bindings/vitals.h"
#include "simkin_bindings/weapon_viewmodel.h"
#include "simkin_bindings/zone_script_executable.h"
#include "skExecutableContext.h"
#include "skInterpreter.h"
#include "skParseException.h"
#include "skRuntimeException.h"
#include "world/entity_types.h"
#include "world/model_archive.h"
#include "world/automap.h"
#include "world/model_collision.h"
#include "world/zone.h"

// SK_DEBUG_SUITE (M68): the developer debug suite -- console, stat overlay,
// instrumentation. Every touchpoint in this file is inside a
// `#if SK_DEBUG_SUITE` block tagged with this same marker, so
// `grep SK_DEBUG_SUITE` finds all of them at removal time. See
// docs/DEBUG_SUITE.md.
#if SK_DEBUG_SUITE
#include <cstdlib>
#include <filesystem>

#include "debug/debug_host.h"
#include "debug/debug_metrics.h"
#include "debug/debug_suite.h"
#include "simkin_bindings/character_progression.h"
#endif

namespace {

// kBackgroundColor is a placeholder -- only the flat fallback fill for
// when a real MenuBackground() sprite fails to load (docs/
// GRAPHICS_FORMAT.md); the real backgrounds themselves are decoded
// sprite art, not a color.
//
// M108: the menu text colors are the **engine's own**, not sampled off a
// screenshot any more. They live on the menu object and the menu
// constructor (`FUN_10073bd8`) seeds all three, as 12-bit RGB444 words:
//
//     menu+0x90 = 0x733   // an unselected *menu item*   (widget+0x5c != 0)
//     menu+0x92 = 0x752   // a *static* item             (widget+0x5c == 0)
//     menu+0x94 = 0xddd   // whichever row is selected
//
// `FUN_10076b64` picks between them exactly that way, and `FUN_1008f8a4`
// expands each nibble with **`nibble << 4`** -- so the channel maxes at
// 0xf0, not 0xff. The old values here were eyeballed from a screenshot
// and every one of them was off: (140,40,40) for what is really
// (112,48,48), (110,95,75) for (112,80,32), (235,235,235) for
// (208,208,208). Small individually; together they are most of why the
// menus read as "close but not right".
constexpr uint16_t MenuColor444(int rgb444) {
    return sk::PackRGB565(((rgb444 >> 8) & 0xF) << 4, ((rgb444 >> 4) & 0xF) << 4,
                          (rgb444 & 0xF) << 4);
}
constexpr uint16_t kBackgroundColor = sk::PackRGB565(16, 16, 32);
constexpr uint16_t kTextColor = MenuColor444(sk_bindings::kMenuItemColor444);
constexpr uint16_t kSelectedTextColor = MenuColor444(sk_bindings::kSelectedItemColor444);
constexpr uint16_t kStaticTextColor = MenuColor444(sk_bindings::kStaticItemColor444);
// M108: the shadow under a *centred* row. `FUN_1008f97c`'s shadow arm
// passes the channels literally -- `FUN_10022d48(env, text, y + 1, 0xb1,
// 0x9c, 0x65, ...)` -- rather than an RGB444 word, so this one is not a
// MenuColor444(). Note the offset is **+1 in y only**; the left-aligned
// arm's shadow (`FUN_1007f49c`, colour 0xb96) is offset in x as well.
constexpr uint16_t kCenteredShadowColor = sk::PackRGB565(0xb1, 0x9c, 0x65);
// Still the old eyeballed blue, and now only for the two non-menu screens
// that borrowed it (the loading banner and the vignette caption), whose
// real draws were never traced. Menu titles use kUiStandardColor below.
constexpr uint16_t kTitleColor = sk::PackRGB565(140, 180, 255);
// M118: the UI palette object at `engine+0x28` -- see menu_executable.h.
// Every widget that draws itself (titles, combos, text areas, table cells,
// buttons, floating labels) takes +8, switches to +10 while it has focus,
// and draws its rectangle in +0xc.
constexpr uint16_t kUiStandardColor = MenuColor444(sk_bindings::kUiStandardColor444);
constexpr uint16_t kUiHighlightColor = MenuColor444(sk_bindings::kUiHighlightColor444);
constexpr uint16_t kUiBoxColor = MenuColor444(sk_bindings::kUiBoxColor444);
constexpr uint16_t kInventoryEquippedColor =
    MenuColor444(sk_bindings::kInventoryEquippedColor444);
constexpr uint16_t kInventoryClassBarredColor =
    MenuColor444(sk_bindings::kInventoryClassBarredColor444);
// M112: the slider marker. The geometry itself lives in
// menu_executable.h next to the menu's other engine-derived constants,
// so the smoke test can check it; only the colour needs packing here.
// The track borrows `menu+0x90`, the *unselected* item colour, and keeps
// it whether or not the row has focus -- only the label changes colour.
constexpr uint16_t kSliderMarkerColor = MenuColor444(sk_bindings::kSliderMarkerColor444);
constexpr uint16_t kStaticItemColor = MenuColor444(sk_bindings::kStaticItemColor444);
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

// M124: `FUN_1008e634`, the engine's own character-count wrap, which the
// popup draw calls with `(w >> 3) + 3`. A string shorter than the limit is
// one line, untouched. Otherwise it walks the text keeping the last space
// or hyphen it passed: a newline ends a line; reaching a space or hyphen
// with more than `limit` characters since the line began cuts the line at
// the *previous* break (that character is dropped); and at the end, a tail
// still over the limit is split once more at its last space. (The tail's
// other arm -- a single word longer than the limit, hard-cut with a
// suffix -- is left whole here; no popup string reaches it.)
std::vector<std::string> WrapWords(const std::string& text, int limit) {
    if (static_cast<int>(text.size()) < limit) return {text};
    std::vector<std::string> lines;
    int start = 0, lastBreak = 0, count = 0;
    const int n = static_cast<int>(text.size());
    for (int i = 0; i < n; ++i) {
        const char c = text[static_cast<size_t>(i)];
        int nextBreak = lastBreak;
        if (c == '\n') {
            lines.push_back(text.substr(static_cast<size_t>(start), static_cast<size_t>(i - start)));
            start = i + 1;
            count = 0;
        } else if (c == ' ' || c == '-') {
            nextBreak = i;
            if (limit < count && lastBreak >= start) {
                lines.push_back(text.substr(static_cast<size_t>(start),
                                            static_cast<size_t>(lastBreak - start)));
                start = lastBreak + 1;
                count = i - lastBreak;
            }
        }
        lastBreak = nextBreak;
        ++count;
    }
    if (limit < count && lastBreak >= start && lastBreak < n && text[static_cast<size_t>(lastBreak)] == ' ') {
        lines.push_back(text.substr(static_cast<size_t>(start), static_cast<size_t>(lastBreak - start)));
        start = lastBreak + 1;
    }
    if (start < n) lines.push_back(text.substr(static_cast<size_t>(start)));
    return lines;
}

// M88: the same word-wrap, but bounded by real pixels instead of a
// character count. AddTable's box is 150 *pixels* wide and the font is
// proportional, so a characters-per-line estimate either wastes a third
// of the box or (as it first did on the quest log) runs "Return five
// types of herbs to Rilora: Foxglove, ..." off the right-hand edge.
// Kept separate from DrawWrappedText rather than replacing it: that one's
// callers pass widths the *scripts* give in characters
// (`textArea.SetTextWidth(11)`, starthelp.s's 0x19), which are not
// pixels and must not be reinterpreted as such.
int DrawWrappedTextPx(sk::Backbuffer& bb, int x, int y, const std::string& text, int maxPixels,
                       uint16_t color) {
    const int lineHeight = sk::BitmapFont::kGlyphHeight + 3;
    if (maxPixels <= 0) maxPixels = sk::Backbuffer::kWidth - x;
    std::vector<std::string> lines;
    std::string current;
    std::istringstream words(text);
    std::string word;
    while (words >> word) {
        std::string candidate = current.empty() ? word : current + " " + word;
        if (sk::BitmapFont::TextWidth(candidate) > maxPixels && !current.empty()) {
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
// **M71 -- corrected, and the correction is the whole "chairs face the
// wrong way" bug.**
//
// The old body here was `raw*2pi/65536 - pi/2`, and its own comment said
// where that came from: the zero reference "was never derived from the
// binary, it was fitted by eye until doors stopped reading as permanently
// open". A door is a flat slab, so that fit could not see a half turn --
// and a half turn is precisely what it got wrong. Written as an error term,
// the old formula differed from the real one by `pi - 2*heading`: exactly
// zero at headings of 90 and 270 degrees, exactly 180 degrees off at 0 and
// 180. Doors in the shipped zones sit overwhelmingly at the two headings
// where the two agree, which is why it survived; a chair at heading 0 was
// drawn with its back to where its back should be.
//
// The real relation was already in this codebase, derived twice over from
// two unrelated functions, just not here:
//
//  * `sk_bindings::PortYawFromEngineYaw` (M48/M49) took it from the spell
//    spawn's `vx = sin(yaw); vy = cos(yaw)`.
//  * The camera matrix itself: `Render3DScene` builds `engine+0x5d8` as
//    `FUN_10073760(m, -roll, -pitch, -heading)`, whose depth row comes out
//    `[sin h, 0, cos h]` -- so the engine's forward is `(sin h, cos h)` in
//    world X/Y, and this port's is `(cos yaw, sin yaw)`.
//
// Both give `yaw = pi/2 - heading`. So a placement's heading converts
// exactly like the camera's does, and this function is now literally the
// same one -- see CameraYawRadians below.
float PlacementYawRadians(uint16_t yawRaw) {
    return sk_bindings::PortYawFromEngineYaw(static_cast<int>(yawRaw));
}

// M71: the placement's other two orientation channels. Unlike the heading
// these need no port-space conversion at all -- `sk::PlacedEntity::rotA/rotB`
// feed straight into the transcribed `BuildRotationMatrix3x4`, so all that
// is wanted is the engine's 65536-per-turn unit in radians.
float PlacementRotRadians(uint16_t raw) {
    constexpr float kTwoPi = 6.28318530718f;
    return static_cast<float>(raw) / 65536.0f * kTwoPi;
}

// M71: `.ent`'s `unkB` low halfword -> the object's `+0x5e`, an 8.8 model
// scale where 256 is 1:1 (docs/ZONE_FORMAT.md's M52 note). Zero never
// occurs in shipped data; guarded anyway so a corrupt record cannot
// collapse a model to a point.
float PlacementScale(uint16_t raw) {
    return raw > 0 ? static_cast<float>(raw) / 256.0f : 1.0f;
}

// A raw engine *heading* (the player object's own `+0xb6` channel, 65536 ==
// one full turn) as this port's camera yaw, and back.
//
// **M71 corrects M61's sign, and the correction is derived twice.** M61
// read the relation off `FUN_100063f0` -- but that function is **strafe**,
// not walk-forward. Its sibling `FUN_100065b4` is the forward one, and it
// advances the entity by
//
//     dx += speed * sin(heading)
//     dy += speed * sin(heading + 0x4000)  ==  +speed * cos(heading)
//
// i.e. forward is `(sin h, cos h)`, ninety degrees off what M61 concluded
// from the strafe. `FUN_100063f0`'s `(cos h, -sin h)` is that vector turned
// a quarter turn clockwise, which is what a strafe is.
//
// Independently, the renderer agrees: `Render3DScene` builds the camera
// matrix as `FUN_10073760(engine+0x5d8, -roll, -pitch, -heading)`, whose
// depth row evaluates to `[sin h, 0, cos h]` against a `(worldX, up,
// worldY)` input vector -- the same forward vector, straight out of the
// projection this port's own `flatForward` reproduces.
//
// Against this port's `(cos yaw, sin yaw)` that gives `yaw = pi/2 -
// heading`, which is exactly the conversion `sk_bindings::
// PortYawFromEngineYaw` has carried since M48 (derived there from the spell
// spawn's `vx = sin(yaw); vy = cos(yaw)`). Three functions, one answer --
// so this now simply calls it rather than keeping a fourth copy that
// disagreed with the other three.
float CameraYawRadians(int32_t rawAngle) {
    return sk_bindings::PortYawFromEngineYaw(static_cast<int>(rawAngle) & 0xffff);
}

// **Pitch is not yaw and never was.** M61 pushed both channels through one
// conversion, which silently gave the pitch a quarter-turn offset it must
// not have (and the wrong sign besides). The camera matrix settles it: with
// `p3 = -pitch`, `FUN_10073760`'s depth row picks up `up * sin(p3) == -up *
// sin(pitch)`, and this port's own `forward = flatForward*cosPitch -
// rz*sinPitch` has the same `-sin` coefficient on the same term. So the raw
// channel *is* the port's pitch, unit conversion and nothing else.
float CameraPitchRadians(int32_t rawAngle) {
    constexpr float kTwoPi = 6.28318530718f;
    return static_cast<float>(static_cast<int16_t>(rawAngle & 0xffff)) / 65536.0f * kTwoPi;
}

// The inverse of CameraYawRadians: this port's camera yaw as the engine's
// own `player+0xb6` heading, [0, 65536). Everything that consumes a real
// decompiled heading formula -- the compass tape and the automap marker --
// goes through this so they cannot drift apart. `pi/2 - x` is its own
// inverse, which is why this is the same reflection run the other way.
int CameraHeadingUnits(float cameraYaw) {
    return sk_bindings::EngineYawFromPortYaw(cameraYaw);
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

// M67: the host side of DoorExecutable::TileStamp, same shape and the same
// layering reason as LiveZoneRegions above. A door's SetPassable() reaches
// the tile grid through this, which is what makes an opened door actually
// walk-through-able -- see door_executable.h and Zone::StampEntityBox.
class LiveTileStamp : public sk_bindings::DoorExecutable::TileStamp {
public:
    void SetZone(sk::Zone* zone) { m_Zone = zone; }
    void StampEntityBox(int worldX, int worldY, int headingRaw, int halfExtentX, int halfExtentY,
                        uint8_t mask, bool set) override {
        if (!m_Zone) return;
        // journal = true: every SetPassable-driven call passes the real
        // `param_3 = 1`, so the touched cells land in the tile-change
        // journal (Zone::tileChanges()).
        m_Zone->StampEntityBox(worldX, worldY, headingRaw, halfExtentX, halfExtentY, mask, set,
                                /*journal=*/true);
    }

private:
    sk::Zone* m_Zone = nullptr;
};

// M119: the interpolation keys of things identified by a serial rather than
// by their script object (engine/interp_serial.h). Tagged into the top bits
// so they can never equal a script pointer, which is how creatures, doors
// and pickups are keyed.
constexpr uint64_t kInterpArrowTag = 1ull << 61;
constexpr uint64_t kInterpSpellTag = 2ull << 61;
constexpr uint64_t kInterpEffectTag = 3ull << 61;

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
    // M71: see DoorInstance's identical pair. (A creature's scale is not
    // here -- it lives on the script, which the placement seeds via
    // MonsterExecutable::SetPlacementScaleRaw and a later SetScale can
    // still change.)
    float rotA = 0.0f, rotB = 0.0f;
    // Reporting only (the debug overlay's `state` column). The real state
    // is the pair below it: the AI package on the script, plus whether
    // `monster+0x20c` holds a target.
    enum class AiState { Idle, Chasing, Attacking } aiState = AiState::Idle;
    // M85: this creature's slot in the object-ID buffer -- the real
    // `actor+0x2d4`, the byte the stencil rasterizers stamp at every
    // pixel of it they draw and the melee target search reads back. 0 is
    // "not registered", which is also the engine's own "nothing here".
    // Allocated once and kept, and released when the creature is
    // destroyed, because the buffer the search reads was stamped by the
    // *previous* frame -- an id that changed between the stamp and the
    // read would point the attack at a different creature.
    int objectId = 0;
    // M86: the red hit flash, `FUN_10067c3c`/`FUN_10064f08`'s ramp-row
    // oscillator. See render3d/zone_renderer.h's HitFlash.
    sk::HitFlash flash;
    // M77: the real `monster+0x20c`, reduced to a bool because in
    // singleplayer the only candidate the tick ever considers is
    // `engine+0x618`, the player. Package 3 *without* a target is a real
    // and reachable state -- it is what the look arm leaves behind on the
    // tick before it acquires -- so this cannot be folded into the package.
    bool hasTarget = false;
    // M130 (multiplayer): tag 0x1e named the *other* player as this
    // creature's target, so here it goes for that player's avatar; and
    // whether this game has announced its own acquisition / burn yet.
    bool mpTargetsPeer = false;
    bool mpTargetSent = false;
    bool mpBurnSent = false;
    // M28: vertex-animation playback state. `animClip` is the clip index
    // the creature's own script named (idle/walk/swing/death); `animTime`
    // counts seconds into it, and the death clip latches (plays once and
    // holds its last frame) instead of looping.
    int animClip = -1;
    float animTime = 0.0f;
    bool animHoldLastFrame = false;
    // M77: the real `PlayAnimation(clip, 1, next, rate)` -- mode 1 plays
    // the clip through once and then hands over to `next`. The attack uses
    // exactly that (`swing`, once, then `idle`), which is what makes a
    // swing a discrete event rather than a looping pose. -1 when the
    // clip currently playing has no successor.
    int animThenClip = -1;
    // M24: the real entities.txt typeId this placement resolved from --
    // ZoneScriptExecutable::NotifyKilled() (a real zone-root script's own
    // AddTrigger()...SetEntityID(id) kill-count trigger, e.g. ghstpass.s's
    // zombieTrigger.SetEntityID(104)) matches kills against this.
    int typeId = -1;
    // Post-M120: the placement's other two orientation channels and scale,
    // raw, so the level-state record can carry them back (see
    // SnapshotLevelState in main()).
    uint16_t rotARaw = 0, rotBRaw = 0, scaleRaw = 256;
    // M122: `+0x98` / `+0xa0`, the movement delta the walk pushes and the
    // ground friction decays -- engine units, `v * frameDelta >> 8` a tick.
    int vx = 0, vy = 0;
    // M45: the real `actor+0x2e4` backlink -- which encounter region
    // spawned this creature, so its death can decrement that region's
    // live count (`FUN_10083c04` calls `FUN_1008b118(actor->region)`).
    // That count is what lets a cleared region spawn again. Null for
    // everything placed by the zone's own `.ent`.
    sk_bindings::EncounterExecutable* encounter = nullptr;
    size_t encounterRegion = 0;
    // M81: whether this creature's death has already been handled --
    // OnKilled(), the loot bag, the zone's kill-count trigger, the
    // encounter's live count. The engine has exactly one death handler
    // (`FUN_10083c04`, the actor's own vtable slot, called from the damage
    // path itself), so it cannot miss a death or run one twice. This port
    // reached the same handling from the four places that damage a
    // creature, which meant a creature killed any *other* way -- an area
    // spell (`FUN_1004720c`, which just calls ApplyDamage on everything in
    // range), a script's own `DoDamage`, the debug console's `killall` --
    // died silently: no loot, no trigger, no OnKilled. The flag makes the
    // handling idempotent so a single sweep can back-stop all of them.
    // M100: that flag now lives on the creature itself, as
    // MonsterExecutable::TakeDeathOwed -- a script's SetDead(true) also
    // clears alive(), and must not read as a death.
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
        // M77: the successor half of the real PlayAnimation(clip, 1, next,
        // rate). A clip that ran out with a successor hands over to it
        // (looping) on the next tick; one without -- the death clip -- goes
        // on holding its final frame, which is what M28 latched it for.
        if (m.animThenClip >= 0 && advanced >= clip->frameCount()) {
            m.animClip = m.animThenClip;
            m.animThenClip = -1;
            m.animHoldLastFrame = false;
            m.animTime = 0.0f;
        }
    } else {
        frameInClip = advanced % clip->frameCount();
        // Keep animTime from growing without bound over a long session.
        float clipSeconds = static_cast<float>(clip->frameCount()) / rate;
        if (clipSeconds > 0.0f && m.animTime >= clipSeconds) m.animTime -= clipSeconds;
    }
    return clip->startFrame + frameInClip;
}

// M72: what the automatic aim-assist aims *at* -- the midpoint of the
// creature's collision cylinder, exactly as `Render3DScene` computes it:
//
//     targetCentre = target->z + Height() / 2
//
// (0x10017ce8: `ldrh r1,[target,#0xa4]` then `asr r0,r0,#1` on the
// Height() result, plus the -0x80/+0x80 pair that only exists to force the
// intermediate through a 16-bit truncation). Height() is the real
// per-creature table -- see sk::MonsterCollisionHeight.
//
// This replaces M30's `MonsterCenterZ`, which measured the drawn model's
// vertical extent instead. That guess is what made the pan fire for
// everything: it produced *some* non-zero aim point for every creature,
// where the engine has a hard yes/no gate on this height.
float MonsterAimCenterZ(const MonsterInstance& m) {
    return m.z + static_cast<float>(sk::MonsterCollisionHeight(m.modelArchiveIndex)) * 0.5f;
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
    // M71: the record's other two orientation channels (radians, engine
    // sense) and its `+0x5e` model scale -- see sk::PlacedEntity::rotA and
    // Zone::EntPlacement::rotARaw.
    float rotA = 0.0f, rotB = 0.0f;
    float scale = 1.0f;
    // M38: the two ways a zone trigger identifies the entity it guards --
    // its entities.txt typeId and its .ent placement name. crypt1.s's five
    // trapped doors are matched purely by name.
    int typeId = -1;
    std::string name;
    // Post-M120: raw placement channels for the level-state record.
    uint16_t rotARaw = 0, rotBRaw = 0, scaleRaw = 256;
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
    // M71: see DoorInstance's identical trio.
    float rotA = 0.0f, rotB = 0.0f;
    float scale = 1.0f;
    // M30: true for a container/loot category (8/12) rather than a
    // directly-collectable world item -- its OnUse() opens a real loot
    // menu instead of transferring itself. Kept explicitly rather than
    // inferred, so an empty container still prompts correctly.
    bool isContainer = false;
    // Post-M120: what the level-state record needs to rebuild it -- the
    // entities.txt typeId (-1 when the creator did not know one) and the
    // raw placement channels.
    int typeId = -1;
    uint16_t yawRaw = 0, rotARaw = 0, rotBRaw = 0, scaleRaw = 256;
};

#if SK_DEBUG_SUITE
// SK_DEBUG_SUITE (M68) ======================================================
//
// The debug suite's view of the running game. `sk_debug` deliberately links
// neither `sk_world` nor `sk_bindings` and cannot see the four Instance
// types above (they are file-local to main.cpp), so everything it needs
// arrives through the `sk_debug::DebugHost` interface -- the same layering
// LevelExecutable::ZoneRegions (M44) and DoorExecutable::TileStamp (M67)
// already use.
//
// `DebugGameRefs` is the pointer bundle main() fills in with the addresses
// of its own locals. Pointers, not copies: everything here has to observe
// the *live* state, and the state that gets replaced on a zone load (the
// Zone itself) is reached through the owning unique_ptr so the reference
// stays valid across a transition.
//
// The debug-only toggles live in this struct rather than in main()'s own
// locals, so that removing the suite removes them too -- there is no
// leftover `bool noclip` for the game to have to ignore.
struct DebugGameRefs {
    std::string scriptRoot;
    sk::StringTable* strings = nullptr;
    sk::EntityTypeTable* entityTypes = nullptr;
    sk::ModelArchive* modelArchive = nullptr;
    sk::ModelCollisionTable* modelCollision = nullptr;
    sk_bindings::MenuStack* stack = nullptr;
    skInterpreter* interpreter = nullptr;
    std::unique_ptr<sk::Zone>* zone = nullptr;
    std::unique_ptr<sk_bindings::ZoneScriptExecutable>* zoneScript = nullptr;
    sk::Camera* camera = nullptr;
    std::vector<MonsterInstance>* monsters = nullptr;
    std::vector<DoorInstance>* doors = nullptr;
    std::vector<PickupInstance>* pickups = nullptr;
    std::vector<TrapInstance>* traps = nullptr;
    std::vector<sk::PlacedEntity>* props = nullptr;
    std::vector<sk_bindings::SpellProjectile>* projectiles = nullptr;
    std::vector<sk_bindings::ArrowProjectile>* arrows = nullptr;
    std::set<size_t>* regionsOccupied = nullptr;
    bool* inGame = nullptr;
    bool* mapOpen = nullptr;

    // Toggles, owned here. Read by the handful of `#if SK_DEBUG_SUITE`
    // guards in the tick loop.
    bool noclip = false;
    bool godMode = false;
    bool freezeAi = false;
    bool showTileGrid = false;
};

class LiveDebugHost : public sk_debug::DebugHost {
public:
    explicit LiveDebugHost(DebugGameRefs& refs) : m_Refs(refs) {}

    bool InGame() const override {
        return m_Refs.inGame && *m_Refs.inGame && m_Refs.zone && *m_Refs.zone;
    }

    void HostTick() override;

    // ---- inspection -----------------------------------------------------

    std::vector<std::string> Pages() const override {
        return {"mini", "world", "player", "zone", "entities", "tile", "inventory", "quests",
                "menu",  "render", "script"};
    }

    void Inspect(const std::string& page, std::vector<sk_debug::StatGroup>& out) const override;

    void Entities(std::vector<sk_debug::EntityRow>& out) const override;

    std::vector<std::string> CatalogKinds() const override {
        return {"zones",   "entities", "scripts", "monsters", "items",
                "weapons", "armor",    "spells",  "races",    "classes"};
    }

    std::vector<std::string> Catalog(const std::string& kind,
                                      const std::string& filter) const override;

    // ---- toggles ---------------------------------------------------------

    std::vector<sk_debug::DebugFlag> Flags() const override {
        return {
            {"noclip", m_Refs.noclip ? 1 : 0, "walk through walls, doors and props"},
            {"god", m_Refs.godMode ? 1 : 0, "ignore all incoming damage"},
            {"freezeai", m_Refs.freezeAi ? 1 : 0, "creatures stop thinking, moving and attacking"},
            {"tilegrid", m_Refs.showTileGrid ? 1 : 0,
             "overlay the tile block flags around the player on the world page"},
        };
    }

    bool SetFlag(const std::string& name, int value, std::string& message) override {
        bool* target = nullptr;
        if (name == "noclip") target = &m_Refs.noclip;
        else if (name == "god") target = &m_Refs.godMode;
        else if (name == "freezeai") target = &m_Refs.freezeAi;
        else if (name == "tilegrid") target = &m_Refs.showTileGrid;
        if (!target) {
            message = "unknown flag `" + name + "` -- `flags` lists them";
            return false;
        }
        *target = value != 0;
        message = name + " = " + (*target ? "1" : "0");
        sk_debug::Log("debug", message);
        return true;
    }

    // ---- world actions ----------------------------------------------------

    bool Teleport(float x, float y, bool tileCoords, std::string& message) override;
    bool TeleportToEntity(int index, std::string& message) override;
    bool LoadZone(const std::string& zone, std::string& message) override;
    bool Face(float degrees, std::string& message) override;
    bool Spawn(const std::string& what, int count, float distance, std::string& message) override;
    bool Give(const std::string& what, int count, std::string& message) override;
    bool Equip(const std::string& what, int hand, std::string& message) override;
    bool KillAll(const std::string& filter, bool asPlayer, std::string& message) override;

    // ---- the native bridge -------------------------------------------------

    std::vector<std::pair<std::string, std::string>> Receivers() const override;
    bool CallNative(const std::string& receiver, const std::string& method,
                     const std::vector<std::string>& args, std::string& message) override;
    bool ScriptEval(const std::string& receiver, const std::string& code,
                     std::string& message) override;

private:
    sk::Zone* zone() const { return m_Refs.zone ? m_Refs.zone->get() : nullptr; }
    // Resolves a receiver name to the real native object `call`/`sk` will
    // run against. Returns nullptr and fills `message` when it cannot.
    skiExecutable* ResolveReceiver(const std::string& name, std::string& message) const;
    // Turns one console token into an skRValue by shape: a decimal integer
    // becomes an int (which is what almost every real native wants),
    // true/false become bools, everything else stays a string. Deliberately
    // simple and documented rather than clever -- a native that wants a
    // string "12" can be reached with `sk` instead.
    static skRValue ArgumentFromText(const std::string& text);
    // Every .s file under `subdirectory` (relative to the script root),
    // matching `filter`, as "subdirectory/name.s".
    std::vector<std::string> ScriptFiles(const std::string& subdirectory,
                                          const std::string& filter) const;
    // entities.txt's fourth column is the entity's script path, so a script
    // name resolves back to the typeId that owns it -- which is what lets
    // `spawn arat.s` and `give ratchest.s` go through the engine's own
    // CreateEntity/CreateItem factories instead of a parallel construction
    // path. 0 when nothing names this script.
    int TypeIdForScript(const std::string& what) const;
    // Resolves a spawn/give argument that is either a script path or a
    // decimal type id into a full path. Empty if it cannot.
    std::string ResolveScriptPath(const std::string& what,
                                   const char* const* searchDirectories) const;
    // Issues one `Level.CreateEntity(typeId, x, y, z)`. Returns false if it
    // could not be made at all.
    bool CreateEntityAt(int typeId, float x, float y);

    DebugGameRefs& m_Refs;
    // Outstanding `spawn` requests, one issued per tick -- see HostTick().
    struct PendingSpawn {
        int typeId = 0;
        float x = 0.0f;
        float y = 0.0f;
    };
    std::vector<PendingSpawn> m_PendingSpawns;
};
// ---- LiveDebugHost implementation ---------------------------------------

namespace {

std::string DebugNumber(double value, int decimals = 0) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.*f", decimals, value);
    return buffer;
}

std::string DebugLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool DebugContains(const std::string& haystack, const std::string& needle) {
    return needle.empty() || DebugLower(haystack).find(DebugLower(needle)) != std::string::npos;
}

// The 21 zone names, taken from the .zmp files actually present rather than
// from a hardcoded list -- so `zones` never drifts from what is installed.
std::vector<std::string> DebugZoneNames(const std::string& scriptRoot) {
    std::vector<std::string> names;
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(scriptRoot, error)) {
        if (error) break;
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".zmp") continue;
        names.push_back(entry.path().stem().string());
    }
    std::sort(names.begin(), names.end());
    return names;
}

}  // namespace

std::vector<std::string> LiveDebugHost::ScriptFiles(const std::string& subdirectory,
                                                     const std::string& filter) const {
    std::vector<std::string> names;
    const std::string directory =
        subdirectory.empty() ? m_Refs.scriptRoot : m_Refs.scriptRoot + "/" + subdirectory;
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(directory, error)) {
        if (error) break;
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".s") continue;
        const std::string name = entry.path().filename().string();
        if (!DebugContains(name, filter)) continue;
        names.push_back(subdirectory.empty() ? name : subdirectory + "/" + name);
    }
    std::sort(names.begin(), names.end());
    return names;
}

std::string LiveDebugHost::ResolveScriptPath(const std::string& what,
                                              const char* const* searchDirectories) const {
    std::string relative = what;
    std::replace(relative.begin(), relative.end(), '\\', '/');
    if (relative.size() < 2 || relative.substr(relative.size() - 2) != ".s") relative += ".s";
    // An explicit path wins; otherwise try each conventional directory, and
    // the script root itself last (183 of the game's .s files live there).
    if (relative.find('/') != std::string::npos) {
        const std::string full = m_Refs.scriptRoot + "/" + relative;
        return std::filesystem::exists(full) ? full : std::string();
    }
    for (const char* const* directory = searchDirectories; *directory; ++directory) {
        const std::string full =
            m_Refs.scriptRoot + "/" + (**directory ? std::string(*directory) + "/" : "") + relative;
        if (std::filesystem::exists(full)) return full;
    }
    return std::string();
}

int LiveDebugHost::TypeIdForScript(const std::string& what) const {
    if (!m_Refs.entityTypes) return 0;
    // Normalise both sides: entities.txt uses backslashes and mixed case
    // ("monsters\Azra_Rat.s"), a console argument will not.
    auto normalise = [](std::string text) {
        std::replace(text.begin(), text.end(), '\\', '/');
        if (text.size() < 2 || text.substr(text.size() - 2) != ".s") text += ".s";
        return DebugLower(text);
    };
    const std::string wanted = normalise(what);
    // An exact path match wins; a bare filename matches on the leaf, so
    // `spawn azra_rat.s` finds `monsters\Azra_Rat.s`.
    const bool bare = wanted.find('/') == std::string::npos;
    for (const auto& entry : m_Refs.entityTypes->all()) {
        if (entry.second.name.empty() || entry.second.name[0] == '!') continue;
        const std::string candidate = normalise(entry.second.name);
        if (candidate == wanted) return entry.first;
        if (bare) {
            const size_t slash = candidate.rfind('/');
            if (slash != std::string::npos && candidate.substr(slash + 1) == wanted) {
                return entry.first;
            }
        }
    }
    return 0;
}

std::vector<std::string> LiveDebugHost::Catalog(const std::string& kind,
                                                 const std::string& filter) const {
    if (kind == "zones") {
        std::vector<std::string> names;
        for (const std::string& name : DebugZoneNames(m_Refs.scriptRoot)) {
            if (DebugContains(name, filter)) names.push_back(name);
        }
        return names;
    }
    if (kind == "entities") {
        std::vector<std::string> rows;
        if (!m_Refs.entityTypes) return rows;
        for (const auto& entry : m_Refs.entityTypes->all()) {
            const std::string row = std::to_string(entry.first) + ":" + entry.second.name;
            if (DebugContains(row, filter)) rows.push_back(row);
        }
        return rows;
    }
    if (kind == "races") {
        std::vector<std::string> rows;
        for (int race = 0; race < sk_bindings::kRaceCount; ++race) {
            const int stringId = sk_bindings::RaceNameStringId(race);
            const std::string name =
                m_Refs.strings && stringId >= 0 ? m_Refs.strings->Get(stringId) : std::string();
            const std::string row = std::to_string(race) + ":" + name;
            if (DebugContains(row, filter)) rows.push_back(row);
        }
        return rows;
    }
    if (kind == "classes") {
        std::vector<std::string> rows;
        for (int classId = 0; classId < sk_bindings::kClassCount; ++classId) {
            const int stringId = sk_bindings::CharacterClassNameStringId(classId);
            const std::string name =
                m_Refs.strings && stringId >= 0 ? m_Refs.strings->Get(stringId) : std::string();
            const std::string row = std::to_string(classId) + ":" + name;
            if (DebugContains(row, filter)) rows.push_back(row);
        }
        return rows;
    }
    if (kind == "monsters" || kind == "scripts") return ScriptFiles("monsters", filter);
    if (kind == "weapons") return ScriptFiles("weapons", filter);
    if (kind == "armor") return ScriptFiles("armor", filter);
    if (kind == "spells") return ScriptFiles("spells", filter);
    if (kind == "items") {
        std::vector<std::string> all;
        for (const char* directory : {"items", "weapons", "armor", "spells"}) {
            for (std::string& name : ScriptFiles(directory, filter)) {
                all.push_back(std::move(name));
            }
        }
        std::sort(all.begin(), all.end());
        return all;
    }
    return {};
}

void LiveDebugHost::Entities(std::vector<sk_debug::EntityRow>& out) const {
    out.clear();
    if (!m_Refs.camera) return;
    const float px = m_Refs.camera->x;
    const float py = m_Refs.camera->y;
    auto distance = [px, py](float x, float y) {
        const float dx = x - px, dy = y - py;
        return std::sqrt(dx * dx + dy * dy);
    };

    int index = 0;
    if (m_Refs.monsters) {
        for (const MonsterInstance& m : *m_Refs.monsters) {
            sk_debug::EntityRow row;
            row.index = index++;
            row.kind = "monster";
            row.name = m.script ? m.script->name() : std::string("<no script>");
            row.x = m.x;
            row.y = m.y;
            row.z = m.z;
            row.distance = distance(m.x, m.y);
            row.typeId = m.typeId;
            row.modelIndex = m.modelArchiveIndex;
            if (m.script) {
                row.health = m.script->currentHealth();
                row.maxHealth = m.script->maxHealth();
                // Not outOfWorld(): a hidden creature is still alive, and
                // printing a reversible state as DEAD would read as a bug.
                // `state` below is where hidden shows up.
                row.alive = m.script->alive() && !m.script->destroyed();
                // M92: `destroyed` and `hidden` reach the same place by
                // different routes -- see MonsterExecutable::outOfWorld().
                // The row keeps them apart because that is the difference
                // a person reading it wants.
                row.state = m.script->destroyed()     ? "destroyed"
                            : m.script->entityHidden() ? "hidden"
                            : m.aiState == MonsterInstance::AiState::Chasing ? "chasing"
                            : m.aiState == MonsterInstance::AiState::Attacking ? "attacking"
                                                                                : "idle";
                if (!m.script->aggressive()) row.state += "/npc";
                if (m.script->usable()) row.state += "/usable";
            }
            out.push_back(std::move(row));
        }
    }
    if (m_Refs.doors) {
        for (const DoorInstance& d : *m_Refs.doors) {
            sk_debug::EntityRow row;
            row.index = index++;
            row.kind = "door";
            row.name = d.name.empty() ? std::string("<unnamed>") : d.name;
            row.x = d.x;
            row.y = d.y;
            row.z = d.z;
            row.distance = distance(d.x, d.y);
            row.typeId = d.typeId;
            row.modelIndex = d.modelArchiveIndex;
            // M67: passability is the field that actually matters for the
            // "an open door is still a wall" class of bug, so it is what a
            // door reports as its state.
            if (d.script) {
                row.state = d.script->entityRemoved() ? "removed"   // M101
                            : d.script->entityHidden() ? "hidden"
                            : d.script->passable()     ? "passable"
                                                       : "solid";
            }
            out.push_back(std::move(row));
        }
    }
    if (m_Refs.pickups) {
        for (const PickupInstance& p : *m_Refs.pickups) {
            sk_debug::EntityRow row;
            row.index = index++;
            row.kind = p.isContainer ? "container" : "pickup";
            row.name = p.script ? p.script->name() : std::string("<no script>");
            row.x = p.x;
            row.y = p.y;
            row.z = p.z;
            row.distance = distance(p.x, p.y);
            row.modelIndex = p.modelArchiveIndex;
            out.push_back(std::move(row));
        }
    }
    if (m_Refs.traps) {
        for (const TrapInstance& t : *m_Refs.traps) {
            sk_debug::EntityRow row;
            row.index = index++;
            row.kind = "trap";
            row.name = t.name.empty() ? std::string("<unnamed>") : t.name;
            row.x = t.x;
            row.y = t.y;
            row.distance = distance(t.x, t.y);
            row.typeId = t.typeId;
            out.push_back(std::move(row));
        }
    }
    std::sort(out.begin(), out.end(),
              [](const sk_debug::EntityRow& a, const sk_debug::EntityRow& b) {
                  return a.distance < b.distance;
              });
}

void LiveDebugHost::Inspect(const std::string& page,
                             std::vector<sk_debug::StatGroup>& out) const {
    const sk_bindings::PlayerExecutable* player = m_Refs.stack ? &m_Refs.stack->player() : nullptr;
    sk::Zone* z = zone();
    const sk::Camera* camera = m_Refs.camera;

    // A page name may carry an argument after a colon ("tile:12,34",
    // "entity:3", "inventory:sword") -- one convention rather than a
    // parameter on the interface.
    const size_t colon = page.find(':');
    const std::string name = colon == std::string::npos ? page : page.substr(0, colon);
    const std::string argument = colon == std::string::npos ? std::string() : page.substr(colon + 1);

    if (name == "mini") {
        sk_debug::StatGroup group;
        if (camera && z) {
            group.Add("@", DebugNumber(camera->x) + "," + DebugNumber(camera->y));
            group.Add("tile", std::to_string(static_cast<int>(camera->x / sk::kTileScale)) + "," +
                                   std::to_string(static_cast<int>(camera->y / sk::kTileScale)));
        } else {
            group.Add("(menus)", "");
        }
        if (m_Refs.monsters) group.Add("mon", std::to_string(m_Refs.monsters->size()));
        out.push_back(std::move(group));
        return;
    }

    if (name == "world") {
        sk_debug::StatGroup group;
        group.title = "world";
        if (!camera || !z) {
            group.Add("state", "not in a zone");
            out.push_back(std::move(group));
            return;
        }
        const int tileX = static_cast<int>(std::floor(camera->x / sk::kTileScale));
        const int tileY = static_cast<int>(std::floor(camera->y / sk::kTileScale));
        group.Add("zone", m_Refs.stack ? m_Refs.stack->currentLevelName() : std::string("?"));
        group.Add("position", DebugNumber(camera->x, 1) + ", " + DebugNumber(camera->y, 1) + ", " +
                                   DebugNumber(camera->z, 1));
        group.Add("tile", std::to_string(tileX) + ", " + std::to_string(tileY) + "  of " +
                               std::to_string(z->width()) + "x" + std::to_string(z->height()));
        // The engine's raw heading, not just the radian yaw: every .ent
        // record, every SetPosition and every tile stamp is in raw units,
        // so a bug report is far easier to act on with the raw number in it.
        group.Add("yaw", DebugNumber(camera->yaw * 180.0f / 3.14159265f, 1) + " deg   raw " +
                              std::to_string(CameraHeadingUnits(camera->yaw)));
        group.Add("pitch", DebugNumber(camera->pitch * 180.0f / 3.14159265f, 1) + " deg");
        if (z->InBounds(tileX, tileY)) {
            const sk::ZmpCell& cell = z->CellAt(tileX, tileY);
            group.Add("cell flags", "0x" + std::to_string(cell.flags));
            group.Add("cell block", std::string(cell.IsBlocked() ? "BLOCKED" : "open") + "  (0x" +
                                         std::to_string(cell.blockFlags) + ")");
            group.Add("cell wall", cell.IsWall() ? "wall" : "-");
            group.Add("cell light", std::to_string(cell.lightLevel));
            group.Add("floor", DebugNumber(z->FloorHeightAt(camera->x, camera->y), 1));
            group.Add("ceiling", DebugNumber(z->CeilingHeightAt(camera->x, camera->y), 1));
        }
        group.Add("map open", m_Refs.mapOpen && *m_Refs.mapOpen ? "yes" : "no");
        out.push_back(std::move(group));

        if (m_Refs.regionsOccupied && !m_Refs.regionsOccupied->empty()) {
            sk_debug::StatGroup regions;
            regions.title = "regions occupied";
            for (size_t regionIndex : *m_Refs.regionsOccupied) {
                if (regionIndex >= z->regions().size()) continue;
                regions.Add(std::to_string(regionIndex), z->regions()[regionIndex].name);
            }
            out.push_back(std::move(regions));
        }

        if (m_Refs.showTileGrid) {
            // A 9x9 ASCII window of the block bit around the player. Reading
            // the grid the collision test actually reads is the fastest way
            // to tell "the door is still stamped" from "the geometry is
            // wrong" -- the exact question M67 turned on.
            sk_debug::StatGroup grid;
            grid.title = "tile block grid (# blocked, W wall, . open, @ you)";
            for (int dy = -4; dy <= 4; ++dy) {
                std::string row;
                for (int dx = -4; dx <= 4; ++dx) {
                    const int tx = tileX + dx, ty = tileY + dy;
                    if (!z->InBounds(tx, ty)) {
                        row += ' ';
                        continue;
                    }
                    if (dx == 0 && dy == 0) {
                        row += '@';
                        continue;
                    }
                    const sk::ZmpCell& cell = z->CellAt(tx, ty);
                    row += cell.IsWall() ? 'W' : (cell.IsBlocked() ? '#' : '.');
                }
                grid.Add("y" + std::to_string(tileY + dy), row);
            }
            out.push_back(std::move(grid));
        }
        return;
    }

    if (name == "player") {
        if (!player) return;
        sk_debug::StatGroup vitals;
        vitals.title = "vitals";
        vitals.Add("health", std::to_string(player->health()) + " / " +
                                  std::to_string(player->maxHealth()));
        vitals.Add("magicka", std::to_string(player->magicka()) + " / " +
                                   std::to_string(player->maxMagicka()));
        vitals.Add("fatigue", std::to_string(player->fatigue()) + " / " +
                                   std::to_string(player->maxFatigue()));
        vitals.Add("level", std::to_string(player->level()));
        vitals.Add("experience", std::to_string(player->experience()) + "  (next in " +
                                      std::to_string(player->experienceToNextLevel()) + ")");
        vitals.Add("level-up points", std::to_string(player->levelUpPoints()));
        vitals.Add("gold", std::to_string(player->gold()));
        out.push_back(std::move(vitals));

        sk_debug::StatGroup attributes;
        attributes.title = "attributes";
        attributes.Add("strength", std::to_string(player->strength()));
        attributes.Add("intelligence", std::to_string(player->intelligence()));
        attributes.Add("willpower", std::to_string(player->willpower()));
        attributes.Add("agility", std::to_string(player->agility()));
        attributes.Add("speed", std::to_string(player->speed()));
        attributes.Add("endurance", std::to_string(player->endurance()));
        attributes.Add("personality", std::to_string(player->personality()));
        attributes.Add("luck", std::to_string(player->luck()));
        out.push_back(std::move(attributes));

        sk_debug::StatGroup derived;
        derived.title = "derived / identity";
        const int raceStringId = sk_bindings::RaceNameStringId(player->race());
        const int classStringId = sk_bindings::CharacterClassNameStringId(player->characterClass());
        derived.Add("race", std::to_string(player->race()) + "  " +
                                 (m_Refs.strings && raceStringId >= 0
                                       ? m_Refs.strings->Get(raceStringId)
                                       : std::string()));
        derived.Add("class", std::to_string(player->characterClass()) + "  " +
                                  (m_Refs.strings && classStringId >= 0
                                        ? m_Refs.strings->Get(classStringId)
                                        : std::string()));
        derived.Add("sex", player->sex() ? "male" : "female");
        derived.Add("attack", std::to_string(player->attack()));
        derived.Add("defense", std::to_string(player->defense()));
        derived.Add("armor rating", std::to_string(player->armorRating()));
        derived.Add("spell to-hit", std::to_string(player->spellToHit()));
        derived.Add("spell resist", std::to_string(player->spellResistance()));
        derived.Add("special ability", std::to_string(player->specialAbility()));
        derived.Add("race ability", std::to_string(player->raceAbility()));
        std::string status;
        if (player->paralyzed()) status += "paralyzed ";
        if (player->blinded()) status += "blinded ";
        if (player->poisoned()) status += "poisoned ";
        if (player->burning()) status += "burning ";
        derived.Add("status", status.empty() ? "-" : status);
        derived.Add("left hand",
                     player->leftItem() ? player->leftItem()->name() : std::string("-"));
        derived.Add("right hand",
                     player->rightItem() ? player->rightItem()->name() : std::string("-"));
        out.push_back(std::move(derived));
        return;
    }

    if (name == "inventory") {
        if (!player) return;
        sk_debug::StatGroup group;
        group.title = "inventory (" + std::to_string(player->inventoryCount()) + " items)";
        for (const auto& item : player->inventory()) {
            if (!item) continue;
            if (!DebugContains(item->name(), argument)) continue;
            std::string value = "type " + std::to_string(item->itemType());
            if (item->quantity() > 1) value += "  x" + std::to_string(item->quantity());
            if (item->equipped()) value += "  EQUIPPED";
            group.Add(item->name(), value);
        }
        if (group.rows.empty()) group.Add("(empty)", "");
        out.push_back(std::move(group));
        return;
    }

    if (name == "quests") {
        if (!player) return;
        sk_debug::StatGroup group;
        group.title = "quests";
        // The three sets are private; the natives are not. Scanning a bounded
        // id range through the real accessors keeps this honest about what a
        // script can actually see, and 0..255 covers every id the corpus uses.
        for (int id = 0; id < 256; ++id) {
            const bool assigned = player->questAssigned(id);
            const bool solved = player->questSolved(id);
            const bool completed = player->questCompleted(id);
            if (!assigned && !solved && !completed) continue;
            std::string state;
            if (assigned) state += "assigned ";
            if (solved) state += "solved ";
            if (completed) state += "completed";
            // M88: the real title, from the engine's own per-quest string
            // table (quest_table.h). Also says outright when an id has no
            // text, since that is exactly the id that can be assigned and
            // still never show up in the quest log.
            const sk_bindings::QuestText text = sk_bindings::QuestTextFor(id);
            std::string label = std::to_string(id);
            if (text.valid() && m_Refs.strings) {
                label += "  " + m_Refs.strings->Get(text.titleId);
            } else {
                label += "  (no quest text -- never shown in the log)";
            }
            if (assigned && !completed && text.valid()) state += "  [IN LOG]";
            group.Add(label, state);
        }
        if (group.rows.empty()) group.Add("(none touched)", "");
        out.push_back(std::move(group));
        return;
    }

    if (name == "menu") {
        // M88: whatever screen is currently on top of the menu stack,
        // row by row -- the only way to see what a real .s screen
        // actually built without reading pixels. Added for the quest log
        // (`call player OpenMenu questlog` then `inspect menu` is the
        // headless equivalent of walking to the character manager and
        // choosing Quest Log), but it is not quest-specific.
        sk_debug::StatGroup group;
        sk_bindings::MenuExecutable* menu = m_Refs.stack ? m_Refs.stack->currentMenu() : nullptr;
        if (!menu) {
            group.title = "menu";
            group.Add("state", "no menu is open");
            out.push_back(std::move(group));
            return;
        }
        group.title = "menu rows";
        // M90: which screen, and which row the confirm key would fire.
        // `scriptName` is what the engine keeps at `menu+0xc` and builds
        // this screen's back handler out of (`<ScriptName>Back`, see
        // MenuExecutable::GoBack()), so it is worth seeing next to the
        // rows; and a prompt whose safe answer is row 1 is only safe if
        // the selection is actually on it.
        group.Add("screen", menu->scriptName().empty() ? std::string("?") : menu->scriptName());
        group.Add("selected", std::to_string(menu->selectedItem()) + "  (1-based row)");
        int index = 0;
        for (const auto& row : menu->rows()) {
            const std::string prefix = std::to_string(index++);
            std::string kind;
            switch (row.kind) {
                case sk_bindings::MenuExecutable::RowKind::MenuItem: kind = "item"; break;
                case sk_bindings::MenuExecutable::RowKind::StaticItem: kind = "static"; break;
                case sk_bindings::MenuExecutable::RowKind::ComboBox: kind = "combo"; break;
                case sk_bindings::MenuExecutable::RowKind::TextArea: kind = "textarea"; break;
                case sk_bindings::MenuExecutable::RowKind::FloatingSprite: kind = "sprite"; break;
                case sk_bindings::MenuExecutable::RowKind::TextEntry: kind = "textentry"; break;
                case sk_bindings::MenuExecutable::RowKind::ItemButton: kind = "itembutton"; break;
                case sk_bindings::MenuExecutable::RowKind::Table: kind = "table"; break;
                case sk_bindings::MenuExecutable::RowKind::Slider: kind = "slider"; break;
            }
            if (row.kind == sk_bindings::MenuExecutable::RowKind::Table) {
                auto* table = static_cast<sk_bindings::TableExecutable*>(row.widget.get());
                group.Add(prefix + " table",
                           std::to_string(table->rowCount()) + " rows, wrap=" +
                               (table->lineWrap() ? "on" : "off") + ", focusable=" +
                               (row.selectable ? "yes" : "no"));
                for (int r = 0; r < table->rowCount(); ++r) {
                    const sk_bindings::TableCell* cell = table->PeekCell(r, 0);
                    std::string text = table->CellText(r, 0);
                    if (cell && (cell->displayFlags & sk_bindings::kCellFlagCentered) != 0) {
                        text = "[centred] " + text;
                    }
                    if (text.empty()) text = "--";
                    group.Add(prefix + "." + std::to_string(r), text);
                }
                continue;
            }
            std::string text = row.literalText;
            if (text.empty() && row.textId >= 0 && m_Refs.strings) {
                text = m_Refs.strings->Get(row.textId);
            }
            group.Add(prefix + " " + kind, text.empty() ? std::string("--") : text);
        }
        out.push_back(std::move(group));
        return;
    }

    if (name == "zone") {
        sk_debug::StatGroup group;
        group.title = "zone";
        if (!z) {
            group.Add("state", "no zone loaded");
            out.push_back(std::move(group));
            return;
        }
        group.Add("name", m_Refs.stack ? m_Refs.stack->currentLevelName() : std::string("?"));
        group.Add("grid", std::to_string(z->width()) + " x " + std::to_string(z->height()));
        group.Add("placements", std::to_string(z->entities().size()));
        group.Add("regions", std::to_string(z->regions().size()));
        group.Add("tile changes", std::to_string(z->tileChanges().size()) +
                                       "  (journalled since load)");
        group.Add("player start", std::to_string(z->playerStartX) + ", " +
                                       std::to_string(z->playerStartY) + ", " +
                                       std::to_string(z->playerStartZ));
        if (m_Refs.modelCollision) {
            group.Add("solid models", std::to_string(m_Refs.modelCollision->solidCount()));
        }
        group.Add("zone script", *m_Refs.zoneScript ? "loaded" : "none");
        out.push_back(std::move(group));

        // How much of the block bit is authored versus stamped is the exact
        // census M67 left open, so it is worth having in front of you.
        int blocked = 0, walls = 0, blockedFloor = 0;
        for (int ty = 0; ty < z->height(); ++ty) {
            for (int tx = 0; tx < z->width(); ++tx) {
                const sk::ZmpCell& cell = z->CellAt(tx, ty);
                if (cell.IsWall()) ++walls;
                if (cell.IsBlocked()) {
                    ++blocked;
                    if (!cell.IsWall()) ++blockedFloor;
                }
            }
        }
        sk_debug::StatGroup census;
        census.title = "tile census";
        census.Add("cells", std::to_string(z->width() * z->height()));
        census.Add("walls", std::to_string(walls));
        census.Add("blocked", std::to_string(blocked));
        census.Add("blocked, not wall", std::to_string(blockedFloor));
        out.push_back(std::move(census));
        return;
    }

    if (name == "entities") {
        std::vector<sk_debug::EntityRow> rows;
        Entities(rows);
        sk_debug::StatGroup counts;
        counts.title = "live entities";
        counts.Add("monsters", m_Refs.monsters ? std::to_string(m_Refs.monsters->size()) : "-");
        counts.Add("doors", m_Refs.doors ? std::to_string(m_Refs.doors->size()) : "-");
        counts.Add("pickups", m_Refs.pickups ? std::to_string(m_Refs.pickups->size()) : "-");
        counts.Add("traps", m_Refs.traps ? std::to_string(m_Refs.traps->size()) : "-");
        counts.Add("static props", m_Refs.props ? std::to_string(m_Refs.props->size()) : "-");
        counts.Add("spell projectiles",
                    m_Refs.projectiles ? std::to_string(m_Refs.projectiles->size()) : "-");
        counts.Add("arrows", m_Refs.arrows ? std::to_string(m_Refs.arrows->size()) : "-");
        out.push_back(std::move(counts));

        sk_debug::StatGroup nearest;
        nearest.title = "nearest";
        for (size_t i = 0; i < rows.size() && i < 14; ++i) {
            std::string value = rows[i].kind + "  d=" + DebugNumber(rows[i].distance);
            if (rows[i].health >= 0) {
                value += "  hp " + std::to_string(rows[i].health) + "/" +
                          std::to_string(rows[i].maxHealth);
            }
            if (!rows[i].state.empty()) value += "  " + rows[i].state;
            nearest.Add("[" + std::to_string(rows[i].index) + "] " + rows[i].name, value);
        }
        if (nearest.rows.empty()) nearest.Add("(none)", "");
        out.push_back(std::move(nearest));
        return;
    }

    if (name == "entity") {
        std::vector<sk_debug::EntityRow> rows;
        Entities(rows);
        int wanted = -1;
        try {
            wanted = std::stoi(argument);
        } catch (...) {
            return;
        }
        for (const sk_debug::EntityRow& row : rows) {
            if (row.index != wanted) continue;
            sk_debug::StatGroup group;
            group.title = row.kind + "  " + row.name;
            group.Add("index", std::to_string(row.index));
            group.Add("position", DebugNumber(row.x, 1) + ", " + DebugNumber(row.y, 1) + ", " +
                                       DebugNumber(row.z, 1));
            group.Add("tile", std::to_string(static_cast<int>(row.x / sk::kTileScale)) + ", " +
                                   std::to_string(static_cast<int>(row.y / sk::kTileScale)));
            group.Add("distance", DebugNumber(row.distance, 1));
            group.Add("state", row.state.empty() ? "-" : row.state);
            if (row.health >= 0) {
                group.Add("health",
                          std::to_string(row.health) + " / " + std::to_string(row.maxHealth));
            }
            group.Add("alive", row.alive ? "yes" : "no");
            if (row.typeId >= 0) group.Add("type id", std::to_string(row.typeId));
            if (row.modelIndex >= 0) {
                group.Add("model index", std::to_string(row.modelIndex));
                if (m_Refs.modelCollision) {
                    const sk::ModelCollision& collision = m_Refs.modelCollision->At(row.modelIndex);
                    group.Add("collision", std::to_string(collision.solid) + " " +
                                                std::to_string(collision.halfExtentX) + " " +
                                                std::to_string(collision.halfExtentY) +
                                                (collision.tileStamped() ? "  TILE-STAMPED" : ""));
                }
            }
            out.push_back(std::move(group));
            return;
        }
        return;
    }

    if (name == "tile") {
        if (!z || !camera) return;
        int tileX = static_cast<int>(std::floor(camera->x / sk::kTileScale));
        int tileY = static_cast<int>(std::floor(camera->y / sk::kTileScale));
        if (!argument.empty()) {
            const size_t comma = argument.find(',');
            if (comma != std::string::npos) {
                try {
                    tileX = std::stoi(argument.substr(0, comma));
                    tileY = std::stoi(argument.substr(comma + 1));
                } catch (...) {
                }
            }
        }
        sk_debug::StatGroup group;
        group.title = "tile " + std::to_string(tileX) + "," + std::to_string(tileY);
        if (!z->InBounds(tileX, tileY)) {
            group.Add("(out of bounds)", std::to_string(z->width()) + "x" +
                                              std::to_string(z->height()));
            out.push_back(std::move(group));
            return;
        }
        const sk::ZmpCell& cell = z->CellAt(tileX, tileY);
        const float worldX = (static_cast<float>(tileX) + 0.5f) * sk::kTileScale;
        const float worldY = (static_cast<float>(tileY) + 0.5f) * sk::kTileScale;
        group.Add("centre", DebugNumber(worldX) + ", " + DebugNumber(worldY));
        group.Add("flags", std::to_string(cell.flags) +
                                (cell.IsWall() ? "  WALL" : "") +
                                (cell.IsLightSource() ? "  LIGHT" : ""));
        group.Add("blockFlags", std::to_string(cell.blockFlags) +
                                     (cell.IsBlocked() ? "  BLOCKED" : "  open"));
        group.Add("light", std::to_string(cell.lightLevel));
        group.Add("zcp index", std::to_string(cell.zcpIndex));
        group.Add("floor", DebugNumber(z->FloorHeightAt(worldX, worldY), 1));
        group.Add("ceiling", DebugNumber(z->CeilingHeightAt(worldX, worldY), 1));
        group.Add("blocks a 48-radius circle",
                   z->CircleHitsWall(worldX, worldY, 48.0f) ? "yes" : "no");
        out.push_back(std::move(group));
        return;
    }

    if (name == "render") {
        sk_debug::StatGroup group;
        group.title = "render";
        group.Add("fps", DebugNumber(sk_debug::Metrics::Get().framesPerSecond(), 2));
        group.Add("frame ms", DebugNumber(sk_debug::Metrics::Get().frameMs(), 2));
        for (const char* sample : {"tick", "render3d", "present", "debug.overlay"}) {
            group.Add(std::string(sample) + " ms",
                       DebugNumber(sk_debug::Metrics::Get().averageMs(sample), 3));
        }
        group.Add("draw entities",
                   std::to_string(sk_debug::Metrics::Get().lastFrame("render.entities")));
        group.Add("visible tiles",
                   std::to_string(sk_debug::Metrics::Get().lastFrame("render.visible_tiles")));
        out.push_back(std::move(group));
        return;
    }

    if (name == "script") {
        const sk_debug::Metrics& metrics = sk_debug::Metrics::Get();
        sk_debug::StatGroup group;
        group.title = "script activity";
        group.Add("statements (frame)", std::to_string(metrics.lastFrame("script.statements")));
        group.Add("statements (total)", std::to_string(metrics.total("script.statements")));
        group.Add("method calls (frame)",
                   std::to_string(metrics.lastFrame("script.method_calls")));
        group.Add("method calls (total)", std::to_string(metrics.total("script.method_calls")));
        group.Add("soft-fails", std::to_string(metrics.total("script.softfails")));
        group.Add("exceptions", std::to_string(metrics.total("script.exceptions")));
        out.push_back(std::move(group));

        sk_debug::StatGroup misses;
        misses.title = "top soft-failed natives";
        std::vector<std::pair<std::string, long long>> ranked;
        for (const std::string& counter : metrics.counterNames()) {
            if (counter.compare(0, 9, "softfail.") != 0) continue;
            ranked.emplace_back(counter.substr(9), metrics.total(counter));
        }
        std::sort(ranked.begin(), ranked.end(),
                  [](const auto& a, const auto& b) { return a.second > b.second; });
        for (size_t i = 0; i < ranked.size() && i < 16; ++i) {
            misses.Add(ranked[i].first, std::to_string(ranked[i].second));
        }
        if (misses.rows.empty()) misses.Add("(none yet)", "");
        out.push_back(std::move(misses));
        return;
    }
}

// ---- actions --------------------------------------------------------------

bool LiveDebugHost::CreateEntityAt(int typeId, float x, float y) {
    if (!InGame()) return false;
    skRValueArray args;
    args.append(skRValue(typeId));
    args.append(skRValue(static_cast<int>(x)));
    args.append(skRValue(static_cast<int>(y)));
    args.append(skRValue(static_cast<int>(zone()->FloorHeightAt(x, y))));
    skRValue result;
    skExecutableContext context(m_Refs.interpreter);
    return m_Refs.stack->level().method(skString("CreateEntity"), args, result, context);
}

void LiveDebugHost::HostTick() {
    // One queued spawn per tick -- see Spawn()'s comment for why it cannot
    // be a loop. Dropped entirely if the zone changed under it, since the
    // queued coordinates belonged to the old one.
    if (m_PendingSpawns.empty()) return;
    if (!InGame()) {
        m_PendingSpawns.clear();
        return;
    }
    const PendingSpawn pending = m_PendingSpawns.front();
    m_PendingSpawns.erase(m_PendingSpawns.begin());
    CreateEntityAt(pending.typeId, pending.x, pending.y);
}

bool LiveDebugHost::Teleport(float x, float y, bool tileCoords, std::string& message) {
    if (!InGame()) {
        message = "not in a zone";
        return false;
    }
    if (tileCoords) {
        x = (std::floor(x) + 0.5f) * sk::kTileScale;
        y = (std::floor(y) + 0.5f) * sk::kTileScale;
    }
    sk::Zone* z = zone();
    const int tileX = static_cast<int>(std::floor(x / sk::kTileScale));
    const int tileY = static_cast<int>(std::floor(y / sk::kTileScale));
    if (!z->InBounds(tileX, tileY)) {
        message = "tile " + std::to_string(tileX) + "," + std::to_string(tileY) +
                   " is outside this zone (" + std::to_string(z->width()) + "x" +
                   std::to_string(z->height()) + ")";
        return false;
    }
    // Through the real SetPosition native, not by writing the camera: the
    // engine's own two-argument form drops the actor onto the floor of the
    // destination tile, and the tick's existing drain applies it. Poking the
    // camera directly would skip the ground snap and leave you standing in
    // the air or inside the floor.
    skRValueArray args;
    args.append(skRValue(static_cast<int>(x)));
    args.append(skRValue(static_cast<int>(y)));
    skRValue result;
    skExecutableContext context(m_Refs.interpreter);
    m_Refs.stack->player().method(skString("SetPosition"), args, result, context);
    message = "teleporting to (" + DebugNumber(x) + ", " + DebugNumber(y) + ")  tile " +
               std::to_string(tileX) + "," + std::to_string(tileY) +
               (z->CellAt(tileX, tileY).IsBlocked() ? "  [that tile is BLOCKED]" : "");
    sk_debug::Log("world", "teleport " + message);
    return true;
}

bool LiveDebugHost::TeleportToEntity(int index, std::string& message) {
    std::vector<sk_debug::EntityRow> rows;
    Entities(rows);
    for (const sk_debug::EntityRow& row : rows) {
        if (row.index != index) continue;
        // Land one tile short of the target rather than inside it, so
        // teleporting to a creature does not put the camera in its model.
        const float dx = m_Refs.camera->x - row.x;
        const float dy = m_Refs.camera->y - row.y;
        const float length = std::sqrt(dx * dx + dy * dy);
        const float offset = length > 1.0f ? sk::kTileScale / length : 0.0f;
        return Teleport(row.x + dx * offset, row.y + dy * offset, false, message);
    }
    message = "no entity with index " + std::to_string(index) + " (see `ents`)";
    return false;
}

bool LiveDebugHost::LoadZone(const std::string& zoneName, std::string& message) {
    if (!m_Refs.stack) {
        message = "no menu stack";
        return false;
    }
    const std::vector<std::string> known = DebugZoneNames(m_Refs.scriptRoot);
    // Case-insensitive, because the .zmp stems and the names scripts pass to
    // LoadLevel() do not agree on case (`GhstPass` vs `ghstpass`).
    std::string resolved;
    for (const std::string& candidate : known) {
        if (DebugLower(candidate) == DebugLower(zoneName)) {
            resolved = candidate;
            break;
        }
    }
    if (resolved.empty()) {
        message = "no zone `" + zoneName + "` -- `zones` lists them";
        return false;
    }
    if (m_Refs.inGame && *m_Refs.inGame) {
        // The same request a script's Level.LoadLevel() makes, so the whole
        // real transition runs: loading screen, zone teardown, entity
        // rebuild, zone-script Init, camera start.
        m_Refs.stack->RequestZoneChange(resolved);
    } else {
        m_Refs.stack->RequestGameStart(resolved);
    }
    message = "travelling to " + resolved;
    sk_debug::Log("world", message);
    return true;
}

bool LiveDebugHost::Face(float degrees, std::string& message) {
    if (!InGame()) {
        message = "not in a zone";
        return false;
    }
    m_Refs.camera->yaw = degrees * 3.14159265f / 180.0f;
    message = "facing " + DebugNumber(degrees, 1) + " deg  (raw heading " +
               std::to_string(CameraHeadingUnits(m_Refs.camera->yaw)) + ")";
    return true;
}

bool LiveDebugHost::Spawn(const std::string& what, int count, float distance,
                           std::string& message) {
    if (!InGame()) {
        message = "not in a zone";
        return false;
    }
    count = std::clamp(count, 1, 64);

    // A bare number is a typeId, which goes through the real
    // Level.CreateEntity(typeId, x, y, z) -- the same native crypt1.s uses
    // to put Umbra Keth in the room, drained by the tick's existing pump.
    const bool numeric = !what.empty() &&
                          what.find_first_not_of("0123456789") == std::string::npos;
    if (numeric) {
        const int typeId = std::atoi(what.c_str());
        if (!m_Refs.entityTypes->Lookup(typeId)) {
            message = "no entities.txt row for type id " + what + " (see `catalog entities`)";
            return false;
        }
        // Queued, one per tick, rather than issued in a loop here.
        // `LevelExecutable` parks CreateEntity's result in a **single**
        // pending slot that the tick drains (its own comment notes nothing
        // in the corpus calls CreateEntity twice without an AddObject
        // between), so a loop would silently lose all but the last. That is
        // a faithful reproduction of the engine, so the fix belongs on this
        // side: spread the calls across ticks. See HostTick().
        for (int i = 0; i < count; ++i) {
            const float angle = m_Refs.camera->yaw +
                                 (static_cast<float>(i) - static_cast<float>(count - 1) * 0.5f) *
                                     0.4f;
            PendingSpawn pending;
            pending.typeId = typeId;
            pending.x = m_Refs.camera->x + std::cos(angle) * distance;
            pending.y = m_Refs.camera->y + std::sin(angle) * distance;
            m_PendingSpawns.push_back(pending);
        }
        message = "Level.CreateEntity(" + what + ") queued x" + std::to_string(count) +
                   " -- one per tick (the engine's pending slot holds one), so " +
                   std::to_string(count) + " tick(s) to land";
        sk_debug::Log("world", message);
        return true;
    }

    // Otherwise it is a script name. entities.txt's fourth column *is* the
    // script path ("monsters\Azra_Rat.s"), so a named script almost always
    // resolves to a real typeId -- and going back through CreateEntity
    // above means a spawned creature is built by exactly the code that
    // builds a placed one, model index and all.
    if (const int typeId = TypeIdForScript(what); typeId > 0) {
        return Spawn(std::to_string(typeId), count, distance, message);
    }

    // No entities.txt row names this script. Construct it directly, the way
    // the zone-load placement loop does -- with no type descriptor there is
    // no model index, so the creature is real but invisible. Said plainly in
    // the result rather than left to be discovered.
    static const char* const kSearch[] = {"monsters", "", nullptr};
    const std::string path = ResolveScriptPath(what, kSearch);
    if (path.empty()) {
        message = "no script `" + what + "` under monsters/ or the script root";
        return false;
    }
    int spawned = 0;
    for (int i = 0; i < count; ++i) {
        skExecutableContext loadContext(m_Refs.interpreter);
        try {
            auto monster = std::make_unique<sk_bindings::MonsterExecutable>(
                skString(path.c_str()), loadContext, m_Refs.strings, m_Refs.stack->player(),
                *m_Refs.stack);
            const float angle = m_Refs.camera->yaw +
                                 (static_cast<float>(i) - static_cast<float>(count - 1) * 0.5f) *
                                     0.4f;
            MonsterInstance instance;
            instance.x = m_Refs.camera->x + std::cos(angle) * distance;
            instance.y = m_Refs.camera->y + std::sin(angle) * distance;
            instance.z = zone()->FloorHeightAt(instance.x, instance.y);
            monster->SetWorldPosition(static_cast<int>(instance.x), static_cast<int>(instance.y),
                                       static_cast<int>(instance.z));
            skExecutableContext callContext(m_Refs.interpreter);
            sk_bindings::RunEntityInit(*monster, m_Refs.stack->scriptRoot(), callContext);
            instance.placementYaw = m_Refs.camera->yaw + 3.14159265f;  // looking back at us
            instance.facingYaw = instance.placementYaw;
            instance.modelArchiveIndex = -1;  // no entities.txt row -> no model
            instance.typeId = 0;
            instance.script = std::move(monster);
            m_Refs.monsters->push_back(std::move(instance));
            ++spawned;
        } catch (skParseException& e) {
            message = std::string("parse error in ") + path + ": " + e.toString().ptr();
            return false;
        } catch (skTreeNodeReaderException& e) {
            message = std::string("parse error in ") + path + ": " + e.toString().ptr();
            return false;
        } catch (skRuntimeException& e) {
            message = std::string("runtime error in ") + path + ": " + e.toString().ptr();
            return false;
        }
    }
    message = "spawned " + std::to_string(spawned) + " x " +
               path.substr(m_Refs.scriptRoot.size() + 1) + " at " + DebugNumber(distance) +
               " units -- NOT in entities.txt, so it has no model and will be invisible";
    sk_debug::Log("world", message);
    return true;
}

bool LiveDebugHost::Give(const std::string& what, int count, std::string& message) {
    if (!m_Refs.stack) {
        message = "no menu stack";
        return false;
    }
    count = std::clamp(count, 1, 99);
    const bool numeric = !what.empty() &&
                          what.find_first_not_of("0123456789") == std::string::npos;
    int given = 0;
    for (int i = 0; i < count; ++i) {
        std::unique_ptr<sk_bindings::ItemExecutable> item;
        // A bare number is a typeId; a script name is resolved back to its
        // typeId through entities.txt first, so both go through the engine's
        // own item factory -- the same call a loot bag's Init() makes.
        const int typeId = numeric ? std::atoi(what.c_str()) : TypeIdForScript(what);
        if (typeId > 0) {
            item = m_Refs.stack->level().CreateItem(typeId, false);
        } else {
            static const char* const kSearch[] = {"items", "weapons", "armor", "spells", "",
                                                   nullptr};
            const std::string path = ResolveScriptPath(what, kSearch);
            if (path.empty()) {
                message = "no item script `" + what +
                           "` under items/ weapons/ armor/ spells/ or the script root";
                return false;
            }
            skExecutableContext loadContext(m_Refs.interpreter);
            try {
                item = std::make_unique<sk_bindings::ItemExecutable>(skString(path.c_str()),
                                                                     loadContext, *m_Refs.stack);
                skExecutableContext callContext(m_Refs.interpreter);
                sk_bindings::RunEntityInit(*item, m_Refs.stack->scriptRoot(), callContext);
            } catch (skParseException& e) {
                message = std::string("parse error in ") + path + ": " + e.toString().ptr();
                return false;
            } catch (skTreeNodeReaderException& e) {
                message = std::string("parse error in ") + path + ": " + e.toString().ptr();
                return false;
            } catch (skRuntimeException& e) {
                message = std::string("runtime error in ") + path + ": " + e.toString().ptr();
                return false;
            }
        }
        if (!item) break;
        if (given == 0) message = "gave " + item->name();
        m_Refs.stack->player().AddItem(std::move(item));
        ++given;
    }
    if (given == 0) {
        message = "could not create `" + what + "`";
        return false;
    }
    message += "  x" + std::to_string(given);
    sk_debug::Log("world", message);
    return true;
}

bool LiveDebugHost::Equip(const std::string& what, int hand, std::string& message) {
    if (!m_Refs.stack) {
        message = "no menu stack";
        return false;
    }
    sk_bindings::PlayerExecutable& player = m_Refs.stack->player();
    sk_bindings::ItemExecutable* found = nullptr;
    for (const auto& item : player.inventory()) {
        if (item && DebugContains(item->name(), what)) {
            found = item.get();
            break;
        }
    }
    if (!found) {
        message = "no carried item matching `" + what + "` (see `inv`)";
        return false;
    }
    // UpdateEquipStatus is the real equip path (M35's equip screen calls
    // exactly this), so hand assignment, two-handed rules and the viewmodel
    // swap all behave as they do in game. The requested hand is advisory:
    // the engine picks, and this reports where it actually went.
    const int result = player.UpdateEquipStatus(found, hand >= 0);
    (void)hand;
    message = std::string(hand >= 0 ? "equipped " : "unequipped ") + found->name() +
               "  (UpdateEquipStatus -> " + std::to_string(result) + ", left=" +
               (player.leftItem() ? player.leftItem()->name() : "-") + ", right=" +
               (player.rightItem() ? player.rightItem()->name() : "-") + ")";
    return true;
}

bool LiveDebugHost::KillAll(const std::string& filter, bool asPlayer, std::string& message) {
    if (!InGame() || !m_Refs.monsters) {
        message = "not in a zone";
        return false;
    }
    // M97: `killall` is unsourced, like a script's DoDamage -- the console
    // is not an attacker, so it pays no experience. `slay` names the player
    // as the source of every blow, which is what a real kill does.
    sk_bindings::SpellActor* attacker =
        asPlayer && m_Refs.stack ? &m_Refs.stack->player() : nullptr;
    int killed = 0;
    for (MonsterInstance& m : *m_Refs.monsters) {
        if (!m.script || !m.script->alive() || m.script->outOfWorld()) continue;
        if (!DebugContains(m.script->name(), filter)) continue;
        // Through ApplyDamage, not by zeroing health: death has to run the
        // real path (loot drop, kill counters, zone triggers), because those
        // are exactly the things worth debugging.
        m.script->ApplyDamage(m.script->currentHealth() + 1, attacker);
        ++killed;
    }
    message = std::string(asPlayer ? "slew " : "killed ") + std::to_string(killed) +
               (filter.empty() ? std::string(" creature(s)")
                                : " creature(s) matching `" + filter + "`");
    sk_debug::Log("world", message);
    return true;
}

// ---- the native bridge ------------------------------------------------------

std::vector<std::pair<std::string, std::string>> LiveDebugHost::Receivers() const {
    std::vector<std::pair<std::string, std::string>> out = {
        {"player", "the Player object -- stats, quests, inventory, gold, race, class"},
        {"level", "the Level global -- CreateEntity, GetEntity, LoadLevel, ambient"},
        {"menu", "the menu currently on top of the stack, if any"},
        {"zone", "this zone's root script object (azra.s and friends)"},
        {"ent:<n>", "one live entity by its `ents` index -- its own script object"},
    };
    return out;
}

skiExecutable* LiveDebugHost::ResolveReceiver(const std::string& receiver,
                                               std::string& message) const {
    if (!m_Refs.stack) {
        message = "no menu stack";
        return nullptr;
    }
    if (receiver == "player") return &m_Refs.stack->player();
    if (receiver == "level") return &m_Refs.stack->level();
    if (receiver == "menu") {
        sk_bindings::MenuExecutable* menu = m_Refs.stack->currentMenu();
        if (!menu) message = "no menu is open";
        return menu;
    }
    if (receiver == "zone") {
        if (!m_Refs.zoneScript || !*m_Refs.zoneScript) {
            message = "no zone script loaded";
            return nullptr;
        }
        return m_Refs.zoneScript->get();
    }
    if (receiver.compare(0, 4, "ent:") == 0) {
        int wanted = -1;
        try {
            wanted = std::stoi(receiver.substr(4));
        } catch (...) {
            message = "bad entity index in `" + receiver + "`";
            return nullptr;
        }
        // Rebuild the same ordering `ents` prints, so the index a user reads
        // off that list is the one that resolves here.
        std::vector<sk_debug::EntityRow> rows;
        Entities(rows);
        for (const sk_debug::EntityRow& row : rows) {
            if (row.index != wanted) continue;
            if (m_Refs.monsters) {
                for (MonsterInstance& m : *m_Refs.monsters) {
                    if (m.script && m.x == row.x && m.y == row.y && m.script->name() == row.name) {
                        return m.script.get();
                    }
                }
            }
            if (m_Refs.doors) {
                for (DoorInstance& d : *m_Refs.doors) {
                    if (d.script && d.name == row.name) return d.script.get();
                }
            }
            if (m_Refs.pickups) {
                for (PickupInstance& p : *m_Refs.pickups) {
                    // Position too, like the monsters: every loot bag and
                    // chest is named "Examine", so the name alone always
                    // resolved to the first one in the list.
                    if (p.script && p.x == row.x && p.y == row.y && p.script->name() == row.name) {
                        return p.script.get();
                    }
                }
            }
        }
        message = "entity " + std::to_string(wanted) + " has no script object";
        return nullptr;
    }
    message = "unknown receiver `" + receiver + "` -- `recv` lists them";
    return nullptr;
}

skRValue LiveDebugHost::ArgumentFromText(const std::string& text) {
    if (text == "true") return skRValue(true);
    if (text == "false") return skRValue(false);
    if (!text.empty()) {
        const size_t start = text[0] == '-' ? 1 : 0;
        if (start < text.size() &&
            text.find_first_not_of("0123456789", start) == std::string::npos) {
            return skRValue(std::atoi(text.c_str()));
        }
    }
    return skRValue(skString(text.c_str()));
}

bool LiveDebugHost::CallNative(const std::string& receiver, const std::string& method,
                                const std::vector<std::string>& args, std::string& message) {
    skiExecutable* target = ResolveReceiver(receiver, message);
    if (!target) {
        if (message.empty()) message = "no such receiver";
        return false;
    }
    skRValueArray arguments;
    for (const std::string& arg : args) arguments.append(ArgumentFromText(arg));
    skRValue result;
    skExecutableContext context(m_Refs.interpreter);
    sk_debug::Count("debug.native_calls");
    try {
        const bool handled = target->method(skString(method.c_str()), arguments, result, context);
        if (!handled) {
            message = receiver + "." + method + " -- the binding refused the call (wrong "
                       "argument count is the usual cause)";
            return false;
        }
    } catch (skParseException& e) {
        message = std::string("parse error: ") + e.toString().ptr();
        return false;
    } catch (skTreeNodeReaderException& e) {
        message = std::string("parse error: ") + e.toString().ptr();
        return false;
    } catch (skRuntimeException& e) {
        message = std::string("runtime error: ") + e.toString().ptr();
        return false;
    }
    // A void native returns an empty skRValue; saying so beats printing a
    // blank line, which reads as "the command did nothing".
    const std::string returned(result.str().ptr());
    message = returned.empty() ? receiver + "." + method + " ran (no return value)" : returned;
    sk_debug::Log("debug", receiver + "." + method + " -> " +
                                (returned.empty() ? std::string("(void)") : returned));
    return true;
}

bool LiveDebugHost::ScriptEval(const std::string& receiver, const std::string& code,
                                std::string& message) {
    skiExecutable* target = ResolveReceiver(receiver, message);
    if (!target) {
        if (message.empty()) message = "no such receiver";
        return false;
    }
    // A bare expression is wrapped into a return, so `sk player GetGold()`
    // and `sk player SetGold(10);` both do what they look like.
    std::string body = code;
    if (body.find(';') == std::string::npos) body = "return " + body + ";";
    skRValueArray arguments;
    skRValue result;
    skExecutableContext context(m_Refs.interpreter);
    try {
        m_Refs.interpreter->executeString(skString("debug-console"), target,
                                           skString(body.c_str()), arguments, result, nullptr,
                                           context);
    } catch (skParseException& e) {
        message = std::string("parse error: ") + e.toString().ptr();
        return false;
    } catch (skTreeNodeReaderException& e) {
        message = std::string("parse error: ") + e.toString().ptr();
        return false;
    } catch (skRuntimeException& e) {
        message = std::string("runtime error: ") + e.toString().ptr();
        return false;
    }
    const std::string returned(result.str().ptr());
    message = returned.empty() ? std::string("ran (no return value)") : returned;
    sk_debug::Log("debug", "sk " + receiver + " " + code + " -> " +
                                (returned.empty() ? std::string("(void)") : returned));
    return true;
}
#endif  // SK_DEBUG_SUITE

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
// M120: `layout` places it in a wider view -- see sk::ViewLayout. The
// default is the device's own frame, where every x below is 0.
void RenderWeaponViewmodel(sk::Backbuffer& backbuffer, const sk_bindings::WeaponViewmodel& vm,
                            sk::SpriteArchive& sprites, const sk::ViewLayout& layout = {}) {
    sk_bindings::ViewmodelDraw draw = sk_bindings::ResolveViewmodelDraw(vm);
    if (!draw.visible || draw.spriteSlot < 0) return;
#if SK_DEBUG_SUITE
    // SK_DEBUG_SUITE (M79): one counter per global.spr slot the viewmodel
    // actually draws, so `diff` says which weapon (or which cast frame) was
    // on screen -- 88..103 for a club, 152..157 for a spell. The only way
    // to see the viewmodel from a console, and the check that found the
    // spell drawing nothing at all.
    sk_debug::Count("viewmodel.slot." + std::to_string(draw.spriteSlot));
#endif
    const sk::Sprite* sprite = sprites.GetSprite(draw.spriteSlot);
    // A strip slot the per-zone sprite manifest didn't pull in falls back
    // to the weapon's own base frame rather than blinking out.
    if (!sprite) sprite = sprites.GetSprite(vm.item->weaponSprite());
    if (!sprite) return;
    // M120: a weapon to the right edge of a wide view, anything else in its
    // middle. Decided from the item's own resting frame, not the frame on
    // screen, so a swing never jumps between the two mid-animation: a
    // weapon's resting canvas has its arm running off the right edge (every
    // shipped one does), a spell's two hands sit either side of the centre.
    int x0 = layout.centreX();
    if (layout.wide()) {
        const sk::Sprite* rest = sprites.GetSprite(vm.item->weaponSprite());
        if (rest && rest->width == sk::Backbuffer::kWidth) {
            bool touchesRight = false;
            for (int y = 0; y < rest->height && !touchesRight; ++y) {
                touchesRight = rest->opaque[static_cast<size_t>(y) * rest->width + rest->width - 1];
            }
            if (touchesRight) x0 = layout.rightX();
        }
    }
    backbuffer.Blit(x0 + draw.x, draw.y, *sprite);
}

// M25: a plain outline rectangle -- ButtonExecutable's real ShowBorder
// (true) (charactermanager.s's Stats/Equip/Quest buttons, docs/
// PORT_ROADMAP.md's M25 entry) drawn using the row's own real w/h.
// M108: the two helpers behind SK_DUMP_MENUS (see main()).

void WriteBackbufferPpm(const sk::Backbuffer& bb, const std::string& path) {
    std::ofstream f(path, std::ios::binary);
    if (!f) {
        std::printf("  could not open %s for writing\n", path.c_str());
        return;
    }
    f << "P6\n" << sk::Backbuffer::kWidth << " " << sk::Backbuffer::kHeight << "\n255\n";
    for (int y = 0; y < sk::Backbuffer::kHeight; ++y) {
        const uint16_t* row = bb.Row(y);
        for (int x = 0; x < sk::Backbuffer::kWidth; ++x) {
            const uint16_t c = row[x];
            f.put(static_cast<char>(((c >> 11) & 0x1f) << 3));
            f.put(static_cast<char>(((c >> 5) & 0x3f) << 2));
            f.put(static_cast<char>((c & 0x1f) << 3));
        }
    }
}

// Every .s file under the script root (and its `menus/` subdirectory)
// that actually builds a menu, named the way MenuStack::OpenMenu wants
// them -- root-relative, no extension, forward slashes.
//
// The test is the presence of a menu-building call rather than a
// hand-written list: the corpus has 514 files that touch a menu and no
// naming convention that separates them from item/monster scripts, and a
// list would go stale the first time one was renamed.
std::vector<std::string> EnumerateMenuScripts(const std::string& scriptRoot) {
    namespace fs = std::filesystem;
    std::vector<std::string> out;
    const char* kMarkers[] = {"ClearMenu(", "AddMenuItem(", "AddStaticItem(", "MenuBackground("};
    for (const char* sub : {"", "menus"}) {
        const fs::path dir = sub[0] ? fs::path(scriptRoot) / sub : fs::path(scriptRoot);
        std::error_code ec;
        if (!fs::is_directory(dir, ec)) continue;
        for (const fs::directory_entry& e : fs::directory_iterator(dir, ec)) {
            if (!e.is_regular_file()) continue;
            const fs::path& p = e.path();
            if (p.extension() != ".s") continue;
            std::ifstream in(p, std::ios::binary);
            const std::string body((std::istreambuf_iterator<char>(in)),
                                    std::istreambuf_iterator<char>());
            bool isMenu = false;
            for (const char* marker : kMarkers) {
                if (body.find(marker) != std::string::npos) {
                    isMenu = true;
                    break;
                }
            }
            if (!isMenu) continue;
            std::string name = p.stem().string();
            if (sub[0]) name = std::string(sub) + "/" + name;
            out.push_back(name);
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

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

// `FUN_1006bee8` / `FUN_1006be58` -- the engine's horizontal and
// vertical pixel runs. Both take a length, not an end coordinate, and
// both write straight into the frame buffer with no clipping (the
// engine's callers stay in bounds by construction); SetPixel clips for
// us, which only matters for a slider marker parked at its maximum,
// where the engine itself would run three pixels past the track.
void DrawHRun(sk::Backbuffer& backbuffer, int x, int y, int len, uint16_t color) {
    for (int i = 0; i < len; ++i) backbuffer.SetPixel(x + i, y, color);
}

void DrawVRun(sk::Backbuffer& backbuffer, int x, int y, int len, uint16_t color) {
    for (int i = 0; i < len; ++i) backbuffer.SetPixel(x, y + i, color);
}

// M118: `FUN_1007f49c`'s left-aligned arm (param_7 == 0): the string once
// at (x+1, y+1) in 0x0b96, then at (x, y) in its own colour. Every widget
// that draws its own label at its own x/y goes through this.
void DrawWidgetLabel(sk::Backbuffer& backbuffer, int x, int y, const std::string& text,
                     uint16_t color) {
    if (text.empty()) return;  // `if (param_3 != 0)`: no text, no draw
    constexpr uint16_t kShadow = MenuColor444(sk_bindings::kTextShadowColor444);
    sk::BitmapFont::DrawString(backbuffer, x + 1, y + 1, text, kShadow);
    sk::BitmapFont::DrawString(backbuffer, x, y, text, color);
}

// M118: the same function's centred arm (param_7 == 1) -- `FUN_1008f97c`,
// which has no x at all: centred across the full 176px, with a shadow one
// pixel *below* in (0xb1, 0x9c, 0x65).
void DrawCenteredWidgetLabel(sk::Backbuffer& backbuffer, int y, const std::string& text,
                             uint16_t color) {
    if (text.empty()) return;
    const int x = (sk::Backbuffer::kWidth - sk::BitmapFont::TextWidth(text)) / 2;
    sk::BitmapFont::DrawString(backbuffer, x, y + 1, text, kCenteredShadowColor);
    sk::BitmapFont::DrawString(backbuffer, x, y, text, color);
}

// M118: `DrawEntitySpriteWithOutline` (`FUN_1007ec80`). The sprite at
// (x, y), and -- when the widget has focus -- a two-pixel ring around it
// in the palette's highlight colour, as eight runs:
//
//     HRun(x,   y-1,   w)   HRun(x,   y,     w)    top, two rows
//     HRun(x,   y+h,   w)   HRun(x,   y+h+1, w)    bottom, two rows
//     VRun(x,   y,     h)   VRun(x-1, y,     h)    left, two columns
//     VRun(x+w, y,     h)   VRun(x+w+1, y,   h)    right, two columns
//
// The inner row and column overwrite the sprite's own edge pixels, and the
// four corners are never filled, so the ring has notched corners. A
// negative id, or one the cache does not hold, draws nothing at all.
void DrawSpriteWithOutline(sk::Backbuffer& backbuffer, sk::SpriteArchive& sprites, int slot,
                           int x, int y, bool focused) {
    if (slot < 0) return;
    const sk::Sprite* art = sprites.GetSprite(slot);
    if (!art) return;
    backbuffer.Blit(x, y, *art);
    if (!focused) return;
    const int w = art->width, h = art->height;
    DrawHRun(backbuffer, x, y - 1, w, kUiHighlightColor);
    DrawHRun(backbuffer, x, y, w, kUiHighlightColor);
    DrawHRun(backbuffer, x, y + h, w, kUiHighlightColor);
    DrawHRun(backbuffer, x, y + h + 1, w, kUiHighlightColor);
    DrawVRun(backbuffer, x, y, h, kUiHighlightColor);
    DrawVRun(backbuffer, x - 1, y, h, kUiHighlightColor);
    DrawVRun(backbuffer, x + w, y, h, kUiHighlightColor);
    DrawVRun(backbuffer, x + w + 1, y, h, kUiHighlightColor);
}

// M118: the rectangle `FUN_1007f050` and `FUN_10032aa4` draw -- see
// kWidgetBoxDX/DY in menu_executable.h. Four runs of a length from one
// corner, so the right and bottom edges land one pixel outside w x h.
void DrawWidgetBox(sk::Backbuffer& backbuffer, int x, int y, int w, int h) {
    const int left = x + sk_bindings::kWidgetBoxDX;
    const int top = y + sk_bindings::kWidgetBoxDY;
    DrawHRun(backbuffer, left, top, w, kUiBoxColor);
    DrawHRun(backbuffer, left, top + h, w, kUiBoxColor);
    DrawVRun(backbuffer, left, top, h, kUiBoxColor);
    DrawVRun(backbuffer, left + w, top, h, kUiBoxColor);
}

// M118: the text-area widget's own draw (`FUN_1008f458`): word-wrapped at
// `widthChars`, each line through the left-aligned arm, 0xc apart. The
// wrap itself is DrawWrappedText's, which M109 already matched against
// every shipped class/race description.
void DrawTextAreaLines(sk::Backbuffer& backbuffer, int x, int y, const std::string& text,
                       int widthChars, uint16_t color) {
    if (widthChars <= 0) widthChars = 27;
    std::vector<std::string> lines;
    std::string current;
    std::istringstream words(text);
    std::string word;
    while (words >> word) {
        std::string candidate = current.empty() ? word : current + " " + word;
        if (static_cast<int>(candidate.size()) > widthChars && !current.empty()) {
            lines.push_back(current);
            current = word;
        } else {
            current = candidate;
        }
    }
    if (!current.empty()) lines.push_back(current);
    for (const std::string& line : lines) {
        DrawWidgetLabel(backbuffer, x, y, line, color);
        y += sk_bindings::kMenuRowPitch;
    }
}

// M124: the engine's popup draw, `FUN_100876f0`, replacing a fixed navy
// box this port drew at (10,60)-(166,150) for every popup in the game --
// the reported "blue submenus", and the reason a shop's long item names
// ran past the box: the text was wrapped at a fixed 22 characters with no
// regard for the popup's own width. In the engine's order:
//
//   * a one-pixel frame in the UI palette's highlight colour (+0xa):
//     top and bottom rows `w` long at y-5 and y+h-5, left and right
//     columns `h` long at x-5 and x-5+w;
//   * if `+0xb8` (on unless a script clears it -- none does), the inside
//     filled row by row in RGB444 **0xca5**, parchment, from y-4 to
//     y+h-6, starting one pixel in and `w - 1` wide;
//   * each item with text, wrapped at `(w >> 3) + 3` characters (never
//     under 8) by FUN_1008e634, every line drawn through FUN_1007f49c at
//     the popup's own x -- shadowed like every other label -- and `+0xa4`
//     (12) further down. The selected item takes the highlight colour; on
//     a four-argument popup item 0 takes the menu's static-item colour;
//     everything else the standard one.
//   * with `+0xb9` set, the height becomes the laid-out text plus 5.
//
// The engine sizes the frame from the *previous* frame's auto-adjusted
// height and fixes it after drawing the text; laying the text out first
// gives the same picture without the one-frame lag.
void RenderPopup(sk::Backbuffer& backbuffer, sk_bindings::PopupMenuExecutable& popup,
                  const sk::StringTable& strings) {
    constexpr uint16_t kFill = MenuColor444(0xca5);
    popup.NormalizeSelectionForDraw();
    const int x = popup.x();
    const int y = popup.y();
    const int w = popup.w();
    const int maxChars = (std::max)(8, (w >> 3) + 3);

    struct Line {
        std::string text;
        uint16_t color;
    };
    std::vector<Line> lines;
    for (size_t i = 0; i < popup.items().size(); ++i) {
        const auto& item = popup.items()[i];
        // `if (item+0x48 != 0)`: UpdatePopupItem(i, "") frees the text
        // (FUN_1007f9a8), and an item with none takes no line at all.
        if (item.blanked) continue;
        std::string text = !item.literalText.empty() ? item.literalText : strings.Get(item.textId);
        if (text.empty()) continue;
        uint16_t color = kUiStandardColor;
        if (popup.titleLine() && i == 0) color = kStaticItemColor;
        // FUN_1007f49c's `selected` argument is `index == +0xa0`, with no
        // selectable test: the engine lights whichever row it points at.
        if (popup.IsItemSelected(i)) color = kUiHighlightColor;
        for (const std::string& part : WrapWords(text, maxChars)) lines.push_back({part, color});
    }
    const int pitch = popup.linePitch();
    const int h = popup.autoAdjust() ? static_cast<int>(lines.size()) * pitch + 5 : popup.h();

    const int left = (std::max)(0, x - 5);
    const int top = (std::max)(0, y - 5);
    DrawHRun(backbuffer, left, top, w, kUiHighlightColor);
    DrawHRun(backbuffer, left, y + h - 5, w, kUiHighlightColor);
    DrawVRun(backbuffer, left, top, h, kUiHighlightColor);
    DrawVRun(backbuffer, left + w, y - 5, h, kUiHighlightColor);
    if (popup.background()) {
        for (int row = y; row + 1 < y + h; ++row) {
            DrawHRun(backbuffer, left + 1, (std::max)(0, row - 4), w - 1, kFill);
        }
    }
    int lineY = y;
    for (const Line& line : lines) {
        DrawWidgetLabel(backbuffer, x, lineY, line.text, line.color);
        lineY += pitch;
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

    // M64 -- CORRECTED, and the previous guess was half right.
    //
    // This used to read `menu.backgroundId() == 69 ? 50 : 8`, with a
    // comment saying the 50 was measured off a screenshot because "no
    // native y-offset call exists". One does: `SetStartCoord`, root
    // binding 0x2c, which writes menu+0x96 -- the exact field the real
    // menu draw (FUN_10076b64) starts its row cursor at. The menu
    // constructor seeds that field with 0x32, so **50 is every menu's
    // default**, not just the main menu's, and the logo art the old
    // comment reasoned from was a coincidence.
    //
    // Not screenshot-verified: this moves every non-main menu's list down
    // 42 pixels, which is what the engine does but is a bigger visible
    // change than any other line in this milestone. The one script that
    // overrides it is `levelup.s`, whose eleven rows need y=10.
    int y = menu.startCoord();
    // M80 -- the row pitch is the engine's, not a guess. `FUN_10076b64`
    // walks the widget list with one cursor and advances it by a literal
    // `0xc` after every text row (`uVar9 = uVar8 + 0xc`, both for a plain
    // static item and for a menu item). This used to be kGlyphHeight + 4,
    // i.e. 16, which is fine for a six-row menu and impossible for a
    // wrapped message: starthelp.s's last page is twelve rows, and at 16
    // it ran 34 pixels off the bottom of a 208px screen, taking its own
    // "I'm on my way." button with it. At 12 it ends at y=194. Every one
    // of the game's nine tutorial pages fits, the longest with 14 pixels
    // to spare -- which is the cross-check that 0xc, the 0x19 wrap width
    // and SetStartCoord's default 0x32 are all being read right.
    const int lineHeight = sk_bindings::kMenuRowPitch;

    // M88: every AddTitle, at its own real y when the script gave one.
    // A title with no y keeps the old behaviour exactly -- drawn at the
    // shared cursor, and advancing it -- so every screen that adds a
    // single untitled-position header renders unchanged.
    //
    // M108: a title is **absolutely positioned and does not consume a
    // row**. `AddTitle` (case 7 of `FUN_1003136c`) defaults its second
    // argument to **10** and stores it in `widget+0x7c`, and builds the
    // widget with kind `+0x58 = 10` -- which the menu draw's switch sends
    // to `default:`, the arm that calls the widget's own vtable slot and,
    // crucially, leaves the row cursor (`uVar9 = uVar8`) untouched. So a
    // title never pushes the list down. This port drew a title with no
    // explicit y at the shared cursor and then advanced it by 14, which
    // moved every row of every titled screen down and put the title where
    // the first row belongs. `MenuTitle::y` now defaults to 10 (see
    // menu_executable.h) so the `title.y >= 0` arm is the only one left.
    // And it is **centred**, with a shadow. The title widget's own draw
    // slot is `FUN_1003559c`, all of four arguments wide:
    //
    //     FUN_1007f49c(5, widget+0x7c, widget+0x48, isSelected,
    //                  widget+0x70, palette, 1, 0, 0);
    //                                        ^ param_7
    //
    // -- and `param_7 == 1` is the arm that discards x entirely and goes
    // to `FUN_1008f97c`, centred across the full 176px with a shadow one
    // pixel below. The literal `5` never reaches the screen. This port
    // drew titles left-aligned at x=4 with no shadow, so every titled
    // screen in the game had its heading in the wrong place.
    //
    // M118: and the colour is resolved. `*(ushort *)(engine->+0x28 + 8)`
    // is the ScreenModeController's standard word, 0x733 -- the same
    // brick red as an unselected menu item, not the blue this port drew
    // (see kUiStandardColor444 for where the pointer is assigned).
    for (const auto& title : menu.titles()) {
        if (title.textId < 0) continue;
        DrawCenteredWidgetLabel(backbuffer, title.y, strings.Get(title.textId), kUiStandardColor);
    }

    // M110: the softkey captions along the bottom -- `case 0xb` of
    // `FUN_10076b64`, absolutely positioned and outside the row flow:
    //
    //     if (widget+0x48 && widget+0x36)
    //         FUN_1007f49c(widget+0x78, widget+0x7c, text, 0, engine,
    //                      widget+0x36, 0, widget+0x94, widget+0x3c);
    //
    // param_7 = 0 is the left arm, which draws the 0x0b96 shadow at
    // (x+1, y+1) and then the text at (x, y); param_8 is the justify flag,
    // and it picks `FUN_1008f8a4` mode 2 -- `FUN_10022f88`, whose rect is
    // `TRect(0, y, x, ...)` right-aligned, so the text's *right edge*
    // lands on x. The colour guard is real: a zero colour draws nothing.
    //
    // M111: and in the *second* face. All 95 shipped sites call
    // `SetFontNum(1)` on these, which `FUN_10022b20` resolves to a
    // `TFontSpec("Swiss", 0xd5)` instead of the ROM legend font; no
    // N-Gage font store has a "Swiss", so the device fell through to
    // Symbian's nearest-font matching on style alone, and LatinPlain12 is
    // the only non-bold Latin text face there is. See bitmap_font.h.
    constexpr sk::BitmapFont::Face kCaptionFace = sk::BitmapFont::Face::Small;
    for (const auto& ft : menu.floatingTexts()) {
        if (ft.textId < 0 || ft.color444 == 0) continue;
        const std::string text = strings.Get(ft.textId);
        if (text.empty()) continue;
        const int x =
            ft.rightJustify ? ft.x - sk::BitmapFont::TextWidth(text, kCaptionFace) : ft.x;
        constexpr uint16_t kFloatShadow = MenuColor444(sk_bindings::kTextShadowColor444);
        sk::BitmapFont::DrawString(backbuffer, x + 1, ft.y + 1, text, kFloatShadow, kCaptionFace);
        sk::BitmapFont::DrawString(backbuffer, x, ft.y, text, MenuColor444(ft.color444),
                                    kCaptionFace);
    }

    for (size_t rowIndex = 0; rowIndex < menu.rows().size(); ++rowIndex) {
        const auto& row = menu.rows()[rowIndex];
        // M60: the shared widget class's SetVisible byte (widget+0x5d).
        // The store screen hides the category button of every category
        // its merchant does not stock and slides the survivors left, so a
        // hidden row must draw nothing at all rather than leaving a gap.
        if (!row.visible) continue;
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
                // Every widget below draws itself (the menu draw's
                // `default:` arm), so none of them uses the row colour the
                // list picked: they take the palette's standard word, and
                // its highlight word while focused.
                const uint16_t widgetColor = isSelected ? kUiHighlightColor : kUiStandardColor;
                auto* button = dynamic_cast<sk_bindings::ButtonExecutable*>(row.widget.get());
                if (row.isQuitButton) {
                    // M118: CORRECTED. M36 centred this across the bottom
                    // of the page. `AddQuitButton` is `FUN_1007fb78(menu,
                    // text, callback, 0x47, 0xc1, 1, 0)`: a floating label
                    // (kind 7, drawn by `FUN_1007f7c8`) at a fixed
                    // (71, 193), through the left-aligned arm.
                    DrawWidgetLabel(backbuffer, sk_bindings::kQuitButtonX,
                                    sk_bindings::kQuitButtonY, label, widgetColor);
                    break;
                }
                if (button && row.x >= 0) {
                    // M118: AddButton's widget is the engine's icon widget,
                    // and this is its draw, `FUN_1007f050`, whole:
                    //
                    //   if (!pageSelected) {                    // +0xb2
                    //     if (activeArt == -1) {                // +0x9c
                    //       if ((focused || ShowBorder) && w > 0 && h > 0)
                    //         box(x-5, y+HAdjust-10, w, h, palette+0xc);
                    //     } else
                    //       DrawEntitySpriteWithOutline(+0x98, focused);
                    //     label(x, y, left-aligned, palette+8 / +10);
                    //   } else if (activeArt != -1)
                    //     DrawEntitySpriteWithOutline(activeArt, focused);
                    //
                    // So a text button's label is *not* centred in its box
                    // -- it sits at the script's own (x, y), and the box is
                    // drawn around it from 5 left and 10 above -- and a
                    // plain text button gets a box while focused even
                    // without ShowBorder. A tab never swaps art on focus;
                    // it gets the highlight ring instead.
                    if (!button->pageSelected()) {
                        if (button->spriteActive() == -1) {
                            if ((isSelected || row.showBorder) && row.w > 0 && row.h > 0) {
                                DrawWidgetBox(backbuffer, row.x, row.y + button->hAdjust(), row.w,
                                              row.h);
                            }
                        } else {
                            DrawSpriteWithOutline(backbuffer, sprites, button->spriteCurrent(),
                                                  row.x, row.y, isSelected);
                        }
                        DrawWidgetLabel(backbuffer, row.x, row.y, label, widgetColor);
                    } else if (button->spriteActive() != -1) {
                        DrawSpriteWithOutline(backbuffer, sprites, button->spriteActive(), row.x,
                                              row.y, isSelected);
                    }
                    break;
                }
                if (row.x >= 0) {
                    // M118: AddFloatingText -- `FUN_1007f8bc`, kind 7, drawn
                    // by `FUN_1007f7c8`: its label at its own (x, y) through
                    // the left-aligned arm, in the palette's colours. (It
                    // was drawn in the *static* item colour with no shadow.)
                    DrawWidgetLabel(backbuffer, row.x, row.y, label, widgetColor);
                } else if (row.kind == RowKind::StaticItem && !row.centered) {
                    // M80: `FUN_10076b64` case 0 -> `FUN_1007f49c` with
                    // `param_7 == 0` -- the left-aligned arm, drawn at the
                    // literal x=9 the caller passes, with a shadow copy one
                    // pixel down and right first. That is every ordinary
                    // AddStaticItem message; the centred arm below is what
                    // a `-----` separator (or an explicit
                    // `AddStaticItem(id, false)`) takes instead. Wrapping
                    // already happened in the binding, so each row here is
                    // one line.
                    // M108: expanded the engine's way. `FUN_1008f8a4`
                    // splits an RGB444 word as `(c & 0xf00) >> 4`,
                    // `c & 0xf0`, `(c & 0xf) << 4` -- i.e. **nibble << 4**,
                    // so a full nibble is 0xf0 and never 0xff. This used
                    // `* 17` (the "replicate the nibble" expansion), which
                    // is the right idea for a generic 444 source and the
                    // wrong one here: it made every shadow, and with the
                    // M108 palette every text colour, up to 6% brighter
                    // than the hardware ever drew.
                    constexpr uint16_t kShadow = MenuColor444(sk_bindings::kTextShadowColor444);
                    sk::BitmapFont::DrawString(backbuffer, sk_bindings::kStaticItemX + 1, y + 1,
                                                label, kShadow);
                    sk::BitmapFont::DrawString(backbuffer, sk_bindings::kStaticItemX, y, label,
                                                color);
                    y += lineHeight;
                } else {
                    // Real menu item rows (mainmenu.s etc., no real x/y of
                    // their own -- AddMenuItem never takes one) are
                    // centered on screen, and selection is color-only (no
                    // left-margin arrow glyph).
                    //
                    // M108 -- now confirmed from the engine rather than a
                    // screenshot, and it holds. `AddMenuItem` (case 0x6d of
                    // `FUN_10078de4`) builds its widget with kind
                    // `+0x58 = 0` -- the *same* case a static item takes --
                    // and never writes `+0x94`, which the widget
                    // constructor `FUN_1007e458` has already zeroed. The
                    // draw passes `param_7 = (widget+0x94) ^ 1`, so a menu
                    // item gets 1: `FUN_1008f97c`, the arm with no x
                    // parameter at all, which lands in `FUN_10022d48` ->
                    // `TRect(0, y, 0xb0, y + fontHeight)` with Symbian's
                    // `DrawText` alignment 1 (ECenter). Centred across the
                    // full 176px screen -- identical to the
                    // `(kWidth - textW) / 2` below.
                    //
                    // What was missing is the **shadow**. `FUN_1008f97c` is
                    // called with its shadow flag set, so before the text
                    // it draws the same string one pixel lower in
                    // RGB(0xb1,0x9c,0x65). Every centred row in the game --
                    // every main-menu entry, every `AddStaticItem(id,false)`
                    // and every `---` separator -- has one, and this port
                    // drew none of them. The left-aligned arm above always
                    // had its (+1,+1) shadow, which is why only the centred
                    // rows looked flat.
                    int textX = (sk::Backbuffer::kWidth - textW) / 2;
                    sk::BitmapFont::DrawString(backbuffer, textX, y + 1, label,
                                                kCenteredShadowColor);
                    sk::BitmapFont::DrawString(backbuffer, textX, y, label, color);
                    y += lineHeight;
                }
                break;
            }
            case RowKind::ItemButton: {
                auto* item = static_cast<sk_bindings::ItemButtonExecutable*>(row.widget.get());
                if (item->visible()) {
                    // M118: the hand-item button's own draw, `FUN_10032aa4`,
                    // whole -- the last of charactermanager.s's rectangles:
                    //
                    //   sprite(+0x98) at (x + 0x2d, y + 0x19)
                    //   if (w > 0 && h > 0)
                    //     box(x-5, y-10, w, h, palette+0xc)   // always
                    //   child text area: this button's text at (x, y)
                    //
                    // The box does not depend on focus -- both hands are
                    // always framed -- and the icon sits in the box's
                    // lower right, not at its corner. The name is the
                    // child text-area widget (`+0xb4`, see
                    // item_button_executable.h): trimmed to 0x17 + "..."
                    // when SetTrimText is on, wrapped at SetTextWidth, and
                    // coloured by focus like every other widget.
                    //
                    // This used to draw the icon at the box's top-left and
                    // no box at all, which is why "Left Hand"/"Right Hand"
                    // floated unframed under the Stats/Equip buttons.
                    const int boxX = row.x >= 0 ? row.x : 12;
                    const int boxY = row.x >= 0 ? row.y : y;
                    const sk::Sprite* icon = item->icon() >= 0 ? sprites.GetSprite(item->icon())
                                                               : nullptr;
                    if (icon) {
                        backbuffer.Blit(boxX + sk_bindings::kItemButtonIconDX,
                                        boxY + sk_bindings::kItemButtonIconDY, *icon);
                    }
                    if (row.w > 0 && row.h > 0) DrawWidgetBox(backbuffer, boxX, boxY, row.w, row.h);
                    std::string label = RowText(item->textId(), item->itemText(), strings);
                    const sk_bindings::ItemButtonTextArea& area = item->textArea();
                    if (area.trimText() &&
                        static_cast<int>(label.size()) > sk_bindings::kTextAreaTrimChars) {
                        label = label.substr(0, sk_bindings::kTextAreaTrimChars) + "...";
                    }
                    DrawTextAreaLines(backbuffer, boxX, boxY, label, area.textWidth(),
                                      isSelected ? kUiHighlightColor : kUiStandardColor);
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
                // M88: a wrapped table (questlog.s is the only shipped
                // caller of SetLineWrap(true)) has rows of *varying*
                // height, so the fixed six-row window above is the wrong
                // budget for it -- a single quest is three rows and its
                // objective line alone wraps to three or four. Draw until
                // AddTable's own real box height is used up instead, and
                // start from the selection rather than centring on it, so
                // scrolling down reveals whole quests.
                const bool wrapped = table->lineWrap();
                const int tableBottom =
                    row.y >= 0 ? row.y + table->height() : sk::Backbuffer::kHeight;
                if (wrapped) {
                    start = (std::max)(0, selected);
                    end = table->rowCount();
                }
                // M124: every other table is laid out the way the engine's
                // table draw (`FUN_1008cca8`) does it, cell by cell:
                //
                //   * a row starts at the table's x; each cell is drawn at
                //     the running x, which then advances by the column's
                //     width plus SetCellSpacing's gap;
                //   * a cell whose text is at least the column's character
                //     limit is wrapped (FUN_1008e634), its lines 15 apart;
                //   * a `continuation` cell (`+0x34`) goes on a new line,
                //     15 below the lowest line so far, back at the table's
                //     x -- the store's "GP : 953 Qty: 3" under the name;
                //   * the row ends 15 below its lowest line;
                //   * nothing is drawn past the table's own height less
                //     SetInset's margin, and sprites 26/27 (`+0xa8`/`+0xac`)
                //     mark rows hidden above and below, centred on the
                //     table (FUN_1008e31c).
                //
                // This replaces a single line per row with the cells
                // joined by two spaces, which put a 140px name and a 150px
                // price side by side on a 176px screen.
                //
                // The engine scrolls by its own `+0xa0`; this keeps the
                // port's rule of scrolling only as far as the selection
                // needs.
                if (!wrapped && row.y >= 0) {
                    constexpr int kCellLine = 0xf;
                    const int left = tableX;
                    const int limitY = row.y + table->height();
                    // FUN_1008cca8 tests after drawing each line: once a
                    // line's y plus the inset is past the table's bottom it
                    // stops -- that line is already on screen -- and puts
                    // the down arrow out. `stopped` reports that.
                    auto layoutRow = [&](int r, int top, bool draw, bool& stopped) {
                        int x = left;
                        int lowest = top;
                        bool any = false;
                        for (int c = 0; c < table->columnCount(); ++c) {
                            const sk_bindings::TableCell* cell = table->PeekCell(
                                static_cast<size_t>(r), static_cast<size_t>(c));
                            const int width = table->columnWidth(c);
                            if (!cell || cell->text.empty()) {
                                x += width + table->cellSpacing();
                                continue;
                            }
                            any = true;
                            int cy = top;
                            if (cell->continuation) {
                                cy = lowest + kCellLine;
                                x = left;
                            }
                            using Compare = sk_bindings::TableExecutable::CellCompare;
                            const Compare compare = table->CompareCell(r, c);
                            if (draw && cell->comparison) {
                                // FUN_100a0cec: the arrow sprite at the
                                // cell's own x/y (25 plus = better,
                                // 24 minus = worse), or the equal mark
                                // as text.
                                if (compare == Compare::Better || compare == Compare::Worse) {
                                    if (const sk::Sprite* arrow = sprites.GetSprite(
                                            compare == Compare::Better
                                                ? sk_bindings::kStoreBetterArrowSprite
                                                : sk_bindings::kStoreWorseArrowSprite)) {
                                        backbuffer.Blit(x, cy, *arrow);
                                    }
                                } else if (compare == Compare::Equal) {
                                    DrawWidgetLabel(backbuffer, x, cy, "=", kUiStandardColor);
                                }
                            }
                            if (cell->comparison) {
                                if (cy + table->inset() > limitY) {
                                    stopped = true;
                                    return lowest;
                                }
                            } else {
                                uint16_t color = kUiStandardColor;
                                switch (table->CellTint(r, c)) {
                                    case sk_bindings::TableExecutable::NameTint::Equipped:
                                        color = kInventoryEquippedColor;
                                        break;
                                    case sk_bindings::TableExecutable::NameTint::ClassBarred:
                                        color = kInventoryClassBarredColor;
                                        break;
                                    default:
                                        break;
                                }
                                if (isSelected && r == selected) color = kUiHighlightColor;
                                const int limit = table->columnCharLimit(c);
                                // M124: a stat-line cell holds " " and is
                                // typeset from its item when drawn.
                                const std::string text =
                                    cell->statLine && cell->item
                                        ? sk_bindings::StatLineText(*cell->item, &strings,
                                                                    player.level())
                                        : cell->text;
                                const std::vector<std::string> parts =
                                    static_cast<int>(text.size()) < limit
                                        ? std::vector<std::string>{text}
                                        : WrapWords(text, limit);
                                for (size_t k = 0; k < parts.size(); ++k) {
                                    if (draw) DrawWidgetLabel(backbuffer, x, cy, parts[k], color);
                                    if (k + 1 < parts.size()) cy += kCellLine;
                                    if (cy + table->inset() > limitY) {
                                        stopped = true;
                                        return (std::max)(lowest, cy);
                                    }
                                }
                            }
                            lowest = (std::max)(lowest, cy);
                            x += width + table->cellSpacing();
                        }
                        return any ? lowest + kCellLine : lowest;
                    };
                    const int rows = table->rowCount();
                    // The whole pass from `first`, returning the last row
                    // it reached. After each complete row the engine also
                    // stops once the row's bottom is past the table's.
                    int yy = row.y;
                    bool truncated = false;
                    auto pass = [&](int first, bool draw) {
                        yy = row.y;
                        truncated = false;
                        int last = first - 1;
                        for (int r = first; r < rows; ++r) {
                            bool stopped = false;
                            const int bottom = layoutRow(r, yy, draw, stopped);
                            last = r;
                            if (stopped || bottom > limitY) {
                                truncated = true;
                                break;
                            }
                            yy = bottom;
                        }
                        return last;
                    };
                    // Scroll only as far as it takes to reach the selection.
                    int first = 0;
                    while (first < selected && pass(first, false) < selected) ++first;
                    pass(first, true);
                    auto drawArrow = [&](int slot, bool below) {
                        const sk::Sprite* arrow = sprites.GetSprite(slot);
                        if (!arrow) return;
                        const int ax = left + table->width() / 2 - arrow->width / 2;
                        const int ay = below ? row.y + table->height() + arrow->height / 2
                                             : row.y - arrow->height;
                        backbuffer.Blit(ax, ay, *arrow);
                    };
                    if (first > 0) drawArrow(0x1a, false);
                    if (truncated) drawArrow(0x1b, true);
                    if (rows == 0) {
                        sk::BitmapFont::DrawString(backbuffer, tableX, row.y, "(empty)",
                                                   kStaticTextColor);
                    }
                    y = yy;
                    break;
                }
                // AddTable's own real box width, in pixels -- the wrap is
                // measured in the font that actually draws it.
                const int wrapPixels =
                    table->width() > 0 ? table->width() : sk::Backbuffer::kWidth - tableX;
                for (int r = start; r < end; ++r) {
                    if (wrapped && y >= tableBottom) break;
                    std::string line = table->CellText(r, 0);
                    for (int c = 1; c < table->columnCount(); ++c) {
                        // M60: the store table's last two columns hold a
                        // single space and draw global.spr 24/25 instead
                        // -- "this is better/worse than what you have in
                        // that hand". No sprite is reachable from this
                        // text-cell path, so they render as the same
                        // three-way marker in ASCII.
                        using Compare = sk_bindings::TableExecutable::CellCompare;
                        const Compare compare = table->CompareCell(r, c);
                        if (compare != Compare::None) {
                            line += compare == Compare::Better  ? " +"
                                    : compare == Compare::Worse ? " -"
                                                                : " =";
                            continue;
                        }
                        std::string cell = table->CellText(r, c);
                        // " " is what the engine writes into a cell whose
                        // content is drawn rather than typeset -- nothing
                        // to add to the line.
                        if (!cell.empty() && cell != " ") line += "  " + cell;
                    }
                    // M118: the name cell's own tint (`FUN_100a0cec`) --
                    // an equipped item in 0x00a, a class-barred one in
                    // 0xc33 -- under the focus override every cell gets
                    // from `FUN_1007f49c`'s param_4.
                    uint16_t rowColor = kUiStandardColor;
                    switch (table->CellTint(r, 0)) {
                        case sk_bindings::TableExecutable::NameTint::Equipped:
                            rowColor = kInventoryEquippedColor;
                            break;
                        case sk_bindings::TableExecutable::NameTint::ClassBarred:
                            rowColor = kInventoryClassBarredColor;
                            break;
                        default:
                            break;
                    }
                    if (isSelected && r == selected) rowColor = kUiHighlightColor;
                    // M88: cell+0x2c bit 1 -- the engine drops the
                    // left-aligned shadowed draw for a centred one (see
                    // table_executable.h's TableCell::displayFlags). It is
                    // what makes a quest's title read as a heading over
                    // its objective line rather than as another row of it.
                    const sk_bindings::TableCell* cell0 = table->PeekCell(r, 0);
                    const bool centered =
                        cell0 && (cell0->displayFlags & sk_bindings::kCellFlagCentered) != 0;
                    if (wrapped && !centered) {
                        y += DrawWrappedTextPx(backbuffer, tableX, y, line, wrapPixels, rowColor) *
                             lineHeight;
                    } else if (centered) {
                        int cx = tableX + (table->width() - sk::BitmapFont::TextWidth(line)) / 2;
                        sk::BitmapFont::DrawString(backbuffer, (std::max)(tableX, cx), y, line,
                                                   rowColor);
                        y += lineHeight;
                    } else {
                        sk::BitmapFont::DrawString(backbuffer, tableX, y, line, rowColor);
                        y += lineHeight;
                    }
                }
                if (table->rowCount() == 0) {
                    sk::BitmapFont::DrawString(backbuffer, tableX, y, "(empty)", kStaticTextColor);
                    y += lineHeight;
                }
                break;
            }
            case RowKind::Slider: {
                // M112: `case 4` of `FUN_10076b64` -- the arm M109 wrongly
                // guessed was the combo box. It is the slider, and it is
                // the only widget kind that draws real geometry rather
                // than just text, which is why it looked like something
                // more exotic than it is.
                //
                // Until now this row was a character meter,
                // "< Label ####------ >", written when no slider art
                // could be found in global.spr. That search was looking
                // for the wrong thing: the engine draws no sprite, it
                // plots the track and the marker pixel by pixel.
                //
                //     if (widget+0x48)                       // the label
                //         FUN_1008f97c(gc, text, cursorY, colour, 1,1,0,0);
                //     left = 0x58 - (widget+0x64 >> 1);
                //     for (i = 0; i < 3; i++)                // the track
                //         FUN_1006bee8(.., left, cursorY+0x11+i,
                //                      widget+0x64, *(ushort*)(menu+0x90));
                //     x = left + widget+0x78 + widget+0x68 - 1;
                //     for (i = 0; i < 3; i++)                // the marker
                //         FUN_1006be58(.., x+i, cursorY+0xd, 0xb, 0x7f0);
                //     cursorY += 0x18;
                //
                // `+0x64` is AddMenuSlider's third argument and `+0x6c`
                // its fourth, both set by `FUN_1007e7f0`; `+0x68` is the
                // live value, seeded there from the mixer and clamped to
                // [0, 0x100]. Both shipped calls pass a max of 100, so the
                // track is 100px wide and one pixel is one unit -- the max
                // doubles as the track's pixel width, which is why the
                // engine needs no scaling anywhere.
                auto* slider = static_cast<sk_bindings::SliderExecutable*>(row.widget.get());
                std::string name = RowText(row.textId, row.literalText, strings);
                int labelX = (sk::Backbuffer::kWidth - sk::BitmapFont::TextWidth(name)) / 2;
                sk::BitmapFont::DrawString(backbuffer, labelX, y + 1, name, kCenteredShadowColor);
                sk::BitmapFont::DrawString(backbuffer, labelX, y, name, color);

                const int trackWidth = slider->maxValue();
                const int trackLeft = sk_bindings::SliderTrackLeft(trackWidth);
                for (int i = 0; i < sk_bindings::kSliderBarThickness; ++i) {
                    DrawHRun(backbuffer, trackLeft, y + sk_bindings::kSliderTrackDY + i, trackWidth,
                             kTextColor);
                }
                // Drawn after the track, as the engine does, so at full
                // value the marker overwrites the track's last column.
                const int markerX = sk_bindings::SliderMarkerX(trackWidth, slider->value());
                for (int i = 0; i < sk_bindings::kSliderBarThickness; ++i) {
                    DrawVRun(backbuffer, markerX + i, y + sk_bindings::kSliderMarkerDY,
                             sk_bindings::kSliderMarkerHeight, kSliderMarkerColor);
                }
                y += sk_bindings::kSliderRowHeight;
                break;
            }
            case RowKind::ComboBox: {
                auto* combo = static_cast<sk_bindings::ComboBoxExecutable*>(row.widget.get());
                int value = combo->currentOptionValue();
                // M95: a numerical combo draws the number itself.
                const std::string text =
                    combo->numericalMode() ? std::to_string(value) : strings.Get(value);
                // M108: at its own x/y, and the row cursor is untouched --
                // the same shape as the text area above.
                //
                // I first read this as `case 4` of `FUN_10076b64` (a
                // cursor-positioned row, centred and bold, with a rule
                // under it, advancing 0x18) and wrote that. It was wrong,
                // and the contact sheet caught it immediately: the combo
                // landed on top of the text area on
                // choosecharactermenu.s. `AddComboBox` (case 0x1e of
                // `FUN_10078de4`) builds its widget with `FUN_1008f82c`,
                // which sets `+0x58 = 10` -- the same kind the text area's
                // `FUN_1008f7d0` sets, and kind 10 takes the menu draw's
                // `default:` arm: the widget's own vtable slot, with
                // `uVar9 = uVar8` leaving the cursor alone. Case 4 belongs
                // to some other widget class. Positions come from
                // `+0x78`/`+0x7c`, which the shared tail at LAB_10079d28
                // fills in from the script's own arguments.
                //
                // M118: and this is the draw itself, `FUN_1008e974`. The
                // "< Assassin >" this port printed was its own invention;
                // the engine draws two arrow sprites and the option text
                // between them:
                //
                //   if (sel != 0)          blit slot 207 at (x, y)
                //   text centred across the screen at y, palette +8/+10
                //   if (sel < count - 1)   blit slot 208 at (x + w - 5, y)
                //
                // An arrow is missing exactly when there is nothing more
                // that way, because the combo clamps rather than wraps.
                if (value < 0) break;  // `if (sel < count)`: no options, no draw
                if (combo->selection() != 0) {
                    if (const sk::Sprite* left = sprites.GetSprite(sk_bindings::kComboLeftArrowSlot)) {
                        backbuffer.Blit(combo->x(), combo->y(), *left);
                    }
                }
                DrawCenteredWidgetLabel(backbuffer, combo->y(), text,
                                        isSelected ? kUiHighlightColor : kUiStandardColor);
                if (combo->selection() < combo->optionCount() - 1) {
                    if (const sk::Sprite* right =
                            sprites.GetSprite(sk_bindings::kComboRightArrowSlot)) {
                        backbuffer.Blit(
                            combo->x() + combo->width() - sk_bindings::kComboRightArrowInset,
                            combo->y(), *right);
                    }
                }
                break;
            }
            case RowKind::TextArea: {
                auto* textArea = static_cast<sk_bindings::TextAreaExecutable*>(row.widget.get());
                // M108: at its own x/y, and the row cursor is untouched.
                // `FUN_1008f458` is the whole widget:
                //
                //     split(text, lines, widget+0x98 /* SetTextWidth */);
                //     x = widget+0x78; y = widget+0x7c;
                //     for (line : lines) {
                //         FUN_1007f49c(x, y, line, ..., 0 /* left */, ...);
                //         y += 0xc;
                //     }
                //
                // and its widget kind takes the menu draw's `default:` arm,
                // which leaves the cursor alone. This port drew every text
                // area at a hardcoded x=12 and at the shared cursor, then
                // advanced the cursor past it. The x was the visible half:
                // choosecharactermenu.s asks for `AddTextArea(5, 45)` with
                // `SetTextWidth(27)`, and 27 characters starting at 12
                // instead of 5 runs off the right edge of a 176px screen --
                // which is why the class descriptions read "considerabl"
                // and "quie".
                //
                // M118: and each line through the left-aligned arm (so it
                // has the 0x0b96 shadow), 0xc apart rather than 15, in the
                // palette's colours rather than the static-item one.
                DrawTextAreaLines(backbuffer, textArea->x(), textArea->y(), strings.Get(row.textId),
                                  textArea->textWidth(),
                                  isSelected ? kUiHighlightColor : kUiStandardColor);
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
                //
                // M118: AddFloatingSprite's widget (`FUN_1007f784`) draws
                // with `DrawEntitySpriteWithOutline` itself, so a focused
                // one -- the portrait under the cursor on the gender
                // screen -- gets the two-pixel highlight ring.
                const sk::Sprite* portrait =
                    fsprite->spriteId() >= 0 ? sprites.GetSprite(fsprite->spriteId()) : nullptr;
                if (portrait && row.x >= 0) {
                    DrawSpriteWithOutline(backbuffer, sprites, fsprite->spriteId(), row.x, row.y,
                                          isSelected);
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
// point heading field (Camera::yaw is a float radian, see camera.h), but
// M61 settled the conversion between the two from the engine's own
// forward-motion code (CameraHeadingUnits() above), so RenderHud() now
// runs the real formula on a real heading rather than approximating it --
// and scrolls the tape the same way round the automap marker turns, which
// it previously did not.
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
// M57: the map overlay -- see world/automap.h for the whole recovery.
// Drawn in the original's own order: backdrop sprite (slot 20), the zone
// caption, then the 64x64-tile grid and the player arrow. It sits after
// the HUD in the frame because it is drawn later in the real HUD pass
// too, and it covers the compass band while open.
void RenderAutomapOverlay(sk::Backbuffer& backbuffer, const sk::Zone& zone,
                           const sk::ExploredTiles& explored, const sk::Camera& camera,
                           sk::SpriteArchive& sprites, const sk::StringTable& strings,
                           const std::string& zoneName) {
    if (const sk::Sprite* backdrop = sprites.GetSprite(sk::kAutomapBackdropSlot)) {
        backbuffer.Blit(0, 0, *backdrop);
    }
    // `FUN_100290a8` is the engine's own internal-zone-name -> display-
    // string lookup, which this port already has as
    // assets/zone_display_names.h (M26 found it from the loading screen).
    //
    // Post-M120: the draw is `FUN_1008f97c(gc, name, 10, 0xfff, 1, 0, 0, 0)`
    // -- the centred arm, shadow on (one pixel below, in 0xb1/0x9c/0x65),
    // legend face, and **white**. The port drew it in the static-item brown
    // 0x752 with no shadow, which all but vanished into the map backdrop.
    int nameId = sk::ZoneDisplayNameStringId(zoneName);
    if (nameId >= 0) {
        constexpr uint16_t kAutomapCaptionColor = MenuColor444(0xfff);
        DrawCenteredWidgetLabel(backbuffer, sk::kAutomapCaptionY, strings.Get(nameId),
                                kAutomapCaptionColor);
    }
    // The engine's angle unit is 0x10000 per turn; this port's camera
    // carries radians, and yaw runs the opposite way round -- M61 derived
    // that from the engine's own forward-motion code rather than fitting
    // it, see CameraHeadingUnits().
    int headingUnits = CameraHeadingUnits(camera.yaw);
    sk::RenderAutomap(backbuffer, zone, explored,
                       static_cast<int>(camera.x) >> 8, static_cast<int>(camera.y) >> 8,
                       headingUnits);
}

// M86: `global.spr` slot 218 -- 176x208, 838 opaque pixels of RGB444
// 0x0d12, one bright red diagonal stroke across the view -- and the ramp
// row `FUN_1006a894` redraws the vitals frame through while it is up.
// See the block inside RenderHud for the two decompiled call sites.
constexpr int kHurtOverlaySlot = 218;
constexpr int kHurtFrameFlashRow = 1;

// M120: `layout` places it in a wider view (sk::ViewLayout): the banner and
// the hit slash centred, the vitals frame on the left edge. The default is
// the device's own frame, where both offsets are 0.
void RenderHud(sk::Backbuffer& backbuffer, const sk_bindings::PlayerExecutable& player,
                sk::SpriteArchive& sprites, float cameraYaw, int hurtTimer = 0,
                const sk::ViewLayout& layout = {}) {
    const int cx = layout.centreX();
    const int lx = layout.leftX();
    const sk::Sprite* compassTape = sprites.GetSprite(0);
    const sk::Sprite* compassFrame = sprites.GetSprite(1);
    if (compassTape && compassFrame) {
        // M61: the real formula, now that the heading direction is settled
        // (CameraHeadingUnits' own comment). The engine takes the *high
        // byte* of `player+0xb6` as a signed value, wraps it into [0,255]
        // and caps at 254 -- which is exactly `(heading >> 8) & 0xff`
        // capped. This used to scroll the tape by `+yaw`, i.e. the wrong
        // way round and against the automap marker, which had already
        // taken `-yaw` on the same field.
        int offset = (std::min)((CameraHeadingUnits(cameraYaw) >> 8) & 0xff, 254);
        backbuffer.BlitRegion(cx + 52, 5, *compassTape, offset, 68);
        backbuffer.Blit(cx, 0, *compassFrame);
    }

    auto drawHandIcon = [&](sk_bindings::ItemExecutable* handItem, int x) {
        if (!handItem) return;
        const sk::Sprite* icon = sprites.GetSprite(handItem->icon());
        if (icon && icon->width <= 32 && icon->height <= 32) backbuffer.Blit(cx + x, 5, *icon);
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
        backbuffer.BlitRegion(lx + 10, y, *fill, 0, fillWidth);
        return true;
    };
    bool health = drawVitalBar(162, 182, player.health(), player.maxHealth());
    bool magicka = drawVitalBar(160, 190, player.magicka(), player.maxMagicka());
    bool fatigue = drawVitalBar(161, 196, player.fatigue(), player.maxFatigue());
    // ---- M86: the red slash the player gets hit with ----
    //
    // Reported alongside the creature flash, and it is the same idea seen
    // from the other side: the player has no model to redraw, so the
    // engine puts the feedback on the HUD.
    //
    // `FUN_10044814`, the player's own DoDamage, opens with
    //
    //     if (stats->+0x7c != 4)                       // not in Sanctuary
    //         if (damage != 0 && (s16)damage >= 0) {
    //             engine->+0x28->+0x17c = 0x40;        // the hurt timer
    //             PlaySound(0x50, ...);                // the impale hit
    //         }
    //
    // and `FUN_1002ae88` -- the same function that draws the three vitals
    // bars -- branches the frame draw on that timer:
    //
    //     if (hud->+0x17c < 1) {
    //         Blit_RLESprite(slot180, 0, 0xa2, blend=0);
    //     } else {
    //         hud->+0x17c -= frameDelta();
    //         FUN_1006a894(slot180, 0, 0xa2, blend=0, flashRow=1);
    //         Blit_RLESprite(slot218, 0, 0, blend=1);
    //     }
    //
    // So being hit does two things at once: the dragon-head vitals frame
    // is redrawn **through hit-flash ramp row 1** -- the same red ramp a
    // struck creature is drawn from, `FUN_1006a894` being Blit_RLESprite
    // with v8's remap spliced into its inner loop -- and `global.spr`
    // **slot 218** is blended over the whole screen at 50%.
    //
    // Slot 218 is 176x208, the full frame, and carries 838 opaque pixels
    // in a single colour, RGB444 0x0d12: one bright red diagonal stroke
    // from the upper right down across the middle of the view. That is
    // the "red slash" exactly, and this port had neither the sprite nor
    // the timer nor the blend mode that draws it.
    //
    // 0x40 is 64 units of 1/256 s, so the whole thing lasts a quarter of
    // a second.
    const bool hurt = hurtTimer > 0;
    uint16_t hurtRamp[16];
    if (hurt) {
        for (int i = 0; i < 16; ++i) hurtRamp[i] = sk::FlashRamp565(kHurtFrameFlashRow, i);
    }
    if (vitalsFrame && (health || magicka || fatigue)) {
        if (hurt) {
            backbuffer.BlitRamped(lx, 162, *vitalsFrame, hurtRamp);
            const sk::Sprite* slash = sprites.GetSprite(kHurtOverlaySlot);
            if (slash) backbuffer.BlitBlend(cx, 0, *slash);
        } else {
            backbuffer.Blit(lx, 162, *vitalsFrame);
        }
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
                    backbuffer.SetPixel(lx + 10 + dx, y + dy,
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

// M127: screen mode 0xb, `FUN_1006b3fc`'s draw -- see engine/credits_roll.h.
// The backdrop is global.spr slot 20 at (0,0); every line between y=1 and
// y=164 is drawn centred, shadowed, in the static-item brown. This used to
// be a placeholder: a navy fill and left-aligned light text, scrolled by
// hand.
void RenderCredits(sk::Backbuffer& backbuffer, sk::SpriteArchive& sprites,
                    const std::vector<std::string>& lines, int scroll) {
    backbuffer.Fill(kBackgroundColor);  // under the backdrop, if it is missing
    if (const sk::Sprite* backdrop = sprites.GetSprite(sk::kCreditsBackgroundSprite)) {
        backbuffer.Blit(0, 0, *backdrop);
    }
    constexpr uint16_t kCreditsColor = MenuColor444(sk::kCreditsColor444);
    for (size_t i = 0; i < lines.size(); ++i) {
        const int y = sk::CreditsLineY(static_cast<int>(i), scroll);
        if (y > sk::kCreditsLastVisibleY) break;
        if (sk::CreditsLineVisible(y)) DrawCenteredWidgetLabel(backbuffer, y, lines[i], kCreditsColor);
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
// screen, not a measurement of actual work done. This port shows the
// banner for every zone change after the first (the first uses
// "Loading..." instead, string 3820) as the best-evidenced substitute.
//
// M131: **where the banner goes and what colour it is** (`FUN_10029cb0`,
// the `mode == 10 || mode == 3` arm):
//
//     FUN_1008f97c(gc, text, 0, controller->+0x198[controller->+0x194], 0, 1, 0, 0);
//     if (++controller->+0x194 > 0xf) controller->+0x194 = 0;
//
// -- centred on the top row, y = 0, no shadow, in a colour that steps
// through a sixteen-entry table the constructor fills with `i * 0x111`:
// black up to white, one step a frame, and round again. The banner
// pulses. It was a still line at y = 4 in the title's 0x733.
constexpr int kLoadingBannerY = 0;
constexpr int kLoadingBannerPulseSteps = 0x10;
constexpr uint16_t LoadingBannerColor(int pulse) {
    return MenuColor444(static_cast<uint16_t>((pulse & 0xf) * 0x111));
}

void RenderLoadingScreen(sk::Backbuffer& backbuffer, sk::SpriteArchive& sprites,
                          const std::string& text, int percent, int pulse) {
    const sk::Sprite* splash = sprites.GetSprite(174);
    if (splash) {
        backbuffer.Blit(0, 0, *splash);
    } else {
        backbuffer.Fill(kBackgroundColor);
    }
    int textX = (sk::Backbuffer::kWidth - sk::BitmapFont::TextWidth(text)) / 2;
    sk::BitmapFont::DrawString(backbuffer, (std::max)(0, textX), kLoadingBannerY, text,
                                LoadingBannerColor(pulse));
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
    // M133: black, not the port's placeholder navy. `FUN_1002bdf4` clears
    // nothing -- it draws the picture and the caption over whatever the
    // buffer holds, and every way into a vignette mode is through
    // `FUN_1001b788`'s fade, whose last step (`FUN_1004fc50`, 15 off every
    // RGB444 nibble, in place) leaves the whole frame at 0x000. The fade in
    // that follows subtracts from that same buffer, so the margin stays
    // black for the whole slideshow.
    backbuffer.Fill(sk::PackRGB565(0, 0, 0));
    const sk::Sprite* art = sprites.GetSprite(def.firstSprite + slide);
    if (art) backbuffer.Blit(kVignetteSpriteX, kVignetteSpriteY, *art);
    // M131: the caption is the engine's text box (`FUN_1002bdf4`):
    //
    //     FUN_100231b4(ctx, text, 0xe, 0x73, 0xab, _, 0xff, 0xff, 0xff,
    //                  bold 0, italic 1, legend 0, align 0, wrap 0x19)
    //
    // -- white, left-aligned under the picture's own left edge, broken
    // every 25 characters, 13 pixels a line. It was one centred line in
    // the title colour, which ran a long caption off both sides.
    sk::DrawTextBox(backbuffer, strings.Get(def.firstTextId + slide), kVignetteSpriteX, 0x73, 0xab,
                    sk::PackRGB565(255, 255, 255), sk::TextBoxAlign::Left, 0x19,
                    sk::BitmapFont::Face::SmallItalic);
}

// M113: where this run's *writable* files go -- saves, `dragonstar.set`
// and the log. Everything that follows used to put them beside the
// executable, which is right for a build directory and wrong for a shipped
// install, where the exe lives in `bin/` and a player never looks there.
// The launcher sets `SK_USER_DIR`; returns empty when it is not set, and
// every caller below then keeps the exact path it used before this
// milestone, so a bare run out of a build directory is unchanged. Joins
// `SK_FONT_TYPEFACE`/`SK_DUMP_MENUS`/`SK_DEBUG_*`.
std::string UserDirectory() {
    const char* fromEnvironment = std::getenv("SK_USER_DIR");
    if (!fromEnvironment || !*fromEnvironment) return std::string();
    std::error_code error;
    std::filesystem::create_directories(fromEnvironment, error);
    if (!error) return fromEnvironment;
    std::printf("shadowkey-port: SK_USER_DIR=%s is unusable (%s) -- falling back\n",
                fromEnvironment, error.message().c_str());
    return std::string();
}

// M113: the window scale, likewise. Was a hardcoded 3x below; the launcher
// offers 2x/3x/4x and passes the choice through here. Out-of-range values
// keep the old default rather than opening an unusable window.
int WindowScale() {
    if (const char* fromEnvironment = std::getenv("SK_SCALE")) {
        const int scale = std::atoi(fromEnvironment);
        if (scale >= 1 && scale <= 8) return scale;
    }
    return 3;
}

// M120: whether the game opens in borderless fullscreen -- the default, as
// in the sister ports' launchers. `SK_FULLSCREEN=0` (the launcher's window
// sizes) opens a window of WindowScale() instead. F11 / Alt+Enter switch
// either way at any time (platform/win32/window.h).
bool StartFullscreen() {
    const char* fromEnvironment = std::getenv("SK_FULLSCREEN");
    return !(fromEnvironment && fromEnvironment[0] == '0');
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
    // M113: `SK_USER_DIR` (empty unless the launcher set it) moves this
    // run's writable files -- the log here, saves and `dragonstar.set`
    // below -- into one folder a player can find. Without it every path is
    // what it was before: the log beside the current directory, the saves
    // beside the executable.
    const std::string userDirectory = UserDirectory();
    sk::StartConsoleTeeLog(userDirectory.empty() ? std::string("shadowkey_port.log")
                                                 : userDirectory + "/shadowkey_port.log");
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
    constexpr int kMenuMoveSound = 99;     // M131: menuNEWxbx.wav, every table
    constexpr int kMenuSelectSound = 100;  //       menu1.wav
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
    // M110: `SK_FONT_TYPEFACE` overrides which typeface is taken from the
    // store -- the game itself asks for two by name, so being able to ask
    // for a different one without a rebuild is how you find out what a
    // given Ceurope.gdr actually carries (a name that is not there makes
    // the loader list the ones that are).
    const char* typeface = std::getenv("SK_FONT_TYPEFACE");
    sk::BitmapFont::LoadRealFont(fontPath, typeface ? typeface : "LatinBold12");
    // M111: the second face, for `SetFontNum(1)` -- see bitmap_font.h's
    // Face comment for why LatinPlain12 is the one the device's
    // nearest-font matching would have landed on. Same store as the
    // legend font, so it costs one extra parse and nothing else; if it
    // fails to load, drawing silently falls back to the Ui face.
    sk::BitmapFont::LoadRealFont(fontPath, "LatinPlain12", sk::BitmapFont::Face::Small);
    // M132: the italic face, from the device's second text font store if
    // it is beside the first (bitmap_font.h). Absent, the Small face is
    // slanted the same way, so nothing is printed about a missing file.
    {
        const std::filesystem::path italicStore =
            std::filesystem::path(fontPath).parent_path() / "Browsereur.gdr";
        std::error_code ignored;
        if (std::filesystem::is_regular_file(italicStore, ignored)) {
            sk::BitmapFont::LoadRealFont(italicStore.string(), "Alpi12",
                                          sk::BitmapFont::Face::SmallItalic);
        }
    }

    skInterpreter interpreter;
    sk_bindings::MenuStack stack(scriptRoot, interpreter, &strings, &soundArchive, &audioEngine);

    // M50: save slots are real files now (menu_stack.h). The real game
    // keeps them under `c:\systemppsR51\`; here they go beside the
    // executable, resolved the same way as every other asset path above
    // so a save does not land in whatever directory the game was launched
    // from. M113: unless `SK_USER_DIR` says otherwise, which is how a
    // shipped install keeps saves out of `bin/` and in a folder a player
    // can actually find (and back up).
    const std::string saveDirectory =
        userDirectory.empty() ? sk::ExecutableDirectory() : userDirectory;
    stack.SetSaveDirectory(saveDirectory);
    // M119: the Options screen's Resolution and Frame Rate rows
    // (engine/pc_settings.h). Their own file beside dragonstar.set, and
    // attached this early so SK_DUMP_MENUS below draws the rows too.
    sk::PcSettingsStore pcSettings(saveDirectory + "/" + sk::PcSettingsStore::kFileName);
    if (pcSettings.Load()) {
        std::printf("loaded %s (resolution %d, frame rate %d)\n",
                    sk::PcSettingsStore::kFileName, pcSettings.settings().resolutionScale(),
                    pcSettings.settings().frameRate());
    }
    stack.SetPcSettings(&pcSettings);
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
    } catch (skTreeNodeReaderException& e) {
        std::printf("shadowkey-port: SCRIPT FILE ERROR loading mainmenu.s: %s\n", e.toString().ptr());
        return 2;
    } catch (skRuntimeException& e) {
        std::printf("shadowkey-port: RUNTIME ERROR running mainmenu.s: %s\n", e.toString().ptr());
        return 2;
    }

    // M108: `SK_DUMP_MENUS=<dir>` renders every menu script in the game to
    // a .ppm in that directory and exits, without opening a window.
    //
    // This exists because menu *layout* had no way of being checked at
    // all. The layout rules are not in the scripts -- only ~250 of the
    // corpus's calls carry a coordinate, and the ~500 menus built from
    // bare `AddMenuItem`/`AddStaticItem` get their positions from the
    // engine (`FUN_10076b64`). So the way to verify a screen is to read
    // those rules out of the decompile and compare them against what this
    // port actually draws -- which needs a picture of what it draws, for
    // every screen, without driving the game by hand.
    //
    // It renders through `RenderMenu` on the real boot path (real string
    // table, real sprites, real font), so what lands in the directory is
    // exactly what the game would put on screen. Menus whose OnDisplay
    // needs game state this early throw; those are caught and reported
    // rather than aborting the sweep.
    if (const char* dumpDir = std::getenv("SK_DUMP_MENUS")) {
        sk::Backbuffer shot;
        int written = 0, failed = 0;
        for (const std::string& name : EnumerateMenuScripts(scriptRoot)) {
            try {
                stack.OpenMenu(name);
                sk_bindings::MenuExecutable* m = stack.currentMenu();
                if (!m) {
                    ++failed;
                    std::printf("  SKIP %-40s (no menu)\n", name.c_str());
                    continue;
                }
                // M118: as the game loop does every tick before drawing --
                // and as the engine does itself, focusing the first
                // selectable widget as it is added (LAB_10079d40) -- so the
                // sheet shows each screen's focus the way a player sees it.
                m->EnsureValidSelection();
                RenderMenu(shot, *m, stack.player(), strings, spriteArchive);
                std::string flat = name;
                for (char& c : flat) {
                    if (c == '/' || c == '\\') c = '_';
                }
                WriteBackbufferPpm(shot, std::string(dumpDir) + "/" + flat + ".ppm");
                ++written;
            } catch (skParseException& e) {
                ++failed;
                std::printf("  FAIL %-40s parse: %s\n", name.c_str(), e.toString().ptr());
            } catch (skTreeNodeReaderException& e) {
                ++failed;
                std::printf("  FAIL %-40s parse: %s\n", name.c_str(), e.toString().ptr());
            } catch (skRuntimeException& e) {
                ++failed;
                std::printf("  FAIL %-40s runtime: %s\n", name.c_str(), e.toString().ptr());
            } catch (...) {
                ++failed;
                std::printf("  FAIL %-40s (unknown)\n", name.c_str());
            }
        }
        std::printf("SK_DUMP_MENUS: %d menu(s) written to %s, %d skipped\n", written, dumpDir,
                    failed);
        return 0;
    }

    // M113: the scale was hardcoded 3x; `SK_SCALE` is how the launcher
    // offers 2x/3x/4x. The title is what a player sees in their taskbar,
    // so it is the game's name rather than the milestone this window was
    // first opened for.
    const int windowScale = WindowScale();
    sk::Window window(sk::Backbuffer::kWidth * windowScale,
                      sk::Backbuffer::kHeight * windowScale,
                      L"The Elder Scrolls Travels: Shadowkey");
    // M120: a fixed Aspect Ratio gives the window its shape -- the height
    // WindowScale() chose, the width the ratio's -- and Original gives it
    // back the device's. Only when the setting changes (or fullscreen is
    // left), and never to a fullscreen or maximized window, so a window the
    // player has shaped by hand is left alone otherwise. Auto follows the
    // window, so it has nothing to apply.
    int appliedAspectIndex = -1;
    bool appliedFullscreen = false;
    auto applyAspectToWindow = [&]() {
        const sk::PcSettings& settings = pcSettings.settings();
        const bool fullscreen = window.IsFullscreen();
        if (settings.aspectIndex == appliedAspectIndex && fullscreen == appliedFullscreen) return;
        appliedAspectIndex = settings.aspectIndex;
        appliedFullscreen = fullscreen;
        if (fullscreen || window.IsMaximized() || settings.aspect() == sk::kAspectAuto) return;
        int clientW = 0, clientH = 0;
        window.ClientSize(&clientW, &clientH);
        if (clientH <= 0) return;
        const int viewWidth = sk::ViewWidthFor(settings, 0, 0);
        const int wantW = (clientH * viewWidth + sk::Backbuffer::kHeight / 2) / sk::Backbuffer::kHeight;
        if (wantW != clientW) window.SetClientSize(wantW, clientH);
    };
    applyAspectToWindow();
    if (StartFullscreen()) window.SetFullscreen(true);

    sk::InputState input;
    // M80: `ParseActionText` has to name whichever key an action is bound
    // to *now*, so the one place the bindings live has to be reachable
    // from a menu script. See simkin_bindings/action_text.h.
    stack.SetInput(&input);

#if SK_DEBUG_SUITE
    // SK_DEBUG_SUITE (M68): declared here, before the input callbacks that
    // have to consult it, and Attach()ed further down once every game local
    // it points at exists. Until then HandleKey/HandleChar return false and
    // the suite is completely inert.
    DebugGameRefs debugRefs;
    LiveDebugHost debugHost(debugRefs);
    sk_debug::DebugSuite debugSuite;
#endif

    // M52: `dragonstar.set` -- the one configuration file the real engine
    // writes (assets/game_config.h). Loading it here is the counterpart of
    // the real loader running before the front end: the action map, the
    // two volumes and the mute-on-call flag all come back, and anything
    // the file does not mention keeps the default the constructors gave
    // it. A missing file is not an error -- a first run has none.
    // M113: beside the saves, for the same reason.
    const std::string configPath = saveDirectory + "/" + sk::GameConfig::kFileName;
    sk::GameConfig config;
    config.CaptureBindings(input);
    config.soundVolume = audioEngine.sfxVolumePercent();
    config.musicVolume = audioEngine.musicVolumePercent();
    config.muteOnCall = stack.muteOnCall();
    config.language = stack.language();
    stack.SetConfigPath(configPath);
    if (config.Load(configPath)) {
        config.ApplyBindings(input);
        audioEngine.SetSfxVolumePercent(config.soundVolume);
        audioEngine.SetMusicVolumePercent(config.musicVolume);
        stack.SetMuteOnCall(config.muteOnCall);
        std::printf("loaded %s\n", configPath.c_str());
    }
    // M99: the GameEngine constructor loads the settings, *then* builds the
    // string table's path from the language they named. The English table
    // is already loaded above for everything that ran before this; swap it
    // for the saved one now.
    if (config.language != stack.language() && !stack.SetLanguage(config.language)) {
        std::printf("language %d has no string table; staying on English\n", config.language);
    }
    // M99: the engine's application-exit handler (`FUN_100209c8`, events
    // 0x100 and 0xbc1) runs `if (!saveDisabled) FUN_10019c04()` -- and a PC
    // window has a close button where the handset had that event, so this
    // is the same write at the same moment. Quit and QuitGame write too.
    struct ConfigWriter {
        sk_bindings::MenuStack& stack;
        ~ConfigWriter() {
            if (!stack.configSaveDisabled()) stack.SaveConfig();
        }
    } configWriter{stack};

    // M119: a controller presses the same slots the keyboard does
    // (engine/gamepad_map.h), so a slot is held while *either* holds it --
    // releasing W must not stop a walk the left stick is still making, nor
    // the other way round. Each side keeps its own record; every change
    // writes the OR of the two.
    // M126: a count, not a flag -- with mouse look on, A and the Left arrow
    // both press the sidestep slot, and letting go of one must not release
    // the other. `keySlot` is the slot each key pressed (-1: none), kept
    // until that key is released so a held key never changes meaning (see
    // engine/mouse_look_keys.h).
    std::array<uint8_t, static_cast<size_t>(sk::ButtonSlot::kCount)> keyHeld{};
    std::array<int, 256> keySlot;
    keySlot.fill(-1);
    sk::GamepadMapper padMapper;
    sk::Gamepad gamepad;

    // M130: the join lobby's address line. The engine found its hosts by
    // Bluetooth inquiry and had nothing to type; over a network a host that
    // is not on the LAN has to be named, and until now that was a text file
    // (mp_host.txt). While "Looking for hosts" is up, typing writes an
    // address under the list and Enter adds it as a host row. The keys that
    // type are kept from the menu while it is open; the arrows, Enter on an
    // empty line and Esc are still the menu's.
    bool mpAddressOpen = false;
    std::string mpAddressEntry;
    std::string mpAddressSubmitted;  // taken by the tick, which owns the lobby
    auto mpAddressKey = [](int vk) {
        return (vk >= '0' && vk <= '9') || (vk >= 'A' && vk <= 'Z') ||
               (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) || vk == VK_DECIMAL ||
               vk == VK_OEM_PERIOD || vk == VK_OEM_MINUS || vk == VK_OEM_1 || vk == VK_BACK;
    };

    window.SetKeyCallback([&](int vkCode, bool down) {
#if SK_DEBUG_SUITE
        // SK_DEBUG_SUITE (M68): first refusal on every key. When the console
        // is open it consumes everything, so a digit typed into a command
        // never also reaches InputState and swings a weapon. Key-*up* is
        // never consumed (see DebugSuite::HandleKey) -- swallowing a release
        // would leave the slot latched down forever.
        const bool ctrlDown = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        const bool shiftDown = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        if (debugSuite.HandleKey(vkCode, down, ctrlDown, shiftDown)) return;
        if (debugSuite.capturingInput() && down) return;
#endif
        if (vkCode < 0 || vkCode >= static_cast<int>(keySlot.size())) return;
        int& latched = keySlot[static_cast<size_t>(vkCode)];
        if (down) {
            if (latched >= 0) return;  // auto-repeat: already pressed
            // M130: typing an address (see mpAddressOpen).
            if (mpAddressOpen &&
                (mpAddressKey(vkCode) || (vkCode == VK_RETURN && !mpAddressEntry.empty()))) {
                return;
            }
            auto slot = sk::MapPcKeyToButtonSlot(vkCode);
            if (!slot) return;
            const sk::ButtonSlot pressed =
                sk::MouseLookKeySlot(*slot, window.MouseLookActive(), input);
            latched = static_cast<int>(pressed);
            ++keyHeld[static_cast<size_t>(pressed)];
            input.SetButton(pressed, true);
        } else {
            if (latched < 0) return;
            const auto pressed = static_cast<sk::ButtonSlot>(latched);
            latched = -1;
            uint8_t& count = keyHeld[static_cast<size_t>(pressed)];
            if (count > 0) --count;
            input.SetButton(pressed, count > 0 || padMapper.SlotHeld(pressed));
        }
    });
    // M69: alt-tabbing away with a key held would otherwise leave that slot
    // latched down forever -- see InputState::ReleaseAll.
    // M119: the pad lets go too. It is not read again until the window is
    // back in front, and a control still held then presses afresh.
    window.SetFocusLostCallback([&]() {
        keyHeld.fill(0);
        keySlot.fill(-1);
        padMapper.ReleaseAll([](sk::ButtonSlot, bool) {});
        input.ReleaseAll();
    });
    window.SetCharCallback([&](wchar_t ch) {
#if SK_DEBUG_SUITE
        // SK_DEBUG_SUITE (M68): before the name-entry handler, so typing a
        // command does not also type into a character name.
        if (ch < 0x80 && debugSuite.HandleChar(static_cast<char>(ch))) return;
#endif
        if (mpAddressOpen) {
            if (ch == 0x08) {
                if (!mpAddressEntry.empty()) mpAddressEntry.pop_back();
            } else if (ch == 0x0D) {
                if (!mpAddressEntry.empty()) {
                    mpAddressSubmitted = mpAddressEntry;
                    mpAddressEntry.clear();
                }
            } else if (mpAddressEntry.size() < 40 &&
                       ((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'z') ||
                        (ch >= 'A' && ch <= 'Z') || ch == '.' || ch == '-' || ch == ':')) {
                mpAddressEntry.push_back(static_cast<char>(ch));
            }
            return;
        }
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

    // M69: the control scheme is the user's own N-Gage-emulator binding
    // sheet -- see engine/pc_key_map.h for the full table and for which
    // N-Gage button each PC key actually *is*.
    std::printf(
        "shadowkey-port: menus -- W/S move selection, A/D cycle combo values or navigate "
        "horizontal screens, Enter confirms, Esc goes back.\n");
    std::printf(
        "shadowkey-port: in the 3D zone -- W/S walk, A/D turn, arrow keys sidestep and look, "
        "Space jumps, Esc returns to the main menu.\n");
    std::printf(
        "shadowkey-port: 'E' (the real Use binding, keypad 3, M15) opens/closes the nearest "
        "door you're facing -- 'Q' and the left mouse button (keypad 7/5) swing your "
        "left/right-hand weapon (M12).\n");
    std::printf(
        "shadowkey-port: Tab (the N-Gage's '#') opens the character manager (real inventory/"
        "stats/quest-log screens, M10) -- Esc there always returns straight to the 3D view. "
        "'M' is the map, 'C'/'G' cycle the left/right hand queues.\n");

    sk_bindings::MenuExecutable* lastMenu = nullptr;
    int creditsScroll = 0;
    bool creditsBackHeldPrev = false;  // M127: GetButtonPrev(0x11)

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
    // M55: the real per-model collision boxes for this zone, straight out
    // of `<zone>_models.txt` -- see world/model_collision.h. Reloaded on
    // every zone load, beside the model/sprite/sound manifests it sits in
    // the same file as.
    sk::ModelCollisionTable gameModelCollision;
    // M57: the map overlay -- see world/automap.h. `gameExplored` is the
    // reveal-as-you-go bitmap the renderer's own visibility raycast fills
    // in; `gameMapOpen` is `player+0x3a4`, the one boolean the whole
    // feature hangs off.
    sk::ExploredTiles gameExplored;
    bool gameMapOpen = false;
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
    // M67: the SetPassable -> tile-grid bridge -- see LiveTileStamp. Each
    // door is attached to it as it is created, below.
    LiveTileStamp gameTileStamp;
    sk::Camera gameCamera;
    // M72: the real `player+0xae` -- "something owns the camera pitch, so
    // do not drift it back to level". The auto-aim block sets it whenever
    // it engages; the actor tick (0x10000c64) clears it the moment the
    // player is moving with no aim target, and only then does the pitch
    // decay toward the horizon. So a pan held after a fight stays held
    // until the player walks.
    bool gamePitchHeld = false;
    // M125: mouse look. `gameMouseLookWanted` is set by every tick that
    // reaches the free-movement branch -- the one place the turn and look
    // keys are read -- and the loop captures the mouse only while it is.
    // `gameMousePitchOwned`: the mouse moved the pitch last, so it keeps
    // it -- no aim-assist, no drift back to level -- until a look key or
    // the right stick takes it back. The two *LastTick values are what the
    // mouse turned in the latest tick, for the frames drawn between ticks.
    bool gameMouseLookWanted = false;
    bool gameMousePitchOwned = false;
    float gameMouseYawLastTick = 0.0f;
    float gameMousePitchLastTick = 0.0f;
    // M73: `player+0xb30`, the movement fatigue accumulator `FUN_10045228`
    // owns. It is per-actor state on the player object in the real engine,
    // so it lives beside the camera here rather than inside the movement
    // block -- it has to survive across frames for a period to elapse at
    // all. See simkin_bindings/vitals.h.
    int gameMoveFatigueAccum = 0;
    // M130: the player's velocity and its turn / look rates (`player+0x98`,
    // `+0xa0`, `+0xb8`, `+0xaa`) -- simkin_bindings/player_move.h -- and the
    // walk bob's decrement for this tick (`FUN_1001f230`).
    sk_bindings::PlayerMotion gameMotion;
    // M130: `player+0x228` (see ZoneRenderer::SceneLights) and the flicker
    // counter. The byte lives on the player object, so a new player has it
    // clear again.
    bool gamePlayerGlow = false;
    uint32_t gameLightSeed = 0;
    int gameSwayThisTick = 0;
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
    // M78: `player+0x204` as `FUN_10042e44` (player vtable +0x274) writes
    // it -- the hand whose action key was last pressed. Non-owning, like
    // gameWeaponViewmodel.item, and cleared through the same two paths.
    sk_bindings::ItemExecutable* gameActiveHandItem = nullptr;
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
    // for reaching it from a menu, wrong for reaching it mid-game). It is
    // a deliberate host-side simplification: RightSelectionKey means
    // "back to gameplay" here, even from a nested Inventory/Stats/
    // QuestLog screen, rather than backing out one level at a time.
    //
    // M91: this flag turns out to be a real engine field, not an
    // invention -- the menu manager's `+0x49`, written by
    // `FUN_100779b8`'s third argument and meaning exactly "this screen is
    // open over a live session". The app's own foreground handler
    // (FUN_1002152c) raises `MainMenu` with it set when the phone comes
    // back to the front mid-game, which is the in-game pause-menu entry
    // point this comment used to say had never been found. See the Esc
    // handler in the movement tick below.
    //
    // M90/M91: two things now stand ahead of the shortcut -- a screen
    // whose own back handler is a *decision* (levelconfirm.s), and a
    // visible popup, which owns the back key outright.
    bool gamePausedForMenu = false;

    // ---- Post-M120: the level-state record (`<level>.dat`) -------------
    //
    // `FUN_100187d0` -- the level being played, as the member `current.sav`
    // keeps for it: the level object's own record (the automap's explored
    // bitmap, the zone script's variables, the tile-change journal), then
    // every saveable entity as `typeId` + its class's save chain. The
    // engine walks its one entity list; this port's live objects are in
    // three, and scenery that runs no script never changes, so it is the
    // three that are written. See MenuStack::StoreLevelState for where the
    // bytes go and the zone-load block for where they come back.
    //
    // `gameLoadedZoneName` is the level these live objects belong to.
    // stack.currentLevelName() cannot answer that during a transition: the
    // travel prompt's LoadLevel has already overwritten it with the
    // destination, exactly as the engine's `app+0x28` is.
    std::string gameLoadedZoneName;
    // M129: when set, every record the snapshot writes also reports the
    // net id of the object it came from, in record order -- which is how
    // the multiplayer entity stream (tag 0x1d) names each one.
    std::vector<int>* snapshotNetIdSink = nullptr;
    auto savedKindForType = [&entityTypes](int32_t typeId) {
        const sk::EntityTypeDescriptor* d = entityTypes.Lookup(typeId);
        return sk::SavedKindForCategory(d ? d->category : -1);
    };
    // A script's location as the record's `+0xe2` carries it -- relative to
    // the script root, the way the `.ent` record names a placement's
    // script.
    auto relativeScriptPath = [&](const sk_bindings::EntityBaseRef& ent) -> std::string {
        skTreeNodeObject* node = ent.entityScriptNode();
        if (!node) return std::string();
        std::string path = sk_bindings::ToStdString(node->getLocation());
        std::replace(path.begin(), path.end(), '\\', '/');
        // The root arrives with the platform's separators (it is built from
        // the executable's own directory), the location with whichever the
        // loader joined it with -- compare both in one spelling.
        std::string root = std::string(scriptRoot) + "/";
        std::replace(root.begin(), root.end(), '\\', '/');
        if (path.compare(0, root.size(), root) == 0) path = path.substr(root.size());
        return path;
    };
    auto fillEntityRecord = [&](sk::SavedEntity& rec, int typeId,
                                const sk_bindings::EntityBaseRef& ent, int yawRaw, int pitchRaw,
                                int rollRaw, int scaleRaw) {
        rec.typeId = typeId;
        rec.kind = savedKindForType(typeId);
        if (snapshotNetIdSink) snapshotNetIdSink->push_back(ent.netId());  // M129
        rec.entity.x = ent.positionX();
        rec.entity.y = ent.positionY();
        rec.entity.z = static_cast<int16_t>(ent.positionZ());
        rec.entity.yaw = static_cast<int16_t>(yawRaw & 0xffff);
        rec.entity.pitch = static_cast<int16_t>(pitchRaw & 0xffff);
        rec.entity.roll = static_cast<int16_t>(rollRaw & 0xffff);
        rec.drawable.scale = static_cast<int16_t>(scaleRaw);
        rec.entity.objectId = ent.entityId();
        rec.entity.artName = relativeScriptPath(ent);
        rec.entity.passable = ent.entityPassable() ? 1 : 0;
        // M122: `+0x60`, the entity flag word (default 2). ShowEntity is
        // `vtable[0x58](arg ^ 1)` = `FUN_100a2148`, which sets or clears its
        // bit 0, and FUN_10064ffc draws nothing while it is set -- so a
        // hidden prop's state is already in the record the engine writes.
        rec.entity.flags = static_cast<int16_t>(0x2 | (ent.entityHidden() ? 0x1 : 0x0));
        rec.entity.shortNameStringId = static_cast<uint16_t>(ent.entityNameId() & 0xffff);
        if (skTreeNodeObject* node = ent.entityScriptNode()) {
            rec.entity.scriptVars = sk_bindings::CollectScriptVars(*node);
        }
    };
    auto snapshotLevelState = [&]() -> std::vector<uint8_t> {
        sk::SavedLevelState state;
        // engine+0xbe0e: a level the player is standing in has rendered.
        state.renderEnabled = 1;
        if (gameZone) {
            state.level.exploredWidth = gameExplored.width();
            state.level.exploredHeight = gameExplored.height();
            state.level.explored = gameExplored.bytes();
            for (const sk::Zone::TileChange& c : gameZone->tileChanges()) {
                state.level.tileChanges.push_back(
                    {c.x, c.y, static_cast<int16_t>(c.blockFlags)});
            }
        }
        if (gameZoneScript) {
            state.level.scriptVars = sk_bindings::CollectScriptVars(*gameZoneScript);
        }
        // `vtable+0x104`, "am I saveable": an entity a script has taken out
        // of the world is gone from the engine's list by now, so it is not
        // written -- which is the whole of why a dead creature stays dead.
        for (const DoorInstance& d : gameDoors) {
            if (!d.script || d.script->entityRemoved()) continue;
            sk::SavedEntity rec;
            // The door's one heading, `+0xb6`: placement plus every
            // AddRotationTurn so far. It comes back as the placement.
            fillEntityRecord(rec, d.typeId, *d.script, d.script->headingRaw(), d.rotARaw,
                             d.rotBRaw, d.scaleRaw);
            rec.entity.usable = d.script->usable() ? 1 : 0;
            if (d.script->useTextId() >= 0) {
                rec.entity.nameStringId = static_cast<uint32_t>(d.script->useTextId());
            }
            state.entities.push_back(std::move(rec));
        }
        for (const PickupInstance& p : gamePickups) {
            if (!p.script || p.typeId < 0) continue;
            const sk_bindings::ItemExecutable& item = *p.script;
            if (item.markedForRemoval() || item.entityRemoved()) continue;
            sk::SavedEntity rec;
            fillEntityRecord(rec, p.typeId, item, p.yawRaw, p.rotARaw, p.rotBRaw, p.scaleRaw);
            rec.entity.usable = item.usable() ? 1 : 0;
            if (item.useTextId() >= 0) {
                rec.entity.nameStringId = static_cast<uint32_t>(item.useTextId());
            }
            rec.stackable.quantity = item.quantity();
            rec.weapon.quantity = item.quantity();
            // A container's contents are its Spellbook list, typeId and
            // count per entry -- and its `+0x160` Init guard is set, which
            // is what keeps a looted chest from restocking on the way back.
            rec.spellbook.destroy = item.destroyWhenEmpty() ? 1 : 0;  // M123: +0x180
            if (p.isContainer) {
                rec.spellbook.initDone = 1;
                rec.spellbook.isContainer = 1;
                for (const auto& content : item.contents()) {
                    if (!content || content->templateId() < 0) continue;
                    sk::SavedSpellEntry entry;
                    entry.typeId = static_cast<int16_t>(content->templateId());
                    entry.charges = content->quantity();
                    rec.spellbook.spells.push_back(entry);
                }
            }
            state.entities.push_back(std::move(rec));
        }
        for (const MonsterInstance& m : gameMonsters) {
            if (!m.script || m.typeId <= 0 || m.script->destroyed()) continue;
            const sk_bindings::MonsterExecutable& mon = *m.script;
            sk::SavedEntity rec;
            // The creature's heading is wherever it last turned, which this
            // port keeps as the instance's facing.
            fillEntityRecord(rec, m.typeId, mon, sk_bindings::EngineYawFromPortYaw(m.facingYaw),
                             m.rotARaw, m.rotBRaw, m.scaleRaw);
            // Where the creature actually is: the instance moves, and the
            // script's mirror is only refreshed for scripted teleports.
            rec.entity.x = static_cast<int32_t>(m.x);
            rec.entity.y = static_cast<int32_t>(m.y);
            rec.entity.z = static_cast<int16_t>(m.z);
            rec.entity.usable = mon.usable() ? 1 : 0;
            if (mon.useTextId() >= 0) {
                rec.entity.nameStringId = static_cast<uint32_t>(mon.useTextId());
            }
            rec.stats.health = static_cast<int16_t>(mon.currentHealth());
            rec.stats.maxHealth = static_cast<int16_t>(mon.maxHealth());
            rec.actor.aggressive = mon.aggressive() ? 1 : 0;
            // `+0x1e4`, the byte the death routine's first line tests: set
            // on a corpse. Its five-second decay is the native timer.
            rec.holder.active = mon.alive() ? 0 : 1;
            if (mon.decayArmed()) {
                rec.entity.timerArmed = 1;
                rec.entity.timerDeadline = static_cast<int32_t>(mon.decayDeadline());
            }
            state.entities.push_back(std::move(rec));
        }
        sk::SaveStream stream;
        state.Write(stream);
        return stream.bytes();
    };
    stack.SetLevelSnapshotter([&]() {
        return gameZone ? snapshotLevelState() : std::vector<uint8_t>();
    });

    // Post-M120: **what a creature shows when it is struck** -- the visible
    // half of its take-damage handler, `FUN_10081844`, which this port has
    // no single copy of (every attack path applies damage its own way), so
    // each of them calls this at the moment of the hit. The handler's whole
    // body sits under `if (!+0x1e4 && !+0x1e2)` -- **a dead or invulnerable
    // creature takes no hit at all**, so it neither bleeds nor flashes. The
    // port armed the red flash regardless, which turned every essential NPC
    // and every corpse caught in a blast red. Then, in the handler's order:
    // the blood spurt (`FUN_10081e5c`, skipped for typeId 0x3f7, the Lothna
    // crystal -- effect_entity.h has every field) and `FUN_10067c3c(self, 8,
    // 0, 5)`, the red ramp rows.
    // M122: **the death floor.** `FUN_10000498`, the actor tick, on the
    // cell it stands in: when `blockFlags & 0x20` is set and the actor is at
    // or below the floor (`FUN_1001beac` at z + 0x180), it takes damage equal
    // to its whole current health -- `stats->TakeDamage(stats+0x2a, 0, 0, 0)`.
    // M57 could only say the bit was "a surface you sink into": 13,064 cells
    // across six zones, and the game's own text calls one of them "the lava
    // below" (string 1963). This port walked over all of it.
    auto onDeathFloor = [&](float x, float y, float z) {
        const int tx = static_cast<int>(x) >> 8, ty = static_cast<int>(y) >> 8;
        if (!gameZone || !gameZone->InBounds(tx, ty)) return false;
        if (!(gameZone->CellAt(tx, ty).blockFlags & 0x20)) return false;
        return z <= gameZone->CollisionFloorHeightAt(x, y, z + 384.0f);
    };

    std::function<void(MonsterInstance&)> mpStrikeHook;  // M129: tag 0x1a, set below
    auto onCreatureStruck = [&](MonsterInstance& victim) {
        if (!victim.script || !victim.script->alive() || victim.script->invulnerable()) return;
        if (victim.typeId != sk_bindings::kBloodImmuneTypeId) {
            stack.level().effects().push_back(sk_bindings::MakeBloodSpurt(
                static_cast<int>(victim.x), static_cast<int>(victim.y),
                static_cast<int>(victim.z), static_cast<int>(gameCamera.x),
                static_cast<int>(gameCamera.y), [] { return std::rand(); }));
        }
        victim.flash.Arm(sk::kFlashPeriodUnits, sk::kFlashRedMin, sk::kFlashRedMax);
        if (mpStrikeHook) mpStrikeHook(victim);
    };

#if SK_DEBUG_SUITE
    // SK_DEBUG_SUITE (M68): now that every game local exists, point the
    // debug host at them and bring the suite up. Pointers to locals of
    // main() are stable for the whole run; the one thing that gets replaced
    // (the Zone) is reached through its owning unique_ptr, so a zone
    // transition does not invalidate anything here.
    debugRefs.scriptRoot = scriptRoot;
    debugRefs.strings = &strings;
    debugRefs.entityTypes = &entityTypes;
    debugRefs.modelArchive = &modelArchive;
    debugRefs.modelCollision = &gameModelCollision;
    debugRefs.stack = &stack;
    debugRefs.interpreter = &interpreter;
    debugRefs.zone = &gameZone;
    debugRefs.zoneScript = &gameZoneScript;
    debugRefs.camera = &gameCamera;
    debugRefs.monsters = &gameMonsters;
    debugRefs.doors = &gameDoors;
    debugRefs.pickups = &gamePickups;
    debugRefs.traps = &gameTraps;
    debugRefs.props = &gameEntities;
    debugRefs.projectiles = &gameProjectiles;
    debugRefs.arrows = &gameArrows;
    debugRefs.regionsOccupied = &gameRegionsOccupied;
    debugRefs.inGame = &inGame;
    debugRefs.mapOpen = &gameMapOpen;
    debugSuite.Attach(debugHost, interpreter, sk::ExecutableDirectory() + "/../debug");
    // The debug UI draws at the window's real resolution, after the game's
    // 176x208 frame has been scaled and blitted -- it never touches the
    // Backbuffer, so it cannot perturb the renderer or the tracked .ppm
    // dumps. See graphics/overlay_surface.h.
    window.SetOverlayCallback([&](sk::OverlaySurface& surface) { debugSuite.Render(surface); });
#endif

    // M119: the gameplay frame -- everything the 3D view draws, as a function
    // of the camera and render lists it is handed, so the tick and the Frame
    // Rate setting's in-between frames draw through one path. It has no side
    // effects on the game: the tick has already advanced every animation,
    // flash and timer, and chosen the prompt line, before it gets here.
    //
    // The scene goes through beginScene/endScene/presentScene, which at the
    // original Resolution are the native Backbuffer, nothing, and a plain
    // Present -- the pre-M119 frame exactly. Above it the scene is drawn at
    // a whole multiple and the 2D layer composited over it
    // (graphics/scene_upscale.h).
    int loadingBannerPulse = 0;  // M131: `controller+0x194`, never reset
    std::string gameplayLabel;  // the prompt line the tick chose, "" for none
    uint16_t gameplayLabelColor = sk::PackRGB565(255, 255, 255);  // M131, FUN_1002acd4
    // M129: a multiplayer notice shown in the prompt line's place while it
    // lasts ("Waiting for host", "<name>: Connection lost").
    std::string mpNotice;
    int mpNoticeTicks = 0;
    // M119: the last two gameplay ticks' render lists (engine/
    // frame_interpolation.h), and whether the tick that just ran ended in
    // the 3D view -- only then is there anything to draw between ticks.
    sk::FrameInterpolator gameplayFrames;
    bool tickWasGameplay = false;
    sk::HiResScene hiResScene;
    int sceneScale = 1;
    // M120: the view the frame is drawn at -- the device's 176 columns, or
    // wider at a wider Aspect Ratio -- and, when wider, the 2D layer as wide
    // as it (graphics/scene_upscale.h's ViewLayout). At the original shape
    // the 2D layer is the Backbuffer itself.
    sk::ViewLayout sceneLayout;
    sk::Backbuffer wideLayer;
    bool wideSceneValid = false;  // M130: wideLayer holds the last wide 3D frame
    auto currentViewWidth = [&]() {
        if (pcSettings.settings().originalAspect()) return sk::Backbuffer::kWidth;
        int clientW = 0, clientH = 0;
        window.ClientSize(&clientW, &clientH);
        return sk::ViewWidthFor(pcSettings.settings(), clientW, clientH);
    };
    // M129: the Direct3D renderer's side of the frame (platform/win32/
    // d3d11_renderer.h). `gpuFrame` is the triangle list the zone renderer
    // records for the card; `gpuUnder` is the native-size software frame the
    // 2D layer is drawn over, kept so the present can tell which pixels the
    // 2D code touched. That software frame is drawn once per gameplay tick
    // -- it is also where the melee probes' object IDs come from -- and
    // reused for the frames the Frame Rate setting draws in between.
    sk::GpuFrame gpuFrame;
    sk::Backbuffer gpuUnder;
    bool gpuUnderStale = true;
    auto syncGpuRenderer = [&]() {
        const bool want = pcSettings.settings().gpuRenderer() && !pcSettings.gpuUnavailable();
        if (want == window.GpuRendererActive()) return;
        if (!want) {
            window.SetGpuRenderer(false);
            std::printf("renderer: software\n");
            return;
        }
        if (window.SetGpuRenderer(true)) {
            std::printf("renderer: Direct3D 11 on %s\n", window.gpu()->adapterName().c_str());
            gpuUnderStale = true;
        } else {
            // Not available here: say so once, show Original on the row, and
            // keep drawing in software. The stored choice is left alone.
            std::printf("renderer: Direct3D 11 unavailable (%s) -- using the software renderer\n",
                        window.gpuError().c_str());
            pcSettings.SetGpuUnavailable(true);
        }
    };
    auto currentRenderScale = [&](int viewWidth) {
        // M129: with the card drawing the 3D view, the software scene is
        // only ever the native-size one under the 2D layer.
        if (window.GpuRendererActive()) return 1;
        if (pcSettings.settings().originalResolution()) return 1;
        int clientW = 0, clientH = 0;
        window.ClientSize(&clientW, &clientH);
        const sk::Viewport view =
            sk::FitPreservingAspect(clientW, clientH, viewWidth, sk::Backbuffer::kHeight);
        return sk::RenderScaleFor(pcSettings.settings(), view.height);
    };
    auto composited = [&]() { return sceneScale > 1 || sceneLayout.wide(); };
    auto sceneLayer = [&]() -> sk::Backbuffer& {
        return sceneLayout.wide() ? wideLayer : backbuffer;
    };
    auto beginScene = [&]() -> sk::Backbuffer& {
        sceneLayout.width = currentViewWidth();
        sceneScale = currentRenderScale(sceneLayout.width);
        return composited() ? hiResScene.BeginScene(sceneScale, sceneLayout.width) : backbuffer;
    };
    auto endScene = [&]() {
        if (composited()) hiResScene.EndScene(sceneLayer());
    };
    auto presentScene = [&]() {
        // M120: a wide frame leaves its middle 176 columns in the Backbuffer
        // too, so anything that later draws over "the last frame" finds the
        // picture it would have found at the original shape.
        wideSceneValid = sceneLayout.wide();  // M130: presentMenu's world bars
        if (sceneLayout.wide()) sk::CopyColumnOut(wideLayer, sceneLayout.centreX(), backbuffer);
#if SK_DEBUG_SUITE
        // SK_DEBUG_SUITE (M68): what the Resolution setting costs outside
        // the renderer -- the composite and the GDI blit.
        const sk::Backbuffer* frame = &backbuffer;
        if (composited()) {
            sk_debug::ScopedTimer composeTimer("compose");
            frame = &hiResScene.Compose(sceneLayer());
        }
        sk_debug::ScopedTimer presentTimer("present");
        window.Present(*frame);
#else
        window.Present(composited() ? hiResScene.Compose(sceneLayer()) : backbuffer);
#endif
    };
    // M130: a full-screen 2D frame -- a menu, a loading screen, the credits
    // -- at a wide Aspect Ratio. The engine's screens are 176 columns and
    // stay exactly that: nothing is stretched, re-laid-out or redrawn. What
    // changes is the two bars beside them, which were black. They are now
    // filled from the screen's own picture:
    //
    //   * over a live game (a pause menu, a popup, "Waiting for players")
    //     the bars are the 3D view that was already there, dimmed, so the
    //     panel sits on the world instead of on a letterbox;
    //   * a menu continues its own backdrop -- the `MenuBackground` sprite
    //     alone, without the rows drawn over it -- reflected outward from
    //     each edge and softened, so the parchment's colour and dark torn
    //     border carry on while any lettering in the art becomes tone
    //     instead of reading backwards. It falls from half brightness
    //     beside the panel to a quarter at the window's edge, which keeps
    //     the eye on the 176 columns the artists drew;
    //   * a screen with no separate backdrop (a loading picture, the
    //     credits) does the same with the frame itself.
    //
    // At the original shape this is window.Present, untouched.
    sk::Backbuffer menuWide;
    sk::Backbuffer menuBackdrop;  // a menu's MenuBackground sprite on its own
    sk::Backbuffer menuSoft;      // the softened frame, when there is none
    auto softenInto = [&](const sk::Backbuffer& src, sk::Backbuffer& dst) {
        // A separable box average, 17 wide: enough that 8-pixel lettering
        // becomes tone, cheap enough to run on every menu frame.
        constexpr int kRadius = 8;
        const int w = src.width(), h = src.height();
        dst.Resize(w, h);
        std::vector<uint16_t> pass(static_cast<size_t>(w) * h);
        auto average = [&](auto read, auto write, int count, int lines) {
            for (int line = 0; line < lines; ++line) {
                for (int i = 0; i < count; ++i) {
                    int r = 0, g = 0, b = 0, n = 0;
                    for (int k = -kRadius; k <= kRadius; ++k) {
                        const int j = i + k;
                        if (j < 0 || j >= count) continue;
                        const uint16_t c = read(line, j);
                        r += (c >> 11) & 0x1f;
                        g += (c >> 5) & 0x3f;
                        b += c & 0x1f;
                        ++n;
                    }
                    write(line, i, static_cast<uint16_t>(((r / n) << 11) | ((g / n) << 5) | (b / n)));
                }
            }
        };
        average([&](int y, int x) { return src.Row(y)[x]; },
                [&](int y, int x, uint16_t c) { pass[static_cast<size_t>(y) * w + x] = c; }, w, h);
        average([&](int x, int y) { return pass[static_cast<size_t>(y) * w + x]; },
                [&](int x, int y, uint16_t c) { dst.Row(y)[x] = c; }, h, w);
    };
    auto dim565 = [](uint16_t c, int k256) -> uint16_t {
        const int r = ((c >> 11) & 0x1f) * k256 >> 8;
        const int g = ((c >> 5) & 0x3f) * k256 >> 8;
        const int b = (c & 0x1f) * k256 >> 8;
        return static_cast<uint16_t>((r << 11) | (g << 5) | b);
    };
    auto presentMenu = [&](const sk::Backbuffer& native, bool overGame,
                           const sk::Backbuffer* backdrop = nullptr) {
        const int width = currentViewWidth();
        if (width <= sk::Backbuffer::kWidth || native.width() != sk::Backbuffer::kWidth) {
            window.Present(native);
            return;
        }
        constexpr int kNear = 128, kFar = 64;  // of 256: beside the panel, at the edge
        menuWide.Resize(width, sk::Backbuffer::kHeight);
        const int x0 = (width - sk::Backbuffer::kWidth) / 2;
        const int rightStart = x0 + sk::Backbuffer::kWidth;
        const bool scene = overGame && wideSceneValid && wideLayer.width() == width &&
                           wideLayer.height() == sk::Backbuffer::kHeight;
        // Softened either way: the main menu's logo is part of its
        // backdrop sprite, and would otherwise read backwards.
        const sk::Backbuffer* margin = nullptr;
        if (!scene) {
            softenInto(backdrop ? *backdrop : native, menuSoft);
            margin = &menuSoft;
        }
        auto level = [&](int distance, int span) {
            return span <= 1 ? kNear : kNear - (kNear - kFar) * distance / (span - 1);
        };
        auto mirrored = [](int distance) {
            // Reflect at the panel's edge, and again a panel further out.
            const int period = 2 * sk::Backbuffer::kWidth;
            const int d = distance % period;
            return d < sk::Backbuffer::kWidth ? d : period - 1 - d;
        };
        for (int y = 0; y < sk::Backbuffer::kHeight; ++y) {
            const uint16_t* panel = native.Row(y);
            const uint16_t* src = scene ? panel : margin->Row(y);
            uint16_t* dst = menuWide.Row(y);
            const uint16_t* world = scene ? wideLayer.Row(y) : nullptr;
            for (int x = 0; x < x0; ++x) {
                const int d = x0 - 1 - x;
                dst[x] = dim565(world ? world[x] : src[mirrored(d)], level(d, x0));
            }
            std::copy_n(panel, sk::Backbuffer::kWidth, dst + x0);
            for (int x = rightStart; x < width; ++x) {
                const int d = x - rightStart;
                dst[x] = dim565(world ? world[x]
                                      : src[sk::Backbuffer::kWidth - 1 - mirrored(d)],
                                level(d, width - rightStart));
            }
        }
        window.Present(menuWide);
    };
    auto menuBackdropFor = [&](sk_bindings::MenuExecutable* menu) -> const sk::Backbuffer* {
        if (!menu || menu->backgroundId() < 0) return nullptr;
        const sk::Sprite* sprite = spriteArchive.GetSprite(menu->backgroundId());
        if (!sprite) return nullptr;
        menuBackdrop.Resize(sk::Backbuffer::kWidth, sk::Backbuffer::kHeight);
        menuBackdrop.Fill(kBackgroundColor);
        menuBackdrop.Blit(0, 0, *sprite);
        return &menuBackdrop;
    };
    // The 2D layer over the scene. `overlays` adds what only the live 3D
    // view draws -- the map, the coordinate readout, the prompt line.
    //
    // M120: at the original shape this is the pre-M120 sequence of draws,
    // into the Backbuffer. Wider, the viewmodel and HUD draw straight into
    // the wide layer at their ViewLayout places, and the overlays -- which
    // are native-only code (text, the automap) and belong in the middle --
    // draw into the Backbuffer holding the layer's middle 176 columns, which
    // is then put back.
    auto drawHudLayer = [&](const sk::Camera& camera, bool overlays) {
        sk::Backbuffer& layer = sceneLayer();
        // M79: **the viewmodel goes under the HUD, not over it.**
        // `FUN_10029cb0`'s in-game arm draws them in one fixed
        // order, and the viewmodel is first:
        //
        //     if (player->+0x204) FUN_1002b1b0(this);  // viewmodel
        //     FUN_1002acd4(this);
        //     if (screenMode == 5) {
        //         FUN_1002ae88(this);   // the three vitals bars
        //         FUN_1002ba64(this);   // the compass banner
        //         FUN_1002bb54(this);   // the hand icons
        //     }
        //     if (player->+0x3a4) FUN_1002b430(this);   // the map
        //
        // This port drew the HUD first and the viewmodel over it,
        // which nothing noticed while every viewmodel was a weapon
        // held bottom-right. A spell's is a pair of hands that
        // reaches across the bottom of the frame, so it covered the
        // vitals bars -- reported the moment spells got their art.
        // The same `if (player->+0x204)` guard is why the draw can
        // dereference the active item without a null check.
        RenderWeaponViewmodel(layer, gameWeaponViewmodel, spriteArchive, sceneLayout);
        RenderHud(layer, stack.player(), spriteArchive, camera.yaw, stack.player().hurtTimer(),
                  sceneLayout);
        if (!overlays) return;
        const bool levelUpCaption = stack.player().levelUpPoints() > 0;  // post-M120
        // M131: **the status column**, `FUN_1002c0e4` (its body is the
        // stats block's own draw at 0x10047a..). Nothing while the map is
        // up. Otherwise, in white, 15 pixels a line:
        //
        //   y = 0x23   what the second periodic channel (`stats+0x7c`) is
        //              running: "Mystic Might:" / "Precise Magic:" /
        //              "Thief Lore:" / "Sanctuary" / "Lethal Strike:" /
        //              "+Fatigue" / "+Health" / "-Health" (the burn) /
        //              "Snowray Powder" -- kinds 1..9;
        //   y = 0x32.. "Blind", "Poison", "Disease" for flag bits 4, 8,
        //              0x10 of `stats+0x44`, then "Paralyze";
        //   then       every timed effect but ItemUsed: "+" or "-" at x=7
        //              by the sign of its stored value, and the stat's
        //              name (string 3044 + stat) at x=15.
        //
        // The port tracked all of it and drew none of it: a poisoned or
        // blinded player was never told.
        struct StatusLine {
            int y;
            std::string sign, text;
        };
        std::vector<StatusLine> statusLines;
        if (!gameMapOpen) {
            const sk_bindings::ActorStats& stats = stack.player().actorStats();
            std::string first;
            switch (stats.periodicKind()) {
                case 1: first = strings.Get(43); break;
                case 2: first = strings.Get(47); break;
                case 3: first = strings.Get(49); break;
                case 4: first = strings.Get(2488); break;
                case 5: first = strings.Get(41); break;
                case 6: first = "+" + strings.Get(3065); break;
                case 7: first = "+" + strings.Get(3064); break;
                case 8: first = "-" + strings.Get(3064); break;
                case 9: first = strings.Get(1731); break;
                default: break;
            }
            if (!first.empty()) statusLines.push_back({0x23, std::string(), first});
            int y = 0x32;
            auto flagLine = [&](bool on, int stringId) {
                if (!on) return;
                statusLines.push_back({y, std::string(), strings.Get(stringId)});
                y += 0xf;
            };
            flagLine(stats.blinded(), 2401);
            flagLine(stats.poisoned(), 2473);
            flagLine((stats.effectFlags() & sk_bindings::ActorStats::kEffectFlagDisease) != 0, 2432);
            flagLine(stats.paralyzed(), 2469);
            for (const sk_bindings::Effect& e : stats.timedEffects()) {
                if (e.stat == sk_bindings::kEffectStatItemUsed) continue;
                statusLines.push_back({y, static_cast<int16_t>(e.stored) < 0 ? "-" : "+",
                                       strings.Get(3044 + e.stat)});
                y += 0xf;
            }
        }
        const bool column = sceneLayout.wide() && (gameMapOpen || stack.player().coordsEnabled() ||
                                                   levelUpCaption || !gameplayLabel.empty() ||
                                                   mpNoticeTicks > 0 || !statusLines.empty());
        if (column) sk::CopyColumnOut(wideLayer, sceneLayout.centreX(), backbuffer);
        // M57: last, over everything -- the map is an overlay, and
        // the game underneath it keeps running.
        if (gameMapOpen) {
            RenderAutomapOverlay(backbuffer, *gameZone, gameExplored, camera,
                                  spriteArchive, strings, stack.currentLevelName());
        }
        // Post-M120: **the "Level Up" prompt.** `FUN_10029cb0`, straight
        // after the map: `if (player+0xf44 > 0) FUN_1002c1a8(hud)`, and
        // that is one call -- `FUN_1008f97c(gc, stringTable[3842], 0xbe,
        // 0xfff, 0, 1, 0, 0)`: "Level Up", centred, at y=190, white, no
        // shadow, in the legend face. It stays up for as long as a point
        // is unspent, and it is the only thing that tells the player the
        // character key (action 12) now opens levelup.s.
        if (levelUpCaption) {
            constexpr int kLevelUpStringId = 3842;
            constexpr int kLevelUpCaptionY = 0xbe;
            const std::string caption = strings.Get(kLevelUpStringId);
            sk::BitmapFont::DrawString(
                backbuffer, (sk::Backbuffer::kWidth - sk::BitmapFont::TextWidth(caption)) / 2,
                kLevelUpCaptionY, caption, MenuColor444(0xfff));
        }
        // M95: `EnableCoords()` -- `FUN_1002c104`, drawn straight
        // after the map in `FUN_10029cb0` (and so over it; unlike the
        // HUD text layer it has no map-open early-out). Its whole
        // body is `sprintf("%d %d", player->x, player->y)` handed
        // to the UI text draw at (0x14, 0x14) in 0xfff, raw world
        // units -- the same numbers a script's SetPosition takes,
        // which is the point of a teleport cheat's readout.
        if (stack.player().coordsEnabled()) {
            char coords[32];
            std::snprintf(coords, sizeof(coords), "%d %d",
                          static_cast<int>(gameCamera.x), static_cast<int>(gameCamera.y));
            sk::BitmapFont::DrawString(backbuffer, 0x14, 0x14, coords,
                                        sk::PackRGB565(255, 255, 255));
        }
        for (const StatusLine& line : statusLines) {
            const uint16_t white = MenuColor444(0xfff);
            if (!line.sign.empty()) {
                sk::BitmapFont::DrawString(backbuffer, 7, line.y, line.sign, white);
            }
            sk::BitmapFont::DrawString(backbuffer, 0xf, line.y, line.text, white);
        }
        if (!gameplayLabel.empty()) {
            // M131: `FUN_1002acd4` -- the engine's text box, centred between
            // x=10 and x=166 at y=30, wrapped at 25 characters. See
            // simkin_bindings/use_prompt.h.
            sk::DrawTextBox(backbuffer, gameplayLabel, sk_bindings::kUsePromptX,
                            sk_bindings::kUsePromptY, sk_bindings::kUsePromptRight,
                            gameplayLabelColor, sk::TextBoxAlign::Centre,
                            sk_bindings::kUsePromptWrapChars, sk::BitmapFont::Face::SmallItalic);
        } else if (mpNoticeTicks > 0 && !mpNotice.empty()) {
            // M129: a multiplayer notice -- the other player left, or fell.
            sk::BitmapFont::DrawString(backbuffer, 4, 34, mpNotice, MenuColor444(0xfff));
        }
        if (column) sk::CopyColumnIn(backbuffer, wideLayer, sceneLayout.centreX());
    };
    // M129: the gameplay frame on the graphics card. Returns false when the
    // card could not draw or present it, and the caller draws in software.
    //
    //   1. The zone renderer records the frame's triangles at the size the
    //      Resolution setting asks for -- any size, Auto being the window's
    //      own -- and the card draws them into its scene target.
    //   2. The 2D layer is drawn exactly as it always is, into the native
    //      Backbuffer (or the wide layer), over a native-size *software*
    //      frame: some of the 2D art blends with what is under it, and the
    //      melee probes read that frame's object IDs. It is redrawn once per
    //      gameplay tick and reused in between.
    //   3. The present shows the card's scene, and over it every native
    //      pixel the 2D code changed -- scene_upscale.h's Compose rule.
    auto drawGameplayFrameGpu = [&](const sk::Camera& camera,
                                    const std::vector<sk::PlacedEntity>& entities,
                                    const std::vector<sk::SpriteBillboard>& billboards) {
        sk::D3D11Renderer* gpu = window.gpu();
        if (!gpu) return false;
        const sk::PcSettings& pc = pcSettings.settings();
        sceneLayout.width = currentViewWidth();
        sceneScale = 1;
        int clientW = 0, clientH = 0;
        window.ClientSize(&clientW, &clientH);
        if (clientW <= 0 || clientH <= 0) return true;  // minimized: nothing to draw
        const sk::Viewport view = sk::FitPreservingAspect(clientW, clientH, sceneLayout.width,
                                                          sk::Backbuffer::kHeight);
        int sceneW = 0, sceneH = 0;
        sk::GpuSceneSizeFor(pc, sceneLayout.width, view.width, view.height, &sceneW, &sceneH);
        zoneRenderer.fogPercent = pc.fogPercent();
        {
#if SK_DEBUG_SUITE
            sk_debug::Count("render.frames");
            sk_debug::ScopedTimer renderTimer("render3d");
#endif
            zoneRenderer.RenderGpu(gpuFrame, sceneW, sceneH, *gameZone, camera, entities,
                                   &modelArchive, billboards);
            if (!gpu->DrawScene(gpuFrame, pc.smoothTextures() ? sk::D3D11Renderer::Filter::Smooth
                                                              : sk::D3D11Renderer::Filter::Original)) {
                return false;
            }
        }
        sk::Backbuffer& layer = sceneLayer();
        layer.Resize(sceneLayout.width, sk::Backbuffer::kHeight);
        if (gpuUnderStale || gpuUnder.width() != layer.width() ||
            gpuUnder.height() != layer.height()) {
            zoneRenderer.Render(layer, *gameZone, camera, entities, &modelArchive, billboards);
            gpuUnder = layer;
            gpuUnderStale = false;
        } else {
            layer = gpuUnder;
        }
        drawHudLayer(camera, /*overlays=*/true);
        if (sceneLayout.wide()) sk::CopyColumnOut(wideLayer, sceneLayout.centreX(), backbuffer);
#if SK_DEBUG_SUITE
        sk_debug::ScopedTimer presentTimer("present");
#endif
        return window.PresentGpuScene(layer, gpuUnder);
    };
    auto drawGameplayFrame = [&](const sk::Camera& camera,
                                 const std::vector<sk::PlacedEntity>& entities,
                                 const std::vector<sk::SpriteBillboard>& billboards) {
        if (window.GpuRendererActive() && drawGameplayFrameGpu(camera, entities, billboards)) {
            return;
        }
        sk::Backbuffer& scene = beginScene();
        zoneRenderer.fogPercent = pcSettings.settings().fogPercent();  // M123
#if SK_DEBUG_SUITE
        // SK_DEBUG_SUITE (M68): a counter and a timer around the 3D pass.
        sk_debug::Count("render.frames");
        {
            sk_debug::ScopedTimer renderTimer("render3d");
            zoneRenderer.Render(scene, *gameZone, camera, entities, &modelArchive, billboards);
        }
#else
        zoneRenderer.Render(scene, *gameZone, camera, entities, &modelArchive, billboards);
#endif
        endScene();
        drawHudLayer(camera, /*overlays=*/true);
        presentScene();
    };

    // One 40ms engine tick: input, simulation, scripts -- and, at the
    // original frame rate, the frame it produces. Everything below was the
    // message loop's own body until M119 split out the pacing around it.

    // =====================================================================
    // M129: multiplayer.
    //
    // The original's own design (docs/MULTIPLAYER_PROTOCOL.md) carried over a
    // socket: two players, each process running the whole game, the session
    // (net/mp_session.h) mirroring events between them. Everything here is
    // the game's side of that -- what the session asks about this game and
    // what each arriving event does to it.
    //
    // What is synchronised is what the original synchronised: each player's
    // own movement (dead-reckoned between packets), and events. Creature
    // positions and health are not on the wire in the shipped protocol
    // either; a kill, a hit, a door, an object leaving the world, a quest
    // flag, shared experience, a mirrored script method and a level change
    // are.
    // =====================================================================
    sk_net::MpWorldFns mpWorld;
    sk_net::MpSession mpSession(mpWorld);
    struct RemoteAvatar {
        bool present = false;
        float x = 0, y = 0, z = 0;
        int vx = 0, vy = 0, vz = 0;  // engine units: position moves by v * delta >> 8
        int heading = 0;             // engine heading, 0x10000 to the turn
        int race = 0, sex = 0, classId = 0;
        std::string name;
        int weapon = 0;
        int clip = 8;                // 5 moving, 8 still, 1/2 attacking, 6 dead
        float animTime = 0.0f;
        int attackTicks = 0;
        bool dead = false;
    } mpAvatar;
    int mpSlot = -1;              // the save whose character plays (MPPreHost / MPPreJoin)
    std::string mpNotifiedLevel;  // the level the session was last told is loaded
    bool mpLoadFromHost = false;  // client: the pending zone change is the host's
    bool mpAnnounced = false;     // host: the pending zone change has been announced
    sk_net::MpStoryState mpIncomingStory;
    bool mpHaveStory = false;
    std::vector<sk_net::MpEntityRecord> mpIncoming;
    std::string mpIncomingLevel;
    int mpHostNextNetId = 0;
    int gameNextNetId = 0;
    std::vector<int> mpRestoreNetIds;  // client: the host's ids, in level-record order
    std::set<int> mpKnownGone;         // ids both players already know have left the world
    // M130: what each container held last tick, by net id (tag 0x31).
    std::map<int, std::vector<int>> mpContainerContents;
    std::set<int> mpLiveIds;           // last tick's live ids, for the "left the world" sweep
    std::set<int> mpRemoteKills;
    float mpPrevX = 0.0f, mpPrevY = 0.0f, mpPrevZ = 0.0f;
    int mpVelX = 0, mpVelY = 0, mpVelZ = 0;
    bool mpWasDead = false;
    bool mpAttackVisualDue = false;

    // The level index tag 0x00 / 0x2e carry. The engine's own table
    // (FUN_10039ca4) was not transcribed; both ends of this port use the
    // shipped zones in name order.
    static const char* const kMpLevels[] = {
        "azra",     "broken1",  "broken2", "crypt1",   "crypt2",       "crypt3", "delfhide",
        "drgnfld",  "dstar_e",  "dstar_w", "erthcave", "fearfrst",     "ffarena", "ghstpass",
        "glaciercrawl", "lakvan", "lothcav", "raiders", "snowline",    "stouttp", "twilite"};
    constexpr int kMpLevelCount = static_cast<int>(sizeof(kMpLevels) / sizeof(kMpLevels[0]));

    auto mpCall = [&](skiExecutable& target, const char* name,
                      std::initializer_list<skRValue> list) -> skRValue {
        skRValueArray args;
        for (const skRValue& v : list) args.append(v);
        skRValue ret;
        skExecutableContext ctxt(&interpreter);
        try {
            target.method(skString(name), args, ret, ctxt);
        } catch (skRuntimeException& ex) {
            std::printf("multiplayer: %s(): %s\n", name, ex.toString().ptr());
        } catch (skParseException& ex) {
            std::printf("multiplayer: %s(): %s\n", name, ex.toString().ptr());
        } catch (skTreeNodeReaderException& ex) {
            std::printf("multiplayer: %s(): %s\n", name, ex.toString().ptr());
        }
        return ret;
    };
    auto mpLocalName = [&]() -> std::string {
        const std::string name =
            sk_bindings::ToStdString(mpCall(stack.player(), "GetPlayerName", {}).str());
        return name.empty() ? sk_net::MpLocalMachineName() : name;
    };
    auto mpSay = [&](const std::string& text) {
        mpNotice = text;
        mpNoticeTicks = 25 * 5;
        std::printf("multiplayer: %s\n", text.c_str());
    };
    auto mpAssignNetIds = [&]() {
        for (DoorInstance& d : gameDoors) {
            if (d.script && d.script->netId() == 0) d.script->SetNetId(++gameNextNetId);
        }
        for (PickupInstance& pk : gamePickups) {
            if (pk.script && pk.script->netId() == 0) pk.script->SetNetId(++gameNextNetId);
        }
        for (MonsterInstance& m : gameMonsters) {
            if (m.script && m.script->netId() == 0) m.script->SetNetId(++gameNextNetId);
        }
    };
    auto mpFindMonster = [&](int id) -> MonsterInstance* {
        for (MonsterInstance& m : gameMonsters) {
            if (m.script && m.script->netId() == id) return &m;
        }
        return nullptr;
    };
    auto mpFindDoor = [&](int id) -> DoorInstance* {
        for (DoorInstance& d : gameDoors) {
            if (d.script && d.script->netId() == id) return &d;
        }
        return nullptr;
    };
    auto mpFindPickup = [&](int id) -> PickupInstance* {
        for (PickupInstance& pk : gamePickups) {
            if (pk.script && pk.script->netId() == id) return &pk;
        }
        return nullptr;
    };
    // Any object, as the two things an event needs of it.
    struct MpObject {
        skiExecutable* executable = nullptr;
        sk_bindings::EntityBaseRef* base = nullptr;
    };
    auto mpFindObject = [&](int id) -> MpObject {
        if (id <= 0) return MpObject{};
        if (MonsterInstance* m = mpFindMonster(id)) return MpObject{m->script.get(), m->script.get()};
        if (DoorInstance* d = mpFindDoor(id)) return MpObject{d->script.get(), d->script.get()};
        if (PickupInstance* pk = mpFindPickup(id)) return MpObject{pk->script.get(), pk->script.get()};
        return MpObject{};
    };

    // ---- what the session asks ----
    mpWorld.levelIndex = [&](const std::string& name) {
        for (int i = 0; i < kMpLevelCount; ++i) {
            if (_stricmp(kMpLevels[i], name.c_str()) == 0) return i;
        }
        return 0;
    };
    mpWorld.levelName = [&](int index) {
        return std::string(kMpLevels[index >= 0 && index < kMpLevelCount ? index : 0]);
    };
    mpWorld.localPlayerState = [&]() {
        sk_net::MpPlayerState st;
        st.x = static_cast<int32_t>(gameCamera.x);
        st.y = static_cast<int32_t>(gameCamera.y);
        // The player's own z is their feet; the camera is an eye height up.
        st.z = static_cast<int16_t>(gameCamera.z - sk::kEyeHeightOffset);
        st.vx = mpVelX;
        st.vy = mpVelY;
        st.vz = static_cast<int16_t>(mpVelZ);
        st.heading = static_cast<int16_t>(CameraHeadingUnits(gameCamera.yaw) & 0xffff);
        st.pitch = static_cast<int16_t>(gameCamera.pitch / 6.28318530718f * 65536.0f);
        return st;
    };
    mpWorld.localPlayerInfo = [&]() {
        sk_net::MpPlayerInfo info;
        info.classId = stack.player().characterClass();
        info.race = stack.player().race();
        info.sex = stack.player().sex();
        info.name = mpLocalName();
        return info;
    };
    mpWorld.localHeldWeapon = [&]() {
        const sk_bindings::ItemExecutable* held = stack.player().rightItem();
        return held ? held->templateId() : 0;
    };
    mpWorld.buildStoryState = [&]() {
        sk_net::MpStoryState story;
        if (gameZone) {
            sk::SavedLevelState state;
            sk::SaveStream in;
            in.Reset(snapshotLevelState());
            state.Read(in, savedKindForType);
            sk::SaveStream out;
            state.level.Write(out);
            story.levelRecord = out.bytes();
        }
        for (int i = 0; i < 256; ++i) {
            story.assigned[i] = stack.player().questAssigned(i) ? 1 : 0;
            story.completed[i] = stack.player().questCompleted(i) ? 1 : 0;
            story.solved[i] = stack.player().questSolved(i) ? 1 : 0;
        }
        for (int i = 0; i < 18 && i < sk::SavedPlayer::kTrailingInts; ++i) {
            skRValue v;
            if (stack.player().getValue(skString(sk::SavedPlayer::kGlobalFlagNames[i]), skString(""),
                                        v)) {
                story.savedInts[i] = v.intValue();
            }
        }
        return story;
    };
    mpWorld.buildEntityStream = [&]() {
        std::vector<sk_net::MpEntityRecord> records;
        if (!gameZone) return records;
        mpAssignNetIds();
        std::vector<int> ids;
        snapshotNetIdSink = &ids;
        const std::vector<uint8_t> bytes = snapshotLevelState();
        snapshotNetIdSink = nullptr;
        sk::SavedLevelState state;
        sk::SaveStream in;
        in.Reset(bytes);
        state.Read(in, savedKindForType);
        for (size_t i = 0; i < state.entities.size() && i < ids.size(); ++i) {
            sk::SaveStream chain;
            state.entities[i].Write(chain);
            sk_net::MpEntityRecord rec;
            rec.typeId = state.entities[i].typeId;
            rec.entityId = ids[i];
            rec.chain = chain.bytes();
            records.push_back(std::move(rec));
        }
        // Last, the number the host's next new object will take, so
        // anything either game creates from here on is numbered alike.
        sk_net::MpEntityRecord counter;
        counter.typeId = -1;
        counter.entityId = gameNextNetId;
        records.push_back(counter);
        return records;
    };

    // ---- joining ----
    mpWorld.onGameStart = [&](const std::string& level) {
        mpIncomingLevel = level;
        mpIncoming.clear();
        mpHaveStory = false;
        mpNotifiedLevel.clear();
        mpAvatar = RemoteAvatar{};
        mpSay(strings.Get(0x13));  // "Waiting for host"
    };
    mpWorld.onStoryState = [&](const sk_net::MpStoryState& story) {
        // Tag 0x29: the host's story replaces the client's -- all three
        // quest arrays and the eighteen saved_* ints.
        mpIncomingStory = story;
        mpHaveStory = true;
        sk_bindings::MpRemoteScope remote;
        for (int i = 0; i < 256; ++i) {
            if ((story.assigned[i] != 0) != stack.player().questAssigned(i)) {
                mpCall(stack.player(), "SetQuestAssigned", {skRValue(i), skRValue(story.assigned[i] != 0)});
            }
            if ((story.completed[i] != 0) != stack.player().questCompleted(i)) {
                mpCall(stack.player(), "SetQuestCompleted", {skRValue(i), skRValue(story.completed[i] != 0)});
            }
            if ((story.solved[i] != 0) != stack.player().questSolved(i)) {
                mpCall(stack.player(), "SetQuestSolved", {skRValue(i), skRValue(story.solved[i] != 0)});
            }
        }
        for (int i = 0; i < 18 && i < sk::SavedPlayer::kTrailingInts; ++i) {
            stack.player().setValue(skString(sk::SavedPlayer::kGlobalFlagNames[i]), skString(""),
                                    skRValue(static_cast<int>(story.savedInts[i])));
        }
    };
    mpWorld.onSpawnEntity = [&](const sk_net::MpEntityRecord& rec) { mpIncoming.push_back(rec); };
    mpWorld.onPlaceAndStart = [&](const sk_net::MpPlaceAndStart& place) {
        // The host's world has arrived: its level record (0x29) and one
        // record per entity (0x1d). Together they are exactly a
        // `<level>.dat` -- so they become this level's member of current.sav
        // and the level is loaded the way a saved level always is (M121).
        sk::SavedLevelState state;
        state.renderEnabled = 1;
        if (mpHaveStory && !mpIncomingStory.levelRecord.empty()) {
            sk::SaveStream in;
            in.Reset(mpIncomingStory.levelRecord);
            state.level.Read(in);
        }
        mpRestoreNetIds.clear();
        mpHostNextNetId = 0;
        for (const sk_net::MpEntityRecord& rec : mpIncoming) {
            if (rec.typeId < 0) {
                mpHostNextNetId = rec.entityId;
                continue;
            }
            sk::SavedEntity e;
            e.typeId = rec.typeId;
            e.kind = savedKindForType(rec.typeId);
            sk::SaveStream in;
            in.Reset(rec.chain);
            e.Read(in, savedKindForType);
            if (in.failed()) continue;
            state.entities.push_back(std::move(e));
            mpRestoreNetIds.push_back(rec.entityId);
        }
        mpIncoming.clear();
        sk::SaveStream out;
        state.Write(out);
        stack.StoreLevelState(mpIncomingLevel, out.bytes());
        // Beside the host: tag 0x06 is the host's own position.
        stack.ArmCameraStart(place.x, place.y, place.z, 0, static_cast<uint16_t>(place.heading), 0);
        mpLoadFromHost = true;
        mpNotifiedLevel.clear();
        stack.RequestZoneChange(mpIncomingLevel);
        std::printf("multiplayer: joining %s (%zu entities from the host)\n",
                    mpIncomingLevel.c_str(), mpRestoreNetIds.size());
    };
    mpWorld.onPlayerLeft = [&](int, const std::string& name) {
        mpAvatar = RemoteAvatar{};
        mpSay((name.empty() ? std::string() : name + ": ") + strings.Get(3811));  // "Connection lost"
    };
    mpWorld.onTravelRequest = [&](const sk_net::MpTravel& travel) {
        // Tag 0x2e, host only: the client walked through a door. The host
        // takes the same door, and announces the level (0x00).
        if (travel.overrideArmed) {
            stack.ArmCameraStart(travel.spawn[0], travel.spawn[1], travel.spawn[2],
                                 travel.spawn[4], travel.spawn[3], travel.spawn[5]);
        }
        stack.RequestZoneChange(kMpLevels[travel.levelIndex >= 0 && travel.levelIndex < kMpLevelCount
                                              ? travel.levelIndex
                                              : 0]);
    };

    // ---- the other player ----
    mpWorld.onPlayerState = [&](const sk_net::MpPlayerState& st) {
        // Tag 0x01. The avatar is created on first sight and from then on
        // coasts on the velocity between packets.
        mpAvatar.present = true;
        mpAvatar.x = static_cast<float>(st.x);
        mpAvatar.y = static_cast<float>(st.y);
        mpAvatar.z = static_cast<float>(st.z) + 128.0f;  // the receiver's own `z + 0x80`
        mpAvatar.vx = st.vx;
        mpAvatar.vy = st.vy;
        mpAvatar.vz = st.vz;
        mpAvatar.heading = static_cast<uint16_t>(st.heading);
    };
    mpWorld.onPlayerInfo = [&](const sk_net::MpPlayerInfo& info) {
        mpAvatar.race = info.race;
        mpAvatar.sex = info.sex;
        mpAvatar.classId = info.classId;
        mpAvatar.name = info.name;
    };
    mpWorld.onHeldWeapon = [&](int, int weapon) { mpAvatar.weapon = weapon; };
    mpWorld.onAttackVisual = [&](int, int anim, int sound) {
        // Tag 0x18: the swing and its noise. No damage -- that is 0x19/0x1a.
        mpAvatar.clip = anim == 2 ? 2 : 1;
        mpAvatar.animTime = 0.0f;
        mpAvatar.attackTicks = 15;
        if (sound == 1) playWorldSound(599, mpAvatar.x, mpAvatar.y);
        if (sound == 2) playWorldSound(0x256, mpAvatar.x, mpAvatar.y);
    };
    mpWorld.onAnimation = [&](int anim, int) {
        if (anim == 6) {  // death: held, nothing animates it after that
            mpAvatar.dead = true;
            mpAvatar.clip = 6;
            mpAvatar.animTime = 0.0f;
        }
    };
    mpWorld.onTextMessage = [&](int, const std::string& text) { mpSay(text); };
    mpWorld.onDamageMe = [&](int amount) { stack.player().ApplyDamage(amount); };

    // ---- creatures and objects ----
    mpWorld.onKill = [&](int id) {
        // Tag 0x19: `DoDamage(5000, localPlayer, 1, 0)` on this game's copy.
        MonsterInstance* m = mpFindMonster(id);
        if (!m || !m->script->alive()) return;
        mpRemoteKills.insert(id);
        sk_bindings::MpRemoteScope remote;
        m->script->ApplyDamage(5000, &stack.player(), /*ranged=*/false);
    };
    mpWorld.onHitForZero = [&](int id) {
        // Tag 0x1a: the hurt reaction without the damage.
        MonsterInstance* m = mpFindMonster(id);
        if (!m || !m->script->alive()) return;
        m->flash.Arm(sk::kFlashPeriodUnits, sk::kFlashRedMin, sk::kFlashRedMax);
        m->hasTarget = true;
    };
    auto mpRemove = [&](int id) {
        const MpObject object = mpFindObject(id);
        mpKnownGone.insert(id);
        if (!object.base) return;
        sk_bindings::MpRemoteScope remote;
        object.base->RemoveFromWorld();
    };
    mpWorld.onTouched = mpRemove;
    mpWorld.onDestroyObject = mpRemove;
    mpWorld.onDoor = [&](int id, bool close) {
        // Tag 0x1f: the receiver turns the door a quarter, plays sound 0x3f
        // and flips the door script's state -- natively, not by running the
        // script's OnUse (which could ask this player for a key).
        DoorInstance* d = mpFindDoor(id);
        if (!d) return;
        const bool open = !close;
        skRValue state;
        const bool has = d->script->getValue(skString("saved_Open"), skString(""), state);
        if (has && (state.intValue() != 0) == open) return;
        sk_bindings::MpRemoteScope remote;
        mpCall(*d->script, "SetPassable", {skRValue(true)});
        mpCall(*d->script, "AddRotationTurn", {skRValue(open ? -0x4000 : 0x4000)});
        mpCall(*d->script, "SetPassable", {skRValue(open)});
        d->script->setValue(skString("saved_Open"), skString(""), skRValue(open ? 1 : 0));
        mpCall(*d->script, "SetUseText", {skRValue(open ? 2989 : 941)});
        playWorldSound(0x3f, d->x, d->y);
    };
    mpWorld.onSetPosition = [&](const sk_net::MpSetPosition& pos) {
        sk_bindings::MpRemoteScope remote;
        if (pos.everyone) {
            // A group teleport: this player goes there, and so does the
            // other one's avatar.
            mpCall(stack.player(), "SetPosition", {skRValue(pos.x), skRValue(pos.y), skRValue(pos.z)});
            mpAvatar.x = static_cast<float>(pos.x);
            mpAvatar.y = static_cast<float>(pos.y);
            mpAvatar.vx = mpAvatar.vy = mpAvatar.vz = 0;
            return;
        }
        const MpObject object = mpFindObject(pos.entityId);
        if (object.executable) {
            mpCall(*object.executable, "SetPosition", {skRValue(pos.x), skRValue(pos.y), skRValue(pos.z)});
        }
    };
    mpWorld.onMirrorMethod = [&](int id, const std::string& method) {
        // Tag 0x34: run the named script method on this game's copy.
        const MpObject object = mpFindObject(id);
        if (!object.executable) return;
        sk_bindings::MpRemoteScope remote;
        mpCall(*object.executable, method.c_str(), {skRValue(0)});
    };
    mpWorld.onInvulnerable = [&](int id, bool flag) {
        const MpObject object = mpFindObject(id);
        if (!object.executable) return;
        sk_bindings::MpRemoteScope remote;
        mpCall(*object.executable, "SetInvulnerable", {skRValue(flag)});
    };
    mpWorld.onCreatureTarget = [&](int id, int playerIndex) {
        // Tag 0x1e: `target(+0x20c)` = that player, package = pursue. A
        // creature that picked *this* player is awake here too; one that
        // picked the other goes for the other's avatar (M130), as the
        // engine's copy does -- its blows land in the other game.
        MonsterInstance* m = mpFindMonster(id);
        if (!m) return;
        m->hasTarget = true;
        m->mpTargetsPeer = playerIndex != mpSession.myIndex();
        m->mpTargetSent = true;
    };
    mpWorld.onAlerted = [&](int id) {
        if (MonsterInstance* m = mpFindMonster(id)) m->hasTarget = true;
    };
    mpWorld.onNoticed = [&](int id) {
        const MpObject object = mpFindObject(id);
        if (!object.executable) return;
        sk_bindings::MpRemoteScope remote;
        mpCall(*object.executable, "OnDetect", {skRValue(0)});
    };

    // ---- world and story ----
    mpWorld.onQuestFlag = [&](int kind, int quest, int value) {
        sk_bindings::MpRemoteScope remote;
        const char* setter = kind == 0 ? "SetQuestAssigned"
                                       : kind == 1 ? "SetQuestSolved" : "SetQuestCompleted";
        mpCall(stack.player(), setter, {skRValue(quest), skRValue(value != 0)});
    };
    mpWorld.onShareXp = [&](int kind, int value) {
        if (kind != 0) return;
        sk_bindings::MpRemoteScope remote;
        stack.player().AddExperience(value);
    };
    mpWorld.onLevelVar = [&](const std::string& name, int value) {
        if (gameZoneScript) {
            gameZoneScript->setValue(skString(name.c_str()), skString(""), skRValue(value));
        }
    };
    // FUN_10067f84: sprites first..last over the actor, for `seconds`.
    auto spawnAttachedEffect = [&](float x, float y, float z, int first, int last, int seconds) {
        sk_bindings::EffectEntity fx;
        fx.firstSprite = first;
        fx.lastSprite = last;
        fx.sprite = first;
        fx.animCursor = first * sk_bindings::kEffectFrameStep;
        fx.animRate = 0xa00;
        fx.life = seconds << 8;
        fx.lifeMax = seconds << 8;
        fx.x = static_cast<int>(x);
        fx.y = static_cast<int>(y);
        fx.z = static_cast<int16_t>(static_cast<int>(z) & 0xffff);
        fx.sizeX = 4;
        fx.sizeY = 4;
        fx.drawFlags = 1;
        stack.level().effects().push_back(fx);
    };
    mpWorld.onUnapplied = [&](uint8_t tag, const sk_net::MpBytes& packet) {
        const sk_net::MpReader r(packet);
        switch (tag) {
            case sk_net::kTagSpellProjectile: {
                // M132, `FUN_10039858`: a projectile with no spell behind
                // it -- it flies from the caster down the caster's heading
                // and hits nothing here; the hit is the other game's.
                const bool avatar = r.U8(4) != 0;
                float x = 0, y = 0, z = 0;
                int heading = 0;
                if (avatar) {
                    if (!mpAvatar.present) return;
                    x = mpAvatar.x;
                    y = mpAvatar.y;
                    z = mpAvatar.z;
                    heading = mpAvatar.heading;
                } else {
                    const MonsterInstance* m = mpFindMonster(r.I32(8));
                    if (!m) return;
                    x = m->x;
                    y = m->y;
                    z = m->z + sk::kEyeHeightOffset;
                    heading = sk_bindings::EngineYawFromPortYaw(m->facingYaw);
                }
                sk_bindings::SpellProjectile shot = sk_bindings::SpawnSpellProjectile(
                    nullptr, nullptr, static_cast<int>(x), static_cast<int>(y), static_cast<int>(z),
                    heading, r.I16(0x18), r.I16(0xc), 0);
                shot.invokeHitTarget = false;
                gameProjectiles.push_back(shot);
                return;
            }
            case sk_net::kTagAttachedEffect: {
                // `FUN_100399a8`: the flame, on this game's copy.
                if (r.U8(4) != 0) {
                    if (mpAvatar.present) {
                        spawnAttachedEffect(mpAvatar.x, mpAvatar.y, mpAvatar.z, r.I32(0xc),
                                            r.I32(0x10), r.I32(0x14));
                    }
                } else if (const MonsterInstance* m = mpFindMonster(r.I32(8))) {
                    if (m->script->alive()) {
                        spawnAttachedEffect(m->x, m->y, m->z, r.I32(0xc), r.I32(0x10), r.I32(0x14));
                    }
                }
                return;
            }
            case sk_net::kTagUmbraBeat: {
                // `FUN_10037954`: the entity *named* "Umbra" takes the
                // sender's `+0x2ee`; on a false it is also run through
                // `DelayReached(1)`, on a true through `DelayReached()`
                // with no argument, which no branch of the script answers.
                const skRValue found =
                    mpCall(stack.level(), "GetEntity", {skRValue(skString("Umbra"))});
                if (found.type() != skRValue::T_Object || !found.obj()) return;
                for (MonsterInstance& m : gameMonsters) {
                    if (static_cast<skiExecutable*>(m.script.get()) != found.obj()) continue;
                    const bool flag = r.U8(8) != 0;
                    sk_bindings::MpRemoteScope remote;
                    mpCall(*m.script, "SetCanTeleport", {skRValue(flag)});
                    if (!flag) mpCall(*m.script, "DelayReached", {skRValue(1)});
                    return;
                }
                return;
            }
            case sk_net::kTagAreaSpell:
                // `FUN_1003c550` calls the area function with a damage of
                // zero, and its whole body is under `0 < damage`. The wave's
                // effects arrive one creature at a time as 0x19 / 0x1a /
                // 0x27. Nothing to do, as in the engine.
                return;
            default:
                break;
        }
        // 0x1c (room event) and 0x26 (effect entity): the engine has a
        // receiver for each and no sender -- their session slots (+0xe8,
        // +0x118) are never called -- so nothing can put one on the wire.
        std::printf("multiplayer: tag 0x%02x received, not applied by this port\n", tag);
    };
    mpWorld.onTakenFromContainer = [&](int containerId, int itemTemplate) {
        // Tag 0x31 (M130): the item goes from this game's copy of the
        // container; one that is emptied leaves through the usual sweep.
        for (PickupInstance& pk : gamePickups) {
            if (!pk.script || pk.script->netId() != containerId) continue;
            pk.script->DiscardContentByTemplate(itemTemplate);
            std::vector<int>& known = mpContainerContents[containerId];
            auto it = std::find(known.begin(), known.end(), itemTemplate);
            if (it != known.end()) known.erase(it);
            return;
        }
    };
    mpWorld.onSetOnFire = [&](int id, int level) {
        // Tag 0x3a (M130): `+0x308 = 1`, `+0x296 = level << 8`. The wire
        // carries the duration only; the flames tick for the engine's 1.
        MonsterInstance* m = mpFindMonster(id);
        if (!m || !m->script->alive()) return;
        m->mpBurnSent = true;
        m->script->ApplyBurn(1, level > 0 ? level : 1);
    };
    mpWorld.onHandOver = [&](const sk_net::MpEntityRecord& rec) {
        // Tag 0x32 (M132), `FUN_1003b85c`: build the object, run its Init,
        // and if it is an item give it to this game's player.
        if (rec.typeId <= 0 || stack.player().HasItemOfTemplate(rec.typeId)) return;
        sk_bindings::MpRemoteScope remote;
        std::unique_ptr<sk_bindings::ItemExecutable> item =
            stack.level().CreateItem(rec.typeId, false);
        if (item) stack.player().AddItem(std::move(item));
    };
    mpWorld.onRegistryFlag = [&](int, int mask, bool set) {
        // Tag 0x38 (M132), `FUN_10037880`.
        stack.player().keyItemFlags().ApplyRemote(static_cast<unsigned>(mask), set);
    };

    // ---- what this game sends ----
    {
        sk_bindings::MpHooks& hooks = sk_bindings::Mp();
        hooks.mirrorMethod = [&](int id, const std::string& method) {
            if (!mpSession.inGame() || id <= 0) return;
            sk_net::MpMirrorMethod m;
            m.entityId = id;
            m.method = method;
            mpSession.Send(m.Encode());
        };
        hooks.destroyObject = [&](int id) {
            if (!mpSession.inGame() || id <= 0 || !mpKnownGone.insert(id).second) return;
            mpSession.Send(sk_net::EncodeEntityId(sk_net::kTagDestroyObject, id));
        };
        hooks.setPosition = [&](bool everyone, int id, int x, int y, int z) {
            if (!mpSession.inGame() || (!everyone && id <= 0)) return;
            sk_net::MpSetPosition pos;
            pos.everyone = everyone;
            pos.entityId = id;
            pos.x = x;
            pos.y = y;
            pos.z = z;
            mpSession.Send(pos.Encode());
        };
        hooks.questFlag = [&](int kind, int quest, int value) {
            if (mpSession.inGame()) mpSession.Send(sk_net::EncodeQuestFlag(kind, quest, value));
        };
        hooks.shareXp = [&](int kind, int value) {
            if (mpSession.inGame()) mpSession.Send(sk_net::EncodeShareXp(kind, value));
        };
        hooks.invulnerable = [&](int id, bool flag) {
            if (mpSession.inGame() && id > 0) mpSession.Send(sk_net::EncodeInvulnerable(id, flag));
        };
        hooks.door = [&](int id, bool open) {
            if (mpSession.inGame() && id > 0) mpSession.Send(sk_net::EncodeDoor(id, !open));
        };
        // M132.
        hooks.registryFlag = [&](int mask, bool set) {
            if (mpSession.inGame()) mpSession.Send(sk_net::EncodeRegistryFlag(0, mask, set));
        };
        hooks.umbraBeat = [&](int id, bool flag) {
            if (mpSession.inGame() && id > 0) mpSession.Send(sk_net::EncodeUmbraBeat(id, flag));
        };
        hooks.handOver = [&](int typeId, int id) {
            if (!mpSession.inGame()) return;
            mpSession.Send(sk_net::EncodeEntityStream(sk_net::kTagHandOver, typeId, id, {}));
        };
        hooks.attachedEffect = [&](sk_bindings::SpellActor* target, int first, int last,
                                   int seconds) {
            if (!target) return;
            if (target == static_cast<sk_bindings::SpellActor*>(&stack.player())) {
                spawnAttachedEffect(gameCamera.x, gameCamera.y, gameCamera.z - sk::kEyeHeightOffset,
                                    first, last, seconds);
                if (mpSession.inGame() && !sk_bindings::Mp().applyingRemote) {
                    mpSession.Send(sk_net::EncodeAttachedEffect(true, mpSession.myIndex(), first,
                                                                last, seconds));
                }
                return;
            }
            for (const MonsterInstance& m : gameMonsters) {
                if (static_cast<sk_bindings::SpellActor*>(m.script.get()) != target) continue;
                spawnAttachedEffect(m.x, m.y, m.z, first, last, seconds);
                if (mpSession.inGame() && !sk_bindings::Mp().applyingRemote &&
                    m.script->netId() > 0) {
                    mpSession.Send(sk_net::EncodeAttachedEffect(false, m.script->netId(), first,
                                                                last, seconds));
                }
                return;
            }
        };

        // The lobby natives -- multiplayermenu.s, hostgamemenu.s,
        // joingamemenu.s.
        hooks.preHost = [&](int slot) {
            if (slot >= 0) mpSlot = slot;
            return mpSlot >= 0;
        };
        hooks.beginHost = [&]() {
            if (mpSession.state() == sk_net::MpSession::State::HostLobby) return true;
            if (!mpSession.BeginHost(mpLocalName())) {
                mpSay(mpSession.lastError());
                return false;
            }
            return true;
        };
        hooks.hostConnect = [&]() {};  // the host is already listening
        hooks.hostPlay = [&]() {
            // "Use character from saved game?" -- the save chosen in the
            // multiplayer menu, loaded now, in the level it was saved in.
            std::string level = mpSlot >= 0 ? stack.LoadGameFromSlot(mpSlot) : std::string();
            if (level.empty()) {
                stack.ClearLevelStates();
                level = "azra";
            }
            if (!mpSession.HostPlay(level)) return false;
            mpAnnounced = true;
            mpNotifiedLevel.clear();
            stack.RequestZoneChange(level);
            return true;
        };
        hooks.beginJoin = [&]() {
            if (mpSession.state() == sk_net::MpSession::State::JoinLobby) return true;
            // The joining player's own character; the story it walks into
            // is the host's (tag 0x29).
            if (mpSlot >= 0) stack.LoadGameFromSlot(mpSlot);
            stack.ClearGameStartRequest();
            // A host on another network: one address per line in mp_host.txt
            // beside the saves, or SK_MP_HOST. LAN hosts are found without.
            if (const char* direct = std::getenv("SK_MP_HOST")) {
                if (direct[0] != 0) mpSession.lobby().AddDirectHost(direct);
            }
            {
                std::ifstream file(saveDirectory + "/mp_host.txt");
                std::string line;
                while (std::getline(file, line)) {
                    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
                    if (!line.empty() && line[0] != '#') mpSession.lobby().AddDirectHost(line);
                }
            }
            if (!mpSession.BeginJoin(mpLocalName())) {
                mpSay(mpSession.lastError());
                return false;
            }
            return true;
        };
        hooks.joinGame = [&](int host) {
            if (mpSession.JoinHost(host)) return true;
            if (!mpSession.lastError().empty()) mpSay(mpSession.lastError());
            return false;
        };
        hooks.rejectClient = [&](int) { mpSession.RejectClient(); };
        hooks.closeLobby = [&]() {
            if (!mpSession.active()) mpSession.Close();
        };
        hooks.localName = [&]() { return mpLocalName(); };
    }
    mpStrikeHook = [&](MonsterInstance& victim) {
        if (!mpSession.inGame() || sk_bindings::Mp().applyingRemote || !victim.script) return;
        if (victim.script->netId() > 0) {
            mpSession.Send(sk_net::EncodeHitForZero(victim.script->netId()));
        }
        mpAttackVisualDue = true;
    };
    auto mpOnLocalDeath = [&](MonsterInstance& dead) {
        if (!mpSession.inGame() || !dead.script) return;
        const int id = dead.script->netId();
        if (id <= 0 || mpRemoteKills.erase(id) > 0) return;  // the other player's kill
        mpSession.Send(sk_net::EncodeEntityId(sk_net::kTagKill, id));
    };

    // A client's own zone change becomes a request to the host (tag 0x2e);
    // the host's is announced (tag 0x00) and then loads as usual.
    auto mpInterceptZoneChange = [&]() -> bool {
        if (!mpSession.inGame()) return false;
        if (mpSession.isClient()) {
            if (mpLoadFromHost) return false;
            sk_net::MpTravel travel;
            travel.levelIndex = mpWorld.levelIndex(stack.requestedZone());
            const sk_bindings::MenuStack::CameraStart& start = stack.cameraStart();
            travel.overrideArmed = start.armed;
            travel.spawn[0] = start.x;
            travel.spawn[1] = start.y;
            travel.spawn[2] = start.z;
            travel.spawn[3] = start.yaw;
            travel.spawn[4] = start.pitch;
            travel.spawn[5] = start.roll;
            mpSession.RequestTravel(travel);
            stack.ClearGameStartRequest();
            stack.ClearCameraStart();
            mpSay(strings.Get(0x13));  // "Waiting for host"
            return true;
        }
        if (!mpAnnounced) {
            mpAnnounced = true;
            mpNotifiedLevel.clear();
            mpSession.HostAnnounceLevel(stack.requestedZone());
        }
        return false;
    };

    // The other player, as one more entity in the frame. Tag 0x22's
    // receiver picks the body by sex and the skin by race:
    // `raceSkin[race] + (sex ? 11 : 8)`.
    auto mpAppendAvatar = [&](std::vector<sk::PlacedEntity>& frame) {
        if (!mpSession.inGame() || !mpAvatar.present) return;
        static const int kRaceSkin[8] = {3, 2, 7, 5, 4, 1, 0, 6};
        // entities.txt 90 is the type the engine creates for an avatar; its
        // model is the female body, and the male one is two rows on.
        const sk::EntityTypeDescriptor* desc = entityTypes.Lookup(90);
        if (!desc) return;
        const int modelIndex = desc->modelArchiveIndex + (mpAvatar.sex == 0 ? 2 : 0);
        const sk::Model* model = modelArchive.GetModel(modelIndex);
        if (!model) return;
        sk::PlacedEntity pe;
        pe.x = mpAvatar.x;
        pe.y = mpAvatar.y;
        pe.z = mpAvatar.z;
        pe.modelArchiveIndex = modelIndex;
        pe.yaw = sk_bindings::PortYawFromEngineYaw(mpAvatar.heading);
        pe.skinIndex = kRaceSkin[mpAvatar.race & 7] + (mpAvatar.sex ? 11 : 8);
        if (const sk::AnimationClip* clip = model->clip(mpAvatar.clip)) {
            if (clip->frameCount() > 0) {
                const float rate = clip->rate > 0 ? clip->fps() : 10.0f * sk::kRateToFps;
                const int advanced = static_cast<int>(mpAvatar.animTime * rate);
                const int inClip = mpAvatar.dead ? (std::min)(advanced, clip->frameCount() - 1)
                                                 : advanced % clip->frameCount();
                pe.frameIndex = clip->startFrame + inClip;
            }
        }
        pe.interpKey = reinterpret_cast<uintptr_t>(&mpAvatar);
        frame.push_back(pe);
    };

    // Once a tick, first thing. True while the other player holds the pause
    // lock (tag 0x15): this game's frame waits, as `FUN_10029cb0` does.
    auto mpTick = [&]() -> bool {
        mpSession.Tick(sk_bindings::kAiFrameDeltaUnits);
        sk_bindings::MpHooks& hooks = sk_bindings::Mp();
        hooks.active = mpSession.active();
        hooks.client = mpSession.isClient();
        if (mpNoticeTicks > 0) --mpNoticeTicks;

        mpAddressOpen = mpSession.state() == sk_net::MpSession::State::JoinLobby &&
                        !mpSession.peerConnected();
        if (!mpAddressOpen) {
            mpAddressEntry.clear();
            mpAddressSubmitted.clear();
        }
        // The lobby screens' rows: who has joined, which hosts were found.
        if (sk_bindings::MenuExecutable* menu = stack.currentMenu()) {
            if (mpSession.state() == sk_net::MpSession::State::HostLobby) {
                const std::string& client = mpSession.playerName(1);
                const bool joined = mpSession.peerConnected() && !client.empty();
                menu->SetLobbyRow("Reject1", joined ? client : std::string(" "), joined);
            } else if (mpSession.state() == sk_net::MpSession::State::JoinLobby) {
                // M130: an address typed on the lobby screen becomes a host.
                if (!mpAddressSubmitted.empty()) {
                    std::string address = mpAddressSubmitted;
                    mpAddressSubmitted.clear();
                    int port = sk_net::kMpGamePort;
                    const size_t colon = address.rfind(':');
                    if (colon != std::string::npos) {
                        const int typed = std::atoi(address.c_str() + colon + 1);
                        if (typed > 0 && typed < 65536) port = typed;
                        address.erase(colon);
                    }
                    if (!address.empty()) {
                        mpSession.lobby().AddDirectHost(address, static_cast<uint16_t>(port));
                    }
                }
                const std::vector<sk_net::MpHostEntry>& hosts = mpSession.lobby().hosts();
                for (int i = 0; i < 4; ++i) {
                    const bool found = static_cast<size_t>(i) < hosts.size();
                    menu->SetLobbyRow("JoinGame" + std::to_string(i + 1),
                                      found ? hosts[static_cast<size_t>(i)].name : strings.Get(3525),
                                      found && !mpSession.peerConnected());
                }
            }
        }
        if (!mpSession.active()) {
            mpAvatar.present = false;
            return false;
        }

        // The level named in the last 0x00 is up: tell the session, once.
        if (gameZone && !stack.gameStartRequested() && !loadingScreenActive &&
            !gameLoadedZoneName.empty() && mpNotifiedLevel != gameLoadedZoneName) {
            mpNotifiedLevel = gameLoadedZoneName;
            if (mpHostNextNetId > gameNextNetId) gameNextNetId = mpHostNextNetId;
            mpHostNextNetId = 0;
            mpRestoreNetIds.clear();
            mpAssignNetIds();
            mpKnownGone.clear();
            mpLiveIds.clear();
            mpRemoteKills.clear();
            mpLoadFromHost = false;
            mpAnnounced = false;
            mpPrevX = gameCamera.x;
            mpPrevY = gameCamera.y;
            mpPrevZ = gameCamera.z;
            mpSession.NotifyLevelLoaded();
        }
        if (!mpSession.inGame() || !gameZone) return false;
        // The game was quit to the menu underneath the session.
        if (!stack.gameActive()) {
            mpSession.Close();
            return false;
        }
        mpAssignNetIds();

        // This player's velocity, for 0x01: what moves the position by the
        // last tick's step at the engine's `v * delta >> 8`.
        mpVelX = static_cast<int>((gameCamera.x - mpPrevX) * 256.0f / sk_bindings::kAiFrameDeltaUnits);
        mpVelY = static_cast<int>((gameCamera.y - mpPrevY) * 256.0f / sk_bindings::kAiFrameDeltaUnits);
        mpVelZ = static_cast<int>((gameCamera.z - mpPrevZ) * 256.0f / sk_bindings::kAiFrameDeltaUnits);
        mpPrevX = gameCamera.x;
        mpPrevY = gameCamera.y;
        mpPrevZ = gameCamera.z;

        // Whatever left the world since the last tick -- a pickup taken, a
        // bag emptied, a script's DestroyObject -- is told once (tag 0x2b).
        {
            std::set<int> live;
            auto visit = [&](sk_bindings::EntityBaseRef* base) {
                if (!base || base->netId() <= 0) return;
                if (base->entityRemoved()) return;
                live.insert(base->netId());
            };
            for (DoorInstance& d : gameDoors) visit(d.script.get());
            for (PickupInstance& pk : gamePickups) visit(pk.script.get());
            for (MonsterInstance& m : gameMonsters) visit(m.script.get());
            for (int id : mpLiveIds) {
                if (live.count(id) != 0 || !mpKnownGone.insert(id).second) continue;
                mpSession.Send(sk_net::EncodeEntityId(sk_net::kTagDestroyObject, id));
            }
            mpLiveIds = std::move(live);
        }
        // M130: three more things the other game is told as they happen.
        if (mpSession.inGame()) {
            // 0x1e: a creature here has taken this player as its target.
            // 0x3a: a creature here has been set on fire.
            for (MonsterInstance& m : gameMonsters) {
                if (!m.script || m.script->netId() <= 0 || !m.script->alive()) continue;
                if (!m.hasTarget) {
                    m.mpTargetSent = false;
                    m.mpTargetsPeer = false;
                } else if (!m.mpTargetSent) {
                    m.mpTargetSent = true;
                    mpSession.Send(sk_net::EncodeCreatureTarget(m.script->netId(),
                                                               mpSession.myIndex()));
                }
                if (!m.script->burning()) {
                    m.mpBurnSent = false;
                } else if (!m.mpBurnSent) {
                    m.mpBurnSent = true;
                    mpSession.Send(sk_net::EncodeSetOnFire(m.script->netId(), 4));
                }
            }
            // 0x31: something was taken out of a container.
            std::map<int, std::vector<int>> now;
            for (PickupInstance& pk : gamePickups) {
                if (!pk.script || !pk.isContainer || pk.script->netId() <= 0) continue;
                if (pk.script->entityRemoved()) continue;
                std::vector<int>& held = now[pk.script->netId()];
                for (const auto& item : pk.script->contents()) held.push_back(item->templateId());
            }
            for (const auto& before : mpContainerContents) {
                auto it = now.find(before.first);
                if (it == now.end()) continue;  // the container itself went: 0x2b covers it
                std::vector<int> left = it->second;
                for (int templateId : before.second) {
                    auto found = std::find(left.begin(), left.end(), templateId);
                    if (found != left.end()) {
                        left.erase(found);
                    } else {
                        mpSession.Send(sk_net::EncodeTaken(before.first, templateId));
                    }
                }
            }
            mpContainerContents = std::move(now);
        } else {
            mpContainerContents.clear();
        }

        // The avatar coasts on its velocity and animates.
        if (mpAvatar.present) {
            const int delta = sk_bindings::kAiFrameDeltaUnits;
            mpAvatar.x += static_cast<float>(mpAvatar.vx * delta) / 256.0f;
            mpAvatar.y += static_cast<float>(mpAvatar.vy * delta) / 256.0f;
            mpAvatar.animTime += 0.04f;
            if (!mpAvatar.dead) {
                if (mpAvatar.attackTicks > 0) {
                    --mpAvatar.attackTicks;
                } else {
                    const int clip = (mpAvatar.vx != 0 || mpAvatar.vy != 0) ? 5 : 8;
                    if (clip != mpAvatar.clip) {
                        mpAvatar.clip = clip;
                        mpAvatar.animTime = 0.0f;
                    }
                }
            }
        }

        // This player's own attack, death and modal screens.
        if (mpAttackVisualDue) {
            mpAttackVisualDue = false;
            const sk_bindings::ItemExecutable* held = stack.player().rightItem();
            const bool ranged = held && held->usesRangedPath();
            mpSession.Send(sk_net::EncodeAttackVisual(mpSession.myIndex(), ranged ? 2 : 1, ranged ? 2 : 1));
        }
        const bool dead = stack.player().health() <= 0;
        if (dead && !mpWasDead) {
            mpSession.Send(sk_net::EncodeAnimation(6, mpSession.myIndex()));
            mpSession.Send(sk_net::EncodeText(mpSession.myIndex(), mpLocalName() + " has fallen."));
        }
        mpWasDead = dead;
        mpSession.SetPauseLock(gamePausedForMenu || !inGame);
        return mpSession.peerHoldsPause() && inGame;
    };

    auto runTick = [&]() {
        gameMouseLookWanted = false;  // M125: set again below if this tick is free movement
        // M129: the multiplayer session's tick. While the other player has
        // a modal screen up, this game's frame does not run: the last one
        // stays, under the engine's own "Waiting for players."
        if (mpTick()) {
            const std::string waiting = strings.Get(4);
            sk::BitmapFont::DrawString(
                backbuffer, (sk::Backbuffer::kWidth - sk::BitmapFont::TextWidth(waiting)) / 2, 100,
                waiting, MenuColor444(0xfff));
            presentMenu(backbuffer, true);
            return;
        }
        // M132: **the engine's own waiting screen** -- `FUN_10029cb0`'s
        // other arm, taken in a session while the two games are not yet in
        // step: the host in its level before the client has loaded, the
        // client before the host's world has finished arriving. The loading
        // splash (slot 174), and on it, centred at y = 180 in the loading
        // banner's pulsing grey,
        //
        //     session->+0x28 ? string 3816 "Receiving game data"
        //                    : string 3817 "Waiting for players..."
        //
        // A level change that is pending or running is left alone: that is
        // the loading screen's, and it is what ends the wait.
        if (mpSession.state() == sk_net::MpSession::State::Starting && mpSession.peerConnected() &&
            !stack.gameStartRequested() && !loadingScreenActive && !quitToMenuActive &&
            (mpSession.isHost() ? (inGame && gameZone != nullptr) : true)) {
            if (const sk::Sprite* splash = spriteArchive.GetSprite(174)) {
                backbuffer.Blit(0, 0, *splash);
            } else {
                backbuffer.Fill(kBackgroundColor);
            }
            const std::string waiting =
                strings.Get(!mpSession.isHost() && !mpIncoming.empty() ? 3816 : 3817);
            sk::BitmapFont::DrawString(
                backbuffer, (sk::Backbuffer::kWidth - sk::BitmapFont::TextWidth(waiting)) / 2, 0xb4,
                waiting, LoadingBannerColor(loadingBannerPulse));
            loadingBannerPulse = (loadingBannerPulse + 1) % kLoadingBannerPulseSteps;
            presentMenu(backbuffer, false);
            return;
        }
        gameMouseYawLastTick = 0.0f;
        gameMousePitchLastTick = 0.0f;
#if SK_DEBUG_SUITE
        // SK_DEBUG_SUITE (M68): rolls the metrics frame and runs any command
        // queued by a keystroke since the last tick. Deliberately here, at
        // the top of the tick, rather than in the window procedure that
        // queued it -- a console command can run real script code, and
        // script has to run inside the tick like everything else.
        {
            constexpr double kTickSeconds = 0.04;  // the fixed 25Hz tick
            debugSuite.BeginTick(kTickSeconds);
            if (debugSuite.quitRequested()) {
                window.Close();
                return;
            }
        }
#endif

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
        if (gameActiveHandItem && gameActiveHandItem->markedForRemoval()) {
            gameActiveHandItem = nullptr;
        }
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
        // M93: and whatever the purge just handed over becomes a loot bag
        // on the floor. `FUN_1002c3a8` -- M81's DropObject, the same
        // function the drop-gold case calls -- spawns typeId 300 with the
        // script tag **"Loot_Dropped"** (the literal at `0x100addf0`, a
        // real `loot_dropped.s` whose Init() is SetName(456)/
        // SetUseText(457)/SetUsable(true) and whose OnUse() opens
        // LootMenu), places it at the owner's position minus half a tile
        // on each of x and y, and appends the dropped item to it. So a
        // drop is a bag you can turn round and loot again -- it is not a
        // delete, which is what this port used to do.
        //
        // The bag's own placement is `entity->vtable[0x14](x - 0x80,
        // y - 0x80, z + 300)`; the +300 is M81's kLootDropRise, the height
        // the floor snap probes from, and it is resolved through the same
        // helper the creature death drop uses so the two agree.
        if (gameZone) {
            constexpr int kDropOffset = 0x80;  // half a tile, both axes
            for (std::unique_ptr<sk_bindings::ItemExecutable>& dropped :
                 stack.player().TakePendingDrops()) {
                if (!dropped) continue;
                std::unique_ptr<sk_bindings::ItemExecutable> bag =
                    stack.level().CreateEntityWithScript(sk_bindings::kDroppedLootTypeId,
                                                          sk_bindings::kDroppedLootScript);
                if (!bag) continue;
                PickupInstance inst;
                inst.x = gameCamera.x - kDropOffset;
                inst.y = gameCamera.y - kDropOffset;
                inst.z = gameZone->SnapActorToGround(
                    inst.x, inst.y,
                    gameCamera.z - sk::kEyeHeightOffset + sk_bindings::kLootDropRise);
                inst.modelArchiveIndex =
                    stack.level().EntityModelIndexOf(sk_bindings::kDroppedLootTypeId);
                inst.isContainer = true;  // the real case re-derives category == 8
                inst.typeId = sk_bindings::kDroppedLootTypeId;  // post-M120: saveable
                bag->SetWorldPosition(static_cast<int>(inst.x), static_cast<int>(inst.y),
                                       static_cast<int>(inst.z));
                std::printf("  [drop] %s -> loot bag at (%.0f, %.0f)\n",
                            dropped->name().c_str(), inst.x, inst.y);
                bag->AddContent(std::move(dropped));
                // M123: FUN_1002c3a8's `+0x180 = 1` -- emptied, it goes.
                bag->SetDestroyWhenEmpty(true);
                inst.script = std::move(bag);
                gamePickups.push_back(std::move(inst));
            }
        }
        // M103: `CastAzraWrath()` -- the vermin bomb. The native already
        // rolled the damage and named the epicentre (the item's owner);
        // what is left is `FUN_1004720c`'s sweep, which is the same one the
        // AzraWrath *spell* runs above in applyCastResult: every creature
        // in the level within kAreaSpellRange of the origin, the origin
        // itself excluded, no sightline and no facing test.
        //
        // The player's own share of the bomb is not here. Both scripts take
        // it through AddEffect in their OnUse, on a separate roll, so the
        // blast never has to damage its own origin.
        for (const sk_bindings::MenuStack::AreaBlast& blast : stack.TakePendingAreaBlasts()) {
            if (blast.damage <= 0) continue;
            float blastX = gameCamera.x, blastY = gameCamera.y;
            const bool fromPlayer =
                blast.origin == nullptr ||
                blast.origin == static_cast<const skiExecutable*>(&stack.player());
            if (!fromPlayer) {
                bool found = false;
                for (const MonsterInstance& m : gameMonsters) {
                    if (m.script.get() != blast.origin) continue;
                    blastX = m.x;
                    blastY = m.y;
                    found = true;
                    break;
                }
                if (!found) continue;  // an owner that is not in this level
            }
            std::printf("  [azrawrath] %d damage around (%.0f, %.0f)\n", blast.damage, blastX,
                        blastY);
            for (MonsterInstance& victim : gameMonsters) {
                if (static_cast<const skiExecutable*>(victim.script.get()) == blast.origin) {
                    continue;
                }
                if (!victim.script->alive() || victim.script->outOfWorld()) continue;
                const float adx = victim.x - blastX, ady = victim.y - blastY;
                if (std::sqrt(adx * adx + ady * ady) >
                    static_cast<float>(sk_bindings::kAreaSpellRange)) {
                    continue;
                }
                onCreatureStruck(victim);  // post-M120
                // The attacker the kill credit goes to is the origin when it
                // is a creature; a bomb the player set off pays the player,
                // through the same SpellActor slot the area spell uses.
                victim.script->ApplyDamage(blast.damage,
                                            fromPlayer ? &stack.player() : nullptr,
                                            /*ranged=*/true);
            }
        }
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
                                                    sk::kQuitProgressStageCount - 1)],
                0);
            presentMenu(backbuffer, false);
            ++quitToMenuTick;
            if (quitToMenuTick <= sk::kQuitProgressStageCount) return;
            quitToMenuActive = false;

            // The teardown itself, in the real thread's own order. Scripts
            // first: a zone script, a monster and a pickup all hold raw
            // pointers into the zone and into each other, so nothing may
            // outlive it.
            // M75: `Level` holds a non-owning pointer to the zone script
            // (level_executable.h's AttachZoneScript) -- detach before it
            // dies, or the next `Level.<field>` on the main menu reads
            // freed memory.
            stack.level().AttachZoneScript(nullptr);
            // Post-M120: and `current.sav` -- the next session is a New Game
            // (which clears it again) or a Load (which replaces it).
            stack.ClearLevelStates();
            gameLoadedZoneName.clear();
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
            stack.level().ClearEffects();  // M63
            gameRegionsOccupied.clear();
            gameZoneRegions.SetZone(nullptr);
            gameTileStamp.SetZone(nullptr);
            gameZone.reset();
            // M101: the engine's quit teardown deletes the player object
            // (FUN_1000e3c4 with keepPlayer 0), so the next session's player
            // is born alive and mortal.
            // M126: and empty-handed. This used to keep the object and clear
            // only its death and quest fields, so a character who died (the
            // death screen's back key is a QuitToMenu) handed their whole
            // inventory to the next New Game. After the level's objects are
            // gone, since a door and a creature hold a reference to the
            // player; the hand-item pointers reset below are never read.
            stack.ResetPlayerForNewSession();
            stack.SetCurrentLevelName(std::string());
            // M91: `engine + 0x6908` goes null with the level, and
            // GameActive() is exactly that pointer -- so mainmenu.s's
            // OnDisplay comes back with the front end's rows (New Game,
            // Credits, Multiplayer) rather than the in-game ones the
            // player just left. See MenuStack::gameActive().
            stack.SetGameActive(false);
            // Same reset the zone-load block does, for the same reason --
            // FUN_1002fca4 zeroes the script clock whenever the level goes.
            stack.gameClock().Reset();
            gameCastCooldown = sk_bindings::SpellCastCooldown{};
            gameMoveFatigueAccum = 0;
            gameClockUnits = 0;
            gamePlayerPeriodicKind = 0;
            gameWeaponViewmodel = sk_bindings::WeaponViewmodel{};
            gameActiveHandItem = nullptr;
            gameVelZ = 0.0f;
            gameMotion = sk_bindings::PlayerMotion{};
            gamePlayerGlow = false;
            zoneRenderer.lights = sk::ZoneRenderer::SceneLights{};
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
                presentMenu(backbuffer, false);
                return;
            }
        }

        // M26: real loading screen -- see RenderLoadingScreen()'s comment.
        // Latches loadingScreenActive the instant a request comes in
        // (initial game start or a real Level.LoadLevel() transition,
        // MenuStack::RequestZoneChange()) and renders a fixed run of
        // staged frames before the actual (synchronous, effectively
        // instant) zone-load work below ever runs.
        if (stack.gameStartRequested() && !loadingScreenActive && mpInterceptZoneChange()) {
            return;  // M129: a client asks the host to travel instead of loading
        }
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
            RenderLoadingScreen(backbuffer, spriteArchive, bannerText, percent,
                                loadingBannerPulse);
            loadingBannerPulse = (loadingBannerPulse + 1) % kLoadingBannerPulseSteps;
            presentMenu(backbuffer, false);
            ++loadingScreenTick;
            if (loadingScreenTick <= kNumStages) {
                return;
            }
            loadingScreenActive = false;
            // Post-M120: `FUN_1006c31c` writes the level being left into
            // `current.sav` before it loads the next one, so every corpse,
            // opened door, emptied chest and `saved_*` variable is there
            // when the player walks back. Not after a Load: that replaced
            // `current.sav` with the slot's copy, and the level being
            // abandoned must not overwrite the slot's version of it.
            {
                const bool discardDeparting = stack.TakeDiscardDepartingLevel();
                if (gameZone && !gameLoadedZoneName.empty() && !discardDeparting) {
                    stack.StoreLevelState(gameLoadedZoneName, snapshotLevelState());
                }
                gameLoadedZoneName.clear();
            }
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
                // M55: same file the model manifest above comes from --
                // columns 2-4 of every row are the model's collision box,
                // which `Entity::Init` copies onto every placement of it.
                gameModelCollision.Load(scriptRoot, stack.requestedZone());
                gameZone = std::move(zone);
                // M100: `<zone>.pth`, which GameEngine_InitLevel reads just
                // after the placements -- the named paths FindPathNode walks
                // (simkin_bindings/path_table.h). Before the zone script's
                // Init, like the engine's.
                stack.level().SetPaths(sk_bindings::LoadPathTable(
                    std::string(scriptRoot) + "/" + stack.requestedZone() + ".pth"));
                // M91: the level pointer GameActive() *is* -- see
                // MenuStack::gameActive(). Set here, where the level
                // becomes real, and cleared with it in the QuitToMenu
                // teardown above.
                stack.SetGameActive(true);
                gameLoadedZoneName = stack.requestedZone();  // post-M120
#if SK_DEBUG_SUITE
                // SK_DEBUG_SUITE (M68)
                sk_debug::Count("zone.loads");
                sk_debug::Log("world", "zone loaded: " + stack.requestedZone());
#endif
                // M57: the original allocates its explored bitmap once
                // and never resizes it (see ExploredTiles::Reset) -- this
                // port sizes it per zone, the one deliberate departure.
                gameExplored.Reset(gameZone->width(), gameZone->height());
                gameMapOpen = false;
                // M44: LockZone/UnlockZone write straight into the cell
                // grid, so the Level global needs the live zone. Repointed
                // on every zone load.
                gameZoneRegions.SetZone(gameZone.get());
                gameTileStamp.SetZone(gameZone.get());
                // M44/M83: a fresh zone starts with nobody recorded inside
                // any region. That does *not* make the first tick fire
                // EnterZone for wherever the player spawns, which is what
                // this comment used to claim: the walk itself only runs on
                // a tick the player moved (see its own comment below), so
                // the region you arrive standing in fires on your first
                // step, not on arrival.
                gameRegionsOccupied.clear();
                // M61: `GameEngine_InitLevel`'s `typeId == 1` branch, both
                // halves of it. The zone's own player-start record is the
                // default; a script that armed `SetCameraStart` before
                // asking for this zone replaces all six values with its
                // own (MenuStack::CameraStart has the field mapping and
                // the lifecycle).
                //
                // This is the same order the engine uses -- the override is
                // consumed during the entity pass, which runs *before* the
                // zone's own script is loaded, so a zone script's Init()
                // cannot arm a spawn for its own arrival. The scripts
                // agree: all 49 sites sit in EnterZone/menu handlers of the
                // zone the player is *leaving*.
                const sk_bindings::MenuStack::CameraStart& cameraStart = stack.cameraStart();
                if (cameraStart.armed) {
                    gameCamera.x = static_cast<float>(cameraStart.x);
                    gameCamera.y = static_cast<float>(cameraStart.y);
                    // The engine truncates the stored 32-bit z to 16 bits
                    // on the way onto the player (`+0xa4` is a halfword),
                    // which is what makes broken1.s's `0 + 512` and
                    // erthcave's -7686 behave; kept literal.
                    gameCamera.z = static_cast<float>(static_cast<int16_t>(cameraStart.z & 0xffff));
                    gameCamera.yaw = CameraYawRadians(cameraStart.yaw);
                    gameCamera.pitch = CameraPitchRadians(cameraStart.pitch);
                } else {
                    gameCamera.x = static_cast<float>(gameZone->playerStartX);
                    gameCamera.y = static_cast<float>(gameZone->playerStartY);
                    gameCamera.z = static_cast<float>(gameZone->playerStartZ);
                    gameCamera.yaw = CameraYawRadians(gameZone->playerStartYawRaw);
                    gameCamera.pitch = CameraPitchRadians(gameZone->playerStartPitchRaw);
                }
                // **Deliberate departure.** The real engine adds its
                // eye-height constant (`CMap+0x1a`) only on the
                // record-based path: the override branch writes `+0xa4`
                // and never recomputes `+0x224`, so a scripted arrival
                // inherits the previous zone's eye height. Reproducing
                // that would only be meaningful if this port's own offset
                // were the real constant, and it is not -- camera.h
                // documents kEyeHeightOffset as a calibrated stand-in for
                // a value the binary never appears to write at all. So it
                // is applied to both paths here, and the asymmetry is
                // recorded in docs/ZONE_FORMAT.md instead of imitated.
                gameCamera.z += sk::kEyeHeightOffset;
                // Consumed. `GameEngine_InitLevel` clears the flag right
                // after its entity pass for exactly this reason: the
                // override is for one arrival, not for every load after it.
                stack.ClearCameraStart();
                gameCamera.fovY = sk::kEngineFovY;
                gameVelZ = 0.0f;
                onGround = true;
                inGame = true;
                // M95: the front end's menus are gone (the engine's level
                // start switches screens, FUN_1002c274, which drops
                // `mgr+0x48`). Before any placement's Init() runs, so a
                // script that opens a menu during the load still counts.
                stack.SetMenuActive(false);

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
                // M63: and the same for the zone's scripted sprite
                // effects, which the incoming zone's own Init()
                // repopulates from scratch.
                stack.level().ClearEffects();
                // M53: and the script-timer clock restarts with the level,
                // exactly as the engine's does (FUN_1002fca4 zeroes it).
                // Every entity carrying a pending deadline is destroyed
                // right here anyway, so there is nothing left holding a
                // stale one.
                stack.gameClock().Reset();

                // M75: build the zone-root script *now*, before any
                // placement runs, and hand it to the `Level` global.
                //
                // `Level` and the zone-root script are the same object in
                // the engine (level_executable.h's AttachZoneScript() has
                // the corpus proof), and the one thing that ordering
                // controls is when `Level.<field>` starts resolving to the
                // right store. Merely constructing the script parses its
                // `.s` and materialises every top-level declaration --
                // `azra.s`'s `EndGame_Trinket [0]`, `crypt2.s`'s
                // `saved_Crys1 [0]` .. `saved_Crys7 [0]` -- so from here on
                // every placement's own Init() sees the real, declared
                // zone variables rather than the no-zone fallback bucket.
                //
                // `Init()` itself still runs at the end of the load, after
                // every door/monster/pickup is registered, because that is
                // what its many `Level.GetEntity("m1")`-shaped lookups
                // need (see the M23 comment down there).
                stack.level().AttachZoneScript(nullptr);
                gameZoneScript.reset();
                {
                    std::string zoneScriptPath =
                        std::string(scriptRoot) + "/" + stack.requestedZone() + ".s";
                    skExecutableContext zoneScriptCtxt(&interpreter);
                    try {
                        gameZoneScript = std::make_unique<sk_bindings::ZoneScriptExecutable>(
                            skString(zoneScriptPath.c_str()), zoneScriptCtxt, stack);
                        stack.level().AttachZoneScript(gameZoneScript.get());
                    } catch (skParseException& ex) {
                        std::printf("shadowkey-port: PARSE ERROR loading zone script %s: %s\n",
                                    zoneScriptPath.c_str(), ex.toString().ptr());
                    } catch (skTreeNodeReaderException& ex) {
                        std::printf("shadowkey-port: SCRIPT FILE ERROR loading zone script %s: %s\n",
                                    zoneScriptPath.c_str(), ex.toString().ptr());
                    } catch (skRuntimeException& ex) {
                        std::printf("shadowkey-port: RUNTIME ERROR loading zone script %s: %s\n",
                                    zoneScriptPath.c_str(), ex.toString().ptr());
                    }
                }

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

                // Post-M120: **a level the session has already visited comes
                // back from its `<level>.dat`, not from its `.ent`.** The
                // engine's order, `GameEngine_InitLevel` -> `FUN_10019780`:
                // the `.ent` pass runs as normal, then -- when `current.sav`
                // holds a member for this level -- `FUN_1000e3c4` destroys
                // every entity it just placed (a tile-stamped door taking its
                // footprint out of the grid as it goes) and `FUN_100188dc`
                // rebuilds the level from the record: the level object's own
                // record first (journal, explored bitmap, the zone script's
                // variables), then each saved entity, record before `Init`.
                //
                // This port never instantiates what the engine would
                // destroy: a placement that would become a live object is
                // simply skipped, and the saved objects are fed through the
                // same per-placement code below with their record attached.
                // Scenery that runs no script is not in the record (it never
                // changes), so it still comes from the `.ent`.
                sk::SavedLevelState savedLevel;
                bool restoringLevel = false;
                // M129: every live object gets the id the multiplayer
                // protocol names it by. Placements are numbered in order; an
                // object restored from a level record the *host* sent takes
                // the id that came with its record, so both players' numbers
                // agree.
                gameNextNetId = 0;
                auto netIdFor = [&](const sk::SavedEntity* saved) {
                    if (saved && !mpRestoreNetIds.empty() && !savedLevel.entities.empty()) {
                        const std::ptrdiff_t index = saved - savedLevel.entities.data();
                        if (index >= 0 && static_cast<size_t>(index) < mpRestoreNetIds.size()) {
                            const int id = mpRestoreNetIds[static_cast<size_t>(index)];
                            if (id > gameNextNetId) gameNextNetId = id;
                            return id;
                        }
                    }
                    return ++gameNextNetId;
                };
                if (const std::vector<uint8_t>* savedBytes =
                        stack.FindLevelState(stack.requestedZone())) {
                    sk::SaveStream savedStream;
                    savedStream.Reset(*savedBytes);
                    savedLevel.Read(savedStream, savedKindForType);
                    restoringLevel = !savedStream.failed();
                    std::printf("shadowkey-port: %s.dat -- %s (%zu saved entities)\n",
                                stack.requestedZone().c_str(),
                                restoringLevel ? "restoring the level from current.sav"
                                               : "unreadable, loading from the .ent",
                                savedLevel.entities.size());
                }
                // M134: a door-class placement the level author *named* but
                // gave no script (`!portcullis` GTA/GTB in delfhide,
                // `!ironcage` cage1..cage12 in broken1, crypt2's star
                // teeth). The engine files every placement under its name,
                // and those scripts reach them with `Level.GetEntity(...)` --
                // as scenery they came back null, and `Gate_Open_A.s`'s
                // OpenGate stopped dead on `DestroyObjectMirror`. "noname"
                // and "none" are the editor's placeholders, not names.
                auto scriptlessNamedDoor = [](const sk::EntityTypeDescriptor& desc,
                                              const std::string& scriptPath,
                                              const std::string& name) {
                    return desc.category == 11 && scriptPath.empty() && !name.empty() &&
                           name != "noname" && name != "none";
                };
                auto becomesLiveObject = [&](const sk::EntityTypeDescriptor& desc,
                                             const std::string& scriptPath,
                                             const std::string& name) {
                    if (scriptlessNamedDoor(desc, scriptPath, name)) return true;
                    return !scriptPath.empty() &&
                           (desc.category == 11 || IsPickupCategory(desc.category) ||
                            desc.category == 2 || desc.category == 7);
                };
                std::vector<std::pair<sk::Zone::EntPlacement, const sk::SavedEntity*>> placements;
                for (const sk::Zone::EntPlacement& e : gameZone->entities()) {
                    const sk::EntityTypeDescriptor* desc = entityTypes.Lookup(e.typeId);
                    if (restoringLevel && desc && becomesLiveObject(*desc, placementScript(e, desc), e.name)) {
                        // M38's trigger candidates are the placements, not
                        // the live objects -- crypt1.s's trapped doors are
                        // matched by .ent name -- so they are still taken.
                        TrapInstance cand;
                        cand.x = static_cast<float>(e.x);
                        cand.y = static_cast<float>(e.y);
                        cand.typeId = e.typeId;
                        cand.name = e.name;
                        gameTrapCandidates.push_back(std::move(cand));
                        // The destroyed door's baked closed footprint (M67)
                        // goes with it; the restored door stamps its own.
                        if (desc->category == 11) {
                            const sk::ModelCollision& dc =
                                gameModelCollision.At(desc->modelArchiveIndex);
                            if (dc.tileStamped()) {
                                gameZone->StampEntityBox(e.x, e.y, e.yawRaw, dc.halfExtentX,
                                                         dc.halfExtentY, 0x04, false,
                                                         /*journal=*/false);
                            }
                        }
                        continue;
                    }
                    placements.push_back({e, nullptr});
                }
                if (restoringLevel) {
                    // FUN_1002f934 / FUN_10071f2c: the level object first.
                    std::vector<sk::Zone::TileChange> journal;
                    for (const sk::SavedLevelObject::TileChange& c : savedLevel.level.tileChanges) {
                        journal.push_back({c.x, c.y, static_cast<uint16_t>(c.value)});
                    }
                    gameZone->RestoreTileChanges(journal);
                    std::vector<uint8_t>& explored = gameExplored.bytes();
                    for (size_t i = 0; i < explored.size() && i < savedLevel.level.explored.size();
                         ++i) {
                        explored[i] = savedLevel.level.explored[i];
                    }
                    if (gameZoneScript) {
                        sk_bindings::RestoreScriptVars(*gameZoneScript,
                                                       savedLevel.level.scriptVars);
                    }
                    for (const sk::SavedEntity& rec : savedLevel.entities) {
                        sk::Zone::EntPlacement e;
                        e.x = rec.entity.x;
                        e.y = rec.entity.y;
                        e.z = rec.entity.z;
                        e.typeId = rec.typeId;
                        e.yawRaw = static_cast<uint16_t>(rec.entity.yaw);
                        e.rotARaw = static_cast<uint16_t>(rec.entity.pitch);
                        e.rotBRaw = static_cast<uint16_t>(rec.entity.roll);
                        e.scaleRaw = rec.drawable.scale > 0 ? static_cast<uint16_t>(rec.drawable.scale)
                                                            : static_cast<uint16_t>(256);
                        e.name = rec.entity.objectId;
                        e.scriptPath = rec.entity.artName;
                        placements.push_back({e, &rec});
                    }
                }
                // Runs a native on a restored object through its own
                // dispatch, the way its script would have.
                auto callRestoreNative = [&](skiExecutable& target, const char* name,
                                             const skRValue& arg) {
                    skRValueArray args;
                    args.append(arg);
                    skRValue ret;
                    skExecutableContext ctxt(&interpreter);
                    try {
                        target.method(skString(name), args, ret, ctxt);
                    } catch (skRuntimeException& ex) {
                        std::printf("shadowkey-port: restoring %s(): %s\n", name,
                                    ex.toString().ptr());
                    }
                };
                for (const auto& placement : placements) {
                    const sk::Zone::EntPlacement& e = placement.first;
                    const sk::SavedEntity* saved = placement.second;
                    const sk::EntityTypeDescriptor* desc = entityTypes.Lookup(e.typeId);
                    if (!desc) continue;
                    const std::string placementScriptPath = placementScript(e, desc);
                    // M38: every placement is a possible trigger target --
                    // matched by typeId (lothcav.s's `AddEntity(1013)`) or
                    // by name (crypt1.s's `AddTrigger("door1")`). Filtered
                    // down to the real ones below, once the zone script has
                    // actually registered its triggers. (A restored object's
                    // placement was taken as a candidate above.)
                    if (!saved) {
                        TrapInstance cand;
                        cand.x = static_cast<float>(e.x);
                        cand.y = static_cast<float>(e.y);
                        cand.typeId = e.typeId;
                        cand.name = e.name;
                        gameTrapCandidates.push_back(std::move(cand));
                    }
                    const bool scriptlessDoor =
                        scriptlessNamedDoor(*desc, placementScriptPath, e.name);
                    if (desc->category == 11 && (!placementScriptPath.empty() || scriptlessDoor)) {
                        const std::string fullPath =
                            scriptlessDoor ? "(no script) " + e.name : placementScriptPath;
                        skExecutableContext loadCtxt(&interpreter);
                        try {
                            auto door = scriptlessDoor
                                            ? std::make_unique<sk_bindings::DoorExecutable>(
                                                  &interpreter, stack.player())
                                            : std::make_unique<sk_bindings::DoorExecutable>(
                                                  skString(fullPath.c_str()), loadCtxt,
                                                  stack.player());
                            // M62: the engine sets a placement's position
                            // (vtable +0x18) *before* calling its Init()
                            // (vtable +0x10) -- see ZONE_FORMAT.md's step
                            // list -- so a script whose Init() reads
                            // GetPositionX/Y/Z sees where it actually is.
                            door->SetWorldPosition(e.x, e.y, e.z);
                            // M67: and its box and heading, so its own
                            // SetPassable() can lift its footprint out of
                            // the tile grid. Before Init() for the same
                            // reason SetWorldPosition is: a script that
                            // touches passability in Init() must stamp
                            // against real geometry, not against (0,0).
                            {
                                const sk::ModelCollision& dc =
                                    gameModelCollision.At(desc->modelArchiveIndex);
                                door->AttachTileStamp(&gameTileStamp, dc.halfExtentX,
                                                       dc.halfExtentY, e.yawRaw);
                            }
                            // M92: and its `.ent` placement name, which is
                            // what `GetID()` answers. Before Init() for the
                            // same reason the position is -- and
                            // `lakvan/sdoor_trapa.s` is one script shared by
                            // four placements that tells them apart with
                            // `if (GetID() = "11s")` in a handler Init()
                            // can reach.
                            door->SetEntityId(e.name);
                            door->SetNetId(netIdFor(saved));  // M129
                            // Post-M120: a restored door's variables and its
                            // passability, before Init -- which is what
                            // lets door.s's Init read the `saved_Open` it
                            // was saved with. SetPassable stamps or lifts
                            // the footprint at the restored heading.
                            if (saved) {
                                if (skTreeNodeObject* node = door->entityScriptNode()) {
                                    sk_bindings::RestoreScriptVars(*node, saved->entity.scriptVars);
                                }
                                callRestoreNative(*door, "SetPassable",
                                                  skRValue(saved->entity.passable != 0));
                                if (saved->entity.flags & 0x1) {  // M122: hidden
                                    callRestoreNative(*door, "ShowEntity", skRValue(false));
                                }
                            }
                            // No script, no Init to run.
                            if (!scriptlessDoor) {
                                skExecutableContext callCtxt(&interpreter);
                                sk_bindings::RunEntityInit(*door, scriptRoot, callCtxt);
                            }
                            DoorInstance inst;
                            inst.placementYaw = PlacementYawRadians(e.yawRaw);
                            inst.rotA = PlacementRotRadians(e.rotARaw);
                            inst.rotB = PlacementRotRadians(e.rotBRaw);
                            inst.scale = PlacementScale(e.scaleRaw);
                            inst.x = static_cast<float>(e.x);
                            inst.y = static_cast<float>(e.y);
                            inst.z = static_cast<float>(e.z);
                            inst.modelArchiveIndex = desc->modelArchiveIndex;
                            inst.typeId = e.typeId;   // M38: zone-trigger identity
                            inst.name = e.name;
                            inst.rotARaw = e.rotARaw;  // post-M120
                            inst.rotBRaw = e.rotBRaw;
                            inst.scaleRaw = e.scaleRaw;
                            inst.script = std::move(door);
                            stack.level().RegisterEntity(e.name, inst.script.get());
                            gameDoors.push_back(std::move(inst));
                        } catch (skParseException& ex) {
                            std::printf("shadowkey-port: PARSE ERROR loading door %s: %s\n",
                                        fullPath.c_str(), ex.toString().ptr());
                        } catch (skTreeNodeReaderException& ex) {
                            std::printf("shadowkey-port: SCRIPT FILE ERROR loading door %s: %s\n",
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
                            // M74: this placement's own entities.txt
                            // category is what gives the object its item
                            // type and its preferred hand -- and, like the
                            // position below, the engine has both before
                            // Init() runs. See game_constants.h.
                            item->SetEntityCategory(desc->category);
                            item->SetTemplateId(e.typeId);
                            // M62: the engine sets a placement's position
                            // (vtable +0x18) *before* calling its Init()
                            // (vtable +0x10) -- see ZONE_FORMAT.md's step
                            // list -- so a script whose Init() reads
                            // GetPositionX/Y/Z sees where it actually is.
                            item->SetWorldPosition(e.x, e.y, e.z);
                            // M92: the placement name GetID() answers --
                            // see the door branch above.
                            item->SetEntityId(e.name);
                            item->SetNetId(netIdFor(saved));  // M129
                            // Post-M120: a restored item's record. A
                            // container's `+0x160` Init guard is part of it
                            // (FUN_10028594 case 0 -- the guard applies to
                            // what is neither item, actor nor door), so a
                            // chest is rebuilt from its record and does not
                            // run its loot roll a second time; anything else
                            // re-runs Init over its restored variables.
                            bool initGuarded = false;
                            if (saved) {
                                if (skTreeNodeObject* node = item->entityScriptNode()) {
                                    sk_bindings::RestoreScriptVars(*node, saved->entity.scriptVars);
                                }
                                const int quantity = saved->kind == sk::SavedEntityKind::Weapon
                                                         ? saved->weapon.quantity
                                                         : saved->stackable.quantity;
                                if (quantity > 0) item->SetQuantity(quantity);
                                item->SetDestroyWhenEmpty(saved->spellbook.destroy != 0);  // M123
                                if (saved->entity.flags & 0x1) {  // M122: hidden
                                    callRestoreNative(*item, "ShowEntity", skRValue(false));
                                }
                                initGuarded = IsContainerCategory(desc->category) &&
                                              saved->spellbook.initDone != 0;
                            }
                            if (initGuarded) {
                                if (saved->entity.nameStringId != 0) {
                                    callRestoreNative(*item, "SetUseText",
                                                      skRValue(static_cast<int>(
                                                          saved->entity.nameStringId)));
                                }
                                callRestoreNative(*item, "SetUsable",
                                                  skRValue(saved->entity.usable != 0));
                                if (saved->entity.shortNameStringId != 0xffff) {
                                    callRestoreNative(*item, "SetName",
                                                      skRValue(static_cast<int>(
                                                          saved->entity.shortNameStringId)));
                                }
                                for (const sk::SavedSpellEntry& entry : saved->spellbook.spells) {
                                    std::unique_ptr<sk_bindings::ItemExecutable> content =
                                        stack.level().CreateItem(entry.typeId);
                                    if (!content) continue;
                                    if (entry.charges > 0) content->SetQuantity(entry.charges);
                                    item->AddContent(std::move(content));
                                }
                            } else {
                                skExecutableContext callCtxt(&interpreter);
                                sk_bindings::RunEntityInit(*item, scriptRoot, callCtxt);
                            }
                            PickupInstance inst;
                            inst.placementYaw = PlacementYawRadians(e.yawRaw);
                            inst.rotA = PlacementRotRadians(e.rotARaw);
                            inst.rotB = PlacementRotRadians(e.rotBRaw);
                            inst.scale = PlacementScale(e.scaleRaw);
                            inst.isContainer = IsContainerCategory(desc->category);
                            inst.x = static_cast<float>(e.x);
                            inst.y = static_cast<float>(e.y);
                            inst.z = static_cast<float>(e.z);
                            inst.modelArchiveIndex = desc->modelArchiveIndex;
                            inst.typeId = e.typeId;  // post-M120
                            inst.yawRaw = e.yawRaw;
                            inst.rotARaw = e.rotARaw;
                            inst.rotBRaw = e.rotBRaw;
                            inst.scaleRaw = e.scaleRaw;
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
                        } catch (skTreeNodeReaderException& ex) {
                            std::printf("shadowkey-port: SCRIPT FILE ERROR loading pickup %s: %s\n",
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
                            // M62: the engine sets a placement's position
                            // (vtable +0x18) *before* calling its Init()
                            // (vtable +0x10) -- see ZONE_FORMAT.md's step
                            // list -- so a script whose Init() reads
                            // GetPositionX/Y/Z sees where it actually is.
                            monster->SetWorldPosition(e.x, e.y, e.z);
                            // M92: the placement name GetID() answers --
                            // see the door branch above.
                            monster->SetEntityId(e.name);
                            monster->SetNetId(netIdFor(saved));  // M129
                            // Post-M120: a restored creature's record, then
                            // its Init -- the engine's order, so whatever
                            // Init sets (SetMaxHealth among it) wins as it
                            // does on the device. A corpse keeps the
                            // `+0x1e4` flag that stops it dying again and
                            // whatever is left of its five-second decay.
                            if (saved) {
                                if (skTreeNodeObject* node = monster->entityScriptNode()) {
                                    sk_bindings::RestoreScriptVars(*node, saved->entity.scriptVars);
                                }
                                monster->RestoreSavedHealth(saved->stats.health,
                                                            saved->stats.maxHealth);
                                if (saved->entity.flags & 0x1) {  // M122: hidden
                                    callRestoreNative(*monster, "ShowEntity", skRValue(false));
                                }
                                if (saved->holder.active != 0) {
                                    int decaySeconds = 0;
                                    if (saved->entity.timerArmed) {
                                        const long long left =
                                            static_cast<long long>(saved->entity.timerDeadline) -
                                            static_cast<long long>(std::time(nullptr));
                                        decaySeconds = static_cast<int>((std::max)(1LL, left));
                                    }
                                    monster->SetDead(true, decaySeconds);
                                }
                            }
                            skExecutableContext callCtxt(&interpreter);
                            sk_bindings::RunEntityInit(*monster, scriptRoot, callCtxt);
                            // M71: step 5 of GameEngine_InitLevel's own
                            // order -- the record's `+0x5e` scale is
                            // written *after* Init(), so the placement wins
                            // over whatever the script asked for at load.
                            monster->SetPlacementScaleRaw(e.scaleRaw);
                            MonsterInstance inst;
                            inst.placementYaw = PlacementYawRadians(e.yawRaw);
                            inst.rotA = PlacementRotRadians(e.rotARaw);
                            inst.rotB = PlacementRotRadians(e.rotBRaw);
                            inst.facingYaw = inst.placementYaw;
                            inst.x = static_cast<float>(e.x);
                            inst.y = static_cast<float>(e.y);
                            inst.z = static_cast<float>(e.z);
                            inst.modelArchiveIndex = desc->modelArchiveIndex;
                            inst.typeId = e.typeId;
                            inst.rotARaw = e.rotARaw;  // post-M120
                            inst.rotBRaw = e.rotBRaw;
                            inst.scaleRaw = e.scaleRaw;
                            inst.script = std::move(monster);
                            inst.script->SetEntityTypeId(inst.typeId);  // M100
                            stack.level().RegisterEntity(e.name, inst.script.get());
                            gameMonsters.push_back(std::move(inst));
                        } catch (skParseException& ex) {
                            std::printf("shadowkey-port: PARSE ERROR loading monster %s: %s\n",
                                        fullPath.c_str(), ex.toString().ptr());
                        } catch (skTreeNodeReaderException& ex) {
                            std::printf("shadowkey-port: SCRIPT FILE ERROR loading monster %s: %s\n",
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
                    // M71: and the two orientation channels and the model
                    // scale the record has always carried and this port
                    // has never read -- 764 scenery placements alone are
                    // authored at something other than 1:1.
                    prop.rotA = PlacementRotRadians(e.rotARaw);
                    prop.rotB = PlacementRotRadians(e.rotBRaw);
                    prop.scale = PlacementScale(e.scaleRaw);
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
                        skExecutableContext callCtxt(&interpreter);
                        sk_bindings::RunEntityInit(*monster, scriptRoot, callCtxt);
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
                    } catch (skTreeNodeReaderException& ex) {
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
                //
                // M75: the object itself was built (and attached to
                // `Level`) before the placement loop above -- see that
                // block's comment. Only its Init() waits until here.
                try {
                    if (!gameZoneScript) {
                        // The construction above already logged why.
                        throw skRuntimeException(skString(""), 0,
                                                  skString("zone script did not load"));
                    }
                    skRValueArray args;
                    args.append(skRValue(0));  // placeholder for Init's "(s)" parameter
                    skRValue ret;
                    skExecutableContext callCtxt(&interpreter);
                    gameZoneScript->method(skString("Init"), args, ret, callCtxt);
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
                    std::printf("shadowkey-port: PARSE ERROR in zone script Init(): %s\n",
                                ex.toString().ptr());
                } catch (skTreeNodeReaderException& ex) {
                    std::printf("shadowkey-port: SCRIPT FILE ERROR in zone script Init(): %s\n",
                                ex.toString().ptr());
                } catch (skRuntimeException& ex) {
                    std::printf("shadowkey-port: RUNTIME ERROR in zone script Init(): %s\n",
                                ex.toString().ptr());
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
                    } catch (skTreeNodeReaderException& ex) {
                        std::printf("shadowkey-port: SCRIPT FILE ERROR in %s DelayReached: %s\n",
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
            // M57: `FUN_1001c9c0`'s own map branch -- an edge-triggered
            // poll of action 9 calling the three-line toggle
            // `FUN_1001ee50`. It sits with the other movement-tick input,
            // not with the menu keys, because that is where the original
            // reads it, and it does not pause anything: the map is an
            // overlay and the game keeps running underneath it.
            if (input.ConsumeBoundJustPressed(sk::Action::MapToggle)) {
                gameMapOpen = !gameMapOpen;
            }
            // M57: the reveal. `TileGrid_RaycastVisibility` ORs a bit into
            // the explored bitmap for every tile a ray passes through, and
            // it does that every frame whether or not the map is open --
            // so this runs unconditionally, off the same call the renderer
            // uses to pick faces.
            if (gameZone) {
                const std::vector<std::pair<int, int>> visible =
                    gameZone->RaycastVisibleTiles(gameCamera.x, gameCamera.y, gameCamera.yaw);
#if SK_DEBUG_SUITE
                // SK_DEBUG_SUITE (M68)
                sk_debug::Count("render.visible_tiles", static_cast<long long>(visible.size()));
#endif
                gameExplored.MarkVisible(visible);
            }
            // M10: the real default control scheme's own CharacterManager
            // action (docs/INPUT_HANDLING.md, KeyHash by default) opens
            // the real charactermanager.s screen chain, pausing the 3D
            // view -- RightSelectionKey (Esc) still returns straight to
            // the main menu when not paused for a menu.
            if (input.ConsumeBoundJustPressed(sk::Action::CharacterManager)) {
                // M90: the engine's own spelling of the name -- see
                // MenuExecutable::GoBack(), which builds this screen's
                // back handler (`CharacterManagerBack`) out of it.
                //
                // Post-M120: **with a level-up point unspent this key opens
                // the level-up screen instead.** `FUN_1002f694`, the level
                // tick that owns action 12: `if (player+0xf44 < 1)
                // FUN_1002c274(ctl, 1) else FUN_1002c274(ctl, 8)` -- mode 1
                // is CharacterManager, mode 8 the `+0x138` screen that
                // DisplayLevelUp also raises, i.e. `levelup.s`. No script
                // ever opens it, so before this a level-up awarded points
                // the player had no way to spend.
                stack.OpenMenu(stack.player().levelUpPoints() > 0 ? "LevelUp"
                                                                  : "CharacterManager");
                inGame = false;
                gamePausedForMenu = true;
                input.ClearPendingEdges();  // M90, see its comment
            } else if (gameMapOpen &&
                       input.ConsumeJustPressed(sk::ButtonSlot::RightSelectionKey)) {
                // **A visible overlay owns the back key** -- the same rule
                // M91 gave a popup on the other side of the pause, and a
                // regression M91 itself introduced on this side. Before
                // this, Esc with the map up fell straight through to the
                // branch below: the main menu came up *over* the map with
                // `gameMapOpen` still set, so Esc again resumed gameplay
                // with the map still covering it, and it read from play as
                // Esc toggling map <-> menu with no way back to the 3D
                // view. `M` still closed it, so it was never a hard lock,
                // but a player who reaches for the back key never finds
                // that out.
                //
                // This is a port-side decision and worth saying so. The
                // engine has no back-key handler for the map at all: the
                // only writers of `player+0x3a4` are `FUN_1001ee50` (the
                // bare toggle the map key calls) and the level-init clear
                // in `FUN_1001f690`, and the real right softkey during
                // gameplay does not raise the main menu -- the only
                // `FUN_100779b8(..., "MainMenu", 1, 0)` at screen mode 5
                // is the app's *foreground-return* handler
                // (`FUN_1002152c`), which is what M91 mapped onto Esc.
                // So what Esc does while an overlay is up is this port's
                // to settle.
                //
                // It settles it the way the engine treats the map
                // everywhere it does have an opinion: as exclusive.
                // `FUN_1002c0e4` -- the HUD text layer, a column of
                // `FUN_1008f8a4` draws at x=15 stepping 15px down --
                // begins with `if (player->mapOpen) return;` and draws
                // nothing at all while the map is up. The map owns the
                // screen; here it owns the back key with it.
                gameMapOpen = false;
                input.ClearPendingEdges();  // M90, see its comment
            } else if (input.ConsumeJustPressed(sk::ButtonSlot::RightSelectionKey)) {
                // M91: **the in-game main menu.** This used to be a bare
                // `inGame = false`, which dropped the player onto
                // whatever menu object happened to still be current --
                // in practice the front-end MainMenu, with its front-end
                // rows, because nothing had re-run its OnDisplay since
                // boot. So the pause menu offered New Game and Credits
                // and had no Save Game at all.
                //
                // The real entry point is `FUN_100779b8(app->0x20,
                // "MainMenu", 1, 0)` -- the app's own foreground handler
                // (FUN_1002152c) raises exactly that when it comes back
                // to the front with the screen mode still on gameplay.
                // The third argument is the flag the menu manager keeps
                // at `+0x49`, "this screen is open over a live session",
                // and it is this port's gamePausedForMenu. mainmenu.s
                // does the rest by itself: its OnDisplay is one long
                // `if (GameActive())`, so with M91's real GameActive the
                // same script builds Return to Game / Load / Save Game /
                // Delete / Options / End Game instead.
                //
                // OpenMenu (not ReopenMenu): Init() is where mainmenu.s
                // CreateMenu()s its nineteen children, and re-running it
                // would rebuild all of them. OnDisplay is the per-visit
                // half, and OpenMenu runs exactly that.
                stack.OpenMenu("MainMenu");
                inGame = false;
                gamePausedForMenu = true;
                input.ClearPendingEdges();  // M90, see its comment
            } else {
                // M130: the walk, the turn and the look are the engine's own
                // (simkin_bindings/player_move.h) -- a thrust and a friction
                // off the Speed attribute, rates that ease in and out, and
                // the actor move's own wall test with the player's real
                // 0x80 half-extent. The fixed 40 units a tick, 0.06 and
                // 0.05 radians a tick and the 48-unit circle are gone.
                constexpr float kPlayerRadius =
                    static_cast<float>(sk_bindings::kPlayerRadiusUnits);
                constexpr float kUnitsToRadians = 6.28318530718f / 65536.0f;
                constexpr int kDelta = sk_bindings::kAiFrameDeltaUnits;
                // yaw's forward vector rotates toward -right as yaw increases
                // (see render3d/zone_renderer.cpp's forward/right basis), so
                // turning right means *decreasing* yaw -- and the engine's
                // heading runs the other way (yaw = pi/2 - heading), so its
                // negative rate (action 2, vtable +0x1b8) is a left turn.
                // M99: through the binding table, as FUN_1001c9c0 polls them
                // (actions 2/3), so the key-configuration screen's changes
                // reach the game.
                {
                    const int rate = sk_bindings::PlayerTurnTarget(/*zoomed=*/false);
                    if (input.GetBoundButton(sk::Action::TurnLeft)) {
                        gameMotion.turnTarget = -rate;
                    } else if (input.GetBoundButton(sk::Action::TurnRight)) {
                        gameMotion.turnTarget = rate;
                    } else {
                        gameMotion.turnTarget = 0;
                    }
                    gameMotion.turnRate =
                        sk_bindings::RampTurnRate(gameMotion.turnRate, gameMotion.turnTarget);
                }
                // LookUp / LookDown (actions 4/5, vtable +0x1c4 / +0x1c8).
                // Holding either overrides the automatic aim-assist below;
                // the rate keeps easing out for a few ticks after release.
                bool manualLook = false;
                {
                    const int rate = sk_bindings::Mp().active
                                         ? sk_bindings::kPlayerLookRateSession
                                         : sk_bindings::kPlayerLookRate;
                    if (input.GetBoundButton(sk::Action::LookUp)) {
                        gameMotion.lookTarget = -rate;
                        manualLook = true;
                    } else if (input.GetBoundButton(sk::Action::LookDown)) {
                        gameMotion.lookTarget = rate;
                        manualLook = true;
                    } else {
                        gameMotion.lookTarget = 0;
                    }
                    gameMotion.lookRate =
                        sk_bindings::RampLookRate(gameMotion.lookRate, gameMotion.lookTarget);
                }
                // M125: mouse look, the PC's version of the right stick --
                // but analog, applied straight to the heading and the
                // pitch instead of pressing TurnLeft/LookUp for a tick.
                // A look key (or the stick, which presses the same ones)
                // takes the pitch back from the mouse, so the aim-assist
                // below works for whoever is not using it.
                if (manualLook) gameMousePitchOwned = false;
                {
                    const sk::PcSettings& pc = pcSettings.settings();
                    gameMouseLookWanted = pc.mouseLook();
                    int mouseDx = 0, mouseDy = 0;
                    window.TakeMouseDelta(&mouseDx, &mouseDy);
                    if (!pc.mouseLook()) {
                        gameMousePitchOwned = false;
                    } else {
                        const float k = pc.mouseLookRadiansPerCount();
                        // Mouse right turns right (yaw decreases, see
                        // above); mouse forward (dy < 0) looks up, as
                        // LookUp does, by lowering the pitch.
                        gameMouseYawLastTick = -static_cast<float>(mouseDx) * k;
                        gameCamera.yaw += gameMouseYawLastTick;
                        if (mouseDy != 0 && !manualLook) gameMousePitchOwned = true;
                        if (gameMousePitchOwned) {
                            const float before = gameCamera.pitch;
                            gameCamera.pitch = std::clamp(
                                gameCamera.pitch + static_cast<float>(mouseDy) * k,
                                -sk::kMaxCameraPitch, sk::kMaxCameraPitch);
                            gameMousePitchLastTick = gameCamera.pitch - before;
                            manualLook = true;
                        }
                    }
                }
                // M72: the real actor tick tests the player's own velocity
                // (`player+0x98`/`+0xa0` non-zero) to decide whether the
                // pitch may drift back to level; the port asks whether the
                // position changed, which is the same question one step on.
                const float preMoveCamX = gameCamera.x;
                const float preMoveCamY = gameCamera.y;
                // `FUN_100445d4`: Speed << 8 >> 1, one more bit while the
                // fatigue pool is empty (M73), capped at 0x4000.
                const int moveStep = sk_bindings::PlayerMoveStep(stack.player().speed(),
                                                                 stack.player().actorFatigue());
                const int moveHeading = CameraHeadingUnits(gameCamera.yaw);
                constexpr float kBlockHeightDelta = sk::kEyeHeightOffset * 2.0f;
                // M55: real per-model collision boxes from
                // `<zone>_models.txt` columns 2-4. A candidate must be solid
                // with both extents non-zero and not `SetPassable(true)`
                // (`Entity+0xd5`) -- doors (closed until door.s opens them,
                // M67), live creatures (M92's passable NPCs excepted) and
                // the static props that are most of what blocks you. Loot
                // and world items are `0 0 0` in every shipped manifest.
                struct SolidEnt {
                    int x, y, halfX, halfY;
                };
                std::vector<SolidEnt> solids;
                {
                    auto add = [&](float ex, float ey, int modelIndex) {
                        const sk::ModelCollision& c = gameModelCollision.At(modelIndex);
                        if (!c.blocks()) return;
                        solids.push_back({static_cast<int>(std::floor(ex)),
                                          static_cast<int>(std::floor(ey)), c.halfExtentX,
                                          c.halfExtentY});
                    };
                    for (const DoorInstance& d : gameDoors) {
                        if (d.script->entityOutOfWorld()) continue;  // M92 hidden, M101 removed
                        if (d.script->passable()) continue;
                        add(d.x, d.y, d.modelArchiveIndex);
                    }
                    for (const MonsterInstance& m : gameMonsters) {
                        if (!m.script->alive() || m.script->outOfWorld()) continue;
                        if (m.script->entityPassable()) continue;
                        // Vertical separation, so a creature on another
                        // floor of the same tile column doesn't block. The
                        // port's own addition -- the real walk is purely
                        // 2D, because its tile lists are.
                        if (std::fabs(gameCamera.z - (m.z + sk::kEyeHeightOffset)) >
                            kBlockHeightDelta) {
                            continue;
                        }
                        add(m.x, m.y, m.modelArchiveIndex);
                    }
                    for (const sk::PlacedEntity& e : gameEntities) {
                        add(e.x, e.y, e.modelArchiveIndex);
                    }
                }
                // The player's Height() is `CMap+0x1a` (FUN_10006230,
                // standing stance), the field the eye offset is read from;
                // M84 pinned it at 0x200. Nothing in the image writes it
                // (its loader is dead code -- the 2026-11-11 audit), so it
                // is still the derived value.
                constexpr float kPlayerCollisionHeight = sk::kEyeHeightOffset;
                const float feetZ = gameCamera.z - sk::kEyeHeightOffset;
                const int startX = static_cast<int>(std::floor(gameCamera.x));
                const int startY = static_cast<int>(std::floor(gameCamera.y));
                // `FUN_10000ab8`, the tile rule: the wall flag, the 0x180
                // step limit and the headroom (Zone::ActorTileBlocked, with
                // the floor sampled at the actor's own offsets inside the
                // probed tile, as the engine passes them), and then the
                // tile's entity list -- a solid, non-passable entity linked
                // into a tile closes the whole tile.
                auto tileRule = [&](int px, int py, int sx, int sy) {
                    if (px < 0 || py < 0) return true;
                    const int tx = px >> 8, ty = py >> 8;
                    if (!gameZone->InBounds(tx, ty)) return true;
                    if (gameZone->ActorTileBlocked(
                            static_cast<float>(tx * 256 + (sx & 0xff)),
                            static_cast<float>(ty * 256 + (sy & 0xff)), feetZ,
                            kPlayerCollisionHeight, /*creature=*/false)) {
                        return true;
                    }
                    for (const SolidEnt& e : solids) {
                        if ((e.x >> 8) == tx && (e.y >> 8) == ty) return true;
                    }
                    return false;
                };
                // The one guard that is not the engine's. There, nothing
                // solid can come to share the player's tile, because the
                // same rule keeps every other actor out of it. This port's
                // creature walk and its scripted spawns are not held to
                // that, and the engine's answer to a blocked centre tile is
                // to put the player back where the tick began -- forever.
                // So a tile the player is *already* standing in while the
                // rule refuses it is held to its wall flag alone.
                const bool startStuck = tileRule(startX, startY, startX, startY);
                sk_bindings::PlayerMoveWorld moveWorld;
                moveWorld.mapWidth = gameZone->width();
                moveWorld.mapHeight = gameZone->height();
                moveWorld.tileBlocked = [&](int px, int py, int sx, int sy) {
                    if (startStuck && px >= 0 && py >= 0 && (px >> 8) == (startX >> 8) &&
                        (py >> 8) == (startY >> 8)) {
                        return gameZone->CellAt(px >> 8, py >> 8).IsWall();
                    }
                    return tileRule(px, py, sx, sy);
                };
                // FUN_100017c8's second half walks one list, the mover's
                // own tile's (M124): what a wide prop does to its
                // neighbours is the rotated tile stamp's job.
                moveWorld.solidsInTile = [&](int tx, int ty,
                                             std::vector<sk_bindings::MoveBox>& out) {
                    for (const SolidEnt& e : solids) {
                        if ((e.x >> 8) == tx && (e.y >> 8) == ty) {
                            out.push_back({e.x, e.y, e.halfX, e.halfY});
                        }
                    }
                };
                auto thrust = [&](sk_bindings::MoveDir dir) {
                    // M73: the movement fatigue drain, `FUN_10045228`, hangs
                    // off vtable +0x1dc, which all four base move functions
                    // call -- once per direction held, blocked or not.
                    const int drain = sk_bindings::MovementFatigueDrain(
                        gameMoveFatigueAccum, sk_bindings::kAiFrameDeltaUnits);
                    if (drain > 0) {
                        stack.player().SetActorFatigue(stack.player().actorFatigue() - drain);
                    }
                    sk_bindings::PlayerThrust(gameMotion, moveHeading, moveStep, dir);
                    // `FUN_1001f230`, the bob, takes the same step.
                    gameSwayThisTick += (kDelta * (moveStep & 0xffff)) >> 8;
                };
                // M99: actions 0/1/6/7 through the bindings, in the engine's
                // order (vtable +0x1cc, +0x1d0, +0x1d4, +0x1d8).
                if (input.GetBoundButton(sk::Action::MoveForward)) thrust(sk_bindings::MoveDir::Forward);
                if (input.GetBoundButton(sk::Action::MoveBackward)) thrust(sk_bindings::MoveDir::Backward);
                if (input.GetBoundButton(sk::Action::SideStepLeft)) thrust(sk_bindings::MoveDir::StrafeLeft);
                if (input.GetBoundButton(sk::Action::SideStepRight)) thrust(sk_bindings::MoveDir::StrafeRight);
                // FUN_10000498's friction, then FUN_10000c64.
                gameMotion.vx = sk_bindings::PlayerFriction(gameMotion.vx);
                gameMotion.vy = sk_bindings::PlayerFriction(gameMotion.vy);
                gameMotion.x = startX;
                gameMotion.y = startY;
                bool freeMove = false;
#if SK_DEBUG_SUITE
                // SK_DEBUG_SUITE (M68): noclip -- the five steps with
                // nothing tested at all.
                freeMove = debugRefs.noclip;
#endif
                if (freeMove) {
                    const int qx = sk_bindings::move_detail::Quarter(gameMotion.vx);
                    const int qy = sk_bindings::move_detail::Quarter(gameMotion.vy);
                    gameMotion.x += 5 * ((qx * kDelta) >> 8);
                    gameMotion.y += 5 * ((qy * kDelta) >> 8);
                } else {
                    // M95: a ghost (`SetGhost(true)`) takes the move's other
                    // arm -- no wall, no entity, seven quartered steps.
                    sk_bindings::PlayerActorMove(gameMotion, moveWorld, kDelta,
                                                 stack.player().ghost(), !onGround);
                }
                if (gameMotion.x != startX) gameCamera.x = static_cast<float>(gameMotion.x);
                if (gameMotion.y != startY) gameCamera.y = static_cast<float>(gameMotion.y);
                // The move also integrates the two rates: heading and pitch
                // `+= rate * delta >> 8`, the pitch stopped at +-0x2000.
                gameCamera.yaw -=
                    static_cast<float>((gameMotion.turnRate * kDelta) >> 8) * kUnitsToRadians;
                gameCamera.pitch +=
                    static_cast<float>((gameMotion.lookRate * kDelta) >> 8) * kUnitsToRadians;
                if (gameCamera.pitch > sk::kMaxCameraPitch) {
                    gameCamera.pitch = sk::kMaxCameraPitch;
                    gameMotion.lookRate = 0;
                } else if (gameCamera.pitch < -sk::kMaxCameraPitch) {
                    gameCamera.pitch = -sk::kMaxCameraPitch;
                    gameMotion.lookRate = 0;
                }

                // M39: scripted teleports. azra.s's `Birg.SummonMe()` and
                // friends are ordinary script handlers whose whole body is
                // a SetPosition (see MonsterExecutable's handler), so the
                // move has to be mirrored onto the live instance -- drained
                // here rather than inside the script call, same
                // defer-to-a-safe-point convention as PickupItem().
                //
                // M62: the same drain now covers every placed entity class,
                // because `SetPosition` is one binding on the Object/Entity
                // base they all inherit (simkin_bindings/
                // entity_position_ref.h), and it applies the real snap
                // rule: an **actor** lands on the surface of the tile it
                // arrives in, anything else lands exactly where it was put.
                auto applyTeleport = [&](sk_bindings::EntityBaseRef& script, float& x,
                                          float& y, float& z) {
                    float nx = 0, ny = 0, nz = 0;
                    if (!script.TakePendingPosition(nx, ny, nz)) return false;
                    const int lift = script.TakePendingSurfaceLift();  // M100
                    x = nx;
                    y = ny;
                    z = nz;
                    // `Map_GetTileAt(engine, x, y) != 0` in the real case --
                    // a destination outside the grid skips the snap rather
                    // than clamping.
                    if (script.isActorForPositioning() && gameZone &&
                        gameZone->InBounds(static_cast<int>(std::floor(nx / sk::kTileScale)),
                                            static_cast<int>(std::floor(ny / sk::kTileScale)))) {
                        // SnapActorToGround is the 0x80 lift; FindPathNode
                        // asks for 300 on the same surface.
                        z = gameZone->SnapActorToGround(nx, ny, nz) +
                            static_cast<float>(lift - sk_bindings::EntityBaseRef::kSetPositionSurfaceLift);
                    }
                    return true;
                };
                for (MonsterInstance& m : gameMonsters) {
                    if (!m.script) continue;
                    applyTeleport(*m.script, m.x, m.y, m.z);
                    // M92: `SetRotationTurn(raw)` -- `entity+0xb6`, the same
                    // field a door's swing accumulates into. Drained here
                    // rather than read per frame because the AI tick owns a
                    // creature's facing and would overwrite it on the very
                    // next one. Both shipped sites are the scripted herb
                    // scene turning Trothgar to face the player
                    // (`Trthgar.SetRotationTurn(-26414)`).
                    int facingRaw = 0;
                    if (m.script->TakePendingRotation(facingRaw)) {
                        m.facingYaw = sk_bindings::PortYawFromEngineYaw(facingRaw & 0xffff);
                    }
                }
                for (DoorInstance& d : gameDoors) {
                    if (d.script) applyTeleport(*d.script, d.x, d.y, d.z);
                }
                for (PickupInstance& p : gamePickups) {
                    if (p.script) applyTeleport(*p.script, p.x, p.y, p.z);
                }
                {
                    // The player's own teleport -- 44 of the corpus's 63
                    // `SetPosition` call sites. The camera *is* the player
                    // here, so the resolved ground height gets the same
                    // eye offset the zone-load path applies, and the fall
                    // velocity is zeroed because the real case zeroes the
                    // entity's motion deltas (`+0x98`/`+0xa0`) on every
                    // teleport.
                    float px = gameCamera.x, py = gameCamera.y, pz = 0.0f;
                    if (applyTeleport(stack.player(), px, py, pz)) {
                        gameCamera.x = px;
                        gameCamera.y = py;
                        gameCamera.z = pz + sk::kEyeHeightOffset;
                        gameVelZ = 0.0f;
                        gameMotion.vx = gameMotion.vy = 0;
                        onGround = true;
                    }
                    // M91: and the *other* direction. Every placed entity
                    // gets its EntityBaseRef seeded at zone load so a
                    // script's own `GetPositionX()` reads where the thing
                    // is (see SetWorldPosition's comment) -- the player
                    // never did, so `GetPlayer().GetPositionX()` answered
                    // 0 and a save recorded the origin. The camera *is*
                    // the player, so mirror it here, once the tick's
                    // movement and any teleport have both settled. Ground
                    // level, not eye level, because that is the height a
                    // scripted `SetPosition` writes.
                    //
                    // SetWorldPosition does not raise the dirty flag, so
                    // this cannot feed back into applyTeleport above.
                    stack.player().SetWorldPosition(
                        static_cast<int>(gameCamera.x), static_cast<int>(gameCamera.y),
                        static_cast<int>(gameCamera.z - sk::kEyeHeightOffset));
                    stack.SetPlayerHeadingUnits(CameraHeadingUnits(gameCamera.yaw));
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
                        inst.script->SetEntityTypeId(inst.typeId);  // M100
                        std::printf("shadowkey-port: CreateEntity(%d) spawned \"%s\" at "
                                    "(%d, %d, %d)\n",
                                    req.typeId, inst.script->name().c_str(), req.x, req.y, req.z);
#if SK_DEBUG_SUITE
                        // SK_DEBUG_SUITE (M68)
                        sk_debug::Count("entity.created");
#endif
                        gameMonsters.push_back(std::move(inst));
                    }
                }

                // M81: `Level.CreateEntityScript(typeId, script, x, y, z)`
                // -- the same split, for the item form. This is the
                // *script-driven* loot drop: `monsters/arat.s` and
                // `monsters/spiderqueen.s` roll their own `Random(1,12)=12`
                // inside their own OnKilled() and place their own bag with
                // it, rather than leaving it to SetLoot; crypt1's
                // `shadowkeygate.s` uses it for three typeId-301 chests.
                // See LevelExecutable::PendingPlacedItem.
                {
                    sk_bindings::LevelExecutable::PendingPlacedItem req;
                    std::unique_ptr<sk_bindings::ItemExecutable> script;
                    while (stack.level().TakePendingPlacedItem(req, script)) {
                        if (!script) continue;
                        const sk::EntityTypeDescriptor* desc = entityTypes.Lookup(req.typeId);
                        PickupInstance inst;
                        inst.x = static_cast<float>(req.x);
                        inst.y = static_cast<float>(req.y);
                        inst.z = static_cast<float>(req.z);
                        if (req.snapToGround && gameZone) {
                            inst.z = gameZone->SnapSpawnedObjectToGround(inst.x, inst.y);
                        }
                        inst.modelArchiveIndex = desc ? desc->modelArchiveIndex : -1;
                        inst.isContainer = desc && IsContainerCategory(desc->category);
                        inst.typeId = req.typeId;  // post-M120: saveable
                        script->SetWorldPosition(static_cast<int>(inst.x),
                                                  static_cast<int>(inst.y),
                                                  static_cast<int>(inst.z));
                        std::printf("shadowkey-port: CreateEntityScript(%d) spawned \"%s\" at "
                                    "(%d, %d, %d)\n",
                                    req.typeId, script->name().c_str(), static_cast<int>(inst.x),
                                    static_cast<int>(inst.y), static_cast<int>(inst.z));
#if SK_DEBUG_SUITE
                        // SK_DEBUG_SUITE (M68)
                        sk_debug::Count("entity.created");
#endif
                        inst.script = std::move(script);
                        gamePickups.push_back(std::move(inst));
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
                                inst.script->SetEntityTypeId(inst.typeId);  // M100
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
                //
                // M83: **the whole walk only runs on a tick the player
                // actually moved.** The real one lives in the actor tick
                // `FUN_1001c9c0`, and wraps its 40-slot loop in
                // `if (player->+0x98 != 0 || player->+0xa0 != 0)` -- the two
                // fields either side of the position words `+0x94`/`+0x9c`,
                // and the same pair M72 already identified as this tick's
                // movement delta (the collision code zeroes one of them and
                // snaps the matching position back, so a walk into a wall
                // reads as not moving). Standing still, the engine never
                // even asks which room it is in, so the per-room "inside"
                // flags at `engine+0x54e4` keep their previous values --
                // which is why this skips the block outright rather than
                // only suppressing the fire.
                //
                // M44 had this backwards, and said so in as many words: "a
                // fresh zone starts with nobody inside any region, so the
                // first tick fires EnterZone for wherever the player
                // spawns". It does not, and it must not. Twelve of the 21
                // zones spawn the player *inside* one of their own regions
                // (docs/ZONE_FORMAT.md's inclusive-containment note lists
                // them -- azra's `start` is x 118..121, y 42..46 and its
                // player start is exactly (118, 46), on the far edge), so
                // this port fired those handlers during the zone-load tick,
                // before the player had done anything at all. In azra that
                // is `starthelp`, the game's first tutorial popup, which in
                // the original appears only once you have walked a few
                // steps -- and while it was up `inGame` was false, so a
                // fresh session opened with the world inert behind a popup
                // nobody had triggered.
                //
                // Reusing preMoveCamX/preMoveCamY costs nothing and is the
                // same stand-in for that velocity pair M72 already uses.
                const bool playerMovedThisTick =
                    gameCamera.x != preMoveCamX || gameCamera.y != preMoveCamY;
                if (gameZone && gameZoneScript && !gameZone->regions().empty() &&
                    playerMovedThisTick) {
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
                        // M80: **an EnterZone handler that opens a menu
                        // has to hand the frame over to it.** This is what
                        // the game's tutorials, its region-triggered
                        // warnings and a good deal of its scripted story
                        // are made of -- all 21 zone root scripts have an
                        // EnterZone, 19 of them open a menu from inside it,
                        // and those handlers hold 128 OpenMenu call sites
                        // between them (azra's eight: starthelp,
                        // daggerhelp, junction, action_queue, temple,
                        // ratchestclue, OhNoSkelos, azra_menu_yousure;
                        // crypt1.s has 37). The menu was already being
                        // *built* -- OpenMenu ran, its Init() ran, its rows
                        // existed -- and then the 3D arm below drew
                        // straight over it and returned, so no popup ever
                        // reached the screen.
                        //
                        // Same before/after currentMenu() comparison, and
                        // the same pause, as the Use path (a door, an NPC's
                        // conversation, a loot bag) and the OnDetect path
                        // already use. RightSelectionKey and the script's
                        // own Quit() both come back to gameplay through
                        // gamePausedForMenu exactly as they do from a
                        // conversation.
                        sk_bindings::MenuExecutable* beforeRegionMenu = stack.currentMenu();
                        sk_bindings::EncounterExecutable* encounter =
                            gameZoneScript->EnterRegion(regions[i].name);
                        if (stack.currentMenu() != beforeRegionMenu) {
                            inGame = false;
                            gamePausedForMenu = true;
                            input.ClearPendingEdges();  // M90, see its comment
                            // Deliberately keeps walking the rest of the
                            // region list: the set diff below has to see
                            // every region the player stands in this tick,
                            // or the ones not visited here fire again next
                            // tick, on top of the menu that just opened.
                        }
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
                constexpr float kHeadroom = 32.0f;    // 0x20, the engine's eye-to-ceiling gap
                if (onGround && input.ConsumeBoundJustPressed(sk::Action::Jump)) {
                    // M51: the real jump (FUN_10044400) is not free. It
                    // refuses outright unless current fatigue is above 5,
                    // spends 5, and plays pl_jump_female.wav (slot 91) or
                    // pl_jump_male.wav (92) chosen on player+0xfac -- the
                    // field SetSex writes (M50). So slot 91 is what a
                    // character with sex 0 uses, which makes 0 female.
                    if (stack.player().actorFatigue() > sk_bindings::kJumpFatigueCost) {
                        stack.player().SetActorFatigue(stack.player().actorFatigue() -
                                                        sk_bindings::kJumpFatigueCost);
                        playPlayerSound(stack.player().sex() == 0 ? sk::kSoundJumpFemale
                                                                  : sk::kSoundJumpMale);
                        gameVelZ = kJumpSpeed;
                        onGround = false;
                    }
                }
                // Post-M121: **fall damage**, the actor move's first test
                // (`FUN_10000c64`): `if (vz < -0xa00 && self == player)
                // stats->TakeDamage(-vz >> 8, 0, 0, 0)` -- unsourced, and
                // every tick the fall is that fast, before this tick's
                // gravity. `vz` is in the engine's per-256-delta units, and
                // this port's velocity is per tick: x 256 / delta. 0xa00 is
                // reached after ~1300 units of free fall at the port's
                // gravity; a short drop never hurts.
                {
                    constexpr int kFallDamageSpeed = -0xa00;
                    const int engineVz = static_cast<int>(
                        gameVelZ * 256.0f / static_cast<float>(sk_bindings::kAiFrameDeltaUnits));
                    if (engineVz < kFallDamageSpeed) {
                        stack.player().ApplyDamage((-engineVz) >> 8);
                    }
                }
                gameVelZ -= kGravity;
                if (gameVelZ < -kMaxFallSpeed) gameVelZ = -kMaxFallSpeed;
                gameCamera.z += gameVelZ;

                // M62: the real ground function, not the raw corner blend.
                // `Zone::CollisionFloorHeightAt` is `FUN_1001beac` and has
                // been in this port since M28 -- but only an arrow ever
                // called it; the player's own footing used
                // `FloorHeightAt`, which always bilinears the four corners
                // and never consults the cell's flags. Two differences,
                // both visible: a tile whose `flags & 0x04` is clear is
                // authored **flat**, at `ZcpEntry+2`, and that is not the
                // same as the corner average (it matches `min(corners)` for
                // only 8% of azra's entries); and a two-storey tile lets an
                // actor already above its ceiling stand on it, which is
                // what an upper floor or a bridge is made of. Standing on
                // one was previously impossible -- the player fell through
                // to the storey below. Passing `gameCamera.z` is what makes
                // that branch reachable, and it is the argument
                // `FloorHeightAt` had no way to take.
                //
                // Post-M120: and probed where the engine probes it. The
                // actor move's ground clamp (`FUN_10000c64`'s tail) asks
                // `FUN_1001beac` for the floor at **feet + 0x180**, the same
                // probe the step limit above uses; this passed the *eye*
                // height, 800 above the feet, which picked a two-storey
                // tile's upper floor from far below it and stood the player
                // on top -- the other half of walking up walls.
                constexpr float kGroundProbe = 384.0f;  // 0x180
                float floorEyeZ =
                    gameZone->CollisionFloorHeightAt(
                        gameCamera.x, gameCamera.y,
                        gameCamera.z - sk::kEyeHeightOffset + kGroundProbe) +
                    sk::kEyeHeightOffset;
                if (gameVelZ <= 0.0f && gameCamera.z <= floorEyeZ) {
                    gameCamera.z = floorEyeZ;
                    gameVelZ = 0.0f;
                    onGround = true;
                } else {
                    onGround = false;
                }
                // M122: the death floor, for the player at its own feet.
                if (onDeathFloor(gameCamera.x, gameCamera.y,
                                 gameCamera.z - sk::kEyeHeightOffset - 0.5f) &&
                    stack.player().health() > 0) {
                    stack.player().ApplyDamage(stack.player().health());
                }
                // Post-M121: the engine's eye clamp, in the player's input
                // poll after `eye = z + CMap+0x1a`: `ceiling(feet + 0x180)
                // - 0x20`, with FUN_1001bdf4's open sky (0x4000) where the
                // tile has no solid ceiling or the player is above it.
                float ceilingZ = gameZone->CollisionCeilingHeightAt(
                    gameCamera.x, gameCamera.y,
                    gameCamera.z - sk::kEyeHeightOffset + kGroundProbe);
                if (gameCamera.z + kHeadroom > ceilingZ) {
                    gameCamera.z = ceilingZ - kHeadroom;
                    if (gameVelZ > 0.0f) gameVelZ = 0.0f;
                }

                // M77: the creature tick below is no longer a from-scratch
                // design. It is FUN_10082224 transcribed -- its package
                // branch, its perception test, its cadence gate, its
                // one-line give-up rule -- with only the movement step (the
                // engine pathfinds through the zone's `.pth` nodes, which
                // are undecoded) and the SetCanTeleport landing spot left
                // as this port's own. The whole derivation, and the four
                // things earlier milestones had backwards, are in
                // simkin_bindings/monster_ai.h; the OnDetect arm's own
                // derivation stays in simkin_bindings/on_detect.h.
                //
                // The one that mattered most: a placed creature does not
                // start asleep. Entity vtable slot +0x10 puts it in package
                // 2 at spawn, before its script's Init() runs -- which is
                // what 280 of the 318 shipped `SetAggressive(true)` scripts
                // depend on, none of them calling any Ai* binding at all.
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
                // M78: bare-handed attacking is gone -- `FUN_10042394`
                // does nothing at all for an empty hand, so there is no
                // fist reach to define. (M131: nor a label radius -- the
                // health label `kMeleeRange` floored is gone too.)
                // M122: a creature's walking speed is no longer a tuned
                // constant here (it was 14 units/tick) -- it is the engine's
                // force-and-friction walk off the creature's Speed stat, see
                // stepCreature and monster_ai.h.
                constexpr float kMonsterRadius = 40.0f;     // world units, wall-collision only
                // M77: `kAggroMaxHeightDelta` and `kLoseInterestTicks`
                // used to live here -- a vertical gate on acquisition and a
                // lost-sight grace period. Both were this port's own
                // inventions, written when the acquire arm was believed to
                // be sight-gated. It is not: its only gates are the
                // scaled-squared distance against SetChaseRadius and the
                // perception roll, and giving up is the same distance and
                // nothing else. The real vertical limit exists but sits on
                // the *attack* (0x200 raw units, and only for melee), which
                // is where it now is. See simkin_bindings/monster_ai.h.

                // Post-M120: **one creature step, the engine's way.** The
                // original has no pathfinder for a chase: `FUN_100008f0`
                // turns the creature to its move goal (the target's own
                // position, rewritten every tick) and walks it forward, and
                // the actor move then resolves the step against the world
                // one axis at a time (`FUN_100017c8`) -- which is what
                // slides a creature along a wall instead of stopping it
                // dead. This port used to try a fan of seven headings
                // instead, its own invention, and tested only the wall
                // flag; the tile rule (Zone::ActorTileBlocked,
                // `FUN_10000ab8`) also refuses a step more than 0x180 high,
                // a ceiling too low for the creature's Height(), and the
                // 0x40 barrier tiles the level designer fenced creatures
                // out with -- 6242 of them across 13 zones (m121).
                //
                // M122: and the step is the engine's own walk, not a tuned
                // constant: friction, then the creature's Speed pushed along
                // its heading, then `v * frameDelta >> 8` (monster_ai.h). A
                // blocked axis loses its delta, as FUN_100017c8 zeroes it.
                // `maxStep` is the stand-off clamp the chase uses.
                //
                // M132: **and the creature's side of the box.** A creature
                // runs the same actor move the player does (`FUN_10000c64`),
                // minus the quartering and the four extra steps:
                //
                //     pos += v * delta >> 8;  clamp to the map by the radius;
                //     if (!ghost && radius != 0) {
                //         FUN_100017c8();                    // the push-out
                //         if (FUN_10000ab8(centre tile)) {   // the tile rule
                //             pos = where the tick began;
                //             a zeroed velocity axis restarts at half;
                //         }
                //     }
                //
                // with `radius` its own `+0x8e`, the model manifest's half
                // extent, and the tile rule's last clause the one M130 gave
                // the player: a tile with any other solid, non-passable
                // entity linked into it is closed. The player is such an
                // entity. So a creature stops with its box against the edge
                // of the player's tile, and two creatures never share one.
                //
                // That is what this port's 48 + 40 contact distance and its
                // 70-unit crowd push were standing in for; both are gone,
                // with the 40-unit wall probe.
                auto stepCreature = [&](MonsterInstance& m, float dirX, float dirY,
                                        float maxStep) {
                    m.vx = sk_bindings::DecayVelocity(m.vx, sk_bindings::kGroundFriction) +
                           sk_bindings::CreatureThrust(m.script->speed(), dirX);
                    m.vy = sk_bindings::DecayVelocity(m.vy, sk_bindings::kGroundFriction) +
                           sk_bindings::CreatureThrust(m.script->speed(), dirY);
                    float sx = static_cast<float>((m.vx * sk_bindings::kAiFrameDeltaUnits) >> 8);
                    float sy = static_cast<float>((m.vy * sk_bindings::kAiFrameDeltaUnits) >> 8);
                    // `maxStep` survives for the script's WalkTo, which this
                    // port stops on its goal.
                    const float len = std::sqrt(sx * sx + sy * sy);
                    if (len > maxStep && len > 0.0f) {
                        sx *= maxStep / len;
                        sy *= maxStep / len;
                    }
                    const float height =
                        static_cast<float>(sk::MonsterCollisionHeight(m.modelArchiveIndex));
                    const sk::ModelCollision& own = gameModelCollision.At(m.modelArchiveIndex);
                    const int radius = own.halfExtentX;
                    const int startX = static_cast<int>(std::floor(m.x));
                    const int startY = static_cast<int>(std::floor(m.y));
                    sk_bindings::PlayerMotion motion;
                    motion.x = startX + static_cast<int>(sx);
                    motion.y = startY + static_cast<int>(sy);
                    motion.vx = static_cast<int>(sx);
                    motion.vy = static_cast<int>(sy);
                    auto commit = [&]() {
                        if (motion.x != startX) m.x = static_cast<float>(motion.x);
                        if (motion.y != startY) m.y = static_cast<float>(motion.y);
                    };
                    // `uVar9 != 0`: an actor with no box is not tested.
                    if (radius == 0 || m.script->entityPassable()) {
                        commit();
                        return;
                    }
                    // Every other solid, non-passable entity, as the tile's
                    // list would hold them.
                    std::vector<sk_bindings::MoveBox> solids;
                    {
                        auto add = [&](float ex, float ey, int modelIndex) {
                            const sk::ModelCollision& c = gameModelCollision.At(modelIndex);
                            if (!c.blocks()) return;
                            solids.push_back({static_cast<int>(std::floor(ex)),
                                              static_cast<int>(std::floor(ey)), c.halfExtentX,
                                              c.halfExtentY});
                        };
                        for (const DoorInstance& d : gameDoors) {
                            if (d.script->entityOutOfWorld() || d.script->passable()) continue;
                            add(d.x, d.y, d.modelArchiveIndex);
                        }
                        for (const MonsterInstance& other : gameMonsters) {
                            if (&other == &m) continue;
                            if (!other.script->alive() || other.script->outOfWorld()) continue;
                            if (other.script->entityPassable()) continue;
                            // Another floor of the same column: the port's
                            // own allowance, as for the player.
                            if (std::fabs(other.z - m.z) > sk::kEyeHeightOffset * 2.0f) continue;
                            add(other.x, other.y, other.modelArchiveIndex);
                        }
                        for (const sk::PlacedEntity& e : gameEntities) {
                            add(e.x, e.y, e.modelArchiveIndex);
                        }
                        if (!stack.player().ghost() &&
                            std::fabs((gameCamera.z - sk::kEyeHeightOffset) - m.z) <=
                                sk::kEyeHeightOffset * 2.0f) {
                            solids.push_back({static_cast<int>(std::floor(gameCamera.x)),
                                              static_cast<int>(std::floor(gameCamera.y)),
                                              sk_bindings::kPlayerRadiusUnits,
                                              sk_bindings::kPlayerRadiusUnits});
                        }
                    }
                    auto tileRule = [&](int px, int py, int selfX, int selfY) {
                        if (px < 0 || py < 0) return true;
                        const int tx = px >> 8, ty = py >> 8;
                        if (!gameZone->InBounds(tx, ty)) return true;
                        if (gameZone->ActorTileBlocked(
                                static_cast<float>(tx * 256 + (selfX & 0xff)),
                                static_cast<float>(ty * 256 + (selfY & 0xff)), m.z, height,
                                /*creature=*/true)) {
                            return true;
                        }
                        for (const sk_bindings::MoveBox& e : solids) {
                            if ((e.x >> 8) == tx && (e.y >> 8) == ty) return true;
                        }
                        return false;
                    };
                    // The same guard the player has, for the same reason: a
                    // creature that begins the tick in a tile the rule
                    // refuses -- placed beside a prop, spawned onto another
                    // -- would be put back there forever. Its own tile is
                    // then held to the wall flag alone.
                    const bool startStuck = tileRule(startX, startY, startX, startY);
                    sk_bindings::PlayerMoveWorld world;
                    world.mapWidth = gameZone->width();
                    world.mapHeight = gameZone->height();
                    world.tileBlocked = [&](int px, int py, int selfX, int selfY) {
                        if (startStuck && px >= 0 && py >= 0 && (px >> 8) == (startX >> 8) &&
                            (py >> 8) == (startY >> 8)) {
                            return gameZone->CellAt(px >> 8, py >> 8).IsWall();
                        }
                        return tileRule(px, py, selfX, selfY);
                    };
                    world.solidsInTile = [&](int tx, int ty,
                                             std::vector<sk_bindings::MoveBox>& out) {
                        for (const sk_bindings::MoveBox& e : solids) {
                            if ((e.x >> 8) == tx && (e.y >> 8) == ty) out.push_back(e);
                        }
                    };
                    sk_bindings::move_detail::ClampToMap(motion, world, radius);
                    const int steppedVx = motion.vx, steppedVy = motion.vy;
                    sk_bindings::move_detail::PushOut(motion, world, radius);
                    // A blocked axis loses its velocity, as before.
                    if (motion.vx == 0 && steppedVx != 0) m.vx = 0;
                    if (motion.vy == 0 && steppedVy != 0) m.vy = 0;
                    if (world.tileBlocked(motion.x, motion.y, motion.x, motion.y)) {
                        motion.x = startX;
                        motion.y = startY;
                    }
                    commit();
                };
                // And where it then stands: the actor move's ground clamp,
                // `floor(z + 0x180) + 0x80` for anything that is not the
                // player -- SnapActorToGround, the same snap M62's
                // SetPosition uses. 1479 of the 1541 placed creatures are
                // authored at exactly that height (m121); this used to put
                // a creature that had moved on the bare floor instead, half
                // a lift below every one that had not.
                auto groundCreature = [&](MonsterInstance& m) {
                    m.z = gameZone->SnapActorToGround(m.x, m.y, m.z);
                };

                // M28: picks the clip a creature should be playing from
                // its own AI state, and latches the death clip so a corpse
                // holds its final pose instead of looping.
                auto setAnimClip = [](MonsterInstance& m, int clip, bool holdLastFrame) {
                    if (clip < 0 || m.animClip == clip) return;
                    m.animClip = clip;
                    m.animTime = 0.0f;
                    m.animHoldLastFrame = holdLastFrame;
                    m.animThenClip = -1;
                };

                // M77: `PlayAnimation(clip, 1, next, 0xf00)` -- vtable
                // +0x148 with the real attack's own arguments. Play `clip`
                // through exactly once, then hand back to `next`. Restarts
                // even if `clip` is already playing, because a second swing
                // a second later is a second swing.
                auto setAnimOnce = [](MonsterInstance& m, int clip, int next) {
                    if (clip < 0) return;
                    m.animClip = clip;
                    m.animTime = 0.0f;
                    m.animHoldLastFrame = true;
                    m.animThenClip = next;
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
                //
                // M101: and a world object a script's DestroyObject took out
                // of the world -- a loot chest's own menu running
                // `GetOpener().DestroyObject()`, a zone script destroying a
                // placed item. Both names that can still reach the object
                // are dropped first: the GetEntity registry and any cached
                // screen it opened.
                for (size_t i = 0; i < gamePickups.size();) {
                    const sk_bindings::ItemExecutable* doomed = gamePickups[i].script.get();
                    if (doomed && (doomed->markedForRemoval() || doomed->entityRemoved())) {
                        stack.level().UnregisterEntity(doomed);
                        stack.ForgetOpener(doomed);
                        gamePickups.erase(gamePickups.begin() + static_cast<long>(i));
                    } else {
                        ++i;
                    }
                }

                // M101: **the player can die.** Whatever took health to 0
                // last tick -- a swing, an arrow, a spell, a poison or burn
                // tick, a script's DoDamage -- went through
                // PlayerExecutable::ApplyDamage, whose kill slot
                // (`FUN_10042cb0`) plays slot 0x50 and opens "DeathMenu",
                // unless SetInvulnerable(true) is set. Opened here, at the
                // top of the next tick, so no script frame is live under it.
                //
                // `FUN_100779b8(menuManager, "DeathMenu", 0, 0)`: the third
                // argument is the "open over a live session" flag MainMenu
                // passes as 1, and it is 0 here -- the death screen is not a
                // pause. So gamePausedForMenu stays false and the screen runs
                // as a front-end menu: its back key is `DeathMenuBack`, which
                // is `QuitToMenu()`, and the only way out.
                if (stack.player().TakePendingDeath()) {
                    playPlayerSound(sk::kSoundAttackHit);
                    stack.OpenMenu("DeathMenu");
                    inGame = false;
                    gamePausedForMenu = false;
                    input.ClearPendingEdges();  // M90, see its comment
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
                // M73: the other half of the real player tick
                // (`FUN_10045294` runs the two back to back, in this
                // order) -- the slow refill of health, magicka and fatigue.
                // Without it the three HUD bars were a one-way ratchet.
                // See simkin_bindings/vitals.h.
                stack.player().TickVitalRegeneration(sk_bindings::kAiFrameDeltaUnits);

                // M63: the scripted sprite effects a zone placed in its
                // own Init() -- frame advance, lifetime and scale
                // ramp (simkin_bindings/effect_entity.h). The gravity term
                // is left at zero because no scripted effect sets the flag
                // that reads it.
                stack.level().TickEffects(sk_bindings::kAiFrameDeltaUnits,
                                          sk_bindings::kEffectGravityUnits);  // post-M120

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
                        // M132: tag 0x25 -- the other game draws the shot
                        // (session vtable +0x114, straight after the spawn).
                        if (mpSession.inGame() && sk_bindings::Mp().sending()) {
                            const int pitch = sk_bindings::EngineAngleFromRadians(castPitch);
                            if (caster->isPlayerActor()) {
                                mpSession.Send(sk_net::EncodeSpellProjectile(
                                    true, mpSession.myIndex(), cast.projectileSprite, 100,
                                    spell->templateId(), pitch));
                            } else {
                                for (const MonsterInstance& m : gameMonsters) {
                                    if (static_cast<sk_bindings::SpellActor*>(m.script.get()) !=
                                            caster ||
                                        m.script->netId() <= 0) {
                                        continue;
                                    }
                                    mpSession.Send(sk_net::EncodeSpellProjectile(
                                        false, m.script->netId(), cast.projectileSprite, 100,
                                        spell->templateId(), pitch));
                                    break;
                                }
                            }
                        }
                    }
                    if (cast.areaDamage > 0) {
                        // FUN_1004720c: every creature in the level within
                        // 12000 units, the caster excluded. No sightline
                        // test and no facing test -- the real loop walks
                        // the level's whole creature list.
                        for (MonsterInstance& victim : gameMonsters) {
                            if (victim.script.get() == caster) continue;
                            if (!victim.script->alive() || victim.script->outOfWorld()) continue;
                            const float adx = victim.x - castX, ady = victim.y - castY;
                            if (std::sqrt(adx * adx + ady * ady) >
                                static_cast<float>(sk_bindings::kAreaSpellRange)) {
                                continue;
                            }
                            onCreatureStruck(victim);  // M86, post-M120
                            // M97: sourced to `FUN_1002fd30(caster)`, so
                            // every creature the wave kills pays the caster,
                            // one award (and at most one level) per corpse.
                            // M98: p5 = 1, and the caster's Strength term
                            // lands on every creature the wave reaches.
                            victim.script->ApplyDamage(cast.areaDamage, caster, /*ranged=*/true);
                            // M132: `FUN_10067f84(victim, 0x3e, 0x44, 2)` --
                            // each one it reaches burns for two seconds (and
                            // a survivor's flame goes out as tag 0x27).
                            if (sk_bindings::Mp().attachedEffect) {
                                sk_bindings::Mp().attachedEffect(victim.script.get(), 0x3e, 0x44, 2);
                            }
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
                    if (caster->isPlayerActor()) {
                        if (!sk_bindings::SpellCastAllowed(*spell, gameCastCooldown,
                                                           gameClockUnits)) {
                            return false;
                        }
                        // M117: the gate's third clause, `player+0x428 == 4`
                        // -- `stats+0x7c`, the Sanctuary channel, the same
                        // field the weapon arm refuses on. No stamp.
                        if (caster->actorStats().periodicKind() ==
                            sk_bindings::ActorStats::kPeriodicSanctuaryTimer) {
                            return false;
                        }
                        // M117: FUN_10046680 stamps `+0xfc4` as the gate
                        // passes, *before* FUN_10046764 casts -- so a cast
                        // refused for want of magicka still restarts the
                        // cooldown. This used to stamp only on success.
                        sk_bindings::NoteSpellCast(gameCastCooldown, gameClockUnits);
                    }
                    sk_bindings::SpellCastResult cast = sk_bindings::CastSpell(*spell, *caster);
                    if (!cast.cast) return false;
                    if (caster->isPlayerActor()) {
                        // M73: a cast costs fatigue as well as magicka.
                        // `FUN_10042394` case 2 spends 3 -- but only here,
                        // after both the refire gate and the cast itself
                        // have succeeded, so a cast refused for want of
                        // magicka is free. Player-only: the creature cast
                        // goes through a different dispatcher that has no
                        // such line. See vitals.h.
                        caster->SetActorFatigue(caster->actorFatigue() -
                                                 sk_bindings::kCastFatigueCost);
                    }
                    applyCastResult(spell, caster, cast, castX, castY, castZ, castYaw, castPitch);
                    return true;
                };

#if SK_DEBUG_SUITE
                // SK_DEBUG_SUITE (M68): freezeai stops the whole creature
                // tick -- timers, perception, movement and attacks -- so a
                // fight can be paused and inspected mid-swing.
                for (size_t monsterIndex = 0;
                     !debugRefs.freezeAi && monsterIndex < gameMonsters.size(); ++monsterIndex) {
#else
                for (size_t monsterIndex = 0; monsterIndex < gameMonsters.size(); ++monsterIndex) {
#endif
                    MonsterInstance& m = gameMonsters[monsterIndex];
                    // M130 (multiplayer): a creature whose target is the
                    // other player runs this same tick against that
                    // player's avatar -- "the player" below is wherever the
                    // camera is, so it is stood in for and put back. Its
                    // blows do nothing here; the other game lands them.
                    struct TargetSwap {
                        sk::Camera& camera;
                        float x, y, z;
                        bool on;
                        ~TargetSwap() {
                            if (!on) return;
                            camera.x = x;
                            camera.y = y;
                            camera.z = z;
                        }
                    } mpTargetSwap{gameCamera, gameCamera.x, gameCamera.y, gameCamera.z, false};
                    if (m.mpTargetsPeer && mpAvatar.present && !mpAvatar.dead && mpSession.inGame()) {
                        mpTargetSwap.on = true;
                        gameCamera.x = mpAvatar.x;
                        gameCamera.y = mpAvatar.y;
                        gameCamera.z = mpAvatar.z + sk::kEyeHeightOffset;
                    }
                    const bool wasAliveBeforeTick = m.script->alive();
                    // M32: the real per-frame AI timers -- the flee/Fear
                    // countdown (which restores the previous package when
                    // it expires) and the paralysis lockout. Runs even for
                    // the dead/destroyed so an effect can't outlive them.
#if SK_DEBUG_SUITE
                    // SK_DEBUG_SUITE (M68): how many creatures the AI pass
                    // actually touched this frame -- the counter that makes
                    // `freezeai` and a stuck creature both visible in `diff`.
                    sk_debug::Count("ai.ticks");
#endif
                    // M122: the death floor, FUN_10000498 -- see
                    // onDeathFloor. A creature is tested half its Height()
                    // below its own z, which for one standing at floor +
                    // 0x80 is under the surface.
                    if (gameZone && wasAliveBeforeTick && !m.script->outOfWorld() &&
                        onDeathFloor(m.x, m.y,
                                     m.z - static_cast<float>(
                                               sk::MonsterCollisionHeight(m.modelArchiveIndex) / 2))) {
                        m.script->ApplyDamage(m.script->currentHealth());
                    }
                    m.script->TickAi(sk_bindings::kAiFrameDeltaUnits);
                    // M34: TickAi() is now a path that can actually kill.
                    if (wasAliveBeforeTick && m.script->deathOwed()) {
                        gameDiedFromEffect.push_back(monsterIndex);
                    }
                    // M100: FUN_1006410c's second half, after its first (the
                    // script Delay, which TickAi ran). A corpse whose five
                    // seconds are up leaves the world and runs OnDecay.
                    // M101: not once a script has taken it out of the world
                    // first -- FUN_1001817c drops it from the entity list
                    // this tick walks, so a corpse destroyed before its five
                    // seconds never decays and never runs OnDecay.
                    if (!m.script->destroyed()) {
                        // M104: OnDecay is the other death hook that opens
                        // screens -- the five Dark Star pit bosses and both
                        // Pergan Asuuls -- so it needs the same hand-off the
                        // OnKilled path below got. See handleDeath().
                        sk_bindings::MenuExecutable* beforeDecayMenu = stack.currentMenu();
                        m.script->TickDecay();
                        if (stack.currentMenu() != beforeDecayMenu) {
                            inGame = false;
                            gamePausedForMenu = true;
                            input.ClearPendingEdges();
                        }
                    }
                    // M23: destroyed() (a real zone-root script's
                    // DestroyObjectMirror()) removes an entity from play
                    // as fully as death does, everywhere alive() is
                    // already checked.
                    if (!m.script->alive()) {
                        setAnimClip(m, m.script->deathAnimation(), true);
                        continue;
                    }
                    if (m.script->outOfWorld()) {
                        setAnimClip(m, m.script->idleAnimation(), false);
                        continue;
                    }
                    // ==== M77: the real tick, in the real order ====
                    //
                    // What follows is FUN_10082224's own body, transcribed
                    // rather than designed. simkin_bindings/monster_ai.h
                    // carries the whole derivation; the short version is
                    // that a placed creature starts in package 2 (entity
                    // vtable slot +0x10 writes it at spawn, before the
                    // script's Init() runs), that the perception test has
                    // no line-of-sight term on the aggressive side, and
                    // that the `0x100 < +0x2c4` cadence gates the whole
                    // approach/attack/give-up decision -- not just the
                    // damage roll, which is how this port had it and why
                    // creatures looked like they were swinging without
                    // pause.

                    // `if (self->+0x2ee) { ... }` -- SetCanTeleport, above
                    // the package branch and so running in every package.
                    // A boss with it set takes the player as its target
                    // when it has none, and jumps to a path node beside it
                    // the moment it falls outside the chase radius.
                    // FUN_10086a18's node search needs the zone's `.pth`
                    // table, which is undecoded (docs/ZONE_FORMAT.md), so
                    // the port lands the creature at the player's own
                    // position offset by its stand-off distance -- the
                    // same *effect* (you cannot outrun it) reached a
                    // simpler way, and the one place in this block that is
                    // not a transcription.
                    if (m.script->canTeleport() && gameZone) {
                        float tdx = gameCamera.x - m.x, tdy = gameCamera.y - m.y;
                        float tdist = std::sqrt(tdx * tdx + tdy * tdy);
                        if (tdist > m.script->chaseRadius() && tdist > 1.0f) {
                            // Two boxes' worth: as near as the tile rule
                            // would ever let it walk.
                            float back = (std::max)(m.script->attackRange(),
                                                     2.0f * sk::kActorHalfExtent);
                            float nx = gameCamera.x - (tdx / tdist) * back;
                            float ny = gameCamera.y - (tdy / tdist) * back;
                            if (!gameZone->CircleHitsWall(nx, ny, kMonsterRadius)) {
                                m.x = nx;
                                m.y = ny;
                                m.z = gameZone->SnapActorToGround(
                                    m.x, m.y, gameCamera.z - sk::kEyeHeightOffset);
#if SK_DEBUG_SUITE
                                // SK_DEBUG_SUITE (M68)
                                sk_debug::Count("npc.teleported");
#endif
                            }
                        }
                    }

                    // `self->+0x2c4 += FUN_1001afa4(engine)` sits above the
                    // package branch too, so it accumulates on every tick
                    // in every package -- including while a creature is
                    // still walking toward you, which is what makes the
                    // first swing land promptly instead of a second after
                    // arrival.
                    m.script->TickAttackCadence(sk_bindings::kAiFrameDeltaUnits);

                    // M32: the real AI package gates everything below.
                    //
                    // Packages 4 (AiFlee), 5 (AiPursue), 6
                    // (AiSpellAssistTarget) and -1 (AiSleep) have no arm in
                    // the tick at all -- a creature left in one keeps its
                    // timers and its move goal and does nothing else.
                    // Reproduced, not invented.
                    int pkg = m.script->aiPackage();

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
                        if (dist > 1.0f && !m.script->paralyzed() && !m.script->immobile()) {
                            const float awayX = -mdx / dist, awayY = -mdy / dist;
                            m.facingYaw = std::atan2(awayY, awayX);
                            stepCreature(m, awayX, awayY, 1e9f);
                        }
                        groundCreature(m);
                        continue;
                    }
                    if (pkg != sk_bindings::MonsterExecutable::kAiIdle &&
                        pkg != sk_bindings::MonsterExecutable::kAiPursue) {
                        setAnimClip(m, m.script->idleAnimation(), false);
                        continue;
                    }

                    // ---- the pursue/attack arm: package 3 with a target ----
                    if (pkg == sk_bindings::MonsterExecutable::kAiPursue && m.hasTarget) {
                        m.aiState = MonsterInstance::AiState::Chasing;
                        // `if (+0x294 == 0) vtable[0x208](self, target)` --
                        // a paralysed creature does not even turn.
                        if (!m.script->paralyzed() && dist > 1.0f) {
                            m.facingYaw = std::atan2(mdy, mdx);
                        }

                        // Evaluated every tick, before the cadence gate:
                        // inside SetAttackRange the creature stops dead and
                        // returns to its idle pose. The swing clip is not
                        // set here -- the attack sets it, once, and it
                        // hands back to idle on its own (see setAnimOnce).
                        const float standAndAttack = m.script->attackRange();
                        const bool inRange = dist <= standAndAttack;
                        if (inRange) {
                            // M122: `+0x98 = +0xa0 = 0` -- the walk stops.
                            m.vx = 0;
                            m.vy = 0;
                            m.aiState = MonsterInstance::AiState::Attacking;
                            if (m.animThenClip < 0) {
                                setAnimClip(m, m.script->idleAnimation(), false);
                            }
                        }

                        bool keepClosing = !inRange;
                        if (m.script->attackCadenceReady()) {
                            // `dz` is the real `|(s16)self+0xa4 -
                            // (s16)target+0xa4|`, i.e. a vertical limit on
                            // melee, not the facing cone an earlier comment
                            // here claimed. A ranged creature ignores it and
                            // skips the reach raycast as well.
                            const float dz =
                                std::fabs(gameCamera.z - (m.z + sk::kEyeHeightOffset));
                            const bool ranged = m.script->ranged();
                            if (inRange &&
                                (ranged || dz <= static_cast<float>(
                                                    sk_bindings::kAttackVerticalLimitUnits))) {
                                // FUN_10082004: the melee reach march, from
                                // the creature's eye to its target's, with a
                                // budget of `SetAttackRange >> 8` tiles. This
                                // port's DDA is 2D and tests the same `.zmp`
                                // blocking bit the real march ends on; see
                                // Zone::HasLineOfSight.
                                const bool reach =
                                    ranged || !gameZone ||
                                    gameZone->HasLineOfSight(m.x, m.y, gameCamera.x, gameCamera.y);
                                // The boss throttle (monster+0x266 /
                                // +0x2c8 / +0x2cc): at most one swing every
                                // SetAttackSpeed seconds of game clock, on
                                // top of the cadence. Nothing sets the
                                // speed, so every boss uses the
                                // constructor's 2 -- half the rate of
                                // everything else.
                                bool throttleOk = true;
                                if (m.script->boss()) {
                                    throttleOk = sk_bindings::BossAttackDue(
                                        m.script->lastAttackClock(),
                                        m.script->attackSpeedSeconds(), gameClockUnits);
                                }
                                if (reach && throttleOk) {
                                    // FUN_100835b8. Its first act is
                                    // `PlayAnimation(swing, 1, idle,
                                    // 0xf00)` -- play the swing clip once,
                                    // then hand over to idle. That is the
                                    // whole of what a swing looks like, and
                                    // getting it wrong (re-asserting the
                                    // clip on all 25 ticks in between) is
                                    // what made creatures look like they
                                    // never stopped attacking.
                                    setAnimOnce(m, m.script->swingAnimation(),
                                                 m.script->idleAnimation());
                                    // M43: the real melee-vs-spell branch
                                    // (FUN_100835b8). A creature with a
                                    // spell in slot 0 rolls SetMeleeRoll
                                    // against rand(0,100) and casts unless
                                    // the roll reaches it -- so a script
                                    // that adds spells but never calls
                                    // SetMeleeRoll (bandit_mage.s,
                                    // highwaymage.s, yelnicin.s) casts on
                                    // every single attack, and one that
                                    // sets 75 (every floater, every ghost)
                                    // casts about three attacks in four.
                                    if (!m.script->RollForMelee()) {
                                        // M48: the real cast. The creature
                                        // pays for it and applies any
                                        // self-targeted half itself; an
                                        // offensive spell now leaves the
                                        // muzzle as a projectile aimed
                                        // along the creature's facing,
                                        // which is what makes a caster's
                                        // spell miss when the player steps
                                        // aside.
                                        sk_bindings::MonsterExecutable::CastAttempt attempt =
                                            m.script->CastSpellAt(&stack.player());
                                        if (attempt.spell && attempt.result.cast) {
                                            applyCastResult(attempt.spell, m.script.get(),
                                                             attempt.result, m.x, m.y, m.z,
                                                             m.facingYaw, 0.0f);
                                        }
                                    } else if (m.script->shootsProjectile()) {
                                        // M49: an archer. FUN_100835b8 puts
                                        // its whole melee resolution inside
                                        // `if (+0x2d8 == -1)`, so a
                                        // creature that calls SetProjectile
                                        // shoots *instead of* swinging --
                                        // it never lands a melee blow at
                                        // all. The type is hardcoded 599 on
                                        // this side (the script's own
                                        // SetProjectile value goes to a
                                        // draw parameter, not the art), the
                                        // damage is `rand % damageMax`
                                        // exactly as on the player's side,
                                        // and the sound is the creature's
                                        // own SetAttackNoise -- which every
                                        // one of the twelve shipped archers
                                        // sets to slot 1, the same bow-fire
                                        // sample the player uses.
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
                                        // M51: FUN_10044814 plays slot 80
                                        // on any positive incoming damage,
                                        // before it applies it -- the same
                                        // sample a connecting swing uses,
                                        // so an impact is one sound
                                        // whichever way it is going.
                                        if (mpTargetSwap.on) dmg = 0;  // M130: the avatar's, not ours
                                        if (dmg > 0) playPlayerSound(sk::kSoundAttackHit);
#if SK_DEBUG_SUITE
                                        // SK_DEBUG_SUITE (M68): god mode.
                                        // Applied here, at the one place a
                                        // creature damages the player, so
                                        // the roll still happens and is
                                        // still traceable -- only the
                                        // damage is dropped.
                                        if (debugRefs.godMode) dmg = 0;
                                        sk_debug::Count("combat.damage_taken", dmg);
                                        sk_debug::Count("combat.hits_on_player");
#endif
                                        // M98: `if (0 < dmg) vtable[0x10](
                                        // playerStats, dmg, self + 0x224, 0,
                                        // 0)` -- the creature is the source
                                        // (its Strength is 0, so what it
                                        // changes is nothing), melee is p5 =
                                        // 0, and the player's own term is the
                                        // Knight's halving.
                                        if (dmg > 0) {
                                            stack.player().ApplyDamage(dmg, m.script.get(),
                                                                       /*ranged=*/false);
                                        }
                                        playWorldSound(m.script->attackNoiseId(), m.x, m.y);
                                    }
                                }
                            } else if (dist > m.script->chaseRadius()) {
                                // `else if (+0x2b8 < d) { target = 0;
                                // package = 2; }` -- the entire give-up
                                // rule. No grace period, no memory of where
                                // you were: this port's kLoseInterestTicks
                                // and lastSeenX/Y were its own invention
                                // and are gone.
                                m.hasTarget = false;
                                m.script->SetAiPackage(
                                    sk_bindings::MonsterExecutable::kAiIdle);
                                m.aiState = MonsterInstance::AiState::Idle;
                                keepClosing = false;
                                setAnimClip(m, m.script->idleAnimation(), false);
                            } else {
                                keepClosing = true;
                            }
                            m.script->JitterAttackCadence();
                        }

                        // The move goal is refreshed every tick, outside
                        // the gate (`if (target && !+0x2bc) { +0x1b9 = 1;
                        // goal = target position; }`), and the in-range
                        // block above is what clears it again.
                        if (keepClosing && !m.script->paralyzed() && !m.script->immobile() &&
                            dist > 1.0f) {
                            if (m.animThenClip < 0) {
                                setAnimClip(m, m.script->walkAnimation(), false);
                            }
                            const float dirX = mdx / dist, dirY = mdy / dist;
                            // Straight at the target and slide along
                            // whatever is in the way -- see stepCreature.
                            // M122: no stand-off clamp at the attack range.
                            // The engine walks until the AI tick finds the
                            // creature inside SetAttackRange and zeroes its
                            // delta (the in-range block above), so it steps
                            // *into* range.
                            // M132: and no contact clamp or crowd push
                            // either -- the player's tile, and every other
                            // creature's, is closed to it by the tile rule.
                            m.facingYaw = std::atan2(dirY, dirX);
                            stepCreature(m, dirX, dirY, 1e9f);
                        }
                        groundCreature(m);
                        continue;
                    }

                    // ---- M102: the script's own walk order ----
                    //
                    // `WalkTo(x, y)` sets the same move goal the AI writes,
                    // and the entity's per-frame move steers toward it while
                    // the flag is up (monster_executable.h). It is stepped
                    // here, in the arm where nothing else is driving the
                    // creature: the chase arm above overwrites the goal with
                    // its target every tick, exactly as the engine does, and
                    // the aggressive acquire below drops the order outright.
                    // Nothing stops the walk on arrival -- the engine has no
                    // arrival test -- so a creature that gets there stands
                    // pushing into the spot, which is what the original does.
                    if (m.script->walkOrderActive() && gameZone && !m.script->paralyzed() &&
                        !m.script->immobile()) {
                        const float wx = static_cast<float>(m.script->walkOrderX()) - m.x;
                        const float wy = static_cast<float>(m.script->walkOrderY()) - m.y;
                        const float wdist = std::sqrt(wx * wx + wy * wy);
                        if (wdist > 1.0f) {
                            const float dirX = wx / wdist, dirY = wy / wdist;
                            m.facingYaw = std::atan2(dirY, dirX);
                            stepCreature(m, dirX, dirY, wdist);
                            groundCreature(m);
                            setAnimClip(m, m.script->walkAnimation(), false);
                        }
                    }

                    // ---- the look arm: package 2, or 3 with no target ----
                    //
                    // One perception test, then a split on one byte. The
                    // aggressive side acquires and chases; the other side
                    // runs the script's OnDetect handler (M76, on_detect.h)
                    // -- that is the whole of the difference between a
                    // monster and an NPC who speaks first.
                    m.aiState = MonsterInstance::AiState::Idle;

                    // `if (candidate && package == 2) package = 3` -- run
                    // before anything else and with the target still null,
                    // so the creature simply lands back in this same arm
                    // next tick. Transcribed because it is observable:
                    // GetCurrentAIPackage() on an untouched creature
                    // reports 3, not 2.
                    if (pkg == sk_bindings::MonsterExecutable::kAiIdle) {
                        m.script->SetAiPackage(sk_bindings::MonsterExecutable::kAiPursue);
                    }

                    // The engine's own two gates, and only these two.
                    // There is deliberately no line-of-sight or vertical
                    // term here: the aggressive acquire arm has neither,
                    // so a creature inside SetChaseRadius (8.4 tiles for
                    // the corpus's dominant 18000) comes for you through a
                    // wall. WORLD_MODEL.md used to say the opposite; see
                    // monster_ai.h.
                    const bool inChaseRadius = dist <= m.script->chaseRadius();
                    bool noticed = true;
                    if (inChaseRadius) {
                        noticed = sk_bindings::DetectionNoticed(
                            sk_bindings::DetectMissPercent(sk_bindings::kDetectStrength,
                                                            stack.player().agility()),
                            std::rand() % 101);
                    }
                    if (!inChaseRadius || !noticed) {
                        setAnimClip(m, m.script->idleAnimation(), false);
                        continue;
                    }

                    if (!m.script->aggressive()) {
                        // M76: the OnDetect arm. Unlike acquisition this
                        // one *is* sight-gated -- `vtable[0x21c]`
                        // (FUN_10004d70), an eye-to-eye march budgeted
                        // `SetAttackRange >> 8` tiles. See on_detect.h.
                        //
                        // This is the mechanism behind every NPC that
                        // speaks first: monsters/bbrawler_talk.s opens
                        // "Talker" on you, monsters/olpac_trailslag.s opens
                        // ghstpass/GP_Menu3 once per save, and
                        // erthcave/azra_zombie.s turns hostile *and* opens
                        // EC_Menu7 in the same handler.
                        bool detectOpenedMenu = false;
                        const float sightRange =
                            static_cast<float>(
                                sk_bindings::SightRangeTiles(m.script->attackRangeRaw())) *
                            sk::kTileScale;
                        if (gameZone && dist <= sightRange &&
                            gameZone->HasLineOfSight(m.x, m.y, gameCamera.x, gameCamera.y)) {
                            sk_bindings::MenuExecutable* beforeMenu = stack.currentMenu();
                            if (m.script->InvokeOnDetect(
                                    static_cast<skiExecutable*>(&stack.player()))) {
#if SK_DEBUG_SUITE
                                // SK_DEBUG_SUITE (M68)
                                sk_debug::Count("npc.detected");
#endif
                            }
                            if (stack.currentMenu() != beforeMenu) {
                                // Same hand-off the Use path already uses:
                                // a handler that opened a menu pauses the
                                // 3D view into it. Stop the AI pass here
                                // rather than ticking the rest of the level
                                // inside a frame the world is no longer
                                // running -- the engine pauses at frame
                                // granularity, so a second creature getting
                                // a turn after the menu is up would be this
                                // port's invention.
                                inGame = false;
                                gamePausedForMenu = true;
                                input.ClearPendingEdges();  // M90, see its comment
                                detectOpenedMenu = true;
                            }
                            m.script->ClearAttackCadence();
                        }
                        // Idle NPCs and merchants still breathe.
                        setAnimClip(m, m.script->idleAnimation(), false);
                        if (detectOpenedMenu) break;
                        continue;
                    }

                    // The aggressive arm: acquire, switch to package 3,
                    // and start walking. `+0x2c4 = 0` here rather than the
                    // jitter the pursue arm uses, so the first swing lands
                    // a full second after acquisition.
                    m.hasTarget = true;
                    m.script->SetAiPackage(sk_bindings::MonsterExecutable::kAiPursue);
                    m.script->ClearAttackCadence();
                    // M102: and the chase owns the move goal from here --
                    // the engine writes its target's position into the same
                    // fields every tick, so a script's WalkTo is over.
                    m.script->ClearWalkOrder();
                    m.aiState = MonsterInstance::AiState::Chasing;
                    setAnimClip(m, m.script->walkAnimation(), false);
#if SK_DEBUG_SUITE
                    // SK_DEBUG_SUITE (M68)
                    sk_debug::Count("ai.acquired");
#endif
                }

                // M72: automatic aim-assist pitch -- the real one.
                //
                // M30 wrote this from the requested behaviour ("facing a
                // small creature the camera tilts down to it") because
                // nothing had been traced that aimed the pitch
                // automatically. It has since been found, in one block at
                // the tail of `Render3DScene` (0x10017b7c..0x10017e20),
                // and it is not a soft function of creature size at all --
                // it is a hard gate, which is why this port panned for
                // bandits and the original does not. Transcribed:
                //
                //   1. global auto-look enable  (`engine+0xbe0b`, set to 1
                //      in the engine constructor; when it is off the actor
                //      tick forces manual mode instead)
                //   2. the player is not manually looking (`player+0xaf`)
                //   3. the map zoom is 1x or 2x (`engine+0x608 <= 0x200`)
                //   4. no modal/controller state suppressing it
                //      (`player+0x204`'s `+0x179`)
                //   5. nothing alive is already under the crosshair --
                //      `engine+0x620 = pick(engine, 88, 104)`, an
                //      ID-buffer read at the exact centre pixel of the
                //      176x208 screen
                //   6. target = the first live creature the player's own
                //      forward ray walks into: `FUN_1001db0c(player, 0x20)`
                //      marches 32 half-tile steps along (sin h, cos h) and
                //      takes the first entity in each tile's list that is
                //      alive, not flagged out (`+0x1e2`), and neither
                //      `vt+0xf0` nor `vt+0x170`
                //   7. that creature must have been drawn this frame
                //      (`target+0xd6`, the flag Render3DScene stamps on
                //      every entity in a visible tile)
                //   8. **`Height() < 0x200`** -- `ldr ip,[r3,#0x108] /
                //      blx / cmp r0,#0x200 / bge skip`. This is the whole
                //      bug: Height() is the per-model table in
                //      world/entity_types.h, 0x100 for rats, spiders,
                //      wormmouths, stingers and wolves and 0x200 for
                //      everything else, so a bandit (model 22/23) fails
                //      here and never pans at all.
                //   9. pitch = atan2(eyeZ - (targetZ + Height()/2),
                //      horizontal distance), via a 32x32 signed-byte atan
                //      table at 0x100f6954 (verified against atan2 to
                //      within 1.4 degrees, so this port just calls atan2)
                //  10. engage only if |pitch| <= 0x2000, i.e. 45 degrees.
                //      Outside that the target is dropped and the pitch is
                //      simply left where it is.
                //
                // Two engine quirks reproduced deliberately because they
                // are load-bearing for how this feels:
                //
                //  * the horizontal distance is quantised to whole tiles.
                //    `vt+0x5c` returns dist^2/256, then `isqrt(x >> 8) *
                //    256` floors it to a multiple of 256. Inside one tile
                //    it becomes 0, atan2 gives +-90 degrees, and rule 10
                //    drops the target -- so nose-to-nose with a rat the
                //    camera holds rather than staring at the floor.
                //  * losing the target does NOT re-level the camera. That
                //    is a separate path: the actor tick clears `+0xae`
                //    only when the player is *moving* with no target, and
                //    only then decays the pitch toward 0 by `v -= v*20/256`
                //    per tick (`FUN_10068814(pitch, 0x14)`).
                //
                // Deviations, both forced: gates 3 and 4 have no port
                // equivalent (there is no map zoom and no `+0x204`
                // controller object), and gate 7 uses Zone::HasLineOfSight
                // in place of the real per-frame visibility stamp.
                if (!manualLook) {
                    // 0x200 -- the `cmp r0,#0x200 / bge` at 0x10017ca4.
                    constexpr int32_t kAutoAimMaxTargetHeight = 0x200;
                    // 0x2000 of 65536 == 45 degrees.
                    constexpr float kAutoAimEngageLimit = 0.78539816339f;
                    // `FUN_1001db0c(player, 0x20)`: 32 steps of
                    // `sinTable[heading] >> 1`, and the table's amplitude
                    // is 256, so each step is half a tile.
                    constexpr int kAutoAimRaySteps = 0x20;
                    constexpr float kAutoAimRayStep = sk::kTileScale * 0.5f;
                    // `FUN_100657bc`: halve the remaining gap each tick,
                    // clamped to 500 raw angle units, snapping inside 5.
                    constexpr float kTurn = 6.28318530718f;  // 65536 raw units
                    constexpr float kAutoAimMaxStep = 500.0f / 65536.0f * kTurn;
                    constexpr float kAutoAimSnap = 5.0f / 65536.0f * kTurn;
                    // `FUN_10068814(pitch, 0x14)`.
                    constexpr float kPitchDecayPerTick = 20.0f / 256.0f;

                    const float fwdX = std::cos(gameCamera.yaw);
                    const float fwdY = std::sin(gameCamera.yaw);

                    // Gate 5 (M130): nothing alive already under the
                    // crosshair -- `engine+0x620 = pick(engine, 88, 104)`,
                    // the ID buffer at the centre pixel, stamped by the
                    // previous frame's draw as it is for the melee probes.
                    // Dead on a creature, the assist lets go.
                    bool crosshairOnCreature = false;
                    if (const uint8_t centreId = zoneRenderer.ObjectIdAt(88, 104)) {
                        for (const MonsterInstance& m : gameMonsters) {
                            if (m.objectId == static_cast<int>(centreId) && m.script->alive() &&
                                !m.script->outOfWorld()) {
                                crosshairOnCreature = true;
                                break;
                            }
                        }
                    }
                    // Gate 6: the first live creature the forward ray
                    // walks into.
                    const MonsterInstance* aimTarget = nullptr;
                    float rayX = gameCamera.x, rayY = gameCamera.y;
                    for (int step = 0;
                         step < kAutoAimRaySteps && !aimTarget && !crosshairOnCreature; ++step) {
                        const int tx = static_cast<int>(std::floor(rayX / sk::kTileScale));
                        const int ty = static_cast<int>(std::floor(rayY / sk::kTileScale));
                        // The real loop bails at the grid edge exactly
                        // this way (`iVar4 < 1`, then `width-1 <= tx ||
                        // height-1 <= ty`).
                        if (tx < 1 || ty < 1 || tx >= gameZone->width() - 1 ||
                            ty >= gameZone->height() - 1) {
                            break;
                        }
                        for (const MonsterInstance& m : gameMonsters) {
                            if (!m.script->alive() || m.script->outOfWorld()) continue;
                            if (static_cast<int>(std::floor(m.x / sk::kTileScale)) != tx) continue;
                            if (static_cast<int>(std::floor(m.y / sk::kTileScale)) != ty) continue;
                            aimTarget = &m;
                            break;
                        }
                        rayX += fwdX * kAutoAimRayStep;
                        rayY += fwdY * kAutoAimRayStep;
                    }
                    // Gate 7: drawn this frame.
                    if (aimTarget && !gameZone->HasLineOfSight(gameCamera.x, gameCamera.y,
                                                              aimTarget->x, aimTarget->y)) {
                        aimTarget = nullptr;
                    }
                    // Gate 8 -- the fix. A creature whose collision height
                    // reaches 0x200 (every humanoid) is not an aim-assist
                    // target at all.
                    if (aimTarget && sk::MonsterCollisionHeight(aimTarget->modelArchiveIndex) >=
                                         kAutoAimMaxTargetHeight) {
                        aimTarget = nullptr;
                    }

                    bool engaged = false;
                    if (aimTarget) {
                        const float dx = aimTarget->x - gameCamera.x;
                        const float dy = aimTarget->y - gameCamera.y;
                        // Quantised to whole tiles, as the engine does.
                        const float horizontal =
                            std::floor(std::sqrt(dx * dx + dy * dy) / sk::kTileScale) *
                            sk::kTileScale;
                        const float desiredPitch =
                            std::atan2(gameCamera.z - MonsterAimCenterZ(*aimTarget), horizontal);
                        if (std::fabs(desiredPitch) <= kAutoAimEngageLimit) {
                            engaged = true;
                            const float delta = desiredPitch - gameCamera.pitch;
                            if (std::fabs(delta) < kAutoAimSnap) {
                                gameCamera.pitch = desiredPitch;
                            } else {
                                gameCamera.pitch += std::clamp(delta * 0.5f, -kAutoAimMaxStep,
                                                               kAutoAimMaxStep);
                            }
                        }
                    }

                    if (engaged) {
                        gamePitchHeld = true;
                    } else if (gameCamera.x != preMoveCamX || gameCamera.y != preMoveCamY) {
                        // Moving with no target: release the hold, then let
                        // the pitch bleed back to level.
                        gamePitchHeld = false;
                    }
                    if (!gamePitchHeld) {
                        gameCamera.pitch -= gameCamera.pitch * kPitchDecayPerTick;
                    }
                } else {
                    // Manual look owns the pitch outright -- the real
                    // `player+0xaf` path sets `+0xae` with it, so nothing
                    // drifts it back while the key is held.
                    gamePitchHeld = true;
                }
                gameCamera.pitch =
                    std::clamp(gameCamera.pitch, -sk::kMaxCameraPitch, sk::kMaxCameraPitch);

                // M21/M81: monster-death loot-bag spawning -- `FUN_10084438`,
                // the whole of it. The dying creature's own
                // `SetLoot(300, "Loot_ratseye", 1, 8)` left two fields on it
                // (MonsterExecutable::lootTypeId()/lootTag()), and this is
                // where the engine reads them back: `FUN_10083c04` gates the
                // drop on the typeId being non-zero -- which is also how a
                // creature whose drop-chance roll failed at Init drops
                // nothing -- and then spawns that typeId with the tag as its
                // script override. Everything else about the bag comes from
                // entities.txt row 212 (`300 30 8 !bag_loot`): category 8
                // makes it a container, and model index 30 is
                // `bag_dropped.bin` in all 21 playable zones' models.txt.
                //
                // M81 fixed three things here, in descending order of how
                // completely they broke it:
                //
                //  1. The script path was built by hand as
                //     `<root>/Loot_ratseye` -- **no `.s`**, so it named no
                //     file. Simkin answers a missing file with an empty
                //     parse rather than an error (skInputFile::open leaves
                //     a null handle and the reader sees immediate eof), so
                //     the bag loaded "successfully" as an empty object:
                //     no Init(), so no `SetUsable(true)`, so
                //     findNearbyPickup() skipped it, and no
                //     CreateEntity/AddObject, so it was empty anyway. Every
                //     dropped bag in the game was an invisible no-op.
                //     LevelExecutable::CreateEntityWithScript() now does the
                //     load through the same ResolveScriptPath() every other
                //     scripted path in this port uses, which also fixes the
                //     eleven `SetLoot("broken1\\loot2")`-style tags that name
                //     a subdirectory.
                //  2. The model index was hardcoded to -1 on the (wrong)
                //     M21-era belief that typeId 300 had no model, because
                //     its entities.txt *name* column is the label-only
                //     `!bag_loot`. The model lives in the row's second
                //     column, not its name, and it is a real one.
                //  3. `isContainer` was never set, and the z was the
                //     creature's own. The engine drops the bag at the
                //     creature's x/y with `z + 300`, then floor-snaps it
                //     (`FUN_100686e0` + the caller's `+0x80` lift), which is
                //     exactly Zone::SnapActorToGround -- so a bag dropped by
                //     something killed mid-air or on a slope still lands on
                //     the floor rather than inside or above it.
                //
                // The rise is `FUN_10084438`'s literal `+ 300` on the
                // requested z: the height the floor snap probes *from*, not
                // a final offset -- see Zone::SnapActorToGround for what the
                // snap then does with it. M93 moved it to game_constants.h,
                // because the inventory drop uses the same literal.
                auto spawnLoot = [&](const MonsterInstance& m) {
                    if (m.script->lootTypeId() == 0) return;
                    std::unique_ptr<sk_bindings::ItemExecutable> bag =
                        stack.level().CreateEntityWithScript(m.script->lootTypeId(),
                                                              m.script->lootTag());
                    if (!bag) return;
                    PickupInstance inst;
                    inst.x = m.x;
                    inst.y = m.y;
                    inst.z = gameZone ? gameZone->SnapActorToGround(m.x, m.y, m.z + sk_bindings::kLootDropRise)
                                       : m.z;
                    inst.modelArchiveIndex =
                        stack.level().EntityModelIndexOf(m.script->lootTypeId());
                    inst.isContainer =
                        IsContainerCategory(stack.level().EntityCategoryOf(m.script->lootTypeId()));
                    inst.typeId = m.script->lootTypeId();  // post-M120: saveable
                    // The engine sets the spawned object's position after
                    // its Init() rather than before (`FUN_10084438` calls
                    // `vtable+0x14` on the returned object) -- no loot
                    // script reads its own position in Init() either way,
                    // but this keeps GetPositionX/Y/Z honest afterwards.
                    bag->SetWorldPosition(static_cast<int>(inst.x), static_cast<int>(inst.y),
                                          static_cast<int>(inst.z));
                    // M123: FUN_10084438's `+0x180 = 1` on the bag it spawns:
                    // lootmenu.s takes an emptied one out of the world only
                    // when this is set, and before this every creature's bag
                    // stayed behind empty and unusable.
                    bag->SetDestroyWhenEmpty(true);
                    inst.script = std::move(bag);
                    gamePickups.push_back(std::move(inst));
                };

                // M81: everything a creature's death owes, in one place --
                // `FUN_10083c04`. Idempotent (M100: MonsterExecutable::TakeDeathOwed),
                // so the places that know they just killed something can
                // call it immediately and the sweep below can back-stop
                // every other way a creature can reach zero health.
                auto handleDeath = [&](MonsterInstance& dead) {
                    // M100: owed, not "not alive" -- a creature a script
                    // marked dead with SetDead is not dead the way a killed
                    // one is, and gets none of this.
                    if (!dead.script->TakeDeathOwed()) return;
                    // The routine's slot-0x178 call, `(entity, 1, 5)`: the
                    // corpse leaves the world in five seconds and its OnDecay
                    // runs then (MonsterExecutable::SetDead / TickDecay).
                    dead.script->SetDead(true, sk_bindings::kCorpseDecaySeconds);
                    // M97: the kill's experience, in the death routine's
                    // own position -- before OnKilled runs and before the
                    // loot. See MonsterExecutable::PayKillExperience.
                    dead.script->PayKillExperience();
                    mpOnLocalDeath(dead);  // M129: tag 0x19
                    // M104: and a death that opens a screen has to be handed
                    // the frame, the same way the Use path and the OnDetect
                    // arm already do it. Without this the menu is built and
                    // then drawn over by the 3D view on the same tick --
                    // M80's shape exactly, in a path M80 did not cover.
                    //
                    // 21 shipped handlers do it: `ratherb.s`'s OnKilled, the
                    // Azra herb rat on the game's first quest, plus the
                    // Crypt of Hearts shade waves, the four Skyrim raiders,
                    // the three raider loot rooms, the five Dark Star pit
                    // bosses and both of Pergan Asuul's killed screens (the
                    // last seven through OnDecay, which reaches this same
                    // hand-off via TickDecay).
                    sk_bindings::MenuExecutable* beforeDeathMenu = stack.currentMenu();
                    dead.script->InvokeOnKilled();
                    if (stack.currentMenu() != beforeDeathMenu) {
                        // Same reasoning as the OnDetect arm's: pause at
                        // frame granularity rather than letting the rest of
                        // the tick run inside a frame the world has stopped.
                        // And its ClearPendingEdges(), for the same M90
                        // reason -- the key that landed the killing blow is
                        // still a pending edge, and the screen that just
                        // opened would eat it on its first frame.
                        inGame = false;
                        gamePausedForMenu = true;
                        input.ClearPendingEdges();
                    }
                    spawnLoot(dead);
                    // M24: a real zone-root script's own kill-count trigger
                    // (ghstpass.s's zombieTrigger etc.) -- no-ops if this
                    // zone's real Init() never set one up watching this
                    // typeId.
                    if (gameZoneScript) gameZoneScript->NotifyKilled(dead.typeId);
                    // M45: the `actor+0x2e4` backlink -- see
                    // MonsterInstance::encounter.
                    if (dead.encounter) dead.encounter->NoteDied(dead.encounterRegion);
                    dead.encounter = nullptr;
                };

                // M34: the deaths the AI tick's damage-over-time channels
                // caused above, given exactly the same treatment a killing
                // blow gets below.
                for (size_t deadIndex : gameDiedFromEffect) {
                    if (deadIndex >= gameMonsters.size()) continue;
                    handleDeath(gameMonsters[deadIndex]);
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
                        if (!victim.script->alive() || victim.script->outOfWorld()) continue;
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
                            // M86, post-M120. The impact has already
                            // applied its damage, but only a live creature
                            // is ever a target, so a killing shot still
                            // bleeds: the engine's spurt comes before the
                            // damage, inside the same handler.
                            if (victim.script->alive()) {
                                onCreatureStruck(victim);
                                break;
                            }
                            if (!victim.script->invulnerable() &&
                                victim.typeId != sk_bindings::kBloodImmuneTypeId) {
                                stack.level().effects().push_back(sk_bindings::MakeBloodSpurt(
                                    static_cast<int>(victim.x), static_cast<int>(victim.y),
                                    static_cast<int>(victim.z), static_cast<int>(gameCamera.x),
                                    static_cast<int>(gameCamera.y), [] { return std::rand(); }));
                            }
                            handleDeath(victim);
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
                        if (!victim.script->alive() || victim.script->outOfWorld()) continue;
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
                            // M86, post-M120. The impact has already
                            // applied its damage, but only a live creature
                            // is ever a target, so a killing shot still
                            // bleeds: the engine's spurt comes before the
                            // damage, inside the same handler.
                            if (victim.script->alive()) {
                                onCreatureStruck(victim);
                                break;
                            }
                            if (!victim.script->invulnerable() &&
                                victim.typeId != sk_bindings::kBloodImmuneTypeId) {
                                stack.level().effects().push_back(sk_bindings::MakeBloodSpurt(
                                    static_cast<int>(victim.x), static_cast<int>(victim.y),
                                    static_cast<int>(victim.z), static_cast<int>(gameCamera.x),
                                    static_cast<int>(gameCamera.y), [] { return std::rand(); }));
                            }
                            handleDeath(victim);
                            break;
                        }
                    }
                    gameArrows.erase(std::remove_if(gameArrows.begin(), gameArrows.end(),
                                                     [](const sk_bindings::ArrowProjectile& shot) {
                                                         return !shot.alive;
                                                     }),
                                      gameArrows.end());
                }

                // Player attack -- UseRightAction/UseLeftAction (Key5/
                // Key7, the real decoded default bindings).
                //
                // M78 rebuilt the front of this around the two functions
                // that actually stand between an attack key and a hit,
                // neither of which this port had:
                //
                //   `FUN_10042394(player, item)` -- player vtable +0x280,
                //   "use the item in this hand", reached once per tick for
                //   each attack key that is **held**. It switches on the
                //   item's type (`FUN_1006d508`): a weapon attacks, a
                //   spell casts, a consumable is used -- and the whole
                //   body is wrapped in `if (param_2 != 0)`, so a hand
                //   holding **nothing does nothing**. There is no
                //   bare-fisted attack in this game; the one here was
                //   invented.
                //
                //   `FUN_100425bc(player)` -- player vtable +0x288, the
                //   attack itself, which opens with the three-part gate
                //   CheckPlayerAttackGate() reproduces: the Speed-long
                //   cadence (`player+0xf48`), the weapon-swap transition,
                //   and the swing accumulator. Everything the attack does
                //   -- the 4 fatigue, the swing, the target search, the
                //   sound and the damage -- is one call below that gate,
                //   in that order.
                //
                // That is the whole of the reported bug list. The swing
                // was a decoration played beside an attack that ignored
                // it; now the accumulator it starts is what holds the next
                // attack off, so the sound and the damage are the swing
                // rather than one-per-keypress, and mashing the key cannot
                // outrun it. A five-frame melee weapon's swing is 1792
                // accumulator units draining at 1024 a second: **1.75
                // seconds**, which is the player's real melee attack rate.
                //
                // M20/M49/M51 own everything below the gate -- the ranged
                // branch's launch-and-forget arrow, the melee target
                // search and which of the two swing sounds plays -- and
                // are unchanged.
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
                // (M49: the shot is a projectile that flies and is stopped by
                // the world, so there is no line-of-sight question left.)
                // `FUN_100425bc`. Only ever reached with a real weapon
                // (`kItemTypeWeapon`) in `weapon`, because the type switch
                // in useHandItem below is the only caller -- which is why
                // there is no null or non-weapon handling left in here.
                auto playerAttack = [&](sk_bindings::ItemExecutable* weapon) {
                    // The gate. `weapon` is `player+0x204` as the real
                    // input handler has just set it (`FUN_10042e44`, one
                    // line earlier), and the only thing read off it here
                    // is the ranged flag that makes a bow's cadence six
                    // times as long. See weapon_viewmodel.h.
                    if (sk_bindings::CheckPlayerAttackGate(
                            gameWeaponViewmodel, weapon, stack.player().speed(),
                            sk_bindings::kViewmodelFrameDeltaUnits) !=
                        sk_bindings::PlayerAttackGate::Allowed) {
                        return;
                    }
                    // M73: `FUN_100425bc` spends 4 fatigue before the
                    // ranged/melee split, before the target search and
                    // before any to-hit roll -- so a whiff and a
                    // connecting sword blow cost the same, and a bow costs
                    // it too. The spell path never reaches this function
                    // in the engine either (it is a different vtable
                    // slot), which is why the cast's own 3 is charged
                    // separately.
                    // M78: charged *after* the gate, where the real order
                    // has it -- a press the cadence or an in-flight swing
                    // refuses is free, and this port used to bill 4 for
                    // every one of them.
                    stack.player().SetActorFatigue(stack.player().actorFatigue() -
                                                    sk_bindings::kAttackFatigueCost);
                    // Read after the spend: the halving test below is on
                    // the post-cost pool, exactly as the real order has it.
                    const bool exhausted =
                        sk_bindings::ExhaustedMeleeHalvesDamage(stack.player().actorFatigue());
#if SK_DEBUG_SUITE
                    // SK_DEBUG_SUITE (M68)
                    sk_debug::Count("combat.swings");
#endif
                    // M78: the swing is started *by the attack*, here,
                    // between the fatigue and the target search -- it is
                    // the same statement in the real function. That is the
                    // connection the port was missing: one attack, one
                    // swing, and the accumulator this seeds is what the
                    // gate above will refuse on for the next 1.75 seconds.
                    sk_bindings::StartWeaponSwing(gameWeaponViewmodel, weapon);
                    // M130: `FUN_10002a38` -- the player attacking with an
                    // item in hand (its `+0x18e`, the clip SetClipSize
                    // fills, is 6 on every item) sets `player+0x228`, and
                    // from then on the world within three tiles is lit.
                    if (weapon) gamePlayerGlow = true;
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
                    if (weapon->usesRangedPath()) {
                        // Slot 1 -- barch_firebow.wav in 21 of the 22
                        // shipped sound tables, and the same slot every
                        // archer script's SetAttackNoise(1) names.
                        playPlayerSound(sk_bindings::kBowFireSound);
                        // `rand() % weapon->damageMax` -- the minimum is
                        // simply not consulted on the ranged path, unlike
                        // the melee RandomRange(min, max) below.
                        const int dmgMax = (std::max)(1, weapon->damageMax());
                        gameArrows.push_back(sk_bindings::SpawnArrowProjectile(
                            &stack.player(), /*ownerIsPlayer=*/true,
                            static_cast<int>(gameCamera.x), static_cast<int>(gameCamera.y),
                            static_cast<int>(gameCamera.z), static_cast<int>(gameCamera.z),
                            sk_bindings::EngineYawFromPortYaw(gameCamera.yaw),
                            sk_bindings::EngineAngleFromRadians(gameCamera.pitch),
                            weapon->projectileTypeId(), std::rand() % dmgMax,
                            stack.player().attack()));
                        return;
                    }
                    // ---- M85: how a melee swing finds what it hits ----
                    //
                    // Reported as "I have to move really close to attack
                    // them properly". The reach this port used was
                    // `weapon->range()`, 384 units for every melee weapon
                    // in the corpus -- a tile and a half -- inside a
                    // 60-degree cone. Neither number is the engine's.
                    //
                    // `FUN_100425bc` does not search the world at all. It
                    // reads the **object-ID buffer** -- `engine+0x5b8`,
                    // the 176x208 byte plane every stencil rasterizer
                    // stamps its actor's `+0x2d4` into as it draws -- at
                    // five fixed screen points:
                    //
                    //     FUN_1001afb0(engine, 0x58, 0x68)   // (88, 104)
                    //     FUN_1001afb0(engine, 0x58, 0x7c)   // (88, 124)
                    //     FUN_1001afb0(engine, 0x58, 0x90)   // (88, 144)
                    //     FUN_1001afb0(engine, 0x58, 0xa4)   // (88, 164)
                    //     FUN_1001afb0(engine, 0x58, 0xb8)   // (88, 184)
                    //
                    // -- one column down the exact centre of the screen,
                    // starting at the vertical centre and stepping 20
                    // pixels at a time toward the bottom, taking the
                    // **first** one that is not empty. So melee in this
                    // game is "whatever your crosshair column is actually
                    // pointing at", occlusion included for free: a
                    // creature behind a wall or behind another creature
                    // never reaches the buffer, so it can never be picked.
                    //
                    // Then three gates, in this order:
                    //
                    //     if (|(s16)target+0xa4 - (s16)player+0xa4| > 0x180) target = 0;
                    //     ... && target->vtable[0xe4]()          // is an actor
                    //     && target->+0x1e2 == 0                 // not invulnerable
                    //     && target->vtable[0x5c](player) < 0x76c
                    //
                    // `vtable+0x5c` is the entity-to-entity distance --
                    // the same slot the area spell's `< 12000` uses -- so
                    // the real melee reach is **0x76c == 1900 raw units,
                    // about seven and a half tiles**, five times what this
                    // port allowed. The vertical gate is the same 0x180
                    // the creature side already uses (monster_ai.h's
                    // kAttackVerticalLimitUnits), and it does *not* fall
                    // through to the next probe: a target picked and then
                    // rejected on height means the swing hits nothing.
                    //
                    // The buffer this reads was stamped by the **previous**
                    // frame's draw, in the engine as much as here -- the
                    // scene is rendered after the tick in both. That is
                    // not a defect to work around: it is why the ids have
                    // to be stable across frames (see MonsterInstance::
                    // objectId).
                    //
                    // `vtable[0xe4]` needs no port equivalent: only live
                    // creatures are ever stamped, which is the same set
                    // that answers it true.
                    auto pickMeleeTarget = [&]() -> MonsterInstance* {
                        constexpr int kProbeX = 0x58;
                        constexpr int kProbeY[] = {0x68, 0x7c, 0x90, 0xa4, 0xb8};
                        uint8_t id = 0;
                        for (int probeY : kProbeY) {
                            id = zoneRenderer.ObjectIdAt(kProbeX, probeY);
                            if (id != 0) break;
                        }
                        if (id == 0) return nullptr;
                        MonsterInstance* found = nullptr;
                        for (MonsterInstance& m : gameMonsters) {
                            if (m.objectId == static_cast<int>(id)) {
                                found = &m;
                                break;
                            }
                        }
                        if (!found) return nullptr;
                        // The height gate, applied to the pick and not
                        // used to reject-and-retry. Both z's carry the
                        // player's eye offset on this side (see the AI
                        // tick's identical line), so it cancels and this
                        // is feet-to-feet, like the engine's `+0xa4` pair.
                        if (std::fabs(gameCamera.z - (found->z + sk::kEyeHeightOffset)) >
                            static_cast<float>(sk_bindings::kPlayerMeleeVerticalLimit)) {
                            return nullptr;
                        }
                        if (!found->script->alive() || found->script->outOfWorld() ||
                            found->script->invulnerable()) {
                            return nullptr;
                        }
                        const float ddx = found->x - gameCamera.x, ddy = found->y - gameCamera.y;
                        if (std::sqrt(ddx * ddx + ddy * ddy) >=
                            static_cast<float>(sk_bindings::kMeleeReachUnits)) {
                            return nullptr;
                        }
                        return found;
                    };
                    MonsterInstance* target = pickMeleeTarget();
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
                    {
                        // M78: the "a non-weapon item swings like a club
                        // for 1-3" arm is gone with the bare fists -- the
                        // only way into this lambda is a real weapon, so
                        // the damage is always the weapon's own
                        // SetDamageMin/SetDamageMax pair (`item+0x1cc` /
                        // `item+0x1ce`, which is what the real melee
                        // branch reads).
                        int dmgMin = weapon->damageMin();
                        int dmgMax = weapon->damageMax();
                        // M43: attack(), not baseAttack() -- a creature can
                        // Drain, Weaken, FeebleBlade, Disease or Blind the
                        // player now, and every one of those is a timed
                        // modifier on the player's own attack stat.
                        // M73: `exhausted` is the fatigue penalty -- see
                        // where it is computed above, and RollDamage's own
                        // comment for where in the roll it lands.
                        // M87: the *player's* roll, not the creature's --
                        // `FUN_100730c8` is inclusive at the top where the
                        // creature's `rand % span` is exclusive, and the
                        // two hardcoded bonus arms live here too. See
                        // simkin_bindings/combat.h.
                        const sk_bindings::ItemExecutable::DamageBonus* spiderBonus =
                            weapon->FindDamageBonus(
                                sk_bindings::ItemExecutable::kSpiderBonusKey);
                        sk_bindings::PlayerMeleeResult roll =
                            sk_bindings::RollPlayerMeleeDamage(
                                stack.player().attack(), target->script->defense(),
                                target->script->armorValue(), dmgMin, dmgMax, exhausted,
                                target->script->spider() && spiderBonus != nullptr,
                                spiderBonus ? spiderBonus->min : 0,
                                spiderBonus ? spiderBonus->max : 0,
                                weapon->templateId() == sk_bindings::kTemplateMagickaEdgeAxe,
                                stack.player().magicka());
                        if (roll.magickaSpent > 0) {
                            stack.player().SetActorMagicka(stack.player().magicka() -
                                                            roll.magickaSpent);
                        }
#if SK_DEBUG_SUITE
                        // SK_DEBUG_SUITE (M68). Counted per swing that finds
                        // a target, whatever the roll -- the M84/M85 reach
                        // configs read it as "the swing reached".
                        sk_debug::Count("combat.hits_by_player");
#endif
                        // M98: a miss ends here. It used to fall through
                        // as a zero-damage hit, which flashed the target
                        // red for a blow that never reached it.
                        if (roll.hit) {
                            // M86: `FUN_10081844`'s own `FUN_10067c3c(self,
                            // 8, 0, 5)`, the red rows -- armed in the take-
                            // damage handler, next to the damage and before
                            // it is applied, so it fires for an absorbed hit
                            // as well as a damaging one. This port has no
                            // single such handler (M81 hit the same problem
                            // for deaths), so it is armed at each of the four
                            // places an attack damages a creature.
                            onCreatureStruck(*target);  // post-M120
                            // M97: FUN_100425bc passes `player + 0x3ac` as
                            // the source, so a killing swing pays the player.
                            // M98: and melee is p5 = 0. The source is also
                            // where the Strength term comes from -- added
                            // after the armour, so an absorbed swing still
                            // lands it.
                            const int dealt = target->script->ApplyDamage(
                                roll.damage, &stack.player(), /*ranged=*/false);
#if SK_DEBUG_SUITE
                            // What came off health, after the terms.
                            sk_debug::Count("combat.damage_dealt", dealt);
#else
                            (void)dealt;
#endif
                        }
                    }
                    if (!target->script->alive()) handleDeath(*target);
                };
                // `FUN_10042394` -- the type switch that is the only way
                // into an attack, a cast or a use.
                // Returns the engine's own return value: 1 everywhere except
                // the consumable arm, whose 0 makes the caller release the
                // key (see below).
                auto useHandItem = [&](sk_bindings::ItemExecutable* handItem) -> bool {
                    // `if (param_2 != 0)` wraps the entire function. An
                    // empty hand is not an attack, not a miss and not a
                    // sound -- there is no bare-handed combat here.
                    if (!handItem) return true;
                    const int type = handItem->itemType();
                    if (type == sk_bindings::kItemTypeWeapon) {
                        // `FUN_1006d508(item) == 1`. The one thing between
                        // the type test and the attack is the Sanctuary
                        // channel (`stats+0x7c == 4`), which is checked
                        // here and nowhere else -- being inside Sanctuary
                        // means you cannot swing.
                        if (stack.player().actorStats().periodicKind() ==
                            sk_bindings::ActorStats::kPeriodicSanctuaryTimer) {
                            return true;
                        }
                        playerAttack(handItem);
                        return true;
                    }
                    // M116: the type alone, as the engine tests it. This
                    // used to be `|| spellTypeId() != 0`, left over from
                    // before M74 made the type real -- and spellTypeId()
                    // falls back to the template id, which every item
                    // spawned from entities.txt has. So every real potion
                    // and loaf of bread was sent here, failed to cast, and
                    // never reached the consumable arm below.
                    if (type == sk_bindings::kItemTypeSpell) {
                        // `FUN_1006d508(item) == 2`. M48: the real cast
                        // never looks for a target -- it charges the
                        // caster, applies its self-targeted half and
                        // launches a projectile down the caster's facing,
                        // so a pure buff (Energize, Sanctuary, HealWound)
                        // works with nothing in front of you.
                        //
                        // M78: the cast's own swing. Case 2's tail spends
                        // 3 fatigue (inside runSpellCast, M73) and then
                        // re-arms the swing from whatever weapon is on
                        // screen -- with **no** cadence gate and no swing
                        // gate, overwriting one in flight. Reproduced as
                        // found: casting is not rate-limited by the swing
                        // the way swinging is, only by the spell's own
                        // refire cooldown and by magicka.
                        if (!runSpellCast(handItem, &stack.player(), gameCamera.x, gameCamera.y,
                                          gameCamera.z, gameCamera.yaw, gameCamera.pitch)) {
                            return true;
                        }
                        sk_bindings::StartSpellSwing(gameWeaponViewmodel);
                        return true;
                    }
                    // `FUN_1006d508(item) == 4`: a consumable. M116: the
                    // real arm is `item->vtable[0x90](item, player, 0)` --
                    // the same OnUsedBy the inventory's Use row runs -- and
                    // it is the one arm that returns **0**. Anything else
                    // (misc, armour) falls out of the switch with a 1.
                    if (type == sk_bindings::kItemTypeConsumable) {
                        handItem->InvokeOnUsedBy(&stack.player());
                        return false;
                    }
                    return true;
                };
                // The real input handler's own order and its own read: the
                // right hand's bound button (0xf) first, then the left
                // (0xe), both with the plain `InputState_GetBoundButton`
                // -- **held**, not an edge. The jump and use keys a few
                // lines above them in the same function do their own edge
                // detection with `_GetBoundButtonPrev`, so the difference
                // is deliberate: attacking is a held action whose rate is
                // the cadence and the swing, not the keyboard's.
                const bool useRightHeld = input.GetBoundButton(sk::Action::UseRightAction);
                const bool useLeftHeld = input.GetBoundButton(sk::Action::UseLeftAction);
                // Drain the edge latches these two slots would otherwise
                // keep, so a press made during play cannot fire again in
                // the next menu that opens.
                input.ConsumeBoundJustPressed(sk::Action::UseRightAction);
                input.ConsumeBoundJustPressed(sk::Action::UseLeftAction);
                // `FUN_10042e44`, player vtable **+0x274** -- the line the
                // real handler runs immediately before each hand's use, and
                // the whole of what puts art on screen for the hand you are
                // pressing:
                //
                //     if (item && (type == 1 || type == 2)) {
                //         player->+0x22c = item->+0x19c;   // its sprite
                //         player->+0x204 = item;
                //     } else {
                //         player->+0x22c = -1;
                //         player->+0x204 = 0;
                //     }
                //
                // M79: **a spell takes the same arm as a weapon.** The port
                // set the active item only inside the weapon branch of
                // FUN_10042394, one level down, so a spell in hand never
                // became `player+0x204` -- which is why a cast drew nothing
                // and re-armed nothing. Everything that is not a weapon or a
                // spell (a consumable in the left hand, say) clears it, so
                // the viewmodel goes empty rather than keeping the last
                // weapon.
                auto setActiveHandItem = [&](sk_bindings::ItemExecutable* handItem) {
                    const bool drawable =
                        handItem != nullptr && (handItem->itemType() == sk_bindings::kItemTypeWeapon ||
                                                handItem->itemType() == sk_bindings::kItemTypeSpell);
                    gameActiveHandItem = drawable ? handItem : nullptr;
                };
                // M116: the two cycle keys, which the real input poll
                // (`FUN_1001c9c0`) handles first of everything, as edges:
                // action 10 (Cycle Right Queue, keypad 0) -> player vtable
                // +0x270, action 11 (Cycle Left Queue, keypad *) -> +0x26c.
                // Each rotates that hand's action queue and wields the new
                // head. They run before this tick's hand use, as there.
                if (input.ConsumeBoundJustPressed(sk::Action::CycleRightQueue)) {
                    stack.player().CycleQueue(sk_bindings::kEquipSlotRight);
                }
                if (input.ConsumeBoundJustPressed(sk::Action::CycleLeftQueue)) {
                    stack.player().CycleQueue(sk_bindings::kEquipSlotLeft);
                }
                // M116: each hand's use goes through a wrapper (player
                // vtable +0x27c right, `FUN_10044c04`; +0x278 left,
                // `FUN_10044c50`) that forces the key up when the use
                // returns 0 -- the consumable arm -- so holding the key
                // drinks one potion, while a weapon keeps swinging.
                if (useRightHeld) {
                    setActiveHandItem(stack.player().rightItem());
                    if (!useHandItem(stack.player().rightItem())) {
                        input.ReleaseBound(sk::Action::UseRightAction);
                    }
                }
                if (useLeftHeld) {
                    setActiveHandItem(stack.player().leftItem());
                    if (!useHandItem(stack.player().leftItem())) {
                        input.ReleaseBound(sk::Action::UseLeftAction);
                    }
                }

                // M81: the back-stop for every other way a creature reaches
                // zero health this tick. The engine needs none of this --
                // its death handler is the actor's own vtable slot, reached
                // from the damage path itself -- but this port damages
                // creatures from several places, and three of them had no
                // death handling at all: an area spell's own damage loop
                // (`FUN_1004720c`, which just ApplyDamage()s everything
                // within 12000 units and is defined too early in this
                // function to reach handleDeath), a script's `DoDamage`
                // native (M65, 19 real call sites), and the debug console's
                // `killall`. A creature killed by any of those used to lie
                // there with no OnKilled(), no loot bag and no kill-count
                // trigger. handleDeath() is idempotent, so the four callers
                // that already report their own kills cost nothing here.
                for (MonsterInstance& m : gameMonsters) {
                    if (m.script && m.script->deathOwed()) handleDeath(m);
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
                // M78: `player+0x204` first. The real draw reads the
                // active weapon directly (`player->0x204->0x19c`), and the
                // active weapon is whichever hand's key you last pressed
                // -- FUN_10042e44 writes it with no transition of its own.
                // Falling back to the equipped hands covers the case that
                // hand has since been emptied, which the real equip path
                // handles the same way.
                {
                    sk_bindings::ItemExecutable* shown = gameActiveHandItem;
                    if (shown != stack.player().leftItem() &&
                        shown != stack.player().rightItem()) {
                        shown = nullptr;
                    }
                    if (!shown || shown->weaponSprite() < 0) shown = stack.player().rightItem();
                    if (!shown || shown->weaponSprite() < 0) shown = stack.player().leftItem();
                    if (shown && shown->weaponSprite() < 0) shown = nullptr;
                    sk_bindings::NotifyWeaponChanged(gameWeaponViewmodel, shown);
                }
                // The bob runs down by FUN_1001f230's own amount: the move
                // step, once per direction held this tick (summed where
                // the thrust is applied). Standing still freezes the phase.
                sk_bindings::TickWeaponViewmodel(gameWeaponViewmodel, gameSwayThisTick);
                gameSwayThisTick = 0;
                // M130: the scene's light sources for the frames this tick
                // draws (ZoneRenderer::SceneLights) -- the player's glow
                // and the lanterns and torches the level placed.
                {
                    sk::ZoneRenderer::SceneLights& lights = zoneRenderer.lights;
                    lights.playerGlow = gamePlayerGlow;
                    lights.playerTileX = static_cast<int>(std::floor(gameCamera.x)) >> 8;
                    lights.playerTileY = static_cast<int>(std::floor(gameCamera.y)) >> 8;
                    lights.seed = ++gameLightSeed;
                    lights.emitterTiles.clear();
                    lights.fullTiles.clear();
                    // M131: a spell in flight (`+0x86 |= 0x10` at its spawn)
                    // pins its tile to full light; Blind quarters the rest.
                    for (const sk_bindings::SpellProjectile& p : gameProjectiles) {
                        if (p.alive) lights.fullTiles.push_back({p.x >> 8, p.y >> 8});
                    }
                    lights.blind = stack.player().blinded();
                    if (gameZone) {
                        for (const sk::Zone::EntPlacement& e : gameZone->entities()) {
                            if (e.typeId != 3 && e.typeId != 0x47 && e.typeId != 0x1787) continue;
                            lights.emitterTiles.push_back(
                                {static_cast<int>(e.x) >> 8, static_cast<int>(e.y) >> 8});
                        }
                    }
                }

                // M15/M16/M19: Action::Use (Key3, docs/INPUT_HANDLING.md's
                // default scheme) interact binding -- doors (M15), usable
                // NPCs (M16, e.g. Tanyin Aldwyr's real dialogue, see
                // monster_executable.h's class comment), and world pickups
                // (M19, e.g. snowline/foxglove.s -- the last remaining
                // unbound category, docs/PORT_ROADMAP.md). Same nearest-in-
                // range-and-facing-cone targeting playerAttack uses above,
                // reused here (and again below for the on-screen use-text
                // prompt).
                // M131: **the use target is the engine's**, `FUN_1001dd40`
                // -- twelve rays fanned eight degrees either side of the
                // heading, half a tile a step, out to two and a half tiles,
                // the first usable entity any of them meets; a wall tile
                // ends the search. See simkin_bindings/use_prompt.h.
                //
                // What it replaces was this port's own: three nearest-first
                // searches (doors, NPCs, pickups) inside a 384-unit, ~72
                // degree cone, with no wall test at all -- so a chest
                // answered Use through a wall, and nothing answered from
                // further than a tile and a half.
                //
                // M75's one gate is unchanged: `entity+0xd8`, usable().
                // Death clears it on a creature (monster_executable.cpp), so
                // a corpse is only found if its script names it again.
                // Within one tile the engine takes the head of the tile's
                // entity list; this port keeps no such list, and takes a
                // pickup before a creature before a door.
                struct UseTarget {
                    DoorInstance* door = nullptr;
                    MonsterInstance* npc = nullptr;
                    PickupInstance* pickup = nullptr;
                };
                auto findUseTarget = [&]() -> UseTarget {
                    struct Candidate {
                        int tx, ty, kind;
                        size_t index;
                    };
                    std::vector<Candidate> candidates;
                    const int eyeZ = static_cast<int>(gameCamera.z);
                    const int heightWindow = stack.player().health() <= 0
                                           ? sk_bindings::kUseHeightWindowDead
                                           : sk_bindings::kUseHeightWindow;
                    auto consider = [&](float x, float y, float z, int kind, size_t index) {
                        if (std::abs(eyeZ - static_cast<int>(z)) > heightWindow) return;
                        candidates.push_back({static_cast<int>(x) >> 8, static_cast<int>(y) >> 8,
                                              kind, index});
                    };
                    for (size_t i = 0; i < gamePickups.size(); ++i) {
                        const PickupInstance& p = gamePickups[i];
                        if (p.script->entityOutOfWorld() || !p.script->usable()) continue;  // M92/M101
                        consider(p.x, p.y, p.z, 0, i);
                    }
                    for (size_t i = 0; i < gameMonsters.size(); ++i) {
                        const MonsterInstance& m = gameMonsters[i];
                        if (m.script->outOfWorld() || !m.script->usable()) continue;
                        consider(m.x, m.y, m.z, 1, i);
                    }
                    for (size_t i = 0; i < gameDoors.size(); ++i) {
                        const DoorInstance& d = gameDoors[i];
                        if (d.script->entityOutOfWorld() || !d.script->usable()) continue;
                        consider(d.x, d.y, d.z, 2, i);
                    }
                    UseTarget target;
                    if (candidates.empty()) return target;
                    const int found = sk_bindings::FindUseTarget(
                        static_cast<int>(gameCamera.x), static_cast<int>(gameCamera.y),
                        sk_bindings::EngineYawFromPortYaw(gameCamera.yaw), gameZone->width(),
                        gameZone->height(),
                        [&](int tx, int ty) { return gameZone->CellAt(tx, ty).IsWall(); },
                        [&](int tx, int ty) {
                            for (size_t i = 0; i < candidates.size(); ++i) {
                                if (candidates[i].tx == tx && candidates[i].ty == ty) {
                                    return static_cast<int>(i);
                                }
                            }
                            return -1;
                        });
                    if (found < 0) return target;
                    const Candidate& c = candidates[static_cast<size_t>(found)];
                    if (c.kind == 0) target.pickup = &gamePickups[c.index];
                    if (c.kind == 1) target.npc = &gameMonsters[c.index];
                    if (c.kind == 2) target.door = &gameDoors[c.index];
                    return target;
                };
                if (input.ConsumeBoundJustPressed(sk::Action::Use)) {
                    const UseTarget useTarget = findUseTarget();
                    DoorInstance* door = useTarget.door;
                    MonsterInstance* npc = useTarget.npc;
                    PickupInstance* pickup = useTarget.pickup;
                    if (pickup) {
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
                        const unsigned beforeMenu = stack.openSerial();  // M133
#if SK_DEBUG_SUITE
                        // SK_DEBUG_SUITE (M68)
                        sk_debug::Count("pickup.used");
#endif
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
                                input.ClearPendingEdges();  // M90, see its comment
                            } else {
                                stack.player().AddItem(std::move(pickup->script));
                                gamePickups.erase(gamePickups.begin() +
                                                   (pickup - gamePickups.data()));
                            }
                        } else if (stack.player().TakePendingPickupItem() ==
                                       static_cast<skiExecutable*>(pickup->script.get())) {
                            // M101: the pickup itself is what takes a world
                            // item out of the world -- keyed on
                            // PickupItem(self) naming this instance, not on
                            // a removal flag. The MirrorDestroyObject(self)
                            // the herbs follow it with is a multiplayer
                            // notify (entity_base_ref.h), and treating it as
                            // the removal is what left the flag on them in
                            // the inventory. `items/shadowkey1.s`'s
                            // DestroyObjectMirror(self) after its pickup
                            // does leave `entityRemoved()` set on the
                            // carried key, which nothing in the inventory
                            // reads.
                            stack.player().AddItem(std::move(pickup->script));
                            gamePickups.erase(gamePickups.begin() +
                                               (pickup - gamePickups.data()));
                        } else if (pickup->script->markedForRemoval() ||
                                   pickup->script->entityRemoved()) {
                            // Gone without being taken: the top-of-tick
                            // sweep erases it, and unnames it first.
                        } else if (stack.openSerial() != beforeMenu) {
                            inGame = false;
                            gamePausedForMenu = true;
                            input.ClearPendingEdges();  // M90, see its comment
                        }
                    } else if (npc) {
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
                        const unsigned before = stack.openSerial();  // M133
                        npc->script->InvokeOnUse();
                        if (stack.openSerial() != before) {
                            inGame = false;
                            gamePausedForMenu = true;
                            input.ClearPendingEdges();  // M90, see its comment
                        }
                    } else if (door) {
                        const unsigned doorMenuSerial = stack.openSerial();  // M133
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
#if SK_DEBUG_SUITE
                        // SK_DEBUG_SUITE (M68): the interaction M67 was about
                        // -- worth a counter and an event of its own, with the
                        // passability on both sides of the call.
                        {
                            const bool before = door->script->passable();
                            sk_debug::Count("door.used");
                            sk_debug::Log("world", "Use door \"" + door->name +
                                                        "\" passable " +
                                                        (before ? "true" : "false") + " -> ");
                        }
#endif
                        door->script->InvokeOnUse();
#if SK_DEBUG_SUITE
                        // SK_DEBUG_SUITE (M68)
                        sk_debug::Log("world",
                                       std::string("  door \"") + door->name + "\" now passable " +
                                           (door->script->passable() ? "true" : "false"));
#endif
                        // M133: and a door's OnUse can open a screen too --
                        // `lockeddoor.s`'s UsePicks, delfhide's
                        // `Cell_lockeddoor.s` and its Use_Cellkey. This
                        // branch never handed the frame over, so the menu
                        // was built and drawn over by the 3D view: no
                        // locked door in the game could be opened by key
                        // or by pick. Same hand-off as the NPC branch.
                        if (stack.openSerial() != doorMenuSerial) {
                            inGame = false;
                            gamePausedForMenu = true;
                            input.ClearPendingEdges();  // M90, see its comment
                        }
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
                // M85: hand every live creature its object-ID byte, the
                // real `actor+0x2d4`. The engine allocates one per actor
                // and registers the actor in `engine+0x14620[id]`; here
                // the id lives on the instance and the reverse lookup is a
                // scan, which is the same relation without a second table
                // to keep in step. Ids are kept for the creature's whole
                // life and only recycled once it is gone, and 0 stays
                // reserved -- `FUN_1001afb0` answers a 0 byte with a null
                // actor, which is exactly "nothing was drawn here".
                //
                // 255 live creatures is the engine's own ceiling (the
                // stamp is a byte); past it a creature simply keeps id 0
                // and cannot be melee-targeted, rather than stealing
                // somebody else's slot.
                {
                    bool idTaken[256] = {};
                    for (const MonsterInstance& m : gameMonsters) {
                        if (m.objectId > 0 && !m.script->outOfWorld()) idTaken[m.objectId] = true;
                    }
                    for (MonsterInstance& m : gameMonsters) {
                        if (m.script->outOfWorld()) {
                            m.objectId = 0;
                            continue;
                        }
                        if (m.objectId != 0) continue;
                        for (int id = 1; id < 256; ++id) {
                            if (idTaken[id]) continue;
                            idTaken[id] = true;
                            m.objectId = id;
                            break;
                        }
                    }
                }
                for (MonsterInstance& m : gameMonsters) {
                    // M23: destroyed() (a real zone-root script's
                    // DestroyObjectMirror()) hides an entity from the
                    // world entirely -- unlike death, which now plays the
                    // creature's own real SetDeathAnimation() clip and
                    // leaves the body in its final pose. Before M28 a
                    // killed monster simply blinked out of existence,
                    // which is also why the death clip every real monster
                    // script sets had nothing to play it.
                    if (m.script->outOfWorld()) continue;
                    // M86: `FUN_10064f08`, and it lives here for the same
                    // reason the engine puts it in `FUN_10064ffc` -- the
                    // flash is advanced by the *draw*, not by the AI tick,
                    // so a creature keeps flashing while `freezeai` holds
                    // the world still and a dead one finishes its flash
                    // over its corpse.
                    m.flash.Tick(sk_bindings::kAiFrameDeltaUnits);
                    // The AI tick's own arming call, `FUN_10067c3c(self,
                    // 8, 7, 0xc)` -- the **green** rows. The engine fires
                    // it from two conditions: any live timed stat node
                    // that is either a negative magnitude (`node+8 < 0`)
                    // or kind 3, and separately the burn flag `+0x308`.
                    // So a creature under a curse or on fire pulses green
                    // for as long as the effect lasts, re-armed every
                    // frame, where a hit flashes red once and stops.
                    if (m.script->alive() && !m.script->outOfWorld()) {
                        bool cursed = m.script->actorStats().burning();
                        if (!cursed) {
                            for (const sk_bindings::Effect& e :
                                 m.script->actorStats().timedEffects()) {
                                if (e.remaining <= 0) continue;
                                if (e.stored < 0 || e.op == 3) {
                                    cursed = true;
                                    break;
                                }
                            }
                        }
                        // Only when nothing louder is already running: the
                        // real order arms the green rows before the red
                        // ones can be armed by a hit, and a hit that lands
                        // this frame overwrites them.
                        if (cursed && !m.flash.active()) {
                            m.flash.Arm(sk::kFlashPeriodUnits, sk::kFlashGreenMin,
                                        sk::kFlashGreenMax);
                        }
                    }
                    // Real per-instance appearance from the creature's own
                    // script (SetSkin/SetScale) -- see
                    // monster_executable.h.
                    // M92: SetModel(n) is a models.txt row and it wins
                    // over the placement's entities.txt one -- that is how
                    // `twilite/volstok_violet.s` turns Volstok into a
                    // zombie (row 69) and how `monsters/ivgrizt.s` is a
                    // goblin (row 61) rather than whatever its placement
                    // says.
                    const int monsterModel = m.script->modelOverride() >= 0
                                                 ? m.script->modelOverride()
                                                 : m.modelArchiveIndex;
                    sk::PlacedEntity pe{m.x, m.y, m.z, monsterModel, m.facingYaw};
                    pe.rotA = m.rotA;  // M71
                    pe.rotB = m.rotB;
                    pe.skinIndex = m.script->skin();
                    pe.scale = m.script->scale();
                    pe.frameIndex = AdvanceMonsterAnimation(m, modelArchive);
                    // M85/M86: the two parameters `FUN_10064ffc` fills in
                    // for a creature and leaves at their defaults for
                    // everything else -- `actor+0x2d4` as the stencil byte
                    // and `actor+0x152` as the flash ramp row (-1 when the
                    // flash is not running, which is what routes the draw
                    // back to the ordinary rasterizers).
                    pe.objectId = m.objectId;
                    // Post-M120: and only while it lives -- FUN_10064ffc
                    // asks `vtable+0x170` first, which is `ldrb r0,
                    // [r0, #0x1e4]`, the dead flag, and draws a corpse with
                    // no ramp at all. The killing blow arms the flash on a
                    // creature that is about to die; this is what keeps the
                    // corpse from flashing red through its death clip.
                    pe.flashLevel = m.script->alive() ? m.flash.row : -1;
                    // M119: its identity for the in-between frames (and its
                    // attached weapon's, which copies this whole entry).
                    pe.interpKey = reinterpret_cast<uintptr_t>(m.script.get());
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
                        // M85: `FUN_10083490` passes -1 as the weapon
                        // draw's id, so an attached weapon is drawn but
                        // never stamped -- you cannot melee-target the
                        // sword in a bandit's hand, only the bandit. It
                        // *does* inherit the body's flash, because the
                        // override copies `actor+0x152` across with the
                        // rest of the transform block.
                        weapon.objectId = 0;
                        frameEntities.push_back(weapon);
                    }
                }
                for (const DoorInstance& d : gameDoors) {
                    // M92: ShowEntity(false) -- see entity_base_ref.h.
                    // M101: and DestroyObject, which nothing undoes.
                    if (d.script->entityOutOfWorld()) continue;
                    // Real wall-facing heading from the .ent placement, plus
                    // whatever the script's own AddRotationTurn() has
                    // accumulated (the 90-degree swing door.s applies on
                    // open).
                    //
                    // M71: composed in the engine's *raw* units, via the
                    // same `headingRaw()` the tile stamp already walks,
                    // rather than adding two radian values. The port's yaw
                    // runs the opposite way round from a raw heading
                    // (`yaw = pi/2 - heading`, see CameraYawRadians), so
                    // adding a raw turn's radians to a converted heading
                    // swung every door the wrong way -- invisible only
                    // because the two happened to cancel for a slab.
                    sk::PlacedEntity door{d.x, d.y, d.z, d.modelArchiveIndex,
                                           sk_bindings::PortYawFromEngineYaw(d.script->headingRaw())};
                    door.rotA = d.rotA;
                    door.rotB = d.rotB;
                    door.scale = d.scale;
                    door.interpKey = reinterpret_cast<uintptr_t>(d.script.get());  // M119
                    frameEntities.push_back(door);
                }
                // M19: still-in-world pickups -- gamePickups shrinks as
                // items are actually picked up (see the Action::Use
                // handling above), so this naturally stops drawing one the
                // instant it's gone.
                for (const PickupInstance& p : gamePickups) {
                    // M92: ShowEntity(false) -- `stouttp.s` hides the "old
                    // trinket" until the shopkeeper mentions it, and
                    // `lothna/loot_pilgrim.s` hides itself on Init().
                    if (p.script->entityOutOfWorld()) continue;  // M101: or removed
                    sk::PlacedEntity pickup{p.x, p.y, p.z, p.modelArchiveIndex, p.placementYaw};
                    pickup.rotA = p.rotA;  // M71
                    pickup.rotB = p.rotB;
                    pickup.scale = p.scale;
                    pickup.interpKey = reinterpret_cast<uintptr_t>(p.script.get());  // M119
                    frameEntities.push_back(pickup);
                }
                // M49: arrows in flight. An arrow is a *model*, not a
                // billboard -- entities.txt maps its typeId 599/598 to
                // models.idx 175/176, which models.txt names arrow.bin and
                // throw_dagger.bin -- which is what puts it in this list and
                // a fireball in the billboard one below. The heading is
                // converted back from the engine's own convention (zero
                // along +y) to this port's.
                for (const sk_bindings::ArrowProjectile& shot : gameArrows) {
                    if (shot.modelIndex < 0) continue;
                    sk::PlacedEntity arrow{static_cast<float>(shot.x), static_cast<float>(shot.y),
                                           static_cast<float>(shot.z), shot.modelIndex,
                                           sk_bindings::PortYawFromEngineYaw(shot.yaw)};
                    arrow.interpKey = kInterpArrowTag | shot.interpSerial;  // M119
                    frameEntities.push_back(arrow);
                }
                // M79: the animated-sprite entities. Spell projectiles and
                // scripted effects are one C++ class in the engine
                // (simkin_bindings/effect_entity.h) with one draw
                // (`FUN_1008b25c`), so they build one list here; the
                // per-entity half-extents come from that class's own doubled
                // sprite-dimension multiply, which is why the sprite has to
                // be resolved before the quad can be sized.
                mpAppendAvatar(frameEntities);  // M129: the other player
                std::vector<sk::SpriteBillboard> frameBillboards;
                for (const sk_bindings::SpellProjectile& shot : gameProjectiles) {
                    const sk::Sprite* art = spriteArchive.GetSprite(shot.sprite);
                    if (!art) continue;
                    sk::SpriteBillboard bb;
                    bb.x = static_cast<float>(shot.x);
                    bb.y = static_cast<float>(shot.y);
                    bb.z = static_cast<float>(shot.z);
                    bb.sprite = art;
                    bb.halfWidth =
                        sk_bindings::SpriteHalfExtent(sk_bindings::kProjectileDrawSize, art->width);
                    bb.halfHeight =
                        sk_bindings::SpriteHalfExtent(sk_bindings::kProjectileDrawSize, art->height);
                    bb.blendMode = sk_bindings::kProjectileBlendMode;
                    bb.blendLevel = sk_bindings::kProjectileBlendLevel;
                    bb.interpKey = kInterpSpellTag | shot.interpSerial;  // M119
                    frameBillboards.push_back(bb);
                }
                for (const sk_bindings::EffectEntity& fx : stack.level().effects()) {
                    if (!fx.alive) continue;
                    const sk::Sprite* art = spriteArchive.GetSprite(fx.sprite);
                    if (!art) continue;
                    sk::SpriteBillboard bb;
                    bb.x = static_cast<float>(fx.x);
                    bb.y = static_cast<float>(fx.y);
                    bb.z = static_cast<float>(fx.z);
                    bb.sprite = art;
                    bb.halfWidth = sk_bindings::EffectHalfWidth(fx, art->width);
                    bb.halfHeight = sk_bindings::EffectHalfHeight(fx, art->height);
                    bb.blendMode = fx.drawFlags;
                    bb.blendLevel = fx.scale;
                    bb.interpKey = kInterpEffectTag | fx.interpSerial;  // M119
                    frameBillboards.push_back(bb);
                }
#if SK_DEBUG_SUITE
                // SK_DEBUG_SUITE (M68): the render-list counters. The frame
                // counter and the 3D-pass timer moved into
                // drawGameplayFrame (M119), which runs once per drawn frame.
                sk_debug::Count("render.entities", static_cast<long long>(frameEntities.size()));
                sk_debug::Count("render.billboards",
                                static_cast<long long>(frameBillboards.size()));
#endif
                // M86: `FUN_1002ae88` counts the hit timer down inside the
                // HUD draw, so this port does too -- one place, and it
                // stops while a menu holds the world.
                stack.player().TickHurtTimer(sk_bindings::kAiFrameDeltaUnits);
                // M131: **the prompt is the engine's**, `FUN_1002acd4`: the
                // use text of whatever the use-target search found, and
                // nothing else. A creature that can be fought is named in
                // yellow, one that cannot in white, a lootable corpse gets
                // "Examine:". The "name  12/30" health line this replaces
                // (M12, with M22's invented 300-unit `kSpellRange` deciding
                // how far a spell in hand could read it from) has no
                // counterpart in the binary -- the game never shows a
                // creature's health.
                gameplayLabel.clear();
                gameplayLabelColor = sk::PackRGB565(255, 255, 255);
                {
                    const UseTarget prompt = findUseTarget();
                    auto useText = [&](int id) {
                        // 0x1006842c: the entity's own text, else string 13.
                        return strings.Get(id >= 0 ? id : sk_bindings::kDefaultUseTextId);
                    };
                    if (prompt.pickup) {
                        gameplayLabel = sk_bindings::UsePromptText(
                            useText(prompt.pickup->script->useTextId()), false, false, false);
                    } else if (prompt.door) {
                        gameplayLabel = sk_bindings::UsePromptText(
                            useText(prompt.door->script->useTextId()), false, false, false);
                    } else if (prompt.npc) {
                        const sk_bindings::MonsterExecutable& m = *prompt.npc->script;
                        gameplayLabel = sk_bindings::UsePromptText(
                            useText(m.useTextId()), true, m.lootTypeId() != 0, !m.alive());
                        if (sk_bindings::UsePromptIsHostileColour(true, m.invulnerable())) {
                            gameplayLabelColor =
                                sk::PackRGB565(sk_bindings::kUsePromptHostileR,
                                               sk_bindings::kUsePromptHostileG,
                                               sk_bindings::kUsePromptHostileB);
                        }
                    }
                }
                // M119: what this tick draws, handed to the frame loop. At the
                // original frame rate it is drawn right here, the same frame
                // the same tick always drew; above it, the loop draws it --
                // and the frames between it and the next tick -- itself.
                // Everything above this line is the tick's; everything in
                // drawGameplayFrame is only drawing.
                gameplayFrames.Push(gameCamera, frameEntities, frameBillboards);
                tickWasGameplay = true;
                gpuUnderStale = true;  // M129: a new tick, a new software frame under the HUD
                if (!pcSettings.settings().originalFrameRate()) return;
                drawGameplayFrame(gameCamera, frameEntities, frameBillboards);
                return;
            }
        }

        // M91: a *visible popup* owns the back key outright, so this
        // shortcut has to stand down while one is up. The real popups
        // carry their own SetBack() target -- mainmenu.s's End Game
        // confirmation is `myPopup.SetBack("CancelQuitGame")`,
        // savegamemenu.s's overwrite prompt is
        // `myPopup.SetBack("CancelSaveGame")` -- and lifting the player
        // straight back into the 3D view instead would both skip that
        // handler and leave the popup latched visible on the cached
        // screen, so the *next* visit came up with a confirmation dialog
        // over it and no way to answer. That is the "menus collide with
        // popups" shape exactly. The regular menu tick below already
        // routes the key to PopupMenuExecutable::GoBack().
        // M99: so does a key-configuration screen waiting for a key -- the
        // right softkey is its cancel (MenuExecutable::TickKeyCapture).
        const bool popupOwnsBackKey =
            stack.currentMenu() != nullptr && (stack.currentMenu()->activePopup() != nullptr ||
                                               stack.currentMenu()->awaitingKey());
        if (gamePausedForMenu && !popupOwnsBackKey &&
            input.ConsumeJustPressed(sk::ButtonSlot::RightSelectionKey)) {
            // See gamePausedForMenu's declaration comment -- resumes
            // gameplay directly, bypassing whatever menu screen
            // (charactermanager.s or a nested Inventory/Stats/QuestLog)
            // is currently open.
            //
            // M90: **except when the screen's own back handler has
            // something to say.** A screen opened over gameplay is not
            // always a view the player can simply be lifted out of --
            // `levelconfirm.s` is a decision, and its
            // `LevelConfirmBack -> DontGo -> Level.RestoreSaveLevel()` is
            // the only thing that puts the level name back after
            // `LoadLevel` wrote the destination over it. Skipping the
            // screen would leave the game running in azra believing it
            // was in ghstpass.
            //
            // MenuExecutable::GoBack() is the engine's own right-softkey
            // dispatch (`<ScriptName>Back`, see its comment). It answers
            // false on every screen that defines no handler, which is the
            // conversation/tutorial case this shortcut was written for,
            // and those still resume immediately.
            if (sk_bindings::MenuExecutable* paused = stack.currentMenu();
                paused && paused->portSettingsOpen()) {
                // M133: Esc on the port settings page goes back one page, to
                // Options, not all the way out into the game.
                paused->ClosePortSettings();
                RenderMenu(backbuffer, *paused, stack.player(), strings, spriteArchive);
                presentMenu(backbuffer, stack.gameActive(), menuBackdropFor(paused));
                return;
            }
            if (sk_bindings::MenuExecutable* paused = stack.currentMenu()) {
                sk_bindings::MenuExecutable* beforeBack = paused;
                bool handled = false;
                try {
                    handled = paused->GoBack();
                } catch (skRuntimeException& e) {
                    std::printf("shadowkey-port: RUNTIME ERROR in back handler: %s\n",
                                e.toString().ptr());
                }
                if (handled && stack.currentMenu() != beforeBack) {
                    // The handler put another screen up. Leave it there.
                    RenderMenu(backbuffer, *stack.currentMenu(), stack.player(), strings,
                                spriteArchive);
                    presentMenu(backbuffer, stack.gameActive(),
                                menuBackdropFor(stack.currentMenu()));
                    return;
                }
                // Anything else -- a plain Quit(), or no handler at all --
                // resumes gameplay below, which is what both were going to
                // do anyway. The close request is consumed here so the
                // block further down doesn't act on it a second time.
                stack.ClearCloseMenuRequest();
            }
            gamePausedForMenu = false;
            inGame = true;
            // M95: this shortcut closes the screen without a script Quit(),
            // so it drops the engine's menu-open flag itself -- otherwise
            // IsMenuActive() would keep answering true in the 3D view and
            // ratherb.s/pergan_asuul.s would never start their scenes.
            stack.SetMenuActive(false);
            // M119: through the Resolution setting's scene target, which at
            // the original Resolution is the Backbuffer itself.
            zoneRenderer.fogPercent = pcSettings.settings().fogPercent();  // M123
            zoneRenderer.Render(beginScene(), *gameZone, gameCamera, gameEntities, &modelArchive);
            endScene();
            // M79: the viewmodel under the HUD here too -- see the main
            // in-game draw above for FUN_10029cb0's own order.
            drawHudLayer(gameCamera, /*overlays=*/false);
            presentScene();
            return;
        }

        if (stack.creditsActive()) {
            // M127: `FUN_1006b3fc`, in its own order -- advance, draw, the
            // end test, then the two skip keys. It rolls by itself; nothing
            // scrolls it by hand.
            creditsScroll = sk::CreditsAdvance(creditsScroll, sk_bindings::kAiFrameDeltaUnits);
            RenderCredits(backbuffer, spriteArchive, stack.creditsLines(), creditsScroll);
            bool close = false;
            if (sk::CreditsEnded(creditsScroll,
                                 static_cast<int>(stack.creditsLines().size()))) {
                playPlayerSound(sk::kCreditsEndSound);  // past menu_sounds.txt: silent
                close = true;
            }
            // Key 7 held, or the right softkey held across two frames.
            const bool backHeld = input.GetButton(sk::ButtonSlot::RightSelectionKey);
            if (input.GetButton(sk::ButtonSlot::Key7) || (backHeld && creditsBackHeldPrev)) {
                playPlayerSound(sk::kCreditsSkipSound);
                close = true;
            }
            creditsBackHeldPrev = backHeld;
            if (close) {
                stack.CloseCredits();
                creditsScroll = 0;
                creditsBackHeldPrev = false;
                input.ClearPendingEdges();  // the held key is not also a press on the menu
            }
            presentMenu(backbuffer, false);
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
                if (menu->awaitingKey()) {
                    // M99: FUN_10075bdc's first branch, ahead of popups and
                    // navigation alike -- see TickKeyCapture.
                    menu->TickKeyCapture(input);
                } else if (sk_bindings::PopupMenuExecutable* popup = menu->activePopup()) {
                    // A visible confirmation popup captures input ahead of
                    // the underlying menu's own row navigation.
                    if (input.ConsumeJustPressedOrRepeat(sk::ButtonSlot::Up)) {
                        popup->MoveSelection(-1);
                    }
                    if (input.ConsumeJustPressedOrRepeat(sk::ButtonSlot::Down)) {
                        popup->MoveSelection(1);
                    }
                    // M131: the popup's tick (0x100767a0) plays the select
                    // sound on either key's press, before acting on it.
                    if (input.ConsumeJustPressed(sk::ButtonSlot::LeftSelectionKey)) {
                        playPlayerSound(kMenuSelectSound);
                        popup->ActivateSelected();
                    }
                    if (input.ConsumeJustPressed(sk::ButtonSlot::RightSelectionKey)) {
                        playPlayerSound(kMenuSelectSound);
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
                    //
                    // M131: **the menu's two sounds.** Every sound table in
                    // the game carries them in the same slots -- 99
                    // `menuNEWxbx.wav`, 100 `menu1.wav` -- and the menu's
                    // four direction handlers and its select handler play
                    // them (`FUN_1001b204(engine, slot, 100, 0, 0, 1, 1)`,
                    // long mislabelled SecretSequence_OnComplete here):
                    //
                    //   * left / right (`FUN_100322a8`, `FUN_100323f0`):
                    //     99, first thing, whatever happens next;
                    //   * up / down (`FUN_10032530`, `FUN_100326cc`): 99
                    //     when the focus goes to another widget, and always
                    //     inside a table;
                    //   * a combo box stepping (`FUN_1008eeb4` and its
                    //     twin): 100, only when the selection moved -- on
                    //     top of the 99 the key itself played;
                    //   * select (`FUN_10032868`): 100, once the enter
                    //     delay has passed and something is focused.
                    //
                    // This port's menus were silent.
                    auto verticalMove = [&](int dy) {
                        const int was = menu->selectedItem();
                        bool inTable = false;
                        if (!menu->NavigateDirectional(0, dy)) {
                            inTable = menu->TryMoveTableSelection(dy);
                            if (!inTable) menu->MoveSelection(dy);
                        }
                        if (inTable || menu->selectedItem() != was) {
                            playPlayerSound(kMenuMoveSound);
                        }
                    };
                    if (input.ConsumeJustPressedOrRepeat(sk::ButtonSlot::Up)) verticalMove(-1);
                    if (input.ConsumeJustPressedOrRepeat(sk::ButtonSlot::Down)) verticalMove(1);
                    if (input.ConsumeJustPressedOrRepeat(sk::ButtonSlot::Left)) {
                        playPlayerSound(kMenuMoveSound);
                        if (!menu->NavigateDirectional(-1, 0)) {
                            // Portrait/name-entry screens (useHoriz) walk
                            // rows; everything else adjusts the selected
                            // combo/slider, if it has one.
                            if (menu->useHoriz()) {
                                menu->MoveSelection(-1);
                            } else if (menu->CycleSelectedCombo(-1)) {
                                playPlayerSound(kMenuSelectSound);
                            }
                        }
                    }
                    if (input.ConsumeJustPressedOrRepeat(sk::ButtonSlot::Right)) {
                        playPlayerSound(kMenuMoveSound);
                        if (!menu->NavigateDirectional(1, 0)) {
                            if (menu->useHoriz()) {
                                menu->MoveSelection(1);
                            } else if (menu->CycleSelectedCombo(1)) {
                                playPlayerSound(kMenuSelectSound);
                            }
                        }
                    }
                    if (input.ConsumeJustPressed(sk::ButtonSlot::LeftSelectionKey)) {
                        // M10: Enter on a selected Table row fires its own
                        // SetCallback() handler (inventory.s's
                        // SelectedInventoryItem, statsscreen.s's
                        // OnTableSel, ...) instead of the outer row's
                        // usual ActivateSelected() path.
                        if (menu->selectedItem() >= 1 && !menu->enterDelayed(std::time(nullptr))) {
                            playPlayerSound(kMenuSelectSound);
                        }
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
                        stack.SetMenuActive(false);  // M95: already false after a Quit()
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
            // M119: a handler that just ran `ClearMenu(); Init();` (options.s's
            // Mute and language rows) has dropped the Resolution and Frame
            // Rate rows since the top of this tick -- put them back before
            // the frame is drawn, not after it.
            menu->EnsurePcSettingRows();
            RenderMenu(backbuffer, *menu, stack.player(), strings, spriteArchive);
            if (mpAddressOpen) {
                // M130: the address line, in the menu's own font and
                // standard colour, above the softkey captions.
                const std::string line = "IP: " + mpAddressEntry + "_";
                sk::BitmapFont::DrawString(backbuffer, 8, sk::Backbuffer::kHeight - 34, line,
                                           MenuColor444(0xfff));
            }
        } else {
            backbuffer.Fill(kBackgroundColor);
        }
        presentMenu(backbuffer, stack.gameActive(), menuBackdropFor(stack.currentMenu()));
    };

    // M119: the controller, read every time round the loop rather than once
    // a tick, so a quick tap between two ticks still lands -- InputState
    // latches the press until the tick consumes it, exactly as it does for
    // a key. Ignored while the window is in the background (a pad, unlike a
    // keyboard, reaches every window at once) and while the debug console
    // has the keyboard.
    auto pollGamepad = [&]() {
        sk::PadFrame pad;
        if (window.IsForeground()) pad = gamepad.Poll();
#if SK_DEBUG_SUITE
        if (debugSuite.capturingInput()) pad = sk::PadFrame{};
#endif
        padMapper.Update(pad, inGame && gameZone != nullptr, input,
                         [&](sk::ButtonSlot slot, bool held) {
                             input.SetButton(slot, held || keyHeld[static_cast<size_t>(slot)] > 0);
                         });
    };

    // M119: the Frame Rate setting's pacing. At Original the loop is what it
    // always was -- a tick when one is due, and the tick draws its own frame.
    // Above it, the gameplay frames between ticks are drawn here, from the
    // last two ticks' render lists, `alpha` of the way from one to the next.
    using FrameClock = std::chrono::steady_clock;
    FrameClock::time_point lastTickAt = FrameClock::now();
    FrameClock::time_point nextFrameAt = lastTickAt;
    sk::Camera interpCamera;
    std::vector<sk::PlacedEntity> interpEntities;
    std::vector<sk::SpriteBillboard> interpBillboards;
    auto frameDue = [&](FrameClock::time_point now, int fps) {
        if (fps <= 0) return true;  // V-Sync (paced by the compositor) and Unlimited
        const auto interval = std::chrono::duration_cast<FrameClock::duration>(
            std::chrono::duration<double>(1.0 / fps));
        if (now < nextFrameAt) return false;
        nextFrameAt += interval;
        if (nextFrameAt < now) nextFrameAt = now + interval;  // fell behind: resync
        return true;
    };

    window.RunMessageLoop([&]() {
        if (window.ShouldClose()) return;
        pollGamepad();
        // M125: the mouse is caught only while the last tick was free
        // movement -- any menu, map, conversation or popup lets it go.
        {
            bool wantMouse = gameMouseLookWanted && inGame && gameZone != nullptr;
#if SK_DEBUG_SUITE
            if (debugSuite.capturingInput()) wantMouse = false;
#endif
            window.SetMouseLook(wantMouse);
        }
        applyAspectToWindow();  // M120: only acts when Aspect Ratio or fullscreen changed
        syncGpuRenderer();      // M129: only acts when the Renderer row changed
        const int fps = pcSettings.settings().frameRate();
        const bool originalRate = fps == sk::kFrameRateOriginal;
        sk::Window::SetFineTimer(!originalRate);

        if (clock.PollTick()) {
            lastTickAt = FrameClock::now();
            tickWasGameplay = false;
            runTick();
            // Anything but gameplay breaks the chain: a menu, a load, a
            // vignette. The next gameplay tick starts it again from itself.
            if (!tickWasGameplay) gameplayFrames.Reset();
            if (originalRate) return;
        }
        if (originalRate || !tickWasGameplay || !gameplayFrames.ready() || !gameZone ||
            window.ShouldClose()) {
            return;
        }
        const FrameClock::time_point now = FrameClock::now();
        if (!frameDue(now, fps)) return;
        const float alpha = std::chrono::duration<float>(now - lastTickAt).count() /
                            std::chrono::duration<float>(sk::GameClock::kTickInterval).count();
        gameplayFrames.Build(alpha, interpCamera, interpEntities, interpBillboards);
        // M125: the mouse's share of the view is not interpolated. A frame
        // between ticks is drawn up to a tick behind the latest one, which
        // is invisible for walking and turning keys but makes a mouse feel
        // like it drags the view through syrup. So the rotation the mouse
        // put into the latest tick is applied in full (the lerp gave only
        // `alpha` of it), and the motion since -- not yet seen by any tick
        // -- is added on top. Skipped when the lerp snapped to the latest
        // tick (a turn past its limit), which already has all of it.
        if (window.MouseLookActive()) {
            const sk::PcSettings& pc = pcSettings.settings();
            const float k = pc.mouseLookRadiansPerCount();
            const float rest = 1.0f - (std::min)(alpha, 1.0f);
            int pendingDx = 0, pendingDy = 0;
            window.PeekMouseDelta(&pendingDx, &pendingDy);
            if (interpCamera.yaw != gameCamera.yaw) interpCamera.yaw += gameMouseYawLastTick * rest;
            interpCamera.yaw -= static_cast<float>(pendingDx) * k;
            if (gameMousePitchOwned) {
                float pitch = interpCamera.pitch + static_cast<float>(pendingDy) * k;
                if (interpCamera.pitch != gameCamera.pitch) pitch += gameMousePitchLastTick * rest;
                interpCamera.pitch = std::clamp(pitch, -sk::kMaxCameraPitch, sk::kMaxCameraPitch);
            }
        }
        drawGameplayFrame(interpCamera, interpEntities, interpBillboards);
        if (fps == sk::kFrameRateVSync) window.WaitForCompositor();
    });
    sk::Window::SetFineTimer(false);

    return 0;
}
