#include "world/model_collision.h"

#include <cstdio>
#include <fstream>
#include <sstream>

namespace sk {

bool ModelCollisionTable::Load(const std::string& scriptRoot, const std::string& zoneName) {
    m_Slots.fill(ModelCollision{});
    std::ifstream in(scriptRoot + "/" + zoneName + "_models.txt");
    if (!in) {
        std::printf("shadowkey-port: no %s_models.txt -- no entity collision this zone\n",
                    zoneName.c_str());
        return false;
    }
    // `"%d %d %d %d %s"`. The real loader gates only the *model* load on
    // the name being something other than "NULL.bin"; the three collision
    // fields are written for every in-range row regardless, so an unused
    // slot ends up 0/0/0 either way.
    std::string line;
    int rows = 0;
    while (std::getline(in, line)) {
        std::istringstream row(line);
        int index = -1, solid = 0, halfX = 0, halfY = 0;
        if (!(row >> index >> solid >> halfX >> halfY)) continue;
        if (index < 0 || index >= kSlotCount) continue;
        m_Slots[static_cast<size_t>(index)] = ModelCollision{solid, halfX, halfY};
        ++rows;
    }
    std::printf("shadowkey-port: %s_models.txt -- %d rows, %d solid\n", zoneName.c_str(), rows,
                solidCount());
    return true;
}

const ModelCollision& ModelCollisionTable::At(int modelArchiveIndex) const {
    if (modelArchiveIndex < 0 || modelArchiveIndex >= kSlotCount) return m_Empty;
    return m_Slots[static_cast<size_t>(modelArchiveIndex)];
}

int ModelCollisionTable::solidCount() const {
    int n = 0;
    for (const ModelCollision& c : m_Slots) {
        if (c.blocks()) ++n;
    }
    return n;
}

}  // namespace sk
