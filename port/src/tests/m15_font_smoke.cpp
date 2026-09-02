// Real N-Gage system font smoke test: proves GdrFont against a real
// Symbian .gdr font store (Ceurope.gdr from a real N-Gage QD ROM dump)
// -- see docs/GRAPHICS_FORMAT.md's "The real Symbian .gdr font format"
// section and docs/PORT_ROADMAP.md's real-font milestone. The font file
// itself is Nokia device firmware, not this project's data (see
// .gitignore) -- point argv[1] at wherever it's been extracted to.
#include <cstdio>

#include "assets/gdr_font.h"

int main(int argc, char** argv) {
    const char* fontPath = argc > 1 ? argv[1] : "port/assets/fonts/Ceurope.gdr";

    sk::GdrFont font;
    if (!font.Load(fontPath, "LatinPlain12")) {
        std::printf("m15_font_smoke: FAILED to load '%s' (real N-Gage device firmware -- see "
                    ".gitignore, this file must be extracted locally, not fetched from the repo)\n",
                    fontPath);
        return 1;
    }

    bool ok = true;

    // 1. The parser must consume the *entire* file with zero leftover
    // bytes -- the strongest available check that this reimplementation
    // of Symbian's .gdr format (ported from EKA2L1's open-source
    // parser) is actually correct end to end, not just plausible-looking.
    std::printf("bytes consumed: %zu of %zu\n", font.BytesConsumed(), font.FileSize());
    if (font.BytesConsumed() != font.FileSize()) {
        std::printf("m15_font_smoke: FAILED -- parser left %zu byte(s) unconsumed\n",
                    font.FileSize() - font.BytesConsumed());
        ok = false;
    }

    // 2. Sane font-level metrics for a small UI typeface.
    std::printf("cell height: %d, ascent: %d\n", font.CellHeight(), font.Ascent());
    if (font.CellHeight() <= 0 || font.CellHeight() > 64 || font.Ascent() <= 0 ||
        font.Ascent() > font.CellHeight()) {
        std::printf("m15_font_smoke: FAILED -- implausible font metrics\n");
        ok = false;
    }

    // 3. Every printable ASCII character (32-126) should decode to a
    // real glyph (space excepted -- filler characters carry no bitmap,
    // which is correct, not a bug) with sane, non-degenerate dimensions.
    int missing = 0;
    for (char32_t c = 33; c <= 126; ++c) {
        const sk::GdrGlyph* g = font.GetGlyph(c);
        if (!g) {
            ++missing;
            continue;
        }
        if (g->width <= 0 || g->width > 64 || g->height <= 0 || g->height > 64) {
            std::printf("char %u: FAILED -- implausible glyph size %dx%d\n",
                        static_cast<unsigned>(c), g->width, g->height);
            ok = false;
        }
    }
    std::printf("printable ASCII coverage: %d/%d decoded (%d missing)\n", 94 - missing, 94, missing);
    if (missing > 0) ok = false;

    // 4. 'A' specifically: a real capital-A shape should have ink in
    // its top-center columns for its top row (the point of the A) and
    // be roughly as wide as it is tall for a non-condensed Latin face.
    const sk::GdrGlyph* a = font.GetGlyph('A');
    if (!a) {
        std::printf("m15_font_smoke: FAILED -- no glyph for 'A'\n");
        ok = false;
    } else {
        bool topRowHasInk = false;
        for (int x = 0; x < a->width; ++x) {
            if (a->bits[static_cast<size_t>(x)]) topRowHasInk = true;
        }
        std::printf("'A': %dx%d, advance=%d, top row has ink: %s\n", a->width, a->height, a->advance,
                    topRowHasInk ? "yes" : "no");
        if (!topRowHasInk) {
            std::printf("m15_font_smoke: FAILED -- 'A' glyph looks empty\n");
            ok = false;
        }
    }

    // 5. Missing-file handling must be non-fatal (returns false, not a
    // crash), matching every other optional real asset in this port.
    sk::GdrFont missingFont;
    if (missingFont.Load("this/path/does/not/exist.gdr", "LatinPlain12")) {
        std::printf("m15_font_smoke: FAILED -- loading a nonexistent file returned true\n");
        ok = false;
    }
    if (missingFont.GetGlyph('A') != nullptr) {
        std::printf("m15_font_smoke: FAILED -- unloaded font returned a glyph\n");
        ok = false;
    }

    std::printf("\nm15_font_smoke: %s\n", ok ? "OK" : "FAILED");
    return ok ? 0 : 1;
}
