// M29 smoke test: the model resource's animation frames and its clip
// table, against the real models.idx/models.huge archive.
//
// docs/MODEL_FORMAT.md previously listed the resource trailer's purpose as
// an open follow-up ("clearly real structure, not slack/padding" but with
// no known consumer). This asserts what it actually is: a whole number of
// 6-byte `(startFrame, endFrame, rate)` records that exactly partition
// [0, frameCount) -- which is what makes it usable as the table a monster
// script's SetIdleAnimation/SetWalkAnimation/SetSwingAnimation/
// SetDeathAnimation numbers index into.
#include <cstdio>
#include <string>

#include "world/model_archive.h"

namespace {

int g_failures = 0;

void Check(bool ok, const std::string& what) {
    std::printf("%-74s %s\n", what.c_str(), ok ? "OK" : "FAILED");
    if (!ok) ++g_failures;
}

}  // namespace

int main() {
    const char* scriptRoot =
        "The-Elder-Scrolls-Travels-Shadowkey_N-Gage_EN-FR-DE-ES-IT_USA-Europe-"
        "EnFrDeEsIt-26102004/system/apps/6r51";

    sk::ModelArchive archive;
    if (!archive.Load(scriptRoot)) {
        std::printf("m29_animation_smoke: FAILED to load models.idx/models.huge\n");
        return 1;
    }

    int parsed = 0, animated = 0, contiguous = 0, framesConsistent = 0;
    int maxClips = 0, maxFrames = 0;
    for (int i = 0; i < archive.entryCount(); ++i) {
        const sk::Model* m = archive.GetModel(i);
        if (!m) continue;
        ++parsed;

        // Every model must expose exactly frameCount * vertsPerFrame
        // vertices, so VertexAt() can never read out of its frame.
        if (m->vertices.size() ==
            static_cast<size_t>(m->frameCount) * static_cast<size_t>(m->vertsPerFrame)) {
            ++framesConsistent;
        }

        if (m->frameCount > 1) {
            ++animated;
            if (m->frameCount > maxFrames) maxFrames = m->frameCount;
            if (static_cast<int>(m->clips.size()) > maxClips) {
                maxClips = static_cast<int>(m->clips.size());
            }
            // The decoded clip table must partition the whole frame range.
            bool ok = !m->clips.empty() && m->clips.front().startFrame == 0 &&
                       m->clips.back().endFrame == m->frameCount;
            for (size_t c = 1; c < m->clips.size() && ok; ++c) {
                if (m->clips[c].startFrame != m->clips[c - 1].endFrame) ok = false;
                if (m->clips[c].frameCount() <= 0) ok = false;
            }
            if (ok) ++contiguous;
        }
    }

    std::printf("parsed %d model(s); %d animated (max %d frames, max %d clips)\n", parsed, animated,
                maxFrames, maxClips);

    Check(parsed > 200, "the real archive parses (200+ non-empty model resources)");
    Check(parsed == framesConsistent,
          "every model exposes exactly frameCount * vertsPerFrame vertices");
    Check(animated > 0, "the real archive contains multi-frame animated models");
    Check(animated == contiguous,
          "every animated model's clip table exactly partitions [0, frameCount)");
    // Real monster scripts index clips up to 8, so the models they use
    // must carry at least 9.
    Check(maxClips >= 9,
          "at least one real model carries 9+ clips (real scripts name clip 8)");

    // Frame selection must actually move the geometry: pick the first
    // animated model and confirm two different frames disagree somewhere.
    bool framesDiffer = false;
    for (int i = 0; i < archive.entryCount() && !framesDiffer; ++i) {
        const sk::Model* m = archive.GetModel(i);
        if (!m || m->frameCount < 2 || m->vertsPerFrame <= 0) continue;
        for (int v = 0; v < m->vertsPerFrame; ++v) {
            const sk::ModelVertex& a = m->VertexAt(0, v);
            const sk::ModelVertex& b = m->VertexAt(m->frameCount - 1, v);
            if (a.x != b.x || a.y != b.y || a.z != b.z) {
                framesDiffer = true;
                break;
            }
        }
    }
    Check(framesDiffer, "an animated model's first and last frames hold different vertex data");

    // Out-of-range access must clamp rather than read past the buffer.
    for (int i = 0; i < archive.entryCount(); ++i) {
        const sk::Model* m = archive.GetModel(i);
        if (!m || m->vertsPerFrame <= 0) continue;
        const sk::ModelVertex& clamped = m->VertexAt(m->frameCount + 500, 0);
        const sk::ModelVertex& last = m->VertexAt(m->frameCount - 1, 0);
        Check(clamped.x == last.x && clamped.y == last.y && clamped.z == last.z,
              "VertexAt() clamps an out-of-range frame to the last one");
        Check(m->clip(9999) == nullptr, "clip() returns null for an out-of-range clip index");
        break;
    }

    std::printf("\nm29_animation_smoke: %s (%d failure(s))\n", g_failures == 0 ? "OK" : "FAILED",
                g_failures);
    return g_failures == 0 ? 0 : 1;
}
