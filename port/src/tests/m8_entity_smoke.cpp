// M8 smoke test: loads the global model archive (models.idx/.huge) and
// entity type table (entities.txt), then resolves every placed entity in
// a real zone's .ent data through the same two-step chain the real
// engine uses (typeId -> entities.txt -> modelArchiveIndex ->
// models.idx/.huge) -- confirms the parsers agree with each other and
// with real game data, and dumps a couple of models' texture skins as
// PPMs for a direct visual sanity check (same technique that caught the
// M6 RGB565/RGB444 palette bug).
#include <cstdio>
#include <fstream>
#include <map>
#include <string>

#include "world/entity_types.h"
#include "world/model_archive.h"
#include "world/zone.h"

namespace {

void WriteSkinPpm(const sk::Model& model, int skinIndex, const std::string& path) {
    std::ofstream f(path, std::ios::binary);
    f << "P6\n" << model.width << " " << model.height << "\n255\n";
    for (int y = 0; y < model.height; ++y) {
        for (int x = 0; x < model.width; ++x) {
            uint16_t c = model.TexelAt(skinIndex, x, y);
            uint8_t r = static_cast<uint8_t>(((c >> 8) & 0xf) * 17);
            uint8_t g = static_cast<uint8_t>(((c >> 4) & 0xf) * 17);
            uint8_t b = static_cast<uint8_t>((c & 0xf) * 17);
            f.put(static_cast<char>(r));
            f.put(static_cast<char>(g));
            f.put(static_cast<char>(b));
        }
    }
    std::printf("  wrote %s (%dx%d)\n", path.c_str(), model.width, model.height);
}

}  // namespace

int main(int argc, char** argv) {
    const std::string scriptRoot =
        argc > 1 ? argv[1]
                  : "The-Elder-Scrolls-Travels-Shadowkey_N-Gage_EN-FR-DE-ES-IT_USA-Europe-"
                    "EnFrDeEsIt-26102004/system/apps/6r51";
    const char* zoneName = argc > 2 ? argv[2] : "azra";

    sk::EntityTypeTable types;
    if (!types.Load(scriptRoot)) {
        std::printf("m8_entity_smoke: FAILED to load entities.txt\n");
        return 1;
    }

    sk::ModelArchive models;
    if (!models.Load(scriptRoot)) {
        std::printf("m8_entity_smoke: FAILED to load models.idx/.huge\n");
        return 1;
    }

    sk::Zone zone;
    if (!zone.Load(scriptRoot, zoneName)) {
        std::printf("m8_entity_smoke: FAILED to load zone %s\n", zoneName);
        return 1;
    }

    int resolvedType = 0, resolvedModel = 0, unresolvedType = 0, nullModel = 0;
    std::map<int32_t, int> typeIdCounts;
    const sk::Model* firstRealModel = nullptr;
    int firstRealArchiveIndex = -1;
    for (const auto& e : zone.entities()) {
        ++typeIdCounts[e.typeId];
        const sk::EntityTypeDescriptor* desc = types.Lookup(e.typeId);
        if (!desc) {
            ++unresolvedType;
            continue;
        }
        ++resolvedType;
        const sk::Model* model = models.GetModel(desc->modelArchiveIndex);
        if (!model) {
            ++nullModel;
            continue;
        }
        ++resolvedModel;
        if (!firstRealModel && model->faces.size() > 4) {  // skip trivial degenerate models
            firstRealModel = model;
            firstRealArchiveIndex = desc->modelArchiveIndex;
        }
    }

    std::printf("\nzone '%s': %zu placed entities, %zu distinct typeIds\n", zoneName,
                zone.entities().size(), typeIdCounts.size());
    std::printf("resolved to a type descriptor: %d / %zu (unresolved: %d)\n", resolvedType,
                zone.entities().size(), unresolvedType);
    std::printf("resolved to a real (non-NULL.bin) model: %d, no-model (NULL.bin/empty): %d\n",
                resolvedModel, nullModel);

    if (firstRealModel) {
        std::printf(
            "\nfirst substantial model: archive index %d, %zu verts, %zu uvs, %zu faces, "
            "%d skin(s), %dx%d texture\n",
            firstRealArchiveIndex, firstRealModel->vertices.size(), firstRealModel->uvs.size(),
            firstRealModel->faces.size(), firstRealModel->skinCount, firstRealModel->width,
            firstRealModel->height);
        WriteSkinPpm(*firstRealModel, 0, "model_skin0.ppm");
    } else {
        std::printf("m8_entity_smoke: FAILED to find any substantial resolved model\n");
        return 1;
    }

    bool ok = resolvedModel > 0 && unresolvedType == 0;
    std::printf("\nm8_entity_smoke: %s\n", ok ? "OK" : "FAILED");
    return ok ? 0 : 1;
}
