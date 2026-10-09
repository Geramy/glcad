#include "exchange/dwg.hpp"

#include <algorithm>
#include <cstring>

namespace gdwg {

namespace {

u32 le32(const u8* p) {
    return static_cast<u32>(p[0]) | (static_cast<u32>(p[1]) << 8) |
           (static_cast<u32>(p[2]) << 16) | (static_cast<u32>(p[3]) << 24);
}
u64 le64(const u8* p) { return static_cast<u64>(le32(p)) | (static_cast<u64>(le32(p + 4)) << 32); }
i32 le32s(const u8* p) { return static_cast<i32>(le32(p)); }

// 0x6C bytes at 0x80, each XORed with the high byte of an LCG seeded 1.
void decrypt_header(u8* h, std::size_t n) {
    u32 seed = 1;
    for (std::size_t i = 0; i < n; ++i) {
        seed = seed * 0x343FD + 0x269EC3;
        h[i] ^= static_cast<u8>(seed >> 16);
    }
}

// 0x14-byte header in the clear, then compressed data.
struct SystemPage {
    u32 type{0}, decompressed{0}, compressed{0}, compression{0}, checksum{0};
};

constexpr u32 kPageMapType = 0x41630E3B;
constexpr u32 kSectionMapType = 0x4163003B;
constexpr u32 kDataPageType = 0x4163043B;
constexpr u32 kSectionMask = 0x4164536B;

struct PageEntry {
    i32 number{0};
    u32 size{0};
    u64 address{0};
};

struct SectionPage {
    i32 number{0};
    u32 data_size{0};
    u64 start{0};
};

struct SectionDesc {
    u64 size{0};
    u32 page_count{0};
    u32 max_decompressed{0};
    u32 compressed{1};
    u32 id{0};
    u32 encrypted{0};
    std::string name;
    std::vector<SectionPage> pages;
};

}

// LZ77: 0x11 ends; <0x10 literal; 0x12-0x1F far; 0x20-0x3F near; >=0x40 short.
std::vector<u8> decompress_r2004(const u8* src, std::size_t src_size, std::size_t dst_size,
                                 std::string* error, std::size_t* produced, std::size_t* consumed) {
    std::vector<u8> out;
    out.reserve(dst_size);
    std::size_t at = 0;
    const auto rc = [&]() -> u8 { return at < src_size ? src[at++] : 0; };
    const auto more = [&]() { return at < src_size; };

    const auto literal_length = [&](u8 opcode) -> std::size_t {
        std::size_t low = opcode & 0x0F;
        if (low == 0) {
            u8 last = 0;
            while ((last = rc()) == 0 && more()) low += 0xFF;
            low += 0x0F + last;
        }
        return low + 3;
    };
    const auto compressed_bytes = [&](u8 opcode, unsigned bits) -> std::size_t {
        std::size_t n = opcode & bits;
        if (n == 0) {
            u8 last = 0;
            while ((last = rc()) == 0 && more()) n += 0xFF;
            n += last + bits;
        }
        return n + 2;
    };
    // Two more offset bytes; returns the first, whose low 2 bits are a literal length.
    const auto two_byte_offset = [&](std::size_t plus, std::size_t& offset) -> u8 {
        const u8 b1 = rc(), b2 = rc();
        offset |= static_cast<std::size_t>(b1 >> 2);
        offset |= static_cast<std::size_t>(b2) << 6;
        offset += plus;
        return b1;
    };
    const auto copy_literals = [&](std::size_t n) -> u8 {
        for (std::size_t i = 0; i < n && more() && out.size() < dst_size; ++i) out.push_back(rc());
        return rc();
    };

    u8 opcode = rc();
    if ((opcode & 0xF0) == 0) opcode = copy_literals(literal_length(opcode));

    while (more() && out.size() < dst_size && opcode != 0x11) {
        std::size_t comp_bytes = 0, comp_offset = 0;
        if (opcode < 0x10 || opcode >= 0x40) {
            comp_bytes = static_cast<std::size_t>(opcode >> 4) - 1;
            const u8 op2 = rc();
            comp_offset = (static_cast<std::size_t>((opcode >> 2) & 3) | (static_cast<std::size_t>(op2) << 2)) + 1;
        } else if (opcode < 0x20) {
            comp_bytes = compressed_bytes(opcode, 7);
            comp_offset = static_cast<std::size_t>(opcode & 8) << 11;
            opcode = two_byte_offset(0x4000, comp_offset);
        } else {
            comp_bytes = compressed_bytes(opcode, 0x1F);
            opcode = two_byte_offset(1, comp_offset);
        }
        if (comp_offset == 0 || comp_offset > out.size()) {
            if (error) *error = "a back-reference reaches before the start of the data";
            return out;
        }
        for (std::size_t i = 0; i < comp_bytes && out.size() < dst_size; ++i)
            out.push_back(out[out.size() - comp_offset]);
        std::size_t lit = opcode & 3;
        if (lit == 0) {
            opcode = rc();
            if ((opcode & 0xF0) == 0) lit = literal_length(opcode);
        }
        if (lit) opcode = copy_literals(lit);
    }
    if (produced) *produced = out.size();
    if (consumed) *consumed = at;
    if (out.size() < dst_size) out.resize(dst_size, 0);
    return out;
}

bool read_r2004_sections(const u8* data, std::size_t size, R2004Sections& out, std::string& error) {
    out = R2004Sections{};
    if (size < 0x100 + 0x14) { error = "the file is too small for a 2004 header"; return false; }

    u8 header[0x6C];
    std::memcpy(header, data + 0x80, sizeof header);
    decrypt_header(header, sizeof header);
    if (std::memcmp(header, "AcFssFcAJMB", 11) != 0) {
        error = "the 2004 file header did not decrypt: this file is damaged or not what it says";
        return false;
    }
    const u64 page_map_address = le64(header + 0x54) + 0x100;
    const u32 section_map_id = le32(header + 0x5C);
    if (page_map_address + 0x14 > size) { error = "the page map lies outside the file"; return false; }

    const auto read_system_page = [&](u64 address, u32 want_type, std::vector<u8>& body) {
        if (address + 0x14 > size) { error = "a system page lies outside the file"; return false; }
        const u8* p = data + address;
        SystemPage sp;
        sp.type = le32(p); sp.decompressed = le32(p + 4); sp.compressed = le32(p + 8);
        sp.compression = le32(p + 12); sp.checksum = le32(p + 16);
        if (sp.type != want_type) { error = "a system page has the wrong type"; return false; }
        if (address + 0x14 + sp.compressed > size) { error = "a system page runs past the end of the file"; return false; }
        if (sp.decompressed > (1u << 26)) { error = "a system page claims an absurd size"; return false; }
        if (sp.compression == 2) {
            std::string err;
            body = decompress_r2004(p + 0x14, sp.compressed, sp.decompressed, &err);
            if (!err.empty()) { error = err; return false; }
        } else {
            body.assign(p + 0x14, p + 0x14 + sp.decompressed);
        }
        return true;
    };

    // Page map entries laid end to end from 0x100.
    std::vector<u8> map_body;
    if (!read_system_page(page_map_address, kPageMapType, map_body)) return false;
    std::vector<PageEntry> pages;
    {
        u64 address = 0x100;
        std::size_t at = 0;
        while (at + 8 <= map_body.size()) {
            PageEntry e;
            e.number = le32s(map_body.data() + at);
            e.size = le32(map_body.data() + at + 4);
            e.address = address;
            at += 8;
            if (e.number < 0) at += 16;       // a gap: parent, left, right, 0
            address += e.size;
            pages.push_back(e);
        }
    }
    const auto page_of = [&](i32 number) -> const PageEntry* {
        for (const auto& p : pages) if (p.number == number) return &p;
        return nullptr;
    };

    const PageEntry* smp = page_of(static_cast<i32>(section_map_id));
    if (!smp) { error = "the page map has no section map page"; return false; }
    std::vector<u8> sec_body;
    if (!read_system_page(smp->address, kSectionMapType, sec_body)) return false;
    if (sec_body.size() < 20) { error = "the section map is truncated"; return false; }
    const u32 n_sections = le32(sec_body.data());
    if (n_sections > 256) { error = "the section map claims " + std::to_string(n_sections) + " sections"; return false; }
    std::vector<SectionDesc> sections;
    std::size_t at = 20;
    for (u32 i = 0; i < n_sections; ++i) {
        if (at + 0x40 + 0x20 > sec_body.size()) { error = "the section map is truncated"; return false; }
        const u8* p = sec_body.data() + at;
        SectionDesc d;
        d.size = le64(p);
        d.page_count = le32(p + 8);
        d.max_decompressed = le32(p + 12);
        d.compressed = le32(p + 20);
        d.id = le32(p + 24);
        d.encrypted = le32(p + 28);
        const char* name = reinterpret_cast<const char*>(p + 32);
        d.name.assign(name, strnlen(name, 64));
        at += 0x60;
        if (d.page_count > 65536) { error = "a section claims too many pages"; return false; }
        for (u32 k = 0; k < d.page_count; ++k) {
            if (at + 16 > sec_body.size()) { error = "the section map is truncated"; return false; }
            SectionPage sp;
            sp.number = le32s(sec_body.data() + at);
            sp.data_size = le32(sec_body.data() + at + 4);
            sp.start = le64(sec_body.data() + at + 8);
            at += 16;
            d.pages.push_back(sp);
        }
        sections.push_back(std::move(d));
    }

    const auto assemble = [&](const SectionDesc& d, std::vector<u8>& buf) {
        // Pages sit at a fixed pitch and may extend past the declared size.
        const u64 span = std::max<u64>(d.size, static_cast<u64>(d.page_count) * d.max_decompressed);
        if (span > (1u << 30)) { error = "section " + d.name + " claims an absurd size"; return false; }
        buf.assign(static_cast<std::size_t>(span), 0);
        for (const SectionPage& sp : d.pages) {
            const PageEntry* pe = page_of(sp.number);
            if (!pe) { error = "section " + d.name + " names a page the map does not have"; return false; }
            if (pe->address + 0x20 > size) { error = "a page of " + d.name + " lies outside the file"; return false; }
            // 0x20-byte page header, masked with the page's address.
            u32 h[8];
            const u32 mask = kSectionMask ^ static_cast<u32>(pe->address);
            for (int k = 0; k < 8; ++k) h[k] = le32(data + pe->address + 4 * k) ^ mask;
            if (h[0] != kDataPageType) { error = "a page of " + d.name + " has the wrong type"; return false; }
            const u32 data_size = h[2];
            const u64 start = static_cast<u64>(h[4]) | (static_cast<u64>(h[5]) << 32);
            if (pe->address + 0x20 + data_size > size) { error = "a page of " + d.name + " runs past the end of the file"; return false; }
            if (d.encrypted == 1) { error = "section " + d.name + " is encrypted, which this build does not read"; return false; }
            std::vector<u8> page;
            if (d.compressed == 2) {
                std::string err;
                std::size_t produced = 0, consumed = 0;
                page = decompress_r2004(data + pe->address + 0x20, data_size, d.max_decompressed, &err, &produced, &consumed);
                if (!err.empty()) { error = d.name + ": " + err; return false; }
                if (produced + 64 < d.max_decompressed && &sp != &d.pages.back())
                    out.notes.push_back(d.name + " page " + std::to_string(sp.number) + ": " + std::to_string(produced) +
                                        " of " + std::to_string(d.max_decompressed) + " bytes, consumed " +
                                        std::to_string(consumed) + " of " + std::to_string(data_size));
            } else {
                page.assign(data + pe->address + 0x20, data + pe->address + 0x20 + data_size);
            }
            if (start > buf.size()) continue;
            const std::size_t n = std::min(page.size(), buf.size() - static_cast<std::size_t>(start));
            std::memcpy(buf.data() + start, page.data(), n);
        }
        return true;
    };

    bool have_handles = false, have_objects = false;
    for (const SectionDesc& d : sections) {
        if (d.name == "AcDb:Handles") { if (!assemble(d, out.handles)) return false; have_handles = true; }
        else if (d.name == "AcDb:AcDbObjects") { if (!assemble(d, out.objects)) return false; have_objects = true; }
        else if (d.name == "AcDb:Header") { if (!assemble(d, out.header)) return false; }
        else if (d.name == "AcDb:Classes") { if (!assemble(d, out.classes)) return false; }
        out.section_names.push_back(d.name);
    }
    if (!have_handles || !have_objects) { error = "the file has no object map or no objects section"; return false; }
    out.ok = true;
    return true;
}

}
