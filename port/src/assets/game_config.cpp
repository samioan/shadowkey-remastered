#include "assets/game_config.h"

#include <cstdio>
#include <fstream>
#include <sstream>

namespace sk {

const char* const GameConfig::kKeyActionMap = "ACTIONMAP";
const char* const GameConfig::kKeyLanguage = "LANGUAGE";
const char* const GameConfig::kKeySoundVolume = "SOUNDVOLUME";
const char* const GameConfig::kKeyMusicVolume = "MUSICVOLUME";
const char* const GameConfig::kKeyMuteOnCall = "MUTEONCALL";
const char* const GameConfig::kFileName = "dragonstar.set";

void GameConfig::CaptureBindings(const InputState& input) {
    for (size_t i = 0; i < actionMap.size(); ++i) {
        actionMap[i] = input.binding(static_cast<Action>(i));
    }
}

void GameConfig::ApplyBindings(InputState& input) const {
    for (size_t i = 0; i < actionMap.size(); ++i) {
        input.RebindRaw(static_cast<Action>(i), actionMap[i]);
    }
}

std::string GameConfig::Serialize() const {
    // FUN_10019c04, field for field. Every key carries its own newline and
    // every value goes through "%d\n", so the file is one token per line.
    std::ostringstream out;
    out << kKeyActionMap << "\n" << kWrittenActions << "\n";
    for (int i = 0; i < kWrittenActions; ++i) out << actionMap[i] << "\n";
    out << kKeyLanguage << "\n" << language << "\n";
    out << kKeySoundVolume << "\n" << soundVolume << "\n";
    out << kKeyMusicVolume << "\n" << musicVolume << "\n";
    out << kKeyMuteOnCall << "\n" << (muteOnCall ? 1 : 0) << "\n";
    return out.str();
}

bool GameConfig::Parse(const std::string& text) {
    // FUN_100199e0: `fscanf("%s")` for a key, `strcmp` against the five
    // known ones, `fscanf("%d")` for the values. A token that matches
    // nothing is simply the next loop iteration's key candidate, which is
    // how the real loader tolerates a file it does not fully understand.
    std::istringstream in(text);
    std::string key;
    bool sawAny = false;
    while (in >> key) {
        if (key == kKeyActionMap) {
            int count = 0;
            if (!(in >> count)) break;
            for (int i = 0; i < count; ++i) {
                int slot = 0;
                if (!(in >> slot)) return sawAny;
                // The real loader passes the index straight through to the
                // binding table with no bound check; clamp instead of
                // scribbling past the array, but keep everything in range.
                if (i < static_cast<int>(actionMap.size())) actionMap[i] = slot;
            }
            sawAny = true;
        } else if (key == kKeyLanguage) {
            if (in >> language) sawAny = true;
        } else if (key == kKeySoundVolume) {
            if (in >> soundVolume) sawAny = true;
        } else if (key == kKeyMusicVolume) {
            if (in >> musicVolume) sawAny = true;
        } else if (key == kKeyMuteOnCall) {
            int v = 0;
            if (in >> v) {
                muteOnCall = v != 0;
                sawAny = true;
            }
        }
    }
    return sawAny;
}

bool GameConfig::Save(const std::string& path) const {
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    const std::string text = Serialize();
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    return static_cast<bool>(out);
}

bool GameConfig::Load(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::ostringstream buf;
    buf << in.rdbuf();
    return Parse(buf.str());
}

}  // namespace sk
