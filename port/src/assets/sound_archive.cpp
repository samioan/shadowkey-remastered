#include "assets/sound_archive.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

#include "audio/vorbis_decoder.h"
#include "audio/wav_file.h"

namespace sk {

namespace {

std::string Lower(const std::string& s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(),
                    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

bool HasExtension(const std::string& lowerName, const char* ext) {
    size_t extLen = std::strlen(ext);
    return lowerName.size() >= extLen && lowerName.compare(lowerName.size() - extLen, extLen, ext) == 0;
}

}  // namespace

bool SoundArchive::LoadCategory(const std::string& scriptRoot, const std::string& category) {
    m_ScriptRoot = scriptRoot;
    m_SlotToFile.clear();
    std::ifstream f(scriptRoot + "/" + category + "_sounds.txt");
    if (!f) {
        std::printf("SoundArchive: could not open %s/%s_sounds.txt\n", scriptRoot.c_str(),
                    category.c_str());
        return false;
    }
    std::string line;
    int entries = 0;
    while (std::getline(f, line)) {
        std::istringstream iss(line);
        int slot;
        std::string filename;
        if (!(iss >> slot >> filename)) continue;
        if (Lower(filename) == "null.wav" || Lower(filename) == "null.ogg") continue;
        m_SlotToFile[slot] = filename;
        ++entries;
    }
    std::printf("SoundArchive: category '%s' -- %d real sound slot(s)\n", category.c_str(), entries);
    return true;
}

bool SoundArchive::IsMusic(int slotIndex) const {
    auto it = m_SlotToFile.find(slotIndex);
    if (it == m_SlotToFile.end()) return false;
    return HasExtension(Lower(it->second), ".ogg");
}

const Sound* SoundArchive::GetSound(int slotIndex) {
    auto it = m_SlotToFile.find(slotIndex);
    if (it == m_SlotToFile.end()) return nullptr;
    std::string fullPath = m_ScriptRoot + "/" + it->second;
    std::string key = Lower(fullPath);

    auto cached = m_Cache.find(key);
    if (cached != m_Cache.end()) return cached->second.empty() ? nullptr : &cached->second;

    Sound sound;
    bool ok = HasExtension(key, ".ogg") ? DecodeOggFile(fullPath, sound) : LoadWavFile(fullPath, sound);
    auto inserted = m_Cache.emplace(key, std::move(sound)).first;
    if (!ok) {
        std::printf("SoundArchive: slot %d ('%s') failed to decode\n", slotIndex, fullPath.c_str());
        return nullptr;
    }
    return &inserted->second;
}

}  // namespace sk
