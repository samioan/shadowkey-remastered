#include "assets/product_database.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>

namespace sk {

namespace {

class Reader {
public:
    Reader(const uint8_t* data, size_t size) : m_Data(data), m_Size(size) {}

    bool ok() const { return m_Ok; }
    size_t offset() const { return m_Offset; }

    uint32_t U8() { return Read(1); }
    uint32_t U16() { return Read(2); }
    uint32_t U32() { return Read(4); }

    bool Bytes(uint8_t* out, size_t n) {
        if (m_Offset + n > m_Size) {
            m_Ok = false;
            return false;
        }
        std::memcpy(out, m_Data + m_Offset, n);
        m_Offset += n;
        return true;
    }

    bool Skip(size_t n) {
        if (m_Offset + n > m_Size) {
            m_Ok = false;
            return false;
        }
        m_Offset += n;
        return true;
    }

private:
    uint32_t Read(size_t n) {
        if (m_Offset + n > m_Size) {
            m_Ok = false;
            return 0;
        }
        uint32_t v = 0;
        for (size_t i = 0; i < n; ++i) v |= static_cast<uint32_t>(m_Data[m_Offset + i]) << (8 * i);
        m_Offset += n;
        return v;
    }

    const uint8_t* m_Data;
    size_t m_Size;
    size_t m_Offset = 0;
    bool m_Ok = true;
};

}  // namespace

bool ProductDatabase::Load(const std::string& scriptRoot) {
    m_Records.clear();
    m_Loaded = false;
    m_Version = 0;

    std::ifstream file(scriptRoot + "/products.dat", std::ios::binary);
    if (!file) {
        std::printf("ProductDatabase: no products.dat under %s\n", scriptRoot.c_str());
        return false;
    }
    std::vector<uint8_t> blob((std::istreambuf_iterator<char>(file)),
                              std::istreambuf_iterator<char>());

    Reader r(blob.data(), blob.size());
    m_Version = static_cast<int>(r.U16());
    const int count = static_cast<int>(r.U16());
    if (!r.ok()) return false;
    // FUN_10035634's own guard, message and all: anything below the record
    // stride is a file from an older build.
    if (m_Version < kProductRecordVersion) {
        std::printf("ProductDatabase: this product database file is obsolete- version #%d\n",
                    m_Version);
        return false;
    }

    m_Records.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        ProductRecord p;
        // The engine's read order, which is not the struct's offset order.
        p.templateId = static_cast<int>(r.U16());           // +0x00
        p.category = static_cast<int>(r.U16());             // +0x14
        p.price = static_cast<int>(r.U32());                // +0x04
        p.basePrice = p.price;                              // +0x08, a copy
        p.rating = static_cast<int>(r.U16());               // +0x0c
        p.descriptionStringId = static_cast<int>(r.U16());  // +0x0e
        p.armorSlot = static_cast<int>(r.U8());             // +0x12
        p.nameStringId = static_cast<int>(r.U16());         // +0x10
        const int flagCount = static_cast<int>(r.U16());
        if (!r.ok()) break;
        // The count is a real length prefix, not a constant -- it is 9 in
        // every shipped row, and the destination has room for 12 bytes.
        for (int c = 0; c < flagCount; ++c) {
            uint8_t flag = 0;
            if (!r.Bytes(&flag, 1)) break;
            if (c < kProductClassCount) p.classFlags[c] = flag;
        }
        if (!r.ok()) break;
        m_Records.push_back(p);
    }

    m_Loaded = r.ok() && static_cast<int>(m_Records.size()) == count;
    std::printf("ProductDatabase: %d products, version %d (%zu of %zu bytes consumed)\n",
                static_cast<int>(m_Records.size()), m_Version, r.offset(), blob.size());
    return m_Loaded;
}

const ProductRecord* ProductDatabase::Find(int templateId) const {
    for (const ProductRecord& p : m_Records) {
        if (p.templateId == templateId) return &p;
    }
    return nullptr;
}

void ProductDatabase::ZeroPrice(int templateId) {
    for (ProductRecord& p : m_Records) {
        if (p.templateId == templateId) {
            p.price = 0;
            return;
        }
    }
}

}  // namespace sk
