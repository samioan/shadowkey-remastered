#include "assets/save_stream.h"

namespace sk {

namespace {
// The write side stores at `buffer[writePos]` and bumps the cursor -- an
// append, since the cursor only ever moves forward within one blob.
inline void Append(std::vector<uint8_t>& out, size_t& pos, uint8_t v) {
    if (pos == out.size()) {
        out.push_back(v);
    } else {
        out[pos] = v;
    }
    ++pos;
}
}  // namespace

bool SaveStream::Take(size_t n) {
    if (m_Failed || m_ReadPos + n > m_Bytes.size()) {
        m_Failed = true;
        return false;
    }
    return true;
}

void SaveStream::WriteU8(uint8_t v) {
    if (m_WritePos >= kBufferCapacity) {
        m_Failed = true;
        return;
    }
    Append(m_Bytes, m_WritePos, v);
}

void SaveStream::WriteI16(int16_t v) {
    const uint16_t u = static_cast<uint16_t>(v);
    WriteU8(static_cast<uint8_t>(u & 0xff));
    WriteU8(static_cast<uint8_t>((u >> 8) & 0xff));
}

void SaveStream::WriteU16(uint16_t v) { WriteI16(static_cast<int16_t>(v)); }

void SaveStream::WriteI32(int32_t v) {
    // FUN_1008beec: the low i16 then the high i16 -- identical bytes to a
    // little-endian 32-bit store, which is what this is.
    const uint32_t u = static_cast<uint32_t>(v);
    WriteI16(static_cast<int16_t>(u & 0xffff));
    WriteI16(static_cast<int16_t>((u >> 16) & 0xffff));
}

void SaveStream::WriteU32(uint32_t v) { WriteI32(static_cast<int32_t>(v)); }

void SaveStream::WriteString8(const std::string& s) {
    WriteI16(static_cast<int16_t>(s.size()));
    for (char c : s) WriteU8(static_cast<uint8_t>(c));
}

void SaveStream::WriteStringW(const std::u16string& s) {
    WriteI16(static_cast<int16_t>(s.size()));
    for (char16_t c : s) WriteI16(static_cast<int16_t>(c));
}

void SaveStream::WriteStringW(const std::string& asciiSource) {
    WriteStringW(Utf16FromAscii(asciiSource));
}

void SaveStream::WriteVec3(int32_t x, int32_t y, int32_t z) {
    WriteI32(x);
    WriteI32(y);
    WriteI32(z);
}

uint8_t SaveStream::ReadU8() {
    if (!Take(1)) return 0;
    return m_Bytes[m_ReadPos++];
}

int16_t SaveStream::ReadI16() {
    if (!Take(2)) return 0;
    const uint16_t lo = m_Bytes[m_ReadPos];
    const uint16_t hi = m_Bytes[m_ReadPos + 1];
    m_ReadPos += 2;
    return static_cast<int16_t>(static_cast<uint16_t>(lo | (hi << 8)));
}

uint16_t SaveStream::ReadU16() { return static_cast<uint16_t>(ReadI16()); }

int32_t SaveStream::ReadI32() {
    if (!Take(4)) return 0;
    const uint32_t b0 = m_Bytes[m_ReadPos];
    const uint32_t b1 = m_Bytes[m_ReadPos + 1];
    const uint32_t b2 = m_Bytes[m_ReadPos + 2];
    const uint32_t b3 = m_Bytes[m_ReadPos + 3];
    m_ReadPos += 4;
    return static_cast<int32_t>(b0 | (b1 << 8) | (b2 << 16) | (b3 << 24));
}

uint32_t SaveStream::ReadU32() { return static_cast<uint32_t>(ReadI32()); }

std::string SaveStream::ReadString8() {
    const int16_t n = ReadI16();
    std::string out;
    if (n <= 0) return out;
    out.reserve(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) out.push_back(static_cast<char>(ReadU8()));
    return out;
}

std::u16string SaveStream::ReadStringW() {
    const int16_t n = ReadI16();
    std::u16string out;
    if (n <= 0) return out;
    out.reserve(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) out.push_back(static_cast<char16_t>(ReadU16()));
    return out;
}

std::string SaveStream::ReadStringWAscii() { return AsciiFromUtf16(ReadStringW()); }

void SaveStream::ReadVec3(int32_t& x, int32_t& y, int32_t& z) {
    x = ReadI32();
    y = ReadI32();
    z = ReadI32();
}

std::u16string SaveStream::Utf16FromAscii(const std::string& s) {
    std::u16string out;
    out.reserve(s.size());
    for (unsigned char c : s) out.push_back(static_cast<char16_t>(c));
    return out;
}

std::string SaveStream::AsciiFromUtf16(const std::u16string& s) {
    std::string out;
    out.reserve(s.size());
    // Everything the game puts through the wide path is a script
    // identifier or an entered character name, i.e. ASCII; anything else
    // is replaced rather than silently truncated to its low byte.
    for (char16_t c : s) out.push_back(c < 0x80 ? static_cast<char>(c) : '?');
    return out;
}

}  // namespace sk
