// M131 smoke test: the engine's use-target search (FUN_1001dd40), its
// prompt (FUN_1002acd4) and the text box that draws it (FUN_100231b4).
// Needs no game data.
//
//   1. The search finds a usable entity dead ahead, out to two and a half
//      tiles, and not at three.
//   2. A wall tile between the player and the entity ends the search --
//      and an entity standing *in* the wall tile is still found.
//   3. The fan is narrow: an entity a tile to the side of the line is not
//      a target; one behind the player never is.
//   4. The prompt's text: a trailing space, "Examine:" for a lootable
//      corpse, yellow only for a creature that can be fought.
//   5. The text box's line break is by character count, quirks included.
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "graphics/text_box.h"
#include "simkin_bindings/amulet_flags.h"
#include "simkin_bindings/use_prompt.h"
#include "simkin_bindings/weapon_viewmodel.h"

namespace {

int g_Failures = 0;
int g_Checks = 0;

void Check(bool ok, const std::string& what) {
    ++g_Checks;
    std::printf("  [%s] %s\n", ok ? "ok" : "FAIL", what.c_str());
    if (!ok) ++g_Failures;
}

using namespace sk_bindings;

// A 32x32 map. Heading 0 is +y (sin 0 = 0 on x, cos 0 = 1 on y).
struct World {
    std::vector<std::pair<int, int>> walls;
    std::vector<std::pair<int, int>> usable;  // index is the id returned

    int Find(int px, int py, int heading) const {
        return FindUseTarget(
            px, py, heading, 32, 32,
            [&](int tx, int ty) {
                for (const auto& w : walls) {
                    if (w.first == tx && w.second == ty) return true;
                }
                return false;
            },
            [&](int tx, int ty) {
                for (size_t i = 0; i < usable.size(); ++i) {
                    if (usable[i].first == tx && usable[i].second == ty) {
                        return static_cast<int>(i);
                    }
                }
                return -1;
            });
    }
};

constexpr int kPx = 10 * 256 + 128, kPy = 10 * 256 + 128;  // centre of tile (10, 10)

}  // namespace

int main() {
    std::printf("m131 use prompt smoke\n");

    // ---- 1. reach
    {
        World w;
        w.usable = {{10, 11}};
        Check(w.Find(kPx, kPy, 0) == 0, "an entity one tile ahead is the target");
        w.usable = {{10, 12}};
        Check(w.Find(kPx, kPy, 0) == 0, "two tiles ahead: still the target");
        // Six samples half a tile apart: 640 units from where the player
        // stands. From a tile's centre that just touches the third tile.
        w.usable = {{10, 13}};
        Check(w.Find(kPx, kPy - 100, 0) == -1, "past 640 units: out of reach");
        w.usable = {{10, 14}};
        Check(w.Find(kPx, kPy, 0) == -1, "four tiles ahead: out of reach from anywhere");
        w.usable = {{10, 10}};
        Check(w.Find(kPx, kPy, 0) == 0, "the player's own tile is searched first");
        w.usable = {{10, 12}, {10, 11}};
        Check(w.Find(kPx, kPy, 0) == 1, "the nearer of two on the line wins");
    }

    // ---- 2. walls
    {
        World w;
        w.usable = {{10, 12}};
        w.walls = {{10, 11}};
        Check(w.Find(kPx, kPy, 0) == -1, "a wall tile in between ends the search");
        w.usable = {{10, 11}};
        Check(w.Find(kPx, kPy, 0) == 0, "an entity standing in the wall tile is found first");
    }

    // ---- 3. the fan
    {
        World w;
        w.usable = {{12, 11}};
        Check(w.Find(kPx, kPy, 0) == -1, "two tiles to the side of the line: not a target");
        w.usable = {{10, 9}};
        Check(w.Find(kPx, kPy, 0) == -1, "behind the player: not a target");
        Check(w.Find(kPx, kPy, 0x8000) == 0, "... until the player turns round");
        w.usable = {{11, 10}};
        Check(w.Find(kPx, kPy, 0x4000) == 0, "heading 0x4000 looks along +x");
    }

    // ---- the map's edge
    {
        World w;
        w.usable = {{0, 1}};
        Check(w.Find(300, 300, 0xc000) == -1, "the border tiles are never searched");
    }

    // ---- 4. the prompt
    Check(UsePromptText("Menlin", false, false, false) == "Menlin ", "a plain prompt gains a space");
    Check(UsePromptText("Goblin", true, true, false) == "Goblin ", "a live creature with loot: its name");
    Check(UsePromptText("Goblin", true, true, true) == "Examine:  Goblin",
          "a lootable corpse says Examine:");
    Check(UsePromptText("Goblin", true, false, true) == "Goblin ", "a corpse with no loot does not");
    Check(UsePromptIsHostileColour(true, false), "a creature that can be fought is yellow");
    Check(!UsePromptIsHostileColour(true, true), "an invulnerable one is white");
    Check(!UsePromptIsHostileColour(false, false), "a door is white");

    // ---- 5. the text box
    {
        using Lines = std::vector<std::string>;
        Check(sk::WrapTextBox("Open", 25) == Lines({"Open", ""}),
              "a short line, and the empty tail the end always emits");
        //                     0         1         2         3
        //                     0123456789012345678901234567890123456789
        const std::string t = "War comes to Hammerfell, danger and ambition";
        const Lines got = sk::WrapTextBox(t, 25);
        Check(got.size() >= 2 && got[0] == "War comes to Hammerfell,",
              "cut at the space before the one that passed 25: \"" + (got.empty() ? "" : got[0]) +
                  "\"");
        Check(got.size() == 3 && got[1] == "danger and ambition" && got[2].empty(),
              "then the rest, then the empty tail");
        const Lines longWord = sk::WrapTextBox("Supercalifragilisticexpialidocious yes", 25);
        Check(!longWord.empty() && longWord[0].empty(),
              "a first word past the limit yields an empty first line");
        std::string joined;
        for (const std::string& l : longWord) joined += l + "|";
        Check(joined.find("Supercalifragilisticexpialidocious") != std::string::npos &&
                  joined.find("yes") != std::string::npos,
              "... and loses no text: " + joined);
    }

    // ---- M132: tag 0x38's receiver (FUN_10037880)
    {
        KeyItemFlags flags;
        flags.ApplyRemote(kFlagRedAmulet, true);
        Check(flags.HasAmulet("redam") && !flags.HasAmulet("blueam"),
              "the other player's amulet sets this game's bit");
        flags.ApplyRemote(kFlagFrozenKey, true);
        Check(flags.test(kFlagFrozenKey) && flags.frozenKeyCount() == 1,
              "a remote frozen key sets the bit and steps the counter");
        flags.ApplyRemote(kFlagFrozenKey, false);
        Check(!flags.test(kFlagFrozenKey) && flags.frozenKeyCount() == 0,
              "... and its removal clears the bit outright and steps it back");
    }

    std::printf("%d checks, %d failure(s)\n", g_Checks, g_Failures);
    return g_Failures == 0 ? 0 : 1;
}
