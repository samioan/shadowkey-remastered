#pragma once

// M50: the byte stream every save-file member is written through.
//
// M40 decoded the save *container* -- an archive of named blobs -- and
// left "what is inside each blob" open. This is the first half of that
// answer: the serializer itself, one small class in the binary that both
// `character.dat` and `<level>.dat` are produced by and parsed with.
//
// The class is 0x14 bytes (constructor `FUN_1008c06c`):
//
//     +0x00  (unused)
//     +0x04  vtable (0x101001d8)
//     +0x08  u8*  buffer -- a single `new[] 0x40000` block, 256 KB
//     +0x0c  u32  write cursor
//     +0x10  u32  read cursor
//
// Two cursors, one buffer, no bounds checks anywhere: a save is built by
// writing from 0, and read by memcpy'ing a blob in and reading from 0
// (`FUN_100195b0` does exactly that -- `+0xc = length; +0x10 = 0`).
// `FUN_1008bfe4` zeroes both cursors, which is how the writer measures a
// blob twice (see kDiskEstimate* below).
//
// The vtable is one big overload set: slots +0x08..+0x44 write, +0x48..
// +0x80 read, and several slots are byte-for-byte identical code at
// different indices (four separate "write one byte" thunks, four "read
// one byte") -- distinct C++ overloads that compiled to the same body.
// So the *wire* format collapses to the handful of primitives below:
//
//     slot           write            read            bytes
//     0x0c/10/14/38  u8               0x48/4c/50/74   1
//     0x20           i16              0x5c            2   (LE)
//     0x24           u16              0x60            2   (LE)
//     0x18/0x34      i32              0x54/0x70       4   (LE)
//     0x1c           u32              0x58            4   (LE)
//     0x28           u32 (memcpy)     0x64            4   (LE)
//     0x2c           8-bit string     0x68            2 + n
//     0x30/0x3c      wide string      0x6c/0x78       2 + 2n
//     0x40           SimKin value     0x7c            variable
//     0x44           i32 triple       0x80            12
//
// Two details worth keeping, both visible in the decompiled pair:
//
//   * **i32 is written as two i16 halves** (`FUN_1008beec` calls the i16
//     writer twice, low half first) while the read side assembles four
//     bytes directly. On a little-endian target those agree exactly,
//     which is why the game never noticed; reproduced here as a plain
//     little-endian i32 because that is what lands on disk.
//
//   * **A string's length prefix is an i16 and the NUL is not written.**
//     `FUN_1008bd54` writes `strlen(s)` then that many bytes; the reader
//     (`FUN_1008b980`) reads them and appends its own terminator. That is
//     the opposite of the *container's* TOC, whose name length includes
//     the NUL (see save_archive.h) -- the two layers disagree, and both
//     are reproduced as they are.
//
// Divergence from the original, deliberately: the game's class cannot
// fail. It has no length field to check a read against and no capacity
// check on a write, because on the device the only producer is the game
// itself. A port parses files it did not write, so reads past the end
// are caught and latch `failed()` instead of walking off the buffer.
// Nothing about the bytes changes.

#include <cstdint>
#include <string>
#include <vector>

namespace sk {

class SaveStream {
public:
    // The real buffer is a fixed `new[] 0x40000`; a save larger than this
    // would have run off the end of it on the device. Kept as a constant
    // so a port-side overflow is reportable rather than silent.
    static constexpr size_t kBufferCapacity = 0x40000;

    SaveStream() = default;
    explicit SaveStream(std::vector<uint8_t> bytes) : m_Bytes(std::move(bytes)) {}

    // --- write side (cursor +0x0c) ---
    void WriteU8(uint8_t v);
    void WriteI8(int8_t v) { WriteU8(static_cast<uint8_t>(v)); }
    void WriteI16(int16_t v);
    void WriteU16(uint16_t v);
    void WriteI32(int32_t v);
    void WriteU32(uint32_t v);
    // FUN_1008bd54: i16 length, then one byte per character, no NUL.
    void WriteString8(const std::string& s);
    // FUN_1008bccc: i16 length, then one i16 per character, no NUL.
    void WriteStringW(const std::u16string& s);
    void WriteStringW(const std::string& asciiSource);
    // FUN_1008bf38: three i32s, used for a position triple.
    void WriteVec3(int32_t x, int32_t y, int32_t z);

    // --- read side (cursor +0x10) ---
    uint8_t ReadU8();
    int8_t ReadI8() { return static_cast<int8_t>(ReadU8()); }
    int16_t ReadI16();
    uint16_t ReadU16();
    int32_t ReadI32();
    uint32_t ReadU32();
    std::string ReadString8();
    std::u16string ReadStringW();
    std::string ReadStringWAscii();
    void ReadVec3(int32_t& x, int32_t& y, int32_t& z);

    // FUN_1008bfe4 -- zeroes *both* cursors without touching the bytes.
    void Rewind() {
        m_WritePos = 0;
        m_ReadPos = 0;
    }
    // What FUN_100195b0 does to a blob pulled out of the archive: install
    // the bytes, set the write cursor to the length, read cursor to 0.
    void Reset(std::vector<uint8_t> bytes) {
        m_Bytes = std::move(bytes);
        m_WritePos = m_Bytes.size();
        m_ReadPos = 0;
    }

    const std::vector<uint8_t>& bytes() const { return m_Bytes; }
    size_t writePos() const { return m_WritePos; }
    size_t readPos() const { return m_ReadPos; }
    bool atEnd() const { return m_ReadPos >= m_Bytes.size(); }

    // Latched by any read that would run past the end, and by a write
    // past kBufferCapacity. The original has neither check.
    bool failed() const { return m_Failed; }

    static std::u16string Utf16FromAscii(const std::string& s);
    static std::string AsciiFromUtf16(const std::u16string& s);

private:
    bool Take(size_t n);

    std::vector<uint8_t> m_Bytes;
    size_t m_WritePos = 0;
    size_t m_ReadPos = 0;
    bool m_Failed = false;
};

}  // namespace sk
