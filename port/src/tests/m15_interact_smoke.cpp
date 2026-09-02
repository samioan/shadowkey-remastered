// M15 (Action::Use interact binding, doors) smoke test: proves
// DoorExecutable against real placed doors, not a synthetic fixture --
// loads the real azra zone, resolves its real typeId-54 (.ent ->
// entities.txt) placements to door.s, actually runs that script's real
// Init()/OnUse() through the vendored interpreter, and checks the
// resulting state (use-text id, open/close toggle, rotation) matches the
// script's own literal values. See docs/PORT_ROADMAP.md's "generic
// Action::Use interact binding" entry and door_executable.h's class
// comment for the real script text this checks against.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <memory>
#include <string>

#include "assets/string_table.h"
#include "graphics/backbuffer.h"
#include "render3d/camera.h"
#include "render3d/zone_renderer.h"
#include "simkin_bindings/door_executable.h"
#include "simkin_bindings/menu_stack.h"
#include "skExecutableContext.h"
#include "skInterpreter.h"
#include "skParseException.h"
#include "skRValue.h"
#include "skRValueArray.h"
#include "skRuntimeException.h"
#include "world/entity_types.h"
#include "world/model_archive.h"
#include "world/zone.h"

namespace {

// Same technique m8_render_entities_smoke.cpp already established for
// visually confirming a transform this test's numeric checks alone can't
// fully vouch for (there, entity position/scale; here, the new
// per-entity yaw rotation render3d/zone_renderer.cpp gained this
// session).
void WriteBackbufferPpm(const sk::Backbuffer& bb, const std::string& path) {
    std::ofstream f(path, std::ios::binary);
    f << "P6\n" << sk::Backbuffer::kWidth << " " << sk::Backbuffer::kHeight << "\n255\n";
    for (int y = 0; y < sk::Backbuffer::kHeight; ++y) {
        const uint16_t* row = bb.Row(y);
        for (int x = 0; x < sk::Backbuffer::kWidth; ++x) {
            uint16_t c = row[x];
            uint8_t r = static_cast<uint8_t>(((c >> 11) & 0x1f) << 3);
            uint8_t g = static_cast<uint8_t>(((c >> 5) & 0x3f) << 2);
            uint8_t b = static_cast<uint8_t>((c & 0x1f) << 3);
            f.put(static_cast<char>(r));
            f.put(static_cast<char>(g));
            f.put(static_cast<char>(b));
        }
    }
    std::printf("wrote %s\n", path.c_str());
}

}  // namespace

int main(int argc, char** argv) {
    const char* scriptRoot =
        argc > 1 ? argv[1]
                  : "The-Elder-Scrolls-Travels-Shadowkey_N-Gage_EN-FR-DE-ES-IT_USA-Europe-"
                    "EnFrDeEsIt-26102004/system/apps/6r51";
    const char* zoneName = argc > 2 ? argv[2] : "azra";

    sk::StringTable strings;
    if (!strings.Load(std::string(scriptRoot) + "/stringtable.eng")) {
        std::printf("m15_interact_smoke: FAILED to load stringtable.eng\n");
        return 1;
    }

    sk::EntityTypeTable entityTypes;
    if (!entityTypes.Load(scriptRoot)) {
        std::printf("m15_interact_smoke: FAILED to load entities.txt\n");
        return 1;
    }

    sk::Zone zone;
    if (!zone.Load(scriptRoot, zoneName)) {
        std::printf("m15_interact_smoke: FAILED to load zone %s\n", zoneName);
        return 1;
    }

    // 1. Real door placements -- azra places 7 typeId-54 (door.s)
    // instances; any one proves the resolution chain. Count them all to
    // also confirm main.cpp's zone-load filter (category==11 + name
    // ends ".s") will see exactly this many.
    int doorCount = 0;
    const sk::Zone::EntPlacement* doorPlacement = nullptr;
    for (const auto& e : zone.entities()) {
        if (e.typeId == 54) {
            ++doorCount;
            if (!doorPlacement) doorPlacement = &e;
        }
    }
    bool ok = true;
    std::printf("real typeId=54 (door.s) placements in %s: %d (expected 7)\n", zoneName, doorCount);
    if (doorCount != 7) ok = false;
    if (!doorPlacement) {
        std::printf("m15_interact_smoke: FAILED -- no typeId=54 (door.s) placement in %s\n",
                    zoneName);
        return 1;
    }

    // 2. Resolve it through entities.txt -> a real script path, same
    // category==11 + ".s"-suffix filter main.cpp's zone-load block uses.
    const sk::EntityTypeDescriptor* desc = entityTypes.Lookup(doorPlacement->typeId);
    if (!desc) {
        std::printf("m15_interact_smoke: FAILED -- typeId 54 has no entities.txt entry\n");
        return 1;
    }
    if (desc->category != 11) {
        std::printf("m15_interact_smoke: FAILED -- typeId 54 category=%d, expected 11 (door)\n",
                    desc->category);
        return 1;
    }
    std::string descName = desc->name;
    bool hasScript =
        descName.size() > 2 && descName.compare(descName.size() - 2, 2, ".s") == 0;
    if (!hasScript) {
        std::printf("m15_interact_smoke: FAILED -- typeId 54 name \"%s\" doesn't end in \".s\"\n",
                    descName.c_str());
        return 1;
    }
    std::string relPath = descName;
    std::replace(relPath.begin(), relPath.end(), '\\', '/');
    std::string fullPath = std::string(scriptRoot) + "/" + relPath;
    std::printf("resolved typeId=54 -> \"%s\" (category=%d)\n", relPath.c_str(), desc->category);

    // 3. Actually run the real script's Init() -- same pattern
    // m12_combat_smoke.cpp already established for MonsterExecutable.
    skInterpreter interpreter;
    sk_bindings::MenuStack stack(scriptRoot, interpreter, &strings);

    skExecutableContext loadCtxt(&interpreter);
    std::unique_ptr<sk_bindings::DoorExecutable> door;
    try {
        door = std::make_unique<sk_bindings::DoorExecutable>(skString(fullPath.c_str()), loadCtxt,
                                                               stack.player());
        skRValueArray args;
        args.append(skRValue(0));  // placeholder for Init's "(s)" parameter
        skRValue ret;
        skExecutableContext callCtxt(&interpreter);
        door->method(skString("Init"), args, ret, callCtxt);
    } catch (skParseException& e) {
        std::printf("m15_interact_smoke: FAILED -- PARSE ERROR loading %s: %s\n", fullPath.c_str(),
                    e.toString().ptr());
        return 1;
    } catch (skRuntimeException& e) {
        std::printf("m15_interact_smoke: FAILED -- RUNTIME ERROR loading %s: %s\n", fullPath.c_str(),
                    e.toString().ptr());
        return 1;
    }

    // 4. Real literal values from door.s's own Init() body: SetUseText(941).
    auto check = [&](const char* label, int actual, int expected) {
        bool pass = actual == expected;
        std::printf("%s: %d (expected %d) %s\n", label, actual, expected, pass ? "OK" : "FAILED");
        if (!pass) ok = false;
    };
    check("useTextId after Init (closed)", door->useTextId(), 941);
    check("yaw after Init (unopened)", static_cast<int>(door->yawRadians() * 1000.0f), 0);

    // 5. OnUse() toggles open -- SetPassable(true), AddRotationTurn(-64*256),
    // SetUseText(2989). -64*256/65536 turns == -0.25 turns == -pi/2 radians.
    door->InvokeOnUse();
    check("useTextId after 1st OnUse (opened)", door->useTextId(), 2989);
    float expectedYawOpen = -6.28318530718f / 4.0f;  // -90 degrees
    bool yawOpenOk = std::abs(door->yawRadians() - expectedYawOpen) < 0.001f;
    std::printf("yaw after 1st OnUse (opened): %f (expected %f) %s\n", door->yawRadians(),
                expectedYawOpen, yawOpenOk ? "OK" : "FAILED");
    if (!yawOpenOk) ok = false;
    bool passableOk = door->passable();
    std::printf("passable after 1st OnUse: %s (expected true) %s\n",
                passableOk ? "true" : "false", passableOk ? "OK" : "FAILED");
    if (!passableOk) ok = false;

    // 6. OnUse() again toggles closed -- SetPassable(true) then
    // SetPassable(false) (net: not passable), AddRotationTurn(64*256)
    // undoes the swing back to 0, SetUseText(941) again.
    door->InvokeOnUse();
    check("useTextId after 2nd OnUse (closed)", door->useTextId(), 941);
    bool yawClosedOk = std::abs(door->yawRadians()) < 0.001f;
    std::printf("yaw after 2nd OnUse (closed): %f (expected 0) %s\n", door->yawRadians(),
                yawClosedOk ? "OK" : "FAILED");
    if (!yawClosedOk) ok = false;
    bool passableClosedOk = !door->passable();
    std::printf("passable after 2nd OnUse: %s (expected false) %s\n",
                door->passable() ? "true" : "false", passableClosedOk ? "OK" : "FAILED");
    if (!passableClosedOk) ok = false;

    std::string useTextNow = strings.Get(door->useTextId());
    std::printf("resolved use-text: \"%s\"\n", useTextNow.c_str());

    // 7. Visual confirmation of the new per-entity yaw render path
    // (render3d/zone_renderer.cpp's SubmitModel entityYaw parameter) --
    // door is currently back in its closed state (yaw 0) from step 6;
    // render it, then reopen it (a 3rd real OnUse() call, same script,
    // just for this dump -- doesn't affect the pass/fail checks above)
    // and render again. Camera placed 300 raw units back along +X from
    // the real placement, looking at it along yaw=0 (matching
    // m8_render_entities_smoke.cpp's camera-forward convention).
    sk::ModelArchive models;
    if (models.Load(scriptRoot)) {
        sk::Camera camera;
        camera.x = static_cast<float>(doorPlacement->x) - 300.0f;
        camera.y = static_cast<float>(doorPlacement->y);
        camera.z = static_cast<float>(doorPlacement->z) + sk::kEyeHeightOffset;
        camera.yaw = 0.0f;
        camera.fovY = 1.2f;

        sk::PlacedEntity pe{static_cast<float>(doorPlacement->x), static_cast<float>(doorPlacement->y),
                             static_cast<float>(doorPlacement->z), desc->modelArchiveIndex, 0.0f};
        sk::Backbuffer backbuffer;
        sk::ZoneRenderer renderer;

        pe.yaw = door->yawRadians();  // 0, closed
        renderer.Render(backbuffer, zone, camera, {pe}, &models);
        WriteBackbufferPpm(backbuffer, "door_closed.ppm");

        door->InvokeOnUse();  // reopen, for this render only
        pe.yaw = door->yawRadians();
        renderer.Render(backbuffer, zone, camera, {pe}, &models);
        WriteBackbufferPpm(backbuffer, "door_open.ppm");
    } else {
        std::printf("m15_interact_smoke: (visual dump skipped -- models.idx/.huge not loaded)\n");
    }

    std::printf("\nm15_interact_smoke: %s\n", ok ? "OK" : "FAILED");
    return ok ? 0 : 1;
}
