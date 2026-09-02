#include "assets/gdr_font.h"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace sk {

namespace {

// ---- byte-stream cursor over an in-memory buffer --------------------

class Cursor {
public:
    explicit Cursor(const std::vector<uint8_t>& data) : data_(data) {}

    bool U8(uint8_t& out) {
        if (pos_ + 1 > data_.size()) return false;
        out = data_[pos_++];
        return true;
    }
    bool I8(int8_t& out) {
        uint8_t v;
        if (!U8(v)) return false;
        out = static_cast<int8_t>(v);
        return true;
    }
    bool U16(uint16_t& out) {
        if (pos_ + 2 > data_.size()) return false;
        out = static_cast<uint16_t>(data_[pos_] | (data_[pos_ + 1] << 8));
        pos_ += 2;
        return true;
    }
    bool U32(uint32_t& out) {
        if (pos_ + 4 > data_.size()) return false;
        out = static_cast<uint32_t>(data_[pos_]) | (static_cast<uint32_t>(data_[pos_ + 1]) << 8) |
              (static_cast<uint32_t>(data_[pos_ + 2]) << 16) |
              (static_cast<uint32_t>(data_[pos_ + 3]) << 24);
        pos_ += 4;
        return true;
    }
    bool Skip(size_t n) {
        if (pos_ + n > data_.size()) return false;
        pos_ += n;
        return true;
    }
    const uint8_t* Ptr(size_t off) const { return data_.data() + off; }
    size_t Pos() const { return pos_; }
    void SetPos(size_t p) { pos_ = p; }
    size_t Size() const { return data_.size(); }

private:
    const std::vector<uint8_t>& data_;
    size_t pos_ = 0;
};

// ---- TCardinality (Symbian's 1/2/3-byte variable-length integer) ----

bool ReadCardinality(Cursor& c, uint32_t& out) {
    uint8_t b1;
    if (!c.U8(b1)) return false;
    if ((b1 & 1) == 0) {
        out = b1 >> 1;
        return true;
    }
    if ((b1 & 2) == 0) {
        uint8_t b2;
        if (!c.U8(b2)) return false;
        out = (static_cast<uint32_t>(b1) + (static_cast<uint32_t>(b2) << 8)) >> 2;
        return true;
    }
    if ((b1 & 4) == 0) {
        uint16_t b2;
        if (!c.U16(b2)) return false;
        out = (static_cast<uint32_t>(b1) + (static_cast<uint32_t>(b2) << 8)) >> 4;
        return true;
    }
    return false;
}

// ---- SCSU-like unicode expander, ported from EKA2L1's
// src/emu/common/src/unicode.cpp (unicode_expander) -- see gdr_font.h's
// header comment for why this is needed at all: the on-disk length of a
// compressed string can only be found by actually decompressing it. ----

constexpr uint8_t kUDX = 0xF1, kUQU = 0xF0, kUD0 = 0xE8, kUC0 = 0xE0;
constexpr uint8_t kSD0 = 0x18, kSC0 = 0x10, kSCU = 0x0F, kSQU = 0x0E, kSDX = 0x0B, kSQ0 = 0x01;

constexpr uint32_t kDynWindowDefault[8] = {0x0080, 0x00C0, 0x0400, 0x0600,
                                            0x0900, 0x3040, 0x30A0, 0xFF00};
constexpr uint32_t kStaticWindows[8] = {0x0000, 0x0080, 0x0100, 0x0300,
                                         0x2000, 0x2080, 0x2100, 0x3000};
constexpr uint32_t kSpecialBases[7] = {0x00C0, 0x0250, 0x0370, 0x0530, 0x3040, 0x30A0, 0xFF60};

uint32_t DynamicWindowBase(int offsetIndex) {
    if (offsetIndex >= 0xF9 && offsetIndex <= 0xFF) return kSpecialBases[offsetIndex - 0xF9];
    if (offsetIndex >= 0x01 && offsetIndex <= 0x67) return static_cast<uint32_t>(offsetIndex) * 0x80;
    if (offsetIndex >= 0x68 && offsetIndex <= 0xA7)
        return static_cast<uint32_t>(offsetIndex) * 0x80 + 0xAC00;
    return 0;
}

class UnicodeExpander {
public:
    UnicodeExpander(const uint8_t* source, size_t avail) : source_(source), avail_(avail) {
        for (int i = 0; i < 8; ++i) dynamicWindows_[i] = kDynWindowDefault[i];
    }

    // Decodes up to `destUnits` UTF-16 code units into `out` (append),
    // stopping early on any malformed/truncated input. Returns the
    // number of *source* bytes consumed -- this, not destUnits, is the
    // real on-disk field size (see header comment).
    size_t Expand(size_t destUnits, std::u16string& out) {
        while (out.size() < destUnits) {
            uint8_t b;
            if (!ReadByte(b)) break;
            bool ok = unicodeMode_ ? HandleUByte(b, out) : HandleSByte(b, out);
            if (!ok) break;
        }
        return pos_;
    }

private:
    bool ReadByte(uint8_t& out) {
        if (avail_ <= 0) return false;
        out = source_[pos_++];
        --avail_;
        return true;
    }

    static bool CanPassthrough(uint8_t sbyte) {
        return sbyte == 0 || sbyte == 0x09 || sbyte == 0x0A || sbyte == 0x0D ||
               (sbyte >= 0x20 && sbyte <= 0x7F);
    }

    void WriteUnit(uint16_t c, std::u16string& out) { out.push_back(static_cast<char16_t>(c)); }

    void WriteChar32(uint32_t c, std::u16string& out) {
        if (c <= 0xFFFF) {
            WriteUnit(static_cast<uint16_t>(c), out);
        } else if (c <= 0x10FFFF) {
            c -= 0x10000;
            WriteUnit(static_cast<uint16_t>(0xD800 + (c >> 10)), out);
            WriteUnit(static_cast<uint16_t>(0xDC00 + (c & 0x3FF)), out);
        }
    }

    bool DefineWindow(int index) {
        uint8_t win;
        if (!ReadByte(win)) return false;
        unicodeMode_ = false;
        uint32_t base = DynamicWindowBase(win);
        activeWindowBase_ = base;
        dynamicWindows_[index] = base;
        return true;
    }

    bool DefineExpansionWindow() {
        uint8_t hi, lo;
        if (!ReadByte(hi) || !ReadByte(lo)) return false;
        unicodeMode_ = false;
        uint32_t base = 0x1000 + (0x80 * ((static_cast<uint32_t>(hi) & 0x1F) * 0x100 + lo));
        activeWindowBase_ = base;
        dynamicWindows_[hi >> 5] = base;
        return true;
    }

    bool QuoteUnicode(std::u16string& out) {
        uint8_t hi, lo;
        if (!ReadByte(hi) || !ReadByte(lo)) return false;
        WriteUnit(static_cast<uint16_t>((hi << 8) | lo), out);
        return true;
    }

    bool HandleUByte(uint8_t ubyte, std::u16string& out) {
        if (ubyte <= 0xDF || ubyte >= 0xF3) {
            uint8_t low;
            if (!ReadByte(low)) return false;
            WriteUnit(static_cast<uint16_t>((ubyte << 8) | low), out);
            return true;
        }
        if (ubyte == kUQU) return QuoteUnicode(out);
        if (ubyte >= kUC0 && ubyte <= kUC0 + 7) {
            activeWindowBase_ = dynamicWindows_[ubyte - kUC0];
            unicodeMode_ = false;
            return true;
        }
        if (ubyte >= kUD0 && ubyte <= kUD0 + 7) return DefineWindow(ubyte - kUD0);
        if (ubyte == kUDX) return DefineExpansionWindow();
        return false;
    }

    bool HandleSByte(uint8_t sbyte, std::u16string& out) {
        if (CanPassthrough(sbyte)) {
            WriteUnit(sbyte, out);
            return true;
        }
        if (sbyte >= 0x80) {
            WriteChar32(activeWindowBase_ + sbyte - 0x80, out);
            return true;
        }
        if (sbyte == kSQU) return QuoteUnicode(out);
        if (sbyte == kSCU) {
            unicodeMode_ = true;
            return true;
        }
        if (sbyte >= kSQ0 && sbyte <= kSQ0 + 7) {
            int window = sbyte - kSQ0;
            uint8_t b;
            if (!ReadByte(b)) return false;
            uint32_t c = b;
            if (c <= 0x7F)
                c += kStaticWindows[window];
            else
                c += dynamicWindows_[window] - 0x80;
            WriteChar32(c, out);
            return true;
        }
        if (sbyte >= kSC0 && sbyte <= kSC0 + 7) {
            activeWindowBase_ = dynamicWindows_[sbyte - kSC0];
            return true;
        }
        if (sbyte >= kSD0 && sbyte <= kSD0 + 7) return DefineWindow(sbyte - kSD0);
        if (sbyte == kSDX) return DefineExpansionWindow();
        return false;
    }

    const uint8_t* source_;
    size_t avail_;
    size_t pos_ = 0;
    uint32_t dynamicWindows_[8];
    uint32_t activeWindowBase_ = 0x0080;
    bool unicodeMode_ = false;
};

// Reads a cardinality-prefixed, SCSU-compressed UTF-16 string, leaving
// the cursor positioned exactly after it. `text` receives the decoded
// content (unused by callers that only need structural skipping, but
// decoded anyway -- it's what lets typefaces be found by name).
bool ReadDesStringUnicode(Cursor& c, std::u16string& text) {
    uint32_t car;
    if (!ReadCardinality(c, car)) return false;
    uint32_t lenUnits = car >> 1;
    uint32_t lenBytes = lenUnits * 2;
    // Generous upper-bound buffer to decompress from -- see header
    // comment; the real consumed size comes back from Expand(), not
    // this formula (this mirrors EKA2L1's read_des_string exactly,
    // bug-for-bug, since real .gdr files were produced against that
    // same contract).
    size_t guessSize = (static_cast<size_t>(lenBytes) + 5) * 2;
    size_t avail = (std::min)(guessSize, c.Size() - c.Pos());
    UnicodeExpander expander(c.Ptr(c.Pos()), avail);
    size_t consumed = expander.Expand(lenUnits, text);
    return c.Skip(consumed);
}

// ---- GDR structural format ----

constexpr uint32_t kGdrStoreWriteOnceLayoutUid = 268435511;
constexpr uint32_t kGdrFontStoreFileUid = 268435513;
constexpr uint32_t kGdrFontStoreFileChecksum = 0x47393853;

struct CharacterMetric {
    int8_t ascent = 0, height = 0, leftAdj = 0, move = 0;
    uint8_t rightAdjust = 0;
};

struct CodeSectionHeader {
    uint16_t start = 0, end = 0;
    uint32_t characterOffset = 0, characterBitmapOffset = 0;
};

struct FontBitmapHeader {
    uint32_t uid = 0;
    uint8_t posture = 0, strokeWeight = 0, isProportional = 0, cellHeight = 0, ascent = 0;
    uint8_t maxCharWidth = 0, maxNormalCharWidth = 0;
    uint32_t bitmapEncoding = 0, metricOffset = 0, metricCount = 0, codeSectionCount = 0;
    std::vector<CodeSectionHeader> sections;
};

struct TypefaceBitmapRef {
    uint32_t uid = 0;
    uint8_t widthFactor = 0, heightFactor = 0;
};

struct TypefaceHeader {
    std::u16string name;
    uint8_t flags = 0;
    std::vector<TypefaceBitmapRef> bitmaps;
};

bool ParseHeader(Cursor& c) {
    uint32_t layoutUid, fileUid, nullUid, checksum, idOff, fntVer, collUid, pxRatio, idOff2;
    if (!(c.U32(layoutUid) && c.U32(fileUid) && c.U32(nullUid) && c.U32(checksum) && c.U32(idOff) &&
          c.U32(fntVer) && c.U32(collUid) && c.U32(pxRatio) && c.U32(idOff2)))
        return false;
    if (layoutUid != kGdrStoreWriteOnceLayoutUid || fileUid != kGdrFontStoreFileUid ||
        checksum != kGdrFontStoreFileChecksum)
        return false;

    uint32_t copyrightCount;
    if (!c.U32(copyrightCount)) return false;
    for (uint32_t i = 0; i < copyrightCount; ++i) {
        std::u16string ignored;
        if (!ReadDesStringUnicode(c, ignored)) return false;
    }
    return true;
}

bool ParseFontBitmapHeader(Cursor& c, FontBitmapHeader& fh) {
    if (!c.U32(fh.uid)) return false;
    if (!c.U8(fh.posture) || !c.U8(fh.strokeWeight) || !c.U8(fh.isProportional) ||
        !c.U8(fh.cellHeight) || !c.U8(fh.ascent) || !c.U8(fh.maxCharWidth) ||
        !c.U8(fh.maxNormalCharWidth))
        return false;
    if (!c.U32(fh.bitmapEncoding) || !c.U32(fh.metricOffset) || !c.U32(fh.metricCount) ||
        !c.U32(fh.codeSectionCount))
        return false;

    fh.sections.resize(fh.codeSectionCount);
    for (auto& sec : fh.sections) {
        if (!c.U16(sec.start) || !c.U16(sec.end) || !c.U32(sec.characterOffset) ||
            !c.U32(sec.characterBitmapOffset))
            return false;
    }
    return true;
}

bool ParseFontBitmapHeaders(Cursor& c, std::vector<FontBitmapHeader>& out) {
    uint32_t count;
    if (!c.U32(count)) return false;
    out.resize(count);
    for (auto& fh : out) {
        if (!ParseFontBitmapHeader(c, fh)) return false;
    }
    return true;
}

bool ParseTypefaceHeader(Cursor& c, TypefaceHeader& th) {
    if (!ReadDesStringUnicode(c, th.name)) return false;
    if (!c.U8(th.flags)) return false;
    uint32_t n;
    if (!c.U32(n)) return false;
    th.bitmaps.resize(n);
    for (auto& b : th.bitmaps) {
        if (!c.U32(b.uid) || !c.U8(b.widthFactor) || !c.U8(b.heightFactor)) return false;
    }
    return true;
}

bool ParseTypefaceHeaders(Cursor& c, std::vector<TypefaceHeader>& out) {
    uint32_t count;
    if (!c.U32(count)) return false;
    out.resize(count);
    for (auto& th : out) {
        if (!ParseTypefaceHeader(c, th)) return false;
    }
    return true;
}

bool ParseMetrics(Cursor& c, std::vector<CharacterMetric>& out) {
    uint32_t count;
    if (!c.U32(count)) return false;
    out.resize(count);
    for (auto& m : out) {
        if (!c.I8(m.ascent) || !c.I8(m.height) || !c.I8(m.leftAdj) || !c.I8(m.move) ||
            !c.U8(m.rightAdjust))
            return false;
    }
    return true;
}

int GetBit(const uint8_t* byteStart, size_t offset) {
    return (byteStart[offset >> 3] >> (offset & 7)) & 1;
}

// Decodes every character in one code_section into `outGlyphs`, keyed
// by absolute codepoint. Bit-unpacking logic ported verbatim from
// EKA2L1's parse_font_code_section_comps (see gdr_font.h header
// comment) -- each character's data starts with a 7- or 15-bit index
// into `metrics` (the leading bit picks which), then a repeat-flag +
// 4-bit run-count-prefixed sequence of 1bpp rows.
bool ParseCodeSection(Cursor& c, const std::vector<CharacterMetric>& metrics,
                      const CodeSectionHeader& header, std::map<char32_t, GdrGlyph>& outGlyphs) {
    uint32_t numOffset;
    if (!c.U32(numOffset)) return false;
    std::vector<uint16_t> offsets(numOffset);
    for (auto& o : offsets) {
        if (!c.U16(o)) return false;
    }

    uint32_t lenByteList;
    if (!c.U32(lenByteList)) return false;
    if (c.Pos() + lenByteList > c.Size()) return false;
    const uint8_t* byteList = c.Ptr(c.Pos());
    if (!c.Skip(lenByteList)) return false;

    for (uint32_t code = header.start; code <= header.end; ++code) {
        uint16_t off = offsets[code - header.start];
        if (off >= 0x7FFF) continue;  // filler (blank) glyph -- nothing to draw

        size_t bitoffset = static_cast<size_t>(off) * 8;
        int totalBits = (GetBit(byteList, bitoffset) == 0) ? 7 : 15;
        ++bitoffset;

        uint32_t metricIndex = 0;
        for (int idx = 0; idx < totalBits; ++idx) {
            metricIndex |= static_cast<uint32_t>(GetBit(byteList, bitoffset)) << idx;
            ++bitoffset;
        }
        if (metricIndex >= metrics.size()) return false;
        const CharacterMetric& metric = metrics[metricIndex];

        int targetHeight = metric.height;
        int rightAdjust = (metric.rightAdjust == 0xFF) ? 0 : metric.rightAdjust;
        int contentWidth = metric.move - metric.leftAdj - rightAdjust;
        if (targetHeight <= 0 || contentWidth <= 0) continue;

        GdrGlyph glyph;
        glyph.width = contentWidth;
        glyph.height = targetHeight;
        glyph.leftBearing = metric.leftAdj;
        glyph.advance = metric.move;
        glyph.ascentAboveBaseline = metric.ascent;
        glyph.bits.assign(static_cast<size_t>(contentWidth) * targetHeight, 0);

        int heightRead = 0;
        while (heightRead < targetHeight) {
            bool repeatLine = GetBit(byteList, bitoffset) == 0;
            ++bitoffset;
            int lineCount = 0;
            for (int i = 0; i < 4; ++i) {
                lineCount |= GetBit(byteList, bitoffset) << i;
                ++bitoffset;
            }
            if (lineCount <= 0) break;  // malformed -- avoid an infinite loop

            if (repeatLine) {
                for (int x = 0; x < contentWidth; ++x) {
                    int theBit = GetBit(byteList, bitoffset);
                    ++bitoffset;
                    for (int k = 0; k < lineCount; ++k) {
                        size_t idx = static_cast<size_t>(heightRead + k) * contentWidth + x;
                        if (idx < glyph.bits.size()) glyph.bits[idx] = static_cast<uint8_t>(theBit);
                    }
                }
            } else {
                for (int y = 0; y < lineCount; ++y) {
                    for (int x = 0; x < contentWidth; ++x) {
                        int theBit = GetBit(byteList, bitoffset);
                        ++bitoffset;
                        size_t idx = static_cast<size_t>(heightRead + y) * contentWidth + x;
                        if (idx < glyph.bits.size()) glyph.bits[idx] = static_cast<uint8_t>(theBit);
                    }
                }
            }
            heightRead += lineCount;
        }

        outGlyphs[static_cast<char32_t>(code)] = std::move(glyph);
    }
    return true;
}

bool ReadWholeFile(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

}  // namespace

bool GdrFont::Load(const std::string& path, const std::string& typefaceName) {
    std::vector<uint8_t> file;
    if (!ReadWholeFile(path, file)) {
        std::printf("GdrFont: could not open %s\n", path.c_str());
        return false;
    }

    Cursor c(file);
    if (!ParseHeader(c)) {
        std::printf("GdrFont: %s does not look like a Symbian .gdr font store\n", path.c_str());
        return false;
    }

    std::vector<FontBitmapHeader> fontBitmapHeaders;
    if (!ParseFontBitmapHeaders(c, fontBitmapHeaders)) return false;

    std::vector<TypefaceHeader> typefaceHeaders;
    if (!ParseTypefaceHeaders(c, typefaceHeaders)) return false;

    // Parse every font_bitmap's body (metrics + glyph data) in file
    // order -- required regardless of which one we keep, since bodies
    // are stored back-to-back with no random-access index.
    std::map<uint32_t, std::map<char32_t, GdrGlyph>> glyphsByUid;
    std::map<uint32_t, const FontBitmapHeader*> headerByUid;

    for (auto& fh : fontBitmapHeaders) {
        std::vector<CharacterMetric> metrics;
        if (!ParseMetrics(c, metrics)) return false;

        std::map<char32_t, GdrGlyph> glyphs;
        for (auto& section : fh.sections) {
            if (!ParseCodeSection(c, metrics, section, glyphs)) return false;
        }
        headerByUid[fh.uid] = &fh;
        glyphsByUid[fh.uid] = std::move(glyphs);
    }

    fileSize_ = file.size();
    bytesConsumed_ = c.Pos();

    // Find the requested typeface by name, matching UTF-16 names
    // against the (ASCII) requested name one code unit at a time --
    // typeface names in these fonts are always plain ASCII in practice
    // ("LatinPlain12" etc.), so no real Unicode comparison is needed.
    const TypefaceHeader* found = nullptr;
    for (auto& th : typefaceHeaders) {
        if (th.name.size() != typefaceName.size()) continue;
        bool match = true;
        for (size_t i = 0; i < th.name.size(); ++i) {
            if (static_cast<char32_t>(th.name[i]) != static_cast<unsigned char>(typefaceName[i])) {
                match = false;
                break;
            }
        }
        if (match) {
            found = &th;
            break;
        }
    }
    if (!found || found->bitmaps.empty()) {
        std::printf("GdrFont: %s has no typeface named '%s'\n", path.c_str(), typefaceName.c_str());
        return false;
    }

    uint32_t uid = found->bitmaps[0].uid;
    auto headerIt = headerByUid.find(uid);
    auto glyphIt = glyphsByUid.find(uid);
    if (headerIt == headerByUid.end() || glyphIt == glyphsByUid.end()) return false;

    cellHeight_ = headerIt->second->cellHeight;
    ascent_ = headerIt->second->ascent;
    glyphs_ = std::move(glyphIt->second);
    loaded_ = true;

    std::printf("GdrFont: loaded '%s' from %s (%zu glyphs, cell height %d)\n", typefaceName.c_str(),
                path.c_str(), glyphs_.size(), cellHeight_);
    return true;
}

const GdrGlyph* GdrFont::GetGlyph(char32_t code) const {
    if (!loaded_) return nullptr;
    auto it = glyphs_.find(code);
    return it == glyphs_.end() ? nullptr : &it->second;
}

}  // namespace sk
