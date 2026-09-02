#include "world/entity_types.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

namespace sk {

bool EntityTypeTable::Load(const std::string& scriptRoot) {
    std::ifstream f(scriptRoot + "/entities.txt");
    if (!f) {
        std::printf("EntityTypeTable: could not open %s/entities.txt\n", scriptRoot.c_str());
        return false;
    }

    byTypeId_.clear();
    std::string line;
    int lineCount = 0;
    while (std::getline(f, line)) {
        std::istringstream ss(line);
        int32_t typeId = 0, modelArchiveIndex = 0, category = 0;
        std::string name;
        if (!(ss >> typeId >> modelArchiveIndex >> category >> name)) continue;  // blank/short line
        // typeId < 7000 -- matches EntityTypeConfig_Load's own filter
        // (docs/ZONE_FORMAT.md); the real file doesn't exceed that anyway.
        if (typeId < 0 || typeId >= 7000) continue;
        EntityTypeDescriptor& d = byTypeId_[typeId];
        d.modelArchiveIndex = modelArchiveIndex;
        d.category = category;
        d.name = name;
        ++lineCount;
    }
    std::printf("EntityTypeTable: loaded %d entries\n", lineCount);
    return lineCount > 0;
}

const EntityTypeDescriptor* EntityTypeTable::Lookup(int32_t typeId) const {
    auto it = byTypeId_.find(typeId);
    return it == byTypeId_.end() ? nullptr : &it->second;
}

}  // namespace sk
