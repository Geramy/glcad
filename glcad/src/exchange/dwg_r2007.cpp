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

// RS: block i's byte j sits at j * block_count + i; parity is not checked.
std::vector<u8> deinterleave(const u8* src, std::size_t src_size, std::size_t block_count, std::size_t data_size) {
    std::vector<u8> out(block_count * data_size, 0);
    for (std::size_t i = 0; i < block_count; ++i)
        for (std::size_t j = 0; j < data_size; ++j) {
            const std::size_t at = j * block_count + i;
            if (at < src_size) out[i * data_size + j] = src[at];
        }
    return out;
}

}

// Literal runs are reordered per the spec: pairs/triples reversed, 16s swapped.
static void copy_literals(u8* dst, const u8* src, u32 length) {
    const auto c1 = [&](u32 off) { *dst++ = src[off]; };
    const auto c2 = [&](u32 off) { dst[0] = src[off + 1]; dst[1] = src[off]; dst += 2; };
    const auto c3 = [&](u32 off) { dst[0] = src[off + 2]; dst[1] = src[off + 1]; dst[2] = src[off]; dst += 3; };
    const auto c4 = [&](u32 off) { std::memcpy(dst, src + off, 4); dst += 4; };
    const auto c8 = [&](u32 off) { std::memcpy(dst, src + off, 8); dst += 8; };
    const auto c16 = [&](u32 off) { std::memcpy(dst, src + off + 8, 8); std::memcpy(dst + 8, src + off, 8); dst += 16; };
    while (length >= 32) { c16(16); c16(0); src += 32; length -= 32; }
    switch (length) {
        case 0: break;
        case 1: c1(0); break;
        case 2: c2(0); break;
        case 3: c3(0); break;
        case 4: c4(0); break;
        case 5: c1(4); c4(0); break;
        case 6: c1(5); c4(1); c1(0); break;
        case 7: c2(5); c4(1); c1(0); break;
        case 8: c8(0); break;
        case 9: c1(8); c8(0); break;
        case 10: c1(9); c8(1); c1(0); break;
        case 11: c2(9); c8(1); c1(0); break;
        case 12: c4(8); c8(0); break;
        case 13: c1(12); c4(8); c8(0); break;
        case 14: c1(13); c4(9); c8(1); c1(0); break;
        case 15: c2(13); c4(9); c8(1); c1(0); break;
        case 16: c16(0); break;
        case 17: c8(9); c1(8); c8(0); break;
        case 18: c1(17); c16(1); c1(0); break;
        case 19: c3(16); c16(0); break;
        case 20: c4(16); c16(0); break;
        case 21: c1(20); c4(16); c16(0); break;
        case 22: c2(20); c4(16); c16(0); break;
        case 23: c3(20); c4(16); c16(0); break;
        case 24: c8(16); c16(0); break;
        case 25: c8(17); c1(16); c16(0); break;
        case 26: c1(25); c8(17); c1(16); c16(0); break;
        case 27: c2(25); c8(17); c1(16); c16(0); break;
        case 28: c4(24); c8(16); c16(0); break;
        case 29: c1(28); c4(24); c8(16); c16(0); break;
        case 30: c2(28); c4(24); c8(16); c16(0); break;
        case 31: c1(30); c4(26); c8(18); c16(2); c2(0); break;
        default: break;
    }
}

bool decompress_r2007(const u8* src, std::size_t src_size, std::vector<u8>& dst, std::size_t dst_size,
                      std::string& error) {
    dst.assign(dst_size, 0);
    std::size_t sp = 0, dp = 0;
    const auto more = [&](std::size_t n) { return sp + n <= src_size; };
    if (src_size < 2) { error = "compressed data too short"; return false; }

    u32 length = 0, offset = 0;
    u8 opcode = src[sp++];
    if ((opcode & 0xF0) == 0x20) {
        sp += 2;
        if (!more(1)) { error = "compressed data too short"; return false; }
        length = src[sp++] & 0x07;
        if (length == 0) { error = "bad leading opcode"; return false; }
    }

    const auto literal_length = [&](u8 op, u32& len) {
        len = static_cast<u32>(op) + 8;
        if (len == 0x17) {
            if (!more(1)) return false;
            u32 n = src[sp++];
            len += n;
            if (n == 0xFF) {
                do {
                    if (!more(2)) return false;
                    n = src[sp] | (static_cast<u32>(src[sp + 1]) << 8);
                    sp += 2;
                    len += n;
                } while (n == 0xFFFF);
            }
        }
        return true;
    };
    const auto instructions = [&](u8& op, u32& off, u32& len) {
        switch (op >> 4) {
            case 0:
                len = (op & 0x0F) + 0x13;
                if (!more(2)) return false;
                off = src[sp++];
                op = src[sp++];
                len = ((op >> 3) & 0x10) + len;
                off = ((op & 0x78) << 5) + 1 + off;
                break;
            case 1:
                len = (op & 0x0F) + 3;
                if (!more(2)) return false;
                off = src[sp++];
                op = src[sp++];
                off = ((op & 0xF8) << 5) + 1 + off;
                break;
            case 2:
                if (!more(2)) return false;
                off = src[sp++];
                off = ((static_cast<u32>(src[sp++]) << 8) & 0xFF00) | off;
                len = op & 7;
                if ((op & 8) == 0) {
                    if (!more(1)) return false;
                    op = src[sp++];
                    len = (op & 0xF8) + len;
                } else {
                    ++off;
                    if (!more(2)) return false;
                    len = (static_cast<u32>(src[sp++]) << 3) + len;
                    op = src[sp++];
                    len = (((op & 0xF8) << 8) + len) + 0x100;
                }
                break;
            default:
                len = op >> 4;
                off = op & 15;
                if (!more(1)) return false;
                op = src[sp++];
                off = (((op & 0xF8) << 1) + off) + 1;
                break;
        }
        return true;
    };

    while (sp < src_size) {
        if (length == 0 && !literal_length(opcode, length)) { error = "bad literal length"; return false; }
        if (dp + length > dst_size || sp + length > src_size) { error = "a literal run overruns"; return false; }
        copy_literals(dst.data() + dp, src + sp, length);
        dp += length;
        sp += length;
        length = 0;
        if (sp >= src_size) return true;
        opcode = src[sp++];
        if (!instructions(opcode, offset, length)) { error = "bad copy instruction"; return false; }
        for (;;) {
            if (dp + length > dst_size) { error = "a copy overruns"; return false; }
            if (offset > dp || offset == 0) {
                error = "a copy reaches before the start (dp " + std::to_string(dp) + " sp " + std::to_string(sp) +
                        " offset " + std::to_string(offset) + " length " + std::to_string(length) + " opcode " + std::to_string(opcode) + ")";
                return false;
            }
            for (u32 i = 0; i < length; ++i) { dst[dp] = dst[dp - offset]; ++dp; }
            length = opcode & 7;
            if (length != 0 || sp >= src_size) break;
            opcode = src[sp++];
            if ((opcode >> 4) == 0) break;
            if ((opcode >> 4) == 0x0F) opcode &= 0x0F;
            if (!instructions(opcode, offset, length)) { error = "bad copy instruction"; return false; }
        }
    }
    return true;
}

namespace {

// RS-coded in 239-byte blocks, then compressed only when smaller.
bool read_system_page(const u8* data, std::size_t size, u64 at, u64 size_comp, u64 size_uncomp,
                      u64 repeat, std::vector<u8>& out, std::string& error) {
    if (repeat == 0 || repeat > 64 || size_comp >= size || size_uncomp >= size) { error = "a system page has absurd sizes"; return false; }
    const u64 pesize = ((size_comp + 7) & ~u64{7}) * repeat;
    const u64 block_count = (pesize + 238) / 239;
    const u64 page_size = (block_count * 255 + 7) & ~u64{7};
    if (at + page_size > size) { error = "a system page runs past the end of the file"; return false; }
    const std::vector<u8> pedata = deinterleave(data + at, static_cast<std::size_t>(page_size),
                                                static_cast<std::size_t>(block_count), 239);
    if (size_comp < size_uncomp)
        return decompress_r2007(pedata.data(), static_cast<std::size_t>(std::min<u64>(pedata.size(), size_comp)),
                                out, static_cast<std::size_t>(size_uncomp), error);
    out.assign(pedata.begin(), pedata.begin() + static_cast<std::ptrdiff_t>(std::min<u64>(size_uncomp, pedata.size())));
    return true;
}

u64 page_size_if_rs_coded(u64 size_comp) {
    const u64 pesize = (size_comp + 7) & ~u64{7};
    const u64 block_count = (pesize + 0xFB - 1) / 0xFB;
    return (block_count * 0xFF + 31) & ~u64{31};
}

struct Page { u64 size{0}; i64 id{0}; u64 offset{0}; };
struct SectionPage { u64 offset{0}, size{0}; i64 id{0}; u64 uncomp{0}, comp{0}; };
struct Section {
    u64 data_size{0}, max_size{0}, encrypted{0};
    std::string name;
    std::vector<SectionPage> pages;
};

std::string utf16_to_utf8(const u8* p, std::size_t bytes) {
    std::string out;
    for (std::size_t i = 0; i + 1 < bytes; i += 2) {
        const u32 c = p[i] | (static_cast<u32>(p[i + 1]) << 8);
        if (c == 0) break;
        if (c < 0x80) out.push_back(static_cast<char>(c));
        else if (c < 0x800) { out.push_back(static_cast<char>(0xC0 | (c >> 6))); out.push_back(static_cast<char>(0x80 | (c & 0x3F))); }
        else { out.push_back(static_cast<char>(0xE0 | (c >> 12))); out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F))); out.push_back(static_cast<char>(0x80 | (c & 0x3F))); }
    }
    return out;
}

}

bool read_r2007_sections(const u8* data, std::size_t size, R2004Sections& out, std::string& error) {
    out = R2004Sections{};
    if (size < 0x480) { error = "the file is too small for a 2007 header"; return false; }

    // Three RS blocks of 239 at 0x80: a small header, then the compressed header.
    const std::vector<u8> pedata = deinterleave(data + 0x80, 0x3D8, 3, 239);
    const i32 compr_len = static_cast<i32>(le32(pedata.data() + 24));
    std::vector<u8> hdr;
    if (compr_len > 0) {
        if (!decompress_r2007(pedata.data() + 32, static_cast<std::size_t>(std::min<i32>(compr_len, 3 * 239 - 32)), hdr, 34 * 8, error)) {
            error = "the 2007 file header did not decompress: " + error;
            return false;
        }
    } else {
        hdr.assign(pedata.begin() + 32, pedata.begin() + 32 + 34 * 8);
    }
    const auto field = [&](int i) { return le64(hdr.data() + 8 * i); };
    const u64 pages_map_offset = field(7);
    const u64 pages_map_size_comp = field(10), pages_map_size_uncomp = field(11);
    const u64 pages_map_correction = field(3);
    const i64 sections_map_id = static_cast<i64>(field(24));
    const u64 sections_map_size_comp = field(22), sections_map_size_uncomp = field(25);
    const u64 sections_map_correction = field(27);
    if (field(0) != 0x70) { error = "the 2007 file header is not what it should be (size field " + std::to_string(field(0)) + ")"; return false; }

    // Page map at 0x480 + offset: (size, id) pairs.
    std::vector<u8> pm;
    if (!read_system_page(data, size, 0x480 + pages_map_offset, pages_map_size_comp, pages_map_size_uncomp,
                          pages_map_correction, pm, error))
        return false;
    std::vector<Page> pages;
    {
        u64 offset = 0x480;
        for (std::size_t at = 0; at + 16 <= pm.size() && at < pages_map_size_uncomp; at += 16) {
            Page p;
            p.size = le64(pm.data() + at);
            p.id = static_cast<i64>(le64(pm.data() + at + 8));
            p.offset = offset;
            offset += p.size;
            pages.push_back(p);
        }
    }
    const auto page_of = [&](i64 id) -> const Page* {
        for (const auto& p : pages) if (p.id == id) return &p;
        return nullptr;
    };

    const Page* smp = page_of(sections_map_id);
    if (!smp) { error = "the page map has no section map page"; return false; }
    std::vector<u8> sm;
    if (!read_system_page(data, size, smp->offset, sections_map_size_comp, sections_map_size_uncomp,
                          sections_map_correction, sm, error))
        return false;
    std::vector<Section> sections;
    {
        std::size_t at = 0;
        while (at + 64 <= sm.size() && at < sections_map_size_uncomp) {
            Section s;
            s.data_size = le64(sm.data() + at);
            s.max_size = le64(sm.data() + at + 8);
            s.encrypted = le64(sm.data() + at + 16);
            const u64 name_length = le64(sm.data() + at + 32);
            const u64 num_pages = le64(sm.data() + at + 56);
            at += 64;
            if (name_length >= 128 || num_pages > 0xF0000) { error = "the section map is corrupt"; return false; }
            u64 nl = name_length + (name_length & 1);
            if (at + nl > sm.size()) break;
            s.name = utf16_to_utf8(sm.data() + at, static_cast<std::size_t>(nl));
            at += static_cast<std::size_t>(nl);
            for (u64 i = 0; i < num_pages; ++i) {
                if (at + 56 > sm.size()) break;
                SectionPage sp;
                sp.offset = le64(sm.data() + at);
                sp.size = le64(sm.data() + at + 8);
                sp.id = static_cast<i64>(le64(sm.data() + at + 16));
                sp.uncomp = le64(sm.data() + at + 24);
                sp.comp = le64(sm.data() + at + 32);
                at += 56;
                s.pages.push_back(sp);
            }
            sections.push_back(std::move(s));
        }
    }

    const auto assemble = [&](const Section& s, std::vector<u8>& buf) {
        if (s.data_size > (1u << 30)) { error = "section " + s.name + " claims an absurd size"; return false; }
        if (s.encrypted) { error = "section " + s.name + " is encrypted, which this build does not read"; return false; }
        buf.assign(static_cast<std::size_t>(s.data_size), 0);
        for (const SectionPage& sp : s.pages) {
            const Page* pg = page_of(sp.id);
            if (!pg) { error = "section " + s.name + " names a page the map does not have"; return false; }
            if (sp.offset > buf.size() || pg->offset >= size) { error = "a page of " + s.name + " lies outside the file"; return false; }
            std::vector<u8> plain;
            if (sp.comp != sp.uncomp || pg->size == page_size_if_rs_coded(sp.comp)) {
                // RS-coded in 0xFB-byte blocks, then compressed when smaller.
                const u64 pesize = (sp.comp + 7) & ~u64{7};
                const u64 block_count = (pesize + 0xFB - 1) / 0xFB;
                const u64 page_size = std::min<u64>(pg->size, size - pg->offset);
                const std::vector<u8> pedata = deinterleave(data + pg->offset, static_cast<std::size_t>(page_size),
                                                            static_cast<std::size_t>(block_count), 0xFB);
                if (sp.comp < sp.uncomp) {
                    if (!decompress_r2007(pedata.data(), static_cast<std::size_t>(std::min<u64>(pedata.size(), sp.comp)),
                                          plain, static_cast<std::size_t>(sp.uncomp), error)) {
                        error = s.name + ": " + error;
                        return false;
                    }
                } else {
                    plain.assign(pedata.begin(), pedata.begin() + static_cast<std::ptrdiff_t>(std::min<u64>(sp.uncomp, pedata.size())));
                }
            } else {
                if (pg->offset + sp.uncomp > size) { error = "a page of " + s.name + " runs past the end of the file"; return false; }
                plain.assign(data + pg->offset, data + pg->offset + sp.uncomp);
            }
            const std::size_t n = std::min(plain.size(), buf.size() - static_cast<std::size_t>(sp.offset));
            std::memcpy(buf.data() + sp.offset, plain.data(), n);
        }
        return true;
    };

    bool have_handles = false, have_objects = false;
    for (const Section& s : sections) {
        if (s.name == "AcDb:Handles") { if (!assemble(s, out.handles)) return false; have_handles = true; }
        else if (s.name == "AcDb:AcDbObjects") { if (!assemble(s, out.objects)) return false; have_objects = true; }
        else if (s.name == "AcDb:Header") { if (!assemble(s, out.header)) return false; }
        else if (s.name == "AcDb:Classes") { if (!assemble(s, out.classes)) return false; }
        out.section_names.push_back(s.name);
    }
    if (!have_handles || !have_objects) { error = "the file has no object map or no objects section"; return false; }
    out.ok = true;
    return true;
}

}
