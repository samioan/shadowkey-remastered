#pragma once

// Loads entities.txt (system/apps/6r51/entities.txt), a single global
// file mapping an entity typeId to its models.idx archive index and
// coarse category enum -- docs/ZONE_FORMAT.md's "Where the type-
// descriptor tree itself comes from: entities.txt" section. Sourced once
// at startup in the real engine (EntityTypeConfig_Load, called from
// GameEngine_FirstTickBootstrap) -- not per-zone, matched here by owning
// one EntityTypeTable for the whole process rather than per-Zone.

#include <cstdint>
#include <map>
#include <string>

namespace sk {

struct EntityTypeDescriptor {
    int32_t modelArchiveIndex = -1;
    int32_t category = 0;
    std::string name;
};

class EntityTypeTable {
public:
    // scriptRoot e.g. ".../system/apps/6r51" -- reads entities.txt from it.
    bool Load(const std::string& scriptRoot);

    // Returns nullptr if typeId has no entry. Note typeId==1 has its own
    // entities.txt line but is never actually looked up here -- the real
    // engine (and this port) special-cases it as the player-start marker
    // in .ent records before any type-descriptor lookup happens
    // (docs/ZONE_FORMAT.md).
    const EntityTypeDescriptor* Lookup(int32_t typeId) const;

private:
    std::map<int32_t, EntityTypeDescriptor> byTypeId_;
};

}  // namespace sk
