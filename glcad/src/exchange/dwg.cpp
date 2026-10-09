#include "exchange/dwg.hpp"
#include "exchange/dwg_text.hpp"
#include "exchange/dwg_clip.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <optional>
#include <map>
#include <unordered_map>
#include <filesystem>

namespace gdwg {
namespace {

constexpr u8 kHeaderSentinel[16] = {0x95, 0xA0, 0x4E, 0x28, 0x99, 0x82, 0x1A,
                                    0xE5, 0x5E, 0x41, 0xE0, 0x5F, 0x9D, 0x3A,
                                    0x4D, 0x00};

u16 read_u16(const u8* p) {
    return static_cast<u16>(p[0] | (static_cast<u16>(p[1]) << 8));
}
u32 read_u32(const u8* p) {
    return static_cast<u32>(p[0]) | (static_cast<u32>(p[1]) << 8) |
           (static_cast<u32>(p[2]) << 16) | (static_cast<u32>(p[3]) << 24);
}

}

const char* version_name(Version v) noexcept {
    switch (v) {
        case Version::R13:   return "AutoCAD R13";
        case Version::R14:   return "AutoCAD R14";
        case Version::R2000: return "AutoCAD 2000";
        case Version::R2004: return "AutoCAD 2004";
        case Version::R2007: return "AutoCAD 2007";
        case Version::R2010: return "AutoCAD 2010";
        case Version::R2013: return "AutoCAD 2013";
        case Version::R2018: return "AutoCAD 2018";
        case Version::Unknown:
        default:             return "unknown";
    }
}

Version version_of(const std::string& tag) noexcept {
    if (tag == "AC1012") return Version::R13;
    if (tag == "AC1014") return Version::R14;
    if (tag == "AC1015") return Version::R2000;
    if (tag == "AC1018") return Version::R2004;
    if (tag == "AC1021") return Version::R2007;
    if (tag == "AC1024") return Version::R2010;
    if (tag == "AC1027") return Version::R2013;
    if (tag == "AC1032") return Version::R2018;
    return Version::Unknown;
}

const SectionLocator* FileHeader::section(u8 number) const {
    for (const SectionLocator& s : sections)
        if (s.number == number) return &s;
    return nullptr;
}

bool read_header(const u8* data, std::size_t size, FileHeader& out,
                 std::string& error) {
    if (!data || size < 0x100) {
        error = "the file is too small to be a DWG";
        return false;
    }

    out = FileHeader{};
    out.tag.assign(reinterpret_cast<const char*>(data), 6);
    out.version = version_of(out.tag);
    if (out.version == Version::Unknown) {
        error = "'" + out.tag + "' is not a DWG version this build reads";
        return false;
    }
    if (out.version >= Version::R2004) {
        out.maintenance = data[0x0B];
        out.preview_address = read_u32(data + 0x0D);
        out.codepage = read_u16(data + 0x13);
        return true;
    }
    if (out.version != Version::R2000) {
        error = std::string(version_name(out.version)) +
                " files are recognised but not yet read; this build reads "
                "AutoCAD 2000 through 2018";
        return false;
    }

    out.maintenance = data[0x0B];
    out.preview_address = read_u32(data + 0x0D);
    out.codepage = read_u16(data + 0x13);

    const u32 count = read_u32(data + 0x15);
    if (count > 64) {
        error = "the section table claims " + std::to_string(count) +
                " entries, which no DWG has";
        return false;
    }

    std::size_t at = 0x19;
    for (u32 i = 0; i < count; ++i) {
        if (at + 9 > size) {
            error = "the section table runs past the end of the file";
            return false;
        }
        SectionLocator s;
        s.number = data[at];
        s.seeker = read_u32(data + at + 1);
        s.size = read_u32(data + at + 5);
        if (s.seeker > size || s.size > size || s.seeker + s.size > size) {
            error = "section " + std::to_string(s.number) +
                    " lies outside the file";
            return false;
        }
        out.sections.push_back(s);
        at += 9;
    }

    at += 2;
    if (at + 16 > size) {
        error = "the header has no closing sentinel";
        return false;
    }
    if (std::memcmp(data + at, kHeaderSentinel, 16) != 0) {
        error = "the header sentinel is wrong; this file is damaged";
        return false;
    }
    return true;
}

bool read_object_map(const u8* data, std::size_t size, const FileHeader& header,
                     std::vector<ObjectRef>& out, std::string& error) {
    out.clear();
    const SectionLocator* map = header.section(2);
    if (!map) {
        error = "the file has no object map";
        return false;
    }
    if (map->seeker + map->size > size) {
        error = "the object map lies outside the file";
        return false;
    }
    return parse_object_map(data + map->seeker, map->size, out, error);
}

bool parse_object_map(const u8* data, std::size_t size, std::vector<ObjectRef>& out,
                      std::string& error) {
    out.clear();
    std::size_t at = 0;
    const std::size_t end = size;

    u64 handle = 0;
    u32 offset = 0;

    while (at + 2 <= end) {
        // The section size is big-endian here, unlike every other integer.
        const u16 section_size =
            static_cast<u16>((static_cast<u16>(data[at]) << 8) | data[at + 1]);
        if (section_size <= 2) break;
        // The size covers the size word and data; the CRC follows outside it.
        if (at + section_size + 2 > end) {
            error = "an object map section runs past the map";
            return false;
        }

        // CRC 0xA001 seeded 0xC0C1 over the section incl. size word; stored big-endian.
        const u16 stored = static_cast<u16>(
            (static_cast<u16>(data[at + section_size]) << 8) |
            data[at + section_size + 1]);
        const u16 computed = crc8(0xC0C1, data + at, section_size);
        if (stored != computed) {
            error = "an object map section failed its CRC";
            return false;
        }

        // Handles accumulate across sections; offsets restart at zero in each.
        offset = 0;
        BitReader r(data + at + 2, section_size - 2u);
        while (!r.exhausted()) {
            const std::size_t before = r.bit_position();
            const u32 handle_delta = r.umc();      // unsigned: handles only grow
            const i32 offset_delta = r.mc();
            if (!r.ok() || r.bit_position() == before) break;

            // Entries are deltas from the previous entry.
            handle += handle_delta;
            offset = static_cast<u32>(static_cast<i64>(offset) + offset_delta);
            out.push_back({handle, offset});
        }
        at += section_size + 2u;
    }
    return true;
}

const char* object_type_name(u16 type) noexcept {
    switch (static_cast<ObjectType>(type)) {
        case ObjectType::Text:       return "TEXT";
        case ObjectType::MText:      return "MTEXT";
        case ObjectType::Attrib:     return "ATTRIB";
        case ObjectType::AttDef:     return "ATTDEF";
        case ObjectType::Block:      return "BLOCK";
        case ObjectType::EndBlk:     return "ENDBLK";
        case ObjectType::SeqEnd:     return "SEQEND";
        case ObjectType::Insert:     return "INSERT";
        case ObjectType::MInsert:    return "MINSERT";
        case ObjectType::Vertex2D:   return "VERTEX_2D";
        case ObjectType::Vertex3D:   return "VERTEX_3D";
        case ObjectType::Polyline2D: return "POLYLINE_2D";
        case ObjectType::Polyline3D: return "POLYLINE_3D";
        case ObjectType::Arc:        return "ARC";
        case ObjectType::Circle:     return "CIRCLE";
        case ObjectType::Line:       return "LINE";
        case ObjectType::DimOrdinate: return "DIMENSION_ORDINATE";
        case ObjectType::DimLinear:  return "DIMENSION_LINEAR";
        case ObjectType::DimAligned: return "DIMENSION_ALIGNED";
        case ObjectType::DimAng3Pt:  return "DIMENSION_ANG3PT";
        case ObjectType::DimAng2Ln:  return "DIMENSION_ANG2LN";
        case ObjectType::DimRadius:  return "DIMENSION_RADIUS";
        case ObjectType::DimDiameter: return "DIMENSION_DIAMETER";
        case ObjectType::Leader:     return "LEADER";
        case ObjectType::Point:      return "POINT";
        case ObjectType::Viewport:   return "VIEWPORT";
        case ObjectType::Ellipse:    return "ELLIPSE";
        case ObjectType::LwPolyline: return "LWPOLYLINE";
        case ObjectType::Hatch:      return "HATCH";
        case ObjectType::Spline:     return "SPLINE";
        case ObjectType::Ray:        return "RAY";
        case ObjectType::Xline:      return "XLINE";
        case ObjectType::Layer:      return "LAYER";
        case ObjectType::LayerTable: return "LAYER_CONTROL";
        default:                     return "";
    }
}

bool read_object_header(const u8* data, std::size_t size, const ObjectRef& ref,
                        RawObject& out, Version version) {
    if (ref.offset + 2 >= size) return false;

    // Size is a modular short and excludes the size field itself.
    BitReader r(data + ref.offset,
                std::min<std::size_t>(size - ref.offset, 64));
    const u32 object_size = r.ms();
    if (!r.ok() || object_size == 0) return false;
    if (ref.offset + object_size > size) return false;

    // 2010+: handle stream size follows the size, and the type is BB-prefixed.
    u32 handle_stream_bits = 0;
    if (version >= Version::R2010) handle_stream_bits = r.umc();
    const std::size_t body_at = r.bit_position();
    u16 type = 0;
    if (version >= Version::R2010) {
        const u8 code = r.bb();
        if (code == 0) type = r.rc();
        else if (code == 1) type = static_cast<u16>(r.rc() + 0x1F0);
        else type = r.rs();
    } else {
        type = r.bs();
    }
    u32 data_bits = 0;
    if (version >= Version::R2010) {
        if (handle_stream_bits > object_size * 8u) return false;
        data_bits = object_size * 8u - handle_stream_bits;
    } else {
        data_bits = r.rl();
    }
    if (!r.ok()) return false;

    out.handle = ref.handle;
    out.offset = ref.offset;
    out.size = object_size;
    out.type = type;
    out.data_bits = data_bits;
    out.body_at = body_at;
    out.data_at = r.bit_position();
    // The map's running handle is only a hint; trust the object's own.
    {
        BitReader h(data + ref.offset, std::min<std::size_t>(size - ref.offset, object_size + 8));
        h.seek_bits(out.data_at);
        const Handle own = h.handle();
        if (h.ok() && own.value != 0) out.handle = own.value;
    }
    return true;
}

namespace {

std::string read_text(BitReader& r);

struct Prefix {
    u8   entmode{0};
    u32  reactors{0};
    bool xdic_missing{false};
    bool nolinks{true};
    u8   ltype_flags{0}, plotstyle_flags{0}, material_flags{0};
    bool full_vs{false}, face_vs{false}, edge_vs{false};
    bool color_handle{false};
    gdoc::Attributes attrs;
};

gdoc::Lineweight lineweight_from_code(u8 code) {
    constexpr int values[]={0,5,9,13,15,18,20,25,30,35,40,50,53,60,70,80,90,100,106,120,140,158,200,211};
    if(code<24)return static_cast<gdoc::Lineweight>(values[code]);
    if(code==29)return gdoc::Lineweight::ByLayer;
    if(code==30)return gdoc::Lineweight::ByBlock;
    return gdoc::Lineweight::Default;
}

// 2007+: string stream at data end; flag bit at data_end - 1, size before it.
struct Strings {
    bool      present{false};
    BitReader r;
    std::string next() {
        if (!present) return {};
        const u16 len = r.bs();
        if (len > 32767 || !r.ok()) return {};
        std::string out;
        for (u16 i = 0; i < len; ++i) {
            const u16 c = r.rs();
            if (c == 0) continue;
            if (c < 0x80) out.push_back(static_cast<char>(c));
            else if (c < 0x800) { out.push_back(static_cast<char>(0xC0 | (c >> 6))); out.push_back(static_cast<char>(0x80 | (c & 0x3F))); }
            else { out.push_back(static_cast<char>(0xE0 | (c >> 12))); out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F))); out.push_back(static_cast<char>(0x80 | (c & 0x3F))); }
        }
        return out;
    }
};

Strings string_stream(const u8* data, std::size_t size, const RawObject& raw, Version version) {
    Strings s;
    if (version < Version::R2007) return s;
    const std::size_t bytes = std::min<std::size_t>(size - raw.offset, raw.size + 8u);
    const std::size_t end = data_end_bits(raw);
    if (end < 33 || end > bytes * 8) return s;
    BitReader t(data + raw.offset, bytes);
    t.seek_bits(end - 1);
    if (!t.b()) return s;
    t.seek_bits(end - 1 - 16);
    u32 data_size = t.rs();
    std::size_t start = end - 1 - 16;
    if (data_size & 0x8000) {
        t.seek_bits(end - 1 - 32);
        const u16 hi = t.rs();
        data_size = (data_size & 0x7FFF) | (static_cast<u32>(hi) << 15);
        start = end - 1 - 32;
    }
    if (data_size > start) return s;
    s.r = BitReader(data + raw.offset, bytes);
    s.r.seek_bits(start - data_size);
    s.present = true;
    return s;
}

// Class numbers start at 500; locator 1 before 2004, compressed section after.
constexpr u8 kClassSentinel[16] = {0x8D, 0xA1, 0xC4, 0xB8, 0xC4, 0xA9, 0xF8, 0xC5,
                                   0xC0, 0xDC, 0xF4, 0x5F, 0xE7, 0xCF, 0xB6, 0x8A};

// AcDb:Header is one strictly ordered bitstream; the walk is verified by GUIDs.
constexpr u8 kVarsSentinel[16] = {0xCF, 0x7B, 0x1F, 0x23, 0xFD, 0xDE, 0x38, 0xA9,
                                  0x5F, 0x7C, 0x68, 0xB8, 0x4E, 0x6D, 0x33, 0x5F};

bool guid_shaped(std::string s) {
    // Some files keep a TV's terminator.
    while (!s.empty() && s.back() == '\0') s.pop_back();
    if (s.size() != 38 || s.front() != '{' || s.back() != '}') return false;
    for (std::size_t i = 1; i + 1 < s.size(); ++i) {
        const bool dash = (i == 9 || i == 14 || i == 19 || i == 24);
        if (dash ? s[i] != '-'
                 : !std::isxdigit(static_cast<unsigned char>(s[i]))) return false;
    }
    return true;
}

std::optional<i32> read_insunits(const u8* buf, std::size_t len, const FileHeader& header, bool* model_active=nullptr, u64* paper_space=nullptr) {
    const Version v = header.version;
    if (v < Version::R2000 || !buf || len < 120) return std::nullopt;
    std::size_t pos = 0;
    for (; pos + 16 <= len && pos < 256; ++pos)
        if (std::memcmp(buf + pos, kVarsSentinel, 16) == 0) break;
    if (pos + 16 > len || pos >= 256) return std::nullopt;
    BitReader r(buf, len);
    r.seek_bits((pos + 16) * 8);
    const u32 sz = r.rl();
    if (!r.ok() || sz < 100 || sz > len) return std::nullopt;
    if ((v >= Version::R2010 && header.maintenance > 3) || v >= Version::R2018) r.rl();
    u32 bitsize = 0;
    if (v >= Version::R2007) bitsize = r.rl();

    // 2007+: every string sits in the tail stream, in fixed order.
    if (v >= Version::R2007) {
        const bool early = v < Version::R2010 ||
                           (v == Version::R2010 && header.maintenance < 3);
        std::size_t flag = pos * 8 + bitsize + (early ? 159 : 191);
        if (flag / 8 >= len) return std::nullopt;
        BitReader t(buf, len);
        t.seek_bits(flag);
        if (!t.b()) return std::nullopt;
        flag -= 16;
        t.seek_bits(flag);
        u32 str_size = t.rs();
        if (str_size & 0x8000) {
            flag -= 16;
            t.seek_bits(flag);
            const u16 hi = t.rs();
            str_size = (str_size & 0x7FFF) | (static_cast<u32>(hi) << 15);
        }
        if (str_size > flag) return std::nullopt;
        Strings strings;
        strings.r = BitReader(buf, len);
        strings.r.seek_bits(flag - str_size);
        strings.present = true;
        // unit names x4, MENU, DIMPOST, DIMAPOST, [2010+: DIMALTMZS, DIMMZS], HYPERLINKBASE, STYLESHEET
        const int skip = v >= Version::R2010 ? 11 : 9;
        for (int i = 0; i < skip; ++i) strings.next();
        if (!guid_shaped(strings.next()) || !guid_shaped(strings.next()))
            return std::nullopt;
    }

    // Strings inline before 2007; colours gained an RGB and flag byte in 2004.
    const auto text = [&]() { if (v < Version::R2007) r.tv(); };
    // 2007+: plain handles live in the handle stream; only HANDSEED stays inline.
    const auto skip_h = [&]() { if (v < Version::R2007) r.handle(); };
    const auto cmc = [&]() {
        r.bs();
        if (v >= Version::R2004) {
            r.bl();
            const u8 f = r.rc();
            if (f & 1) text();
            if (f & 2) text();
        }
    };
    const auto bd3 = [&]() { f64 a, b, c; r.bd3(a, b, c); };
    const auto rd2 = [&]() { f64 a, b; r.rd2(a, b); };
    const auto bds = [&](int n) { for (int i = 0; i < n; ++i) r.bd(); };
    const auto bss = [&](int n) { for (int i = 0; i < n; ++i) r.bs(); };
    const auto bits = [&](int n) { for (int i = 0; i < n; ++i) r.b(); };

    if (v >= Version::R2013) {                       // BLL REQUIREDVERSIONS
        const u8 n = static_cast<u8>((r.bb() << 1) | (r.b() ? 1 : 0));
        for (u8 i = 0; i < n; ++i) r.rc();
    }
    bds(4);                                          // unit conversion ratios
    if (v <= Version::R2004) { text(); text(); text(); text(); }   // unit names
    r.bl(); r.bl();                                  // unknowns 8, 9
    if (v == Version::R2000) skip_h();               // current VX record
    bits(2);                                         // DIMASO DIMSHO
    bits(7);                                         // PLINEGEN..LIMCHECK
    if (v >= Version::R2004) r.b();                  // undocumented
    bits(4);                                         // USRTIMER..SPLFRAME
    bits(2);                                         // MIRRTEXT WORLDVIEW
    const bool tilemode=r.b(); bits(2);              // TILEMODE PLIMCHECK VISRETAIN
    bits(2);                                         // DISPSILH PELLIPSE
    bss(1);                                          // PROXYGRAPHICS
    bss(1);                                          // TREEDEPTH
    bss(4);                                          // LUNITS LUPREC AUNITS AUPREC
    bss(1);                                          // ATTMODE
    bss(1);                                          // PDMODE
    if (v >= Version::R2004) { r.bl(); r.bl(); r.bl(); }   // unknowns 12-14
    bss(5);                                          // USERI1-5
    bss(14);                                         // SPLINESEGS..TEXTQLTY
    bds(9);                                          // LTSCALE..PLINEWID
    bds(5);                                          // USERR1-5
    bds(4);                                          // CHAMFERA-D
    bds(3);                                          // FACETRES CMLSCALE CELTSCALE
    text();                                          // MENU
    r.bl(); r.bl(); r.bl(); r.bl();                  // TDUCREATE TDUUPDATE
    if (v >= Version::R2004) { r.bl(); r.bl(); r.bl(); }   // unknowns 15-17
    r.bl(); r.bl(); r.bl(); r.bl();                  // TDINDWG TDUSRTIMER
    cmc();                                           // CECOLOR
    r.handle();                                      // HANDSEED, a DATAHANDLE: inline in every version
    skip_h(); skip_h(); skip_h();                    // CLAYER TEXTSTYLE CELTYPE
    if (v >= Version::R2007) skip_h();               // CMATERIAL
    skip_h(); skip_h();                              // DIMSTYLE CMLSTYLE
    r.bd();                                          // PSVPSCALE
    bd3(); bd3(); bd3(); rd2(); rd2(); r.bd();       // paper space: base, extents, limits, elevation
    bd3(); bd3(); bd3(); skip_h();                   // PUCS org/x/y, name
    skip_h(); r.bs(); skip_h();                      // PUCSORTHOREF/VIEW/BASE
    for (int i = 0; i < 6; ++i) bd3();               // PUCSORG faces
    bd3(); bd3(); bd3(); rd2(); rd2(); r.bd();       // model space
    bd3(); bd3(); bd3(); skip_h();                   // UCS org/x/y, name
    skip_h(); r.bs(); skip_h();                      // UCSORTHOREF/VIEW/BASE
    for (int i = 0; i < 6; ++i) bd3();               // UCSORG faces
    text(); text();                                  // DIMPOST DIMAPOST
    bds(9);                                          // DIMSCALE..DIMTM
    if (v >= Version::R2007) { r.bd(); r.bd(); r.bs(); cmc(); }   // DIMFXL..DIMTFILLCLR
    bits(6);                                         // DIMTOL..DIMSE2
    bss(3);                                          // DIMTAD DIMZIN DIMAZIN
    if (v >= Version::R2007) r.bs();                 // DIMARCSYM
    bds(8);                                          // DIMTXT..DIMGAP
    r.bd();                                          // DIMALTRND
    r.b(); r.bs(); bits(4);                          // DIMALT DIMALTD DIMTOFL DIMSAH DIMTIX DIMSOXD
    cmc(); cmc(); cmc();                             // DIMCLRD DIMCLRE DIMCLRT
    bss(11);                                         // DIMADEC..DIMJUST
    bits(2);                                         // DIMSD1 DIMSD2
    bss(4);                                          // DIMTOLJ DIMTZIN DIMALTZ DIMALTTZ
    r.b(); r.bs();                                   // DIMUPT DIMATFIT
    if (v >= Version::R2007) r.b();                  // DIMFXLON
    if (v >= Version::R2010) { r.b(); r.bd(); r.bd(); }   // DIMTXTDIRECTION DIMALTMZF DIMMZF
    for (int i = 0; i < 5; ++i) skip_h();            // DIMTXSTY DIMLDRBLK DIMBLK DIMBLK1 DIMBLK2
    if (v >= Version::R2007) { skip_h(); skip_h(); skip_h(); }   // DIMLTYPE DIMLTEX1 DIMLTEX2
    r.bs(); r.bs();                                  // DIMLWD DIMLWE
    for (int i = 0; i < 9; ++i) skip_h();            // the table control objects
    if (v == Version::R2000) skip_h();               // VX control
    skip_h(); skip_h(); skip_h();                    // group, mlinestyle, named-object dicts
    r.bs(); r.bs();                                  // TSTACKALIGN TSTACKSIZE
    text(); text();                                  // HYPERLINKBASE STYLESHEET
    skip_h(); skip_h(); skip_h();                    // layout, plotsettings, plotstyle dicts
    if (v >= Version::R2004) { skip_h(); skip_h(); } // material, color dicts
    if (v >= Version::R2007) skip_h();               // visualstyle dict
    if (v >= Version::R2013) skip_h();               // lightlist dict
    const u32 flags = r.bl();                        // FLAGS
    const u16 insunits = r.bs();                     // $INSUNITS, at last
    const u16 cepsntype = r.bs();
    if (cepsntype == 3) skip_h();
    if (!r.ok()) return std::nullopt;
    if (v < Version::R2007) {
        // Pre-2007 the GUIDs follow inline.
        if (!guid_shaped(r.tv()) || !guid_shaped(r.tv())) return std::nullopt;
        if (!r.ok()) return std::nullopt;
    }
    if (insunits > 24 || cepsntype > 3 || flags >= 0x8000) return std::nullopt;
    if(model_active) *model_active=tilemode;
    if(paper_space && v<Version::R2007) *paper_space=r.handle().value;
    return static_cast<i32>(insunits);
}

void read_classes(const u8* buf, std::size_t len, const FileHeader& header,
                  std::vector<std::pair<u16, std::string>>& out) {
    const Version version = header.version;
    std::size_t pos = 0;
    for (; pos + 16 <= len; ++pos)
        if (std::memcmp(buf + pos, kClassSentinel, 16) == 0) break;
    if (pos + 16 > len) return;
    BitReader r(buf, len);
    r.seek_bits((pos + 16) * 8);
    const u32 sz = r.rl();
    if (sz < 4 || sz > len) return;
    if (version < Version::R2004) {
        const std::size_t end_bits = (pos + 20 + sz) * 8;
        for (int guard = 0; guard < 4096 && r.ok() && r.bit_position() + 32 < end_bits; ++guard) {
            const u16 number = r.bs();
            r.bs();                                   // proxy flags
            r.tv();                                   // application name
            r.tv();                                   // C++ class name
            std::string dxf = r.tv();
            while (!dxf.empty() && dxf.back() == '\0') dxf.pop_back();
            r.b();                                    // was a proxy
            r.bs();                                   // 0x1f2 entity, 0x1f3 object
            if (!r.ok() || dxf.empty()) break;
            out.push_back({number, dxf});
        }
        return;
    }
    if ((version >= Version::R2010 && header.maintenance > 3) || version >= Version::R2018) r.rl();   // handle size
    u32 bitsize = 0;
    if (version >= Version::R2007) bitsize = r.rl();
    const u16 max_num = r.bs();
    r.rc(); r.rc(); r.b();
    if (max_num < 500 || max_num > 500 + 5000) return;
    Strings strings;
    if (version >= Version::R2007) {
        const bool early = version < Version::R2010 || (version == Version::R2010 && header.maintenance < 3);
        std::size_t start = pos * 8 + bitsize + (early ? 159 : 191);
        if (start / 8 >= len) return;
        BitReader t(buf, len);
        t.seek_bits(start);
        if (!t.b()) return;
        start -= 16;
        t.seek_bits(start);
        u32 data_size = t.rs();
        if (data_size & 0x8000) {
            start -= 16;
            t.seek_bits(start);
            const u16 hi = t.rs();
            data_size = (data_size & 0x7FFF) | (static_cast<u32>(hi) << 15);
        }
        if (data_size > start) return;
        strings.r = BitReader(buf, len);
        strings.r.seek_bits(start - data_size);
        strings.present = true;
    }
    for (u16 i = 0; i < max_num - 499 && r.ok(); ++i) {
        const u16 number = r.bs();
        r.bs();                                       // proxy flags
        std::string dxf;
        if (version >= Version::R2007) { strings.next(); strings.next(); dxf = strings.next(); }
        else { r.tv(); r.tv(); dxf = r.tv(); }
        while (!dxf.empty() && dxf.back() == '\0') dxf.pop_back();
        r.b();                                        // was a proxy
        r.bs();                                       // item class id
        r.bl();                                       // instances
        r.bs(); r.bs();                               // dwg / maintenance version
        r.bl(); r.bl();
        if (!r.ok()) break;
        if (!dxf.empty()) out.push_back({number, dxf});
    }
}

// Index; from 2004 also a true colour and optional names.
std::string read_string(BitReader& r, Strings& strings, Version version);

void read_cmc(BitReader& r, Strings& strings, Version version) {
    r.bs();
    if (version >= Version::R2004) {
        r.bl();
        const u8 flag = r.rc();
        if (flag < 4) {
            if (flag & 1) read_string(r, strings, version);
            if (flag & 2) read_string(r, strings, version);
        }
    }
}

std::string read_string(BitReader& r, Strings& strings, Version version) {
    if (version >= Version::R2007) return strings.next();
    return read_text(r);
}

bool read_common_entity_data(BitReader& r, Version version, Prefix* out = nullptr) {
    Prefix pf;
    r.handle();                       // the entity's own handle

    // EED: size, app handle, bytes; repeats until a size of zero.
    for (int guard = 0; guard < 64; ++guard) {
        const u16 eed_size = r.bs();
        if (eed_size == 0) break;
        r.handle();
        for (u16 i = 0; i < eed_size; ++i) r.rc();
        if (!r.ok()) return false;
    }

    // Graphic size: RL through 2007, BLL from 2010.
    if (r.b()) {
        u64 graphic_size = 0;
        if (version >= Version::R2010) {
            const unsigned len = (static_cast<unsigned>(r.bb()) << 1) | (r.b() ? 1u : 0u);
            if (len > 8) return false;
            for (unsigned i = 0; i < len; ++i) graphic_size |= static_cast<u64>(r.rc()) << (8 * i);
        } else {
            graphic_size = r.rl();
        }
        if (graphic_size > (1u << 24)) return false;
        for (u64 i = 0; i < graphic_size; ++i) r.rc();
    }

    pf.entmode = r.bb();
    const u32 reactors = r.bl();
    if (reactors > 4096) return false;
    pf.reactors = reactors;
    if (version >= Version::R2004) pf.xdic_missing = r.b();
    if (version >= Version::R2013) r.b();   // has binary data

    // No-links flag exists through R2002 only.
    if (version < Version::R2004) pf.nolinks = r.b();

    if (version >= Version::R2004) {
        // ENC: index in low 9 bits; 0x20 transparency, 0x40 handle, 0x80 true colour.
        const u16 raw = r.bs();
        const u16 flags = static_cast<u16>(raw >> 8);
        const auto index=raw&0x1ff;
        pf.attrs.color=index==0?gdoc::Color::by_block():index==256?gdoc::Color::by_layer():gdoc::Color::aci(index);
        pf.color_handle=flags&0x40;
        if (flags & 0x20) {
            const auto alpha=r.bl();
            if((alpha>>24)==3)pf.attrs.transparency=int(std::lround((255-(alpha&255))*100.0/255));
            else if((alpha>>24)==1)pf.attrs.transparency=-2;
        }
        if (!pf.color_handle && (flags & 0x80)) {
            const auto rgb=r.bl();
            pf.attrs.color=gdoc::Color::rgb(u8(rgb>>16),u8(rgb>>8),u8(rgb));
        }
        if ((flags & 0x41) == 0x41) read_text(r);
        if ((flags & 0x42) == 0x42) read_text(r);
    } else {
        const auto index=r.bs();
        pf.attrs.color=index==0?gdoc::Color::by_block():index==256?gdoc::Color::by_layer():gdoc::Color::aci(index);
    }
    pf.attrs.linetype_scale=r.bd();
    if(!std::isfinite(pf.attrs.linetype_scale)||pf.attrs.linetype_scale<=0)pf.attrs.linetype_scale=1;
    pf.ltype_flags = r.bb();
    if(pf.ltype_flags==1)pf.attrs.linetype="ByBlock";
    if(pf.ltype_flags==2)pf.attrs.linetype="Continuous";
    pf.plotstyle_flags = r.bb();
    if (version >= Version::R2007) { pf.material_flags = r.bb(); r.rc(); }   // material, shadow flags
    if (version >= Version::R2010) { pf.full_vs = r.b(); pf.face_vs = r.b(); pf.edge_vs = r.b(); }
    pf.attrs.visible=r.bs()==0;
    pf.attrs.lineweight=lineweight_from_code(r.rc());
    if (out) *out = pf;
    return r.ok();
}

struct EntityHandles {
    u64 owner{0};
    u64 layer{0};
    u64 extension{0};
    u64 color{0}, linetype{0};
    std::vector<Handle> rest;
};

EntityHandles entity_handles(const u8* data, std::size_t size, const RawObject& raw,
                             const Prefix& pf, Version version) {
    EntityHandles out;
    const std::size_t start = data_end_bits(raw);
    const std::size_t avail = size - raw.offset;
    const std::size_t bytes = std::min<std::size_t>(avail, raw.size + 4u);
    if (start / 8 >= bytes) return out;
    BitReader r(data + raw.offset, bytes);
    r.seek_bits(start);
    // References may be relative to the entity's own handle.
    if (pf.entmode == 0) out.owner = r.handle(raw.handle).value;
    for (u32 i = 0; i < pf.reactors && r.ok(); ++i) r.handle(raw.handle);
    if (!pf.xdic_missing) out.extension=r.handle(raw.handle).value;
    if (version <= Version::R2000 && !pf.nolinks) { r.handle(raw.handle); r.handle(raw.handle); }   // prev, next
    if (pf.color_handle) out.color=r.handle(raw.handle).value;
    out.layer = r.handle(raw.handle).value;
    if (pf.ltype_flags == 3) out.linetype=r.handle(raw.handle).value;
    if (version >= Version::R2007 && pf.material_flags == 3) r.handle();
    if (pf.plotstyle_flags == 3) r.handle();
    if (version >= Version::R2010) {
        if (pf.full_vs) r.handle();
        if (pf.face_vs) r.handle();
        if (pf.edge_vs) r.handle();
    }
    for (int i = 0; i < 4100 && r.ok() && !r.exhausted(); ++i) {
        const Handle h = r.handle(raw.handle);
        if (!r.ok()) break;
        out.rest.push_back(h);
    }
    return out;
}

bool read_block_header(const u8* data, std::size_t size, const RawObject& raw, Version version,
                       std::string& name, f64& bx, f64& by, bool& is_xref,
                       bool& overlay, bool& unloaded, std::string& path) {
    const std::size_t end_bytes = (data_end_bits(raw) + 7u) / 8u;
    if (end_bytes > size - raw.offset) return false;
    Strings strings = string_stream(data, size, raw, version);
    BitReader r(data + raw.offset, end_bytes);
    r.seek_bits(raw.data_at);
    r.handle();
    for (int guard = 0; guard < 64; ++guard) {
        const u16 eed_size = r.bs();
        if (eed_size == 0) break;
        r.handle();
        for (u16 i = 0; i < eed_size; ++i) r.rc();
        if (!r.ok()) return false;
    }
    const u32 reactors = r.bl();
    if (reactors > 4096 || !r.ok()) return false;
    if (version >= Version::R2004) r.b();
    if (version >= Version::R2013) r.b();
    name = read_string(r, strings, version);
    if (version <= Version::R2004) { r.b(); r.bs(); r.b(); }   // is_xref_ref, is_xref_resolved, is_xref_dep
    else r.bs();
    r.b();                                  // anonymous
    r.b();                                  // has attributes
    const bool blkisxref = r.b();
    overlay = r.b();
    is_xref = blkisxref || overlay;
    if (version >= Version::R2000) unloaded=r.b(); // zero means loaded
    if (version >= Version::R2004 && !is_xref) r.bl();   // owned object count
    f64 bz;
    r.bd3(bx, by, bz);
    path=read_string(r,strings,version);
    return r.ok() && !name.empty();
}

std::string plain_words(const std::string& in) {
    std::string out;
    for (std::size_t i = 0; i < in.size(); ++i) {
        if (in[i] == '\\' && i + 1 < in.size()) {
            const char c = in[i + 1];
            if (c == 'P') { out += ' '; ++i; continue; }
            if (c == '\\' || c == '{' || c == '}') { out += c; ++i; continue; }
            const std::size_t semi = in.find(';', i);
            if (semi != std::string::npos) { i = semi; continue; }
        }
        if (in[i] == '{' || in[i] == '}') continue;
        out += in[i];
    }
    return out;
}

// The TV length includes the terminating NUL.
std::string read_text(BitReader& r) {
    const u16 len = r.bs();
    if (len > 8192 || !r.ok()) return {};
    std::string out;
    out.reserve(len);
    for (u16 i = 0; i < len; ++i) out.push_back(static_cast<char>(r.rc()));
    while (!out.empty() && out.back() == '\0') out.pop_back();
    return r.ok() ? detail::decode_text(out) : std::string{};
}

bool read_table_prefix(const u8* data, std::size_t size, const RawObject& raw,
                     std::string& name, Version version, BitReader& r, Strings& strings,
                     std::vector<std::pair<u64,u32>>* extended_longs = nullptr) {
    if (!data || raw.offset >= size || raw.data_at >= data_end_bits(raw)) return false;
    strings = string_stream(data, size, raw, version);
    const std::size_t end_bytes = (data_end_bits(raw) + 7u) / 8u;
    if (end_bytes > size - raw.offset) return false;
    r = BitReader(data + raw.offset, end_bytes);
    r.seek_bits(raw.data_at);

    r.handle();
    bool terminated = false;
    for (int guard = 0; guard < 64; ++guard) {
        const u16 eed_size = r.bs();
        if (!r.ok()) return false;
        if (eed_size == 0) { terminated = true; break; }
        const auto app = r.handle(raw.handle);
        if (!r.ok() || r.bit_position() > data_end_bits(raw) ||
            eed_size > (data_end_bits(raw) - r.bit_position()) / 8) return false;
        // AcCmTransparency holds exactly one 1071 integer.
        if (extended_longs && eed_size == 5) {
            const auto code = r.rc();
            const auto value = r.rl();
            if (code == 71 && app.value) extended_longs->emplace_back(app.value, value);
        } else {
            r.seek_bits(r.bit_position() + std::size_t(eed_size) * 8);
        }
    }
    if (!terminated) return false;
    const u32 reactors = r.bl();
    if (reactors > 4096 || !r.ok()) return false;
    if (version >= Version::R2004) r.b();   // xdictionary missing flag
    if (version >= Version::R2013) r.b();

    name = read_string(r, strings, version);
    if (name.empty() || !r.ok()) return false;
    return strings.r.ok() && r.bit_position() <= data_end_bits(raw);
}

bool read_layer_name(const u8* data, std::size_t size, const RawObject& raw,
                     std::string& name, Version version, gdoc::Layer* layer = nullptr,
                     std::vector<std::pair<u64,u32>>* extended_longs = nullptr) {
    BitReader r; Strings strings;
    if (!read_table_prefix(data,size,raw,name,version,r,strings,extended_longs)) return false;
    if (layer) {
        // Layer flags: frozen 1, off 2, locked 8.
        layer->name = name;
        if (version >= Version::R2000) {
            if (version <= Version::R2004) { r.b(); r.bs(); r.b(); } else r.bs();   // xref flags
            const u16 flags = r.bs();
            layer->frozen = (flags & 1) != 0;
            layer->on = (flags & 2) == 0;
            layer->locked = (flags & 8) != 0;
            layer->plottable = (flags & 16) != 0;
            layer->lineweight = lineweight_from_code(u8((flags >> 5) & 31));
            const u16 index = r.bs();
            if (version >= Version::R2004) {
                const u32 rgb = r.bl();
                const u8 byte = r.rc();
                if (byte & 1) read_string(r, strings, version);
                if (byte & 2) read_string(r, strings, version);
                if (rgb & 0xFF000000u) layer->color = gdoc::Color::rgb(static_cast<u8>(rgb >> 16), static_cast<u8>(rgb >> 8), static_cast<u8>(rgb));
                else if (index > 0 && index < 256) layer->color = gdoc::Color::aci(index);
            } else if (index > 0 && index < 256) {
                layer->color = gdoc::Color::aci(index);
            }
            if (!r.ok()) return false;
        }
    }
    return r.ok() && strings.r.ok() && r.bit_position() <= data_end_bits(raw);
}

// The layer is the absolute hard pointer; prev/next are relative.
u64 layer_handle_of(const u8* data, std::size_t size, const RawObject& raw) {
    const std::size_t start = data_end_bits(raw);
    const std::size_t avail = size - raw.offset;
    const std::size_t bytes = std::min<std::size_t>(avail, raw.size + 4u);
    if (start / 8 >= bytes) return 0;

    BitReader r(data + raw.offset, bytes);
    r.seek_bits(start);
    u64 layer = 0;
    for (int i = 0; i < 8; ++i) {
        const Handle h = r.handle();
        if (!r.ok()) break;
        if (h.code == 5) layer = h.value;      // hard pointer: the layer
    }
    return layer;
}

}

bool read_dimension_association(const u8* data, std::size_t size, const RawObject& raw,
                                Version version, DimensionAssociation& out) {
    if(version<Version::R2000 || version>Version::R2018 || !data || raw.offset>=size) return false;
    const auto end=data_end_bits(raw), object_end=raw.body_at+std::size_t(raw.size)*8;
    if(raw.data_at>=end || end>object_end || (object_end+7)/8>size-raw.offset) return false;
    BitReader r(data+raw.offset,(end+7)/8), h(data+raw.offset,(object_end+7)/8);
    r.seek_bits(raw.data_at);h.seek_bits(end);
    if(r.handle().value!=raw.handle)return false;
    bool terminated=false;
    for(unsigned i=0;i<64 && r.ok();++i) {
        const auto bytes=r.bs();if(!bytes){terminated=true;break;}
        r.handle(raw.handle);
        if(r.bit_position()>end || bytes>(end-r.bit_position())/8)return false;
        r.seek_bits(r.bit_position()+std::size_t(bytes)*8);
    }
    if(!terminated)return false;
    const auto reactors=r.bl();if(reactors>4096)return false;
    const bool missing=version>=Version::R2004?r.b():false;
    if(version>=Version::R2013 && r.b())return false;
    DimensionAssociation result;result.handle=raw.handle;
    result.owner=h.handle(raw.handle).value;
    for(u32 i=0;i<reactors && h.ok();++i)h.handle(raw.handle);
    if(!missing)h.handle(raw.handle);
    auto strings=string_stream(data,size,raw,version);
    const auto numeric_end=version>=Version::R2007?(strings.present?strings.r.bit_position():end-1):end;
    std::size_t strings_end=end;
    if(strings.present) {
        BitReader footer(data+raw.offset,(end+7)/8);footer.seek_bits(end-17);
        strings_end=end-((footer.rs()&0x8000)?33:17);
        if(numeric_end<raw.data_at || numeric_end>strings_end)return false;
    }
    result.flags=r.bl();result.trans_space=r.b();result.rotated_type=r.rc();
    result.dimension=h.handle(raw.handle).value;
    if(result.flags>15 || !result.dimension)return false;
    const auto handles=[&](std::vector<u64>& values) {
        const auto count=r.bl();if(count>4096)return false;
        for(u32 i=0;i<count && h.ok();++i)values.push_back(h.handle(raw.handle).value);
        return h.ok() && h.bit_position()<=object_end;
    };
    const auto paths=[&](std::vector<std::string>& values) {
        const auto count=r.bl();if(count>4096)return false;
        for(u32 i=0;i<count && r.ok();++i)values.push_back(read_string(r,strings,version));
        return r.ok() && strings.r.ok();
    };
    bool last=false;
    for(u8 i=0;i<6;++i) {
        if(!(result.flags&(1u<<i)) && !last)continue;
        DimensionAssociation::PointRef p;p.index=i;
        if(read_string(r,strings,version)!="AcDbOsnapPointRef")return false;
        p.osnap=r.rc();if(p.osnap>13 || !handles(p.objects))return false;
        if(p.osnap) {
            p.subentity=r.bl();p.marker=r.bl();if(!paths(p.paths))return false;
        }
        p.parameter=r.bd();r.bd3(p.x,p.y,p.z);
        if(p.osnap==6 || p.osnap==11) {
            if(!handles(p.intersection_objects))return false;
            p.intersection_subentity=r.bl();p.intersection_marker=r.bl();
            if(!paths(p.intersection_paths))return false;
        }
        last=p.last_point=r.b();
        if(!r.ok() || !std::isfinite(p.parameter) || !std::isfinite(p.x) ||
           !std::isfinite(p.y) || !std::isfinite(p.z))return false;
        result.points.push_back(std::move(p));
    }
    // Require the numeric stream to end exactly at its declared boundary.
    if(last || !r.ok() || !h.ok() || !strings.r.ok() || r.bit_position()!=numeric_end ||
       (strings.present && strings.r.bit_position()!=strings_end) || h.bit_position()>object_end)return false;
    out=std::move(result);return true;
}

bool read_dimension_style(const u8* data, std::size_t size, const RawObject& raw,
                          Version version, gdim::Style& out) {
    if(version<Version::R2000 || version>Version::R2018) return false;
    BitReader r; Strings strings; gdim::Style style;
    if(!read_table_prefix(data,size,raw,style.name,version,r,strings)) return false;
    if(version<=Version::R2004) {r.b();r.bs();r.b();} else r.bs();
    const auto text=[&](){return read_string(r,strings,version);};
    const auto colour=[&](){read_cmc(r,strings,version);};
    const auto post=text();text();
    const auto marker=post.find("<>");
    if(marker==std::string::npos)style.prefix=post;
    else {style.prefix=post.substr(0,marker);style.suffix=post.substr(marker+2);}
    style.dimscale=r.bd();style.dimasz=r.bd();style.dimexo=r.bd();r.bd();
    style.dimexe=r.bd();r.bd();r.bd();style.dimtp=r.bd();style.dimtm=r.bd();
    if(version>=Version::R2007) {r.bd();r.bd();r.bs();colour();} // fixed extensions, jog, fill
    style.dimtol=r.b();r.b();style.dimtih=r.b();style.dimtoh=r.b();
    style.dimse1=r.b();style.dimse2=r.b();
    const auto placement=r.bs();
    style.text_pos=placement==0?gdim::TextPos::Centred:placement==4?gdim::TextPos::Below:gdim::TextPos::Above;
    const auto zeros=r.bs();style.dimzin_leading=zeros&4;style.dimzin_trailing=zeros&8;r.bs();
    if(version>=Version::R2007)r.bs(); // arc symbol
    style.dimtxt=r.bd();r.bd();const auto tick=r.bd();
    if(tick>0) {style.arrow=gdim::ArrowType::Tick;style.dimasz=tick;}
    r.bd();style.dimlfac=r.bd();style.dimtvp=r.bd();r.bd();style.dimgap=r.bd();r.bd();
    r.b();r.bs();r.b();r.b();r.b();r.b(); // alternate units and fit flags
    colour();colour();colour();
    r.bs(); // angular precision is distinct from linear DIMDEC
    style.dimdec=r.bs();r.bs();r.bs();r.bs();r.bs();r.bs();
    const auto format=r.bs();
    switch(format) {
        case 1:style.format=gunit::Style::Scientific;break;
        case 2:style.format=gunit::Style::Decimal;break;
        case 3:style.format=gunit::Style::Engineering;break;
        case 4:style.format=gunit::Style::Architectural;break;
        case 5:style.format=gunit::Style::Fractional;break;
        default:return false;
    }
    r.bs();r.bs();r.bs(); // decimal separator, movement, justification
    style.dimsd1=r.b();style.dimsd2=r.b();
    r.bs();r.bs();r.bs();r.bs();r.b();r.bs(); // tolerances and fit
    if(version>=Version::R2007)r.b();
    if(version>=Version::R2010) {r.b();r.bd();text();r.bd();text();}
    r.bs();r.bs();r.b(); // dimension/extension lineweights and table flag
    if(!r.ok() || !strings.r.ok() || r.bit_position()>data_end_bits(raw))return false;
    for(double value : {style.dimscale,style.dimasz,style.dimexo,style.dimexe,
                        style.dimtp,style.dimtm,style.dimtxt,style.dimlfac,style.dimtvp,style.dimgap})
        if(!std::isfinite(value))return false;
    if(style.dimscale<=0 || style.dimtxt<=0 || style.dimasz<0 || style.dimexo<0 ||
       style.dimexe<0 || style.dimlfac<=0 || style.dimdec>8)return false;
    out=std::move(style);
    return true;
}

bool read_text_style(const u8* data, std::size_t size, const RawObject& raw,
                     Version version, gtext::TextStyleDef& out) {
    if (version < Version::R2000 || version > Version::R2018) return false;
    BitReader r; Strings strings; gtext::TextStyleDef style;
    if (!read_table_prefix(data,size,raw,style.name,version,r,strings)) return false;
    if (version <= Version::R2004) { r.b(); r.bs(); r.b(); } else r.bs();
    const bool shape_file = r.b();
    style.vertical = r.b();
    style.height = float(r.bd());
    style.width_factor = float(r.bd());
    style.oblique = float(r.bd()*180/3.141592653589793);
    const auto generation = r.rc();
    style.backwards = generation & 2;
    style.upside_down = generation & 4;
    r.bd(); // last-used height
    style.font = read_string(r,strings,version);
    style.big_font = read_string(r,strings,version);
    // Shape-file records describe SHAPE resources, not text styles.
    if (shape_file || !r.ok() || !strings.r.ok() || r.bit_position() > data_end_bits(raw) ||
        !std::isfinite(style.height) || style.height < 0 ||
        !std::isfinite(style.width_factor) || style.width_factor <= 0 ||
        !std::isfinite(style.oblique) || std::abs(style.oblique) >= 85) return false;
    out = std::move(style);
    return true;
}

bool read_layer_appearance(const u8* data, std::size_t size, const RawObject& raw,
                           Version version, LayerAppearance& out) {
    if (version < Version::R2000 || version > Version::R2018) return false;
    LayerAppearance decoded;
    std::string name;
    if (!read_layer_name(data,size,raw,name,version,&decoded.layer,&decoded.extended_longs)) return false;
    out = std::move(decoded);
    return true;
}

bool read_entity_appearance(const u8* data,std::size_t size,const RawObject& raw,
                           Version version,EntityAppearance& out) {
    if(!data || raw.offset>=size || version<Version::R2000 || version>Version::R2018)return false;
    const auto end=(data_end_bits(raw)+7)/8;
    if(end>size-raw.offset || raw.data_at>=data_end_bits(raw))return false;
    BitReader r(data+raw.offset,end);r.seek_bits(raw.data_at);
    Prefix prefix;
    if(!read_common_entity_data(r,version,&prefix) || r.bit_position()>data_end_bits(raw))return false;
    const auto handles=entity_handles(data,size,raw,prefix,version);
    if(!handles.layer)return false;
    out={prefix.attrs,handles.layer,handles.color,handles.linetype};
    return true;
}

ReadResult read(const u8* data, std::size_t size, const gcm::WorkingVolume& v) {
    ReadResult out;

    if (!read_header(data, size, out.header, out.error)) return out;

    R2004Sections sections;
    const u8* obj_data = data;
    std::size_t obj_size = size;
    bool model_active=true; u64 current_paper=0;
    if (out.header.version >= Version::R2004) {
        // Only 2007 uses Reed-Solomon; 2010+ use 2004 pages with 2007 object encoding.
        const bool ok = out.header.version == Version::R2007 ? read_r2007_sections(data, size, sections, out.error)
                                                               : read_r2004_sections(data, size, sections, out.error);
        if (!ok) return out;
        if (!parse_object_map(sections.handles.data(), sections.handles.size(), out.objects, out.error))
            return out;
        if (const auto iu = read_insunits(sections.header.data(), sections.header.size(), out.header,&model_active,&current_paper))
            out.insunits = *iu;
        obj_data = sections.objects.data();
        obj_size = sections.objects.size();
    } else if (!read_object_map(data, size, out.header, out.objects, out.error)) {
        return out;
    } else if (const SectionLocator* hv = out.header.section(0)) {
        if (hv->seeker < size && hv->size <= size - hv->seeker)
            if (const auto iu = read_insunits(data + hv->seeker, hv->size, out.header,&model_active,&current_paper))
                out.insunits = *iu;
    }
    const Version version = out.header.version;
    if (version >= Version::R2004) {
        read_classes(sections.classes.data(), sections.classes.size(), out.header, out.classes);
    } else if (const SectionLocator* cls = out.header.section(1)) {
        if (cls->seeker < size && cls->size <= size - cls->seeker)
            read_classes(data + cls->seeker, cls->size, out.header, out.classes);
    }
    u16 multileader_type = 0, hatch_type=0, spatial_type=0, layout_type=0, dimassoc_type=0;
    for(const auto& [num,name]:out.classes) if(name=="DIMASSOC") dimassoc_type=num;
    for(const auto& [num,name]:out.classes) if(name=="LAYOUT") layout_type=num;
    for(const auto& [num,name]:out.classes) if(name=="SPATIAL_FILTER") spatial_type=num;
    for(const auto& [num,name]:out.classes) if(name=="HATCH") hatch_type=num;
    for (const auto& [num, name] : out.classes) if (name == "MULTILEADER") multileader_type = num;
    std::unordered_map<u16,gdoc::UnderlayKind> underlay_types,underlay_definitions;
    for(const auto& [num,name]:out.classes) {
        const auto kind=name.starts_with("PDF")?gdoc::UnderlayKind::Pdf:name.starts_with("DWF")?gdoc::UnderlayKind::Dwf:name.starts_with("DGN")?gdoc::UnderlayKind::Dgn:gdoc::UnderlayKind::Unknown;
        if(kind==gdoc::UnderlayKind::Unknown)continue;
        if(name.ends_with("UNDERLAY"))underlay_types[num]=kind;
        if(name.ends_with("DEFINITION"))underlay_definitions[num]=kind;
    }
    data = obj_data;
    size = obj_size;

    std::unordered_map<u64, std::string> layer_names, app_names;
    std::unordered_map<u64,std::vector<std::pair<u64,u32>>> layer_eed;
    std::unordered_map<u64,std::string> style_names,dimstyle_names;
    std::unordered_map<u64,gdoc::Attributes> entity_attributes;
    std::unordered_map<u64,gdim::Style> raw_dimstyles;
    struct BlockInfo { std::string name; f64 bx{0}, by{0}; bool xref{false}, overlay{false}, unloaded{false}; std::string path; };
    std::unordered_map<u64, BlockInfo> block_headers;
    struct DictionaryInfo { std::unordered_map<std::string,u64> entries; u64 owner{0}; };
    struct ClipInfo { std::vector<gcm::Lattice2> points; bool enabled{true}; u64 owner{0}; };
    std::unordered_map<u64,DictionaryInfo> dictionaries;
    std::unordered_map<u64,ClipInfo> clips;

    struct Found {
        Entity  entity;
        u64     owner{0};
        u8      entmode{2};
    };
    struct FoundText {
        Text text;
        u64  owner{0};
        u8   entmode{2};
    };
    struct FoundInsert {
        u64 block{0}, layer{0}, owner{0}, handle{0}, extension{0};
        u8  entmode{2};
        f64 x{0}, y{0}, sx{1}, sy{1}, rot{0};
    };
    struct FoundDim {
        DimensionRec rec;
        u64          owner{0};
        u8           entmode{2};
    };
    struct UnderlayDefinition {std::string path,sheet;gdoc::UnderlayKind kind;};
    struct FoundUnderlay {gdoc::Underlay underlay;u64 definition{},owner{},layer{};u8 entmode{2};};
    std::unordered_map<u64,UnderlayDefinition> underlay_defs;
    std::vector<FoundUnderlay> found_underlays;
    std::vector<Found> found;
    std::vector<FoundText> found_texts;
    std::vector<std::pair<u64,gdoc::AttributeDef>> found_attdefs;
    struct FoundAttrib { u64 owner{0}, handle{0}; std::string tag; Text text; };
    std::vector<FoundAttrib> found_attributes;
    std::vector<FoundInsert> inserts;
    std::vector<FoundDim> found_dims;
    struct FoundPoly { u64 handle{0}; bool closed{false}; Entity proto; u64 owner{0}; u8 entmode{2}; };
    struct FoundVert { u64 owner{0}; u64 seq_poly{0}; f64 x{0}, y{0}; u8 flag{0}; };
    u64 running_poly = 0;   // through R2000, vertices follow their polyline in the stream
    std::vector<FoundPoly> found_polys;
    std::vector<FoundVert> found_verts;
    struct FoundHatch { HatchRec rec; u64 owner{0}; u8 entmode{2}; };
    std::vector<FoundHatch> found_hatches;
    struct FoundLayout { gdxf::Layout sheet; u64 block{0}, active_viewport{0}; u32 order{0}; double paper_unit{.001}; };
    std::vector<FoundLayout> found_layouts;
    struct FoundViewport { gplot::Viewport vp; u64 owner{0}, handle{0}, clip{0}; u8 entmode{0}; std::vector<u64> frozen; };
    std::vector<FoundViewport> found_viewports;
    struct FoundSpline { ggeo::NurbsCurve2<gcm::F64> curve; u64 owner; u8 entmode; };
    std::vector<FoundSpline> found_splines;
    std::unordered_map<u64,std::vector<gplot::PaperPoint>> clip_polygons;


    for (const ObjectRef& ref : out.objects) {
        RawObject raw;
        if (!read_object_header(data, size, ref, raw, version)) {
            out.skipped.push_back("object " + std::to_string(ref.handle) + " could not be read");
            continue;
        }
        if (object_type_name(raw.type)[0] == '\0' && !(multileader_type && raw.type == multileader_type) && !(hatch_type && raw.type==hatch_type) && !(spatial_type && raw.type==spatial_type) && !(layout_type && raw.type==layout_type) && !(dimassoc_type && raw.type==dimassoc_type) && raw.type!=34 && !underlay_types.contains(raw.type) && !underlay_definitions.contains(raw.type)) ++out.unknown_objects;
        out.raw.push_back(raw);

        if(dimassoc_type && raw.type==dimassoc_type) {
            DimensionAssociation association;
            if(read_dimension_association(data,size,raw,version,association))
                out.dimension_associations.push_back(std::move(association));
            else out.skipped.push_back("dimension association " + std::to_string(raw.handle) + " could not be read");
            continue;
        }
        if(underlay_definitions.contains(raw.type)) {
            const auto end=data_end_bits(raw);if((end+7)/8>size-raw.offset)continue;
            BitReader r(data+raw.offset,(end+7)/8);r.seek_bits(raw.data_at);r.handle();
            for(int guard=0;guard<64;++guard){const auto count=r.bs();if(!count)break;r.handle();for(u16 i=0;i<count;++i)r.rc();}
            const auto reactors=r.bl();if(reactors>4096||!r.ok())continue;
            if(version>=Version::R2004)r.b();if(version>=Version::R2013)r.b();
            auto strings=string_stream(data,size,raw,version);
            auto path=read_string(r,strings,version),sheet=read_string(r,strings,version);
            if(r.ok()&&!path.empty())underlay_defs.emplace(raw.handle,UnderlayDefinition{std::move(path),std::move(sheet),underlay_definitions.at(raw.type)});
            continue;
        }
        if(layout_type && raw.type==layout_type) {
            const auto end=data_end_bits(raw);
            if((end+7)/8>size-raw.offset) continue;
            BitReader r(data+raw.offset,(end+7)/8); r.seek_bits(raw.data_at); r.handle();
            for(int guard=0;guard<64;++guard) { const auto n=r.bs(); if(!n) break; r.handle(); for(u16 i=0;i<n;++i) r.rc(); }
            const auto reactors=r.bl(); if(reactors>4096 || !r.ok()) continue;
            const bool missing=version>=Version::R2004?r.b():false;
            if(version>=Version::R2013) r.b();
            auto strings=string_stream(data,size,raw,version);
            const auto text=[&] { return read_string(r,strings,version); };
            const auto p3=[&] { double x,y,z; r.bd3(x,y,z); };
            const auto p2=[&] { double x,y; r.rd2(x,y); };
            FoundLayout f; auto& l=f.sheet;
            text(); text(); r.bs();
            l.margin_left=r.bd()*.001; l.margin_bottom=r.bd()*.001; l.margin_right=r.bd()*.001; l.margin_top=r.bd()*.001;
            l.width=r.bd()*.001; l.height=r.bd()*.001; l.paper_name=text();
            r.bd(); r.bd(); const auto units=r.bs(),rotation=r.bs(); r.bs();
            f.paper_unit=units==0?.0254:.001;
            r.bd(); r.bd(); r.bd(); r.bd();
            if(version<=Version::R2000) text();
            r.bd(); r.bd(); l.plot_style_table=text(); r.bs(); r.bd(); r.bd(); r.bd();
            if(version>=Version::R2004) { r.bs(); r.bs(); r.bs(); }
            l.name=text(); f.order=r.bl(); r.bs(); p3(); p2(); p2(); p3(); p3(); p3(); r.bd(); r.bs(); p3(); p3();
            if(version>=Version::R2004) r.rl();
            BitReader h(data+raw.offset,std::min<std::size_t>(size-raw.offset,raw.size+4u)); h.seek_bits(end);
            h.handle(raw.handle); for(u32 i=0;i<reactors;++i) h.handle(raw.handle); if(!missing) h.handle(raw.handle);
            if(version>=Version::R2004) h.handle(raw.handle); if(version>=Version::R2007) h.handle(raw.handle);
            f.block=h.handle(raw.handle).value; f.active_viewport=h.handle(raw.handle).value;
            if(rotation==1 || rotation==3) { std::swap(l.width,l.height); std::swap(l.margin_left,l.margin_bottom); std::swap(l.margin_right,l.margin_top); }
            if(r.ok() && h.ok() && f.block && !l.name.empty() && l.name!="Model" && std::isfinite(l.width) && std::isfinite(l.height) && l.width>0 && l.height>0) found_layouts.push_back(std::move(f));
            continue;
        }

        if(raw.type==42 || (spatial_type && raw.type==spatial_type)) {
            const auto end=data_end_bits(raw);
            if((end+7)/8>size-raw.offset) continue;
            BitReader r(data+raw.offset,(end+7)/8); r.seek_bits(raw.data_at); r.handle();
            for(int guard=0;guard<64;++guard) {
                const auto n=r.bs(); if(!n) break; r.handle(); for(u16 i=0;i<n;++i) r.rc();
            }
            const auto reactors=r.bl(); if(reactors>4096 || !r.ok()) continue;
            const bool missing=version>=Version::R2004?r.b():false;
            if(version>=Version::R2013) r.b();
            BitReader handles(data+raw.offset,std::min<std::size_t>(size-raw.offset,raw.size+4u)); handles.seek_bits(end);
            const auto owner=handles.handle(raw.handle).value;
            for(u32 i=0;i<reactors;++i) handles.handle(raw.handle);
            const auto extension=missing?0:handles.handle(raw.handle).value;
            if(raw.type==42) {
                const auto count=r.bl(); if(count>4096) continue;
                r.bs(); r.rc(); auto strings=string_stream(data,size,raw,version);
                DictionaryInfo dict; dict.owner=owner;
                for(u32 i=0;i<count && r.ok() && handles.ok();++i) {
                    auto name=read_string(r,strings,version);
                    dict.entries.emplace(std::move(name),handles.handle(raw.handle).value);
                }
                if(r.ok() && handles.ok() && !extension) dictionaries.emplace(raw.handle,std::move(dict));
            } else {
                const auto count=r.bs(); if(count<2 || count>4096) continue;
                std::vector<std::pair<double,double>> points;
                for(u16 i=0;i<count;++i) { double x,y; r.rd2(x,y); points.emplace_back(x,y); }
                double nx,ny,nz,ox,oy,oz; r.bd3(nx,ny,nz); r.bd3(ox,oy,oz);
                const auto enabled=r.bs(),front=r.bs(); if(front) r.bd(); const auto back=r.bs(); if(back) r.bd();
                double inverse[12],transform[12]; for(auto& d:inverse) d=r.bd(); for(auto& d:transform) d=r.bd();
                if(!r.ok() || !handles.ok() || extension || front || back || enabled>1 || nx!=0 || ny!=0 || nz!=1 || ox!=0 || oy!=0 || oz!=0) continue;
                bool planar=true;
                for(int i=0;i<12;++i) if(!std::isfinite(inverse[i]) || transform[i]!=(i==0||i==4||i==8?1:0)) planar=false;
                if(!planar || inverse[2]!=0 || inverse[5]!=0 || inverse[11]!=0) continue;
                if(points.size()==2 && (inverse[1]!=0 || inverse[3]!=0)) {
                    const auto a=points[0],b=points[1]; points={a,{b.first,a.second},b,{a.first,b.second}};
                }
                ClipInfo clip; clip.enabled=enabled; clip.owner=owner;
                for(const auto& [x,y]:points) {
                    const auto px=x*inverse[0]+y*inverse[3]+inverse[9],py=x*inverse[1]+y*inverse[4]+inverse[10];
                    if(!std::isfinite(px) || !std::isfinite(py)) { planar=false; break; }
                    clip.points.push_back(gcm::to_lattice(v,px,py));
                }
                if(planar && detail::valid_clip(clip.points)) clips.emplace(raw.handle,std::move(clip));
            }
            continue;
        }

        const ObjectType type = hatch_type&&raw.type==hatch_type?ObjectType::Hatch:static_cast<ObjectType>(raw.type);
        if (type != ObjectType::Vertex2D && type != ObjectType::Vertex3D &&
            type != ObjectType::Polyline2D && type != ObjectType::Polyline3D)
            running_poly = 0;
        if(raw.type==53) {
            gtext::TextStyleDef style;
            if(read_text_style(data,size,raw,version,style)) {
                const auto unit=gdxf::metres_per(static_cast<gdxf::Units>(out.insunits));
                style.height*=float(unit>0?unit:1);
                if(std::isfinite(style.height)) {
                    style_names[raw.handle]=style.name;
                    out.text_styles.push_back(std::move(style));
                }
            } else out.skipped.push_back("text style " + std::to_string(raw.handle) + " is unsupported or could not be read");
            continue;
        }
        if(raw.type==69) {
            gdim::Style style;
            if(read_dimension_style(data,size,raw,version,style)) {
                dimstyle_names[raw.handle]=style.name;
                raw_dimstyles.emplace(raw.handle,std::move(style));
            } else out.skipped.push_back("dimension style " + std::to_string(raw.handle) + " is unsupported or could not be read");
            continue;
        }
        if (raw.type == 67) { // APPID has the same common table-name prefix.
            std::string name;
            if (read_layer_name(data,size,raw,name,version)) app_names[raw.handle] = std::move(name);
            continue;
        }
        if (type == ObjectType::Layer) {
            LayerAppearance appearance;
            if (read_layer_appearance(data,size,raw,version,appearance)) {
                layer_names[raw.handle] = appearance.layer.name;
                layer_eed[raw.handle] = std::move(appearance.extended_longs);
                out.layers.add(std::move(appearance.layer));
            } else {
                out.skipped.push_back("layer " + std::to_string(raw.handle) + " could not be read");
            }
            continue;
        }
        if (type == ObjectType::BlockHeader) {
            BlockInfo bi;
            if (read_block_header(data, size, raw, version, bi.name, bi.bx, bi.by, bi.xref, bi.overlay, bi.unloaded, bi.path))
                block_headers[raw.handle] = bi;
            continue;
        }

        // Bounded by the data end, not the object length; handles follow.
        const std::size_t data_end_bytes = (data_end_bits(raw) + 7u) / 8u;
        if (data_end_bytes > size - raw.offset) continue;
        BitReader r(data + raw.offset, data_end_bytes);
        r.seek_bits(raw.data_at);
        Prefix pf;
        if (!read_common_entity_data(r, version, &pf)) continue;
        entity_attributes[raw.handle]=pf.attrs;
        const EntityHandles hs = entity_handles(data, size, raw, pf, version);
        const u64 underlay_definition=underlay_types.contains(raw.type)&&!hs.rest.empty()?hs.rest.front().value:0;
        Strings strings = string_stream(data, size, raw, version);
        const auto place_layer = [&](auto& item) {
            item.layer_handle = hs.layer;
        };

        if(raw.type==34 && version>=Version::R2000) {
            FoundViewport f; f.owner=hs.owner; f.entmode=pf.entmode; f.handle=raw.handle;
            double x,y,z,tx,ty,tz,nx,ny,nz; r.bd3(x,y,z); const double width=r.bd(),height=r.bd();
            r.bd3(tx,ty,tz); r.bd3(nx,ny,nz); f.vp.rotation=r.bd(); const double view_height=r.bd();
            r.bd(); r.bd(); r.bd(); r.bd(); double cx,cy; r.rd2(cx,cy);
            double a,b; for(int i=0;i<3;++i) r.rd2(a,b); r.bs(); if(version>=Version::R2007) r.bs();
            const auto frozen=r.bl(),flags=r.bl();
            if(!r.ok() || frozen>4096 || hs.rest.size()<frozen+1 || !std::isfinite(width) || !std::isfinite(height) || width<=0 || height<=0 || !std::isfinite(view_height) || view_height<=0 || !std::isfinite(f.vp.rotation)) continue;
            if(std::abs(nx)>1e-9 || std::abs(ny)>1e-9 || nz<=0 || (flags&7)) { out.skipped.push_back("3D or perspective paper viewport cannot be represented in a 2D layout"); continue; }
            f.vp.lo={x-width*.5,y-height*.5}; f.vp.hi={x+width*.5,y+height*.5};
            f.vp.centre_x=tx+cx*std::cos(f.vp.rotation)-cy*std::sin(f.vp.rotation);
            f.vp.centre_y=ty+cx*std::sin(f.vp.rotation)+cy*std::cos(f.vp.rotation);
            f.vp.rotation=-f.vp.rotation;
            f.vp.scale=view_height/height; f.vp.on=!(flags&0x20000); f.vp.locked=flags&0x4000;
            for(u32 i=0;i<frozen;++i) f.frozen.push_back(hs.rest[i].value);
            if(flags&0x10000) f.clip=hs.rest[frozen].value;
            found_viewports.push_back(std::move(f)); continue;
        }

        if(underlay_types.contains(raw.type)) {
            FoundUnderlay item;item.definition=underlay_definition;item.owner=hs.owner;item.layer=hs.layer;item.entmode=pf.entmode;
            auto& u=item.underlay;u.kind=underlay_types.at(raw.type);u.name="Underlay_"+std::to_string(raw.handle);
            double nx,ny,nz,x,y,z,sx,sy,sz;r.bd3(nx,ny,nz);r.bd3(x,y,z);u.rotation=r.bd();r.bd3(sx,sy,sz);
            const auto flags=r.rc();u.contrast=r.rc();u.contrast_neutral=100;u.fade=r.rc();const auto count=r.bl();
            if(!r.ok()||count>4096)continue;
            u.source_unit=u.kind==gdoc::UnderlayKind::Pdf?.0254:1.;u.source_unit_known=true;u.source_coordinates_pending=u.kind!=gdoc::UnderlayKind::Pdf;
            for(u32 i=0;i<count;++i){double px,py;r.rd2(px,py);u.clip.push_back(gcm::to_lattice(v,px*u.source_unit,py*u.source_unit));}
            if((flags&16)&&r.bit_position()+2<=data_end_bits(raw)){const auto holes=r.bs();if(holes>4096)continue;for(u16 i=0;i<holes;++i){double px,py;r.rd2(px,py);u.clip_hole.push_back(gcm::to_lattice(v,px*u.source_unit,py*u.source_unit));}}
            if(!r.ok()||!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(sx)||!std::isfinite(sy)||!std::isfinite(u.rotation)||sx==0||sy==0)continue;
            if(nx!=0||ny!=0||nz!=1||z!=0){out.skipped.push_back("non-planar underlay placement");continue;}
            if(u.clip.size()==2){const auto a=u.clip[0],b=u.clip[1];u.clip={a,{b.x,a.y},b,{a.x,b.y}};}
            u.insertion=gcm::to_lattice(v,x,y);u.scale=sx/u.source_unit;u.y_factor=sy/sx;
            u.clip_enabled=flags&1;u.visible=(flags&2)&&pf.attrs.visible;u.monochrome=flags&4;u.adjust_background=flags&8;u.plottable=true;
            found_underlays.push_back(std::move(item));continue;
        }
        if (type == ObjectType::Polyline2D || type == ObjectType::Polyline3D) {
            bool closed = false;
            if (type == ObjectType::Polyline2D) {
                const u16 flag = r.bs();
                r.bs();                                 // curve type
                r.bd(); r.bd();                         // start and end width
                r.bt();                                 // thickness
                r.bd();                                 // elevation
                closed = (flag & 1) != 0;
            } else {
                r.rc();                                 // curve type
                closed = (r.rc() & 1) != 0;
            }
            if (!r.ok()) continue;
            FoundPoly fp;
            fp.handle = raw.handle;
            fp.closed = closed;
            fp.proto.handle = raw.handle;
            fp.proto.kind = object_type_name(raw.type);
            fp.proto.layer_handle = hs.layer;
            fp.owner = hs.owner;
            fp.entmode = pf.entmode;
            running_poly = raw.handle;
            found_polys.push_back(std::move(fp));
            continue;
        }
        if (type == ObjectType::Vertex2D || type == ObjectType::Vertex3D) {
            const u8 flag = r.rc();
            f64 x, y, z;
            r.bd3(x, y, z);
            if (r.ok()) found_verts.push_back({hs.owner, running_poly, x, y, flag});
            continue;
        }
        if(type==ObjectType::Spline) {
            const auto scenario=r.bl();
            if(version>=Version::R2013) { r.bl(); r.bl(); }
            const auto degree=r.bl();
            if(scenario!=1||degree<1||degree>32) { out.skipped.push_back("unsupported spline parameterization"); continue; }
            r.b(); r.b(); r.b(); r.bd(); r.bd();
            const auto knots=r.bl(),controls=r.bl(); const bool weighted=r.b();
            if(knots>1000000||controls>1000000||knots!=controls+degree+1) continue;
            ggeo::NurbsCurve2<gcm::F64> curve; curve.degree=int(degree);
            for(u32 i=0;i<knots;++i) curve.knots.knots.push_back(r.bd());
            for(u32 i=0;i<controls;++i) { double x,y,z; r.bd3(x,y,z); double w=weighted?r.bd():1; curve.control.push_back({ggeo::d2::Point<gcm::F64>::from(x,y),gcm::F64(w)}); }
            if(!r.ok()||!curve.valid()) continue;
            found_splines.push_back({std::move(curve),hs.owner,pf.entmode});
            continue;
        }
        if (type == ObjectType::Hatch) {
            if (version >= Version::R2004) {
                r.bl(); r.bl();                         // gradient flag, reserved
                r.bd(); r.bd();                         // gradient angle, shift
                r.bl(); r.bd();                         // single colour, tint
                const u32 ncol = r.bl();
                if (ncol > 1000 || !r.ok()) continue;
                for (u32 i = 0; i < ncol; ++i) { r.bd(); read_cmc(r, strings, version); }
                read_string(r, strings, version);       // gradient name
            }
            r.bd();                                     // elevation
            { f64 ex, ey, ez; r.bd3(ex, ey, ez); }
            FoundHatch fh;
            HatchRec& h = fh.rec;
            h.pattern = read_string(r, strings, version);
            h.solid = r.b();
            r.b();                                      // associative
            const u32 npaths = r.bl();
            if (npaths == 0 || npaths > 1024 || !r.ok()) continue;
            bool bad = false, has_derived = false;
            std::vector<std::pair<f64, f64>> first_loop;
            const auto push_seg = [&](f64 ax, f64 ay, f64 bx, f64 by, bool first_path) {
                gsnap::Shape sgt;
                sgt.kind = gsnap::ShapeKind::Segment;
                sgt.a = gcm::to_lattice(v, ax, ay);
                sgt.b = gcm::to_lattice(v, bx, by);
                if (sgt.a.x != sgt.b.x || sgt.a.y != sgt.b.y) h.boundary.push_back(sgt);
                if (first_path) first_loop.push_back({ax, ay});
            };
            for (u32 pi = 0; pi < npaths && r.ok() && !bad; ++pi) {
                const u32 pflag = r.bl();
                has_derived = has_derived || (pflag & 4);
                if (!(pflag & 2)) {
                    const u32 nsegs = r.bl();
                    if (nsegs > 4096) { bad = true; break; }
                    f64 px = 0, py = 0; bool have_prev = false;
                    for (u32 k = 0; k < nsegs && r.ok(); ++k) {
                        const u8 ct = r.rc();
                        if (ct == 1) {
                            f64 ax, ay, bx, by;
                            r.rd2(ax, ay); r.rd2(bx, by);
                            push_seg(ax, ay, bx, by, pi == 0);
                            px = bx; py = by; have_prev = true;
                        } else if (ct == 2 || ct == 3) {
                            f64 cx, cy, major = 0, ux = 1, uy = 0, ratio = 1;
                            r.rd2(cx, cy);
                            if (ct == 2) { major = r.bd(); }
                            else {
                                f64 mx, my; r.rd2(mx, my);
                                major = std::hypot(mx, my);
                                if (major > 1e-12) { ux = mx / major; uy = my / major; }
                                ratio = r.bd();
                            }
                            f64 a0 = r.bd(), a1 = r.bd();
                            const bool ccw = r.b();
                            if (!ccw) std::swap(a0, a1);
                            while (a1 < a0) a1 += 6.28318530717958647692;
                            const int steps = 24;
                            f64 lx = 0, ly = 0;
                            for (int q = 0; q <= steps; ++q) {
                                const f64 t = a0 + (a1 - a0) * (f64(q) / steps);
                                const f64 ex = major * std::cos(t), ey = major * ratio * std::sin(t);
                                const f64 wx = cx + ux * ex - uy * ey;
                                const f64 wy = cy + uy * ex + ux * ey;
                                if (q) push_seg(lx, ly, wx, wy, pi == 0);
                                lx = wx; ly = wy;
                            }
                            px = lx; py = ly; have_prev = true;
                        } else if (ct == 4) {
                            r.bl();                     // degree
                            const bool rational = r.b();
                            r.b();                      // periodic
                            const u32 nk = r.bl();
                            const u32 nc = r.bl();
                            if (nk > 4096 || nc > 4096 || !r.ok()) { bad = true; break; }
                            for (u32 q = 0; q < nk; ++q) r.bd();
                            f64 lx = px, ly = py;
                            for (u32 q = 0; q < nc; ++q) {
                                f64 wx, wy;
                                r.rd2(wx, wy);
                                if (rational) r.bd();
                                if (q && have_prev) push_seg(lx, ly, wx, wy, pi == 0);
                                lx = wx; ly = wy; have_prev = true;
                            }
                            if (version >= Version::R2010) {
                                const u32 nfit = r.bl();
                                if (nfit > 4096) { bad = true; break; }
                                if (nfit) {
                                    for (u32 q = 0; q < nfit; ++q) { f64 wx, wy; r.rd2(wx, wy); }
                                    f64 tx, ty; r.rd2(tx, ty); r.rd2(tx, ty);
                                }
                            }
                            px = lx; py = ly;
                        } else { bad = true; break; }
                    }
                } else {
                    const bool bulges = r.b();
                    r.b();                              // closed flag: the loop closes anyway
                    const u32 npts = r.bl();
                    if (npts > 8192 || !r.ok()) { bad = true; break; }
                    std::vector<std::pair<f64, f64>> pts;
                    std::vector<f64> bulge;
                    for (u32 k = 0; k < npts && r.ok(); ++k) {
                        f64 wx, wy;
                        r.rd2(wx, wy);
                        pts.push_back({wx, wy});
                        bulge.push_back(bulges ? r.bd() : 0.0);
                    }
                    for (std::size_t k = 0; k + 1 <= pts.size() && pts.size() >= 2; ++k) {
                        const auto& a = pts[k % pts.size()];
                        const auto& b = pts[(k + 1) % pts.size()];
                        if (k + 1 == pts.size() && a == b) break;
                        const f64 bg = bulge[k % pts.size()];
                        if (std::fabs(bg) < 1e-12) {
                            push_seg(a.first, a.second, b.first, b.second, pi == 0);
                        } else {
                            // bulge = tan(sweep / 4); a positive bulge bows left of a->b.
                            const f64 th = 4.0 * std::atan(bg);
                            const f64 half = std::hypot(b.first - a.first, b.second - a.second) * 0.5;
                            const f64 mx = (a.first + b.first) * 0.5, my = (a.second + b.second) * 0.5;
                            const f64 sag = bg * half;
                            const f64 dx = (b.first - a.first), dy = (b.second - a.second);
                            const f64 len = std::hypot(dx, dy);
                            const f64 nx = len > 1e-12 ? -dy / len : 0.0, ny = len > 1e-12 ? dx / len : 0.0;
                            const int steps = std::clamp(int(std::fabs(th) * 8), 4, 24);
                            f64 lx = a.first, ly = a.second;
                            for (int q = 1; q <= steps; ++q) {
                                const f64 t = f64(q) / steps;
                                const f64 wx = a.first + dx * t + nx * sag * 4.0 * t * (1.0 - t);
                                const f64 wy = a.second + dy * t + ny * sag * 4.0 * t * (1.0 - t);
                                push_seg(lx, ly, wx, wy, pi == 0);
                                lx = wx; ly = wy;
                            }
                        }
                        if (k + 1 == pts.size()) break;
                    }
                }
                r.bl();                                 // boundary handle count: handles live in the handle stream
            }
            if (bad || !r.ok() || h.boundary.empty()) continue;
            r.bs(); r.bs();                             // style, pattern type
            if (!h.solid) {
                h.angle = r.bd() * 57.2957795130823209;
                h.scale = r.bd();
                r.b();                                  // doubled
                const u16 ndef = r.bs();
                if (ndef > 512) continue;
                for (u16 k = 0; k < ndef && r.ok(); ++k) {
                    r.bd(); r.bd(); r.bd(); r.bd(); r.bd();
                    const u16 ndash = r.bs();
                    if (ndash > 512) break;
                    for (u16 q = 0; q < ndash; ++q) r.bd();
                }
            }
            if (has_derived) r.bd();                    // pixel size
            bool have_seed = false;
            if (r.ok()) {
                const u32 nseeds = r.bl();
                if (r.ok() && nseeds > 0 && nseeds < 4096) {
                    f64 sx, sy;
                    r.rd2(sx, sy);
                    if (r.ok()) { h.seed = gcm::to_lattice(v, sx, sy); have_seed = true; }
                }
            }
            if (!have_seed && first_loop.size() >= 3) {
                f64 cx = 0, cy = 0;
                for (const auto& q : first_loop) { cx += q.first; cy += q.second; }
                cx /= f64(first_loop.size()); cy /= f64(first_loop.size());
                const auto inside = [&](f64 x, f64 y) {
                    bool in = false;
                    for (std::size_t i = 0, j = first_loop.size() - 1; i < first_loop.size(); j = i++) {
                        const auto& a = first_loop[i];
                        const auto& b = first_loop[j];
                        if ((a.second > y) != (b.second > y) &&
                            x < (b.first - a.first) * (y - a.second) / (b.second - a.second) + a.first)
                            in = !in;
                    }
                    return in;
                };
                if (!inside(cx, cy)) {
                    const auto& a = first_loop[0];
                    const auto& b = first_loop[1];
                    const f64 mx = (a.first + b.first) * 0.5, my = (a.second + b.second) * 0.5;
                    const f64 len = std::hypot(b.first - a.first, b.second - a.second);
                    const f64 nx = -(b.second - a.second) / (len > 0 ? len : 1), ny = (b.first - a.first) / (len > 0 ? len : 1);
                    const f64 eps = len * 0.01;
                    if (inside(mx + nx * eps, my + ny * eps)) { cx = mx + nx * eps; cy = my + ny * eps; }
                    else { cx = mx - nx * eps; cy = my - ny * eps; }
                }
                h.seed = gcm::to_lattice(v, cx, cy);
                have_seed = true;
            }
            if (!have_seed) continue;
            fh.rec.layer_handle = hs.layer;
            fh.owner = hs.owner;
            fh.entmode = pf.entmode;
            found_hatches.push_back(std::move(fh));
            continue;
        }

        if (multileader_type && raw.type == multileader_type) {
            if (version >= Version::R2010) r.bs();      // class version
            const u32 nleaders = r.bl();
            if (nleaders > 64 || !r.ok()) continue;
            std::vector<std::vector<std::pair<f64, f64>>> lines;
            bool bad = false;std::size_t total_points=0;
            for (u32 li = 0; li < nleaders && r.ok() && !bad; ++li) {
                const bool has_last = r.b();
                const bool has_dogleg = r.b();
                f64 lx = 0, ly = 0, lz = 0, dx = 1, dy = 0, dz = 0;
                if (has_last) r.bd3(lx, ly, lz);
                if (has_dogleg) r.bd3(dx, dy, dz);
                const u32 nbreaks = r.bl();
                if (nbreaks > 256) { bad = true; break; }
                for (u32 k = 0; k < nbreaks; ++k) { f64 a, b, c; r.bd3(a, b, c); r.bd3(a, b, c); }
                r.bl();                                 // branch index
                const f64 dogleg = r.bd();
                const u32 nlines = r.bl();
                if (nlines > 256 || lines.size()+nlines>256 || !r.ok()) { bad = true; break; }
                for (u32 k = 0; k < nlines && r.ok(); ++k) {
                    const u32 npts = r.bl();
                    if (npts > 4096 || total_points+npts+2>4096) { bad = true; break; }
                    total_points+=npts+2;
                    std::vector<std::pair<f64, f64>> pts;
                    for (u32 q = 0; q < npts; ++q) { f64 x, y, z; r.bd3(x, y, z); pts.push_back({x, y}); }
                    const u32 nb = r.bl();
                    if (nb > 256) { bad = true; break; }
                    for (u32 q = 0; q < nb; ++q) { f64 a, b, c; r.bd3(a, b, c); r.bd3(a, b, c); }
                    r.bl();                             // line index
                    if (version >= Version::R2010) {
                        r.bs();                         // type
                        r.bs(); r.bl();                 // colour index, rgb
                        const u8 cflag = r.rc();
                        if (cflag < 4) { if (cflag & 1) strings.next(); if (cflag & 2) strings.next(); }
                        r.bl();                         // lineweight
                        r.bd();                         // arrow size
                        r.bl();                         // flags
                    }
                    if (has_last) {
                        pts.push_back({lx, ly});
                        if (has_dogleg && dogleg != 0.0) pts.push_back({lx + dx * dogleg, ly + dy * dogleg});
                    }
                    lines.push_back(std::move(pts));
                }
                if (version >= Version::R2010) r.bs();  // attach direction
            }
            if (bad || !r.ok()) continue;
            // The context: scale, base, text height ... then the note.
            r.bd();
            { f64 a, b, c; r.bd3(a, b, c); }
            const f64 text_height = r.bd();
            const f64 arrow_size=r.bd();r.bd();          // landing gap
            r.bs(); r.bs(); r.bs(); r.bs();
            Text note;
            if (r.b()) {                                // has text content
                note.text = plain_words(read_string(r, strings, version));
                f64 nx, ny, nz, px, py, pz;
                r.bd3(nx, ny, nz);
                r.bd3(px, py, pz);
                f64 ddx, ddy, ddz;
                r.bd3(ddx, ddy, ddz);                   // direction
                note.pos = gcm::to_lattice(v, px, py);
                note.height = text_height;
                note.angle = std::atan2(ddy, ddx);
            }
            if (!r.ok()||!std::isfinite(text_height)||text_height<0||!std::isfinite(arrow_size)||arrow_size<0) continue;
            DimensionRec rec;rec.dim.kind=gdim::Kind::Leader;rec.layer_handle=hs.layer;
            for(const auto& points:lines) {
                gdim::Dimension::LeaderPath path;
                for(auto [x,y]:points) {
                    const auto point=gcm::to_lattice(v,x,y);
                    if(path.vertices.empty()||point.x!=path.vertices.back().x||point.y!=path.vertices.back().y)path.vertices.push_back(point);
                }
                if(path.vertices.size()>1)rec.dim.leader_paths.push_back(std::move(path));
            }
            if(rec.dim.leader_paths.empty())continue;
            rec.dim.p1=rec.dim.leader_paths.front().vertices.front();rec.dim.p2=rec.dim.leader_paths.front().vertices[1];
            rec.dim.p3=note.text.empty()?rec.dim.leader_paths.front().vertices.back():note.pos;
            rec.dim.text_override=note.text;rec.dim.leader_text_absolute=true;
            rec.dim.leader_text_height=text_height;rec.dim.leader_text_angle=note.angle;rec.dim.leader_arrow_size=arrow_size;
            rec.handle=raw.handle;rec.extension_handle=hs.extension;
            found_dims.push_back({std::move(rec),hs.owner,pf.entmode});
            continue;
        }

        if(type==ObjectType::Leader && version>=Version::R2000) {
            r.b();const auto annotation=r.bs(),path=r.bs();const auto count=r.bl();
            if(path!=0 || count<2 || count>4096 || !r.ok()) {out.skipped.push_back("unsupported or invalid leader path");continue;}
            std::vector<gcm::Lattice2> points;
            for(u32 i=0;i<count;++i){double x,y,z;r.bd3(x,y,z);points.push_back(gcm::to_lattice(v,x,y));}
            double x,y,z;r.bd3(x,y,z);r.bd3(x,y,z);const bool planar=x==0&&y==0&&z==1;
            r.bd3(x,y,z);r.bd3(x,y,z);r.bd3(x,y,z);r.bd();r.bd();r.b();const bool arrows=r.b();r.bs();r.b();r.b();
            if(!r.ok()||!planar||hs.rest.size()<2) continue;
            DimensionRec rec;rec.dim.kind=gdim::Kind::Leader;rec.dim.p1=points.front();rec.dim.p2=points[1];rec.dim.p3=points.back();rec.dim.leader_paths.push_back({std::move(points),arrows});
            rec.layer_handle=hs.layer;rec.style_handle=hs.rest[1].value;rec.annotation_handle=annotation==0?hs.rest[0].value:0;
            rec.handle=raw.handle;rec.extension_handle=hs.extension;
            found_dims.push_back({std::move(rec),hs.owner,pf.entmode});
            continue;
        }

        // ATTRIB begins exactly as TEXT does.
        if (type == ObjectType::Text || type == ObjectType::MText || type == ObjectType::Attrib || type == ObjectType::AttDef) {
            Text t;t.handle=raw.handle;if(!hs.rest.empty()) t.style_handle=hs.rest.front().value;
            if (type == ObjectType::Text || type == ObjectType::Attrib || type == ObjectType::AttDef) {
                const u8 df = r.rc();
                if (!(df & 0x01)) r.rd();                   // elevation
                f64 x, y;
                r.rd2(x, y);
                if (!(df & 0x02)) { x=r.dd(x); y=r.dd(y); }     // alignment point
                f64 ex, ey, ez;
                r.be(ex, ey, ez);
                r.bt();
                if (!(df & 0x04)) r.rd();                   // oblique
                f64 rotation = 0.0;
                if (!(df & 0x08)) rotation = r.rd();
                const f64 height = r.rd();
                if (!(df & 0x10)) r.rd();                   // width factor
                t.text = read_string(r, strings, version);
                t.pos = gcm::to_lattice(v, x, y);
                t.height = height;
                t.angle = rotation;
                if(!(df&0x20)) r.bs();
                const int ha=(df&0x40)?0:int(r.bs());
                const int va=(df&0x80)?0:int(r.bs());
                if (r.ok()) {
                    t.halign = (ha == 1 || ha == 4) ? 1 : (ha == 2 ? 2 : (ha >= 3 ? 1 : 0));
                    t.valign = ha == 4 ? 2 : va;
                }
            } else {
                f64 x, y, z;
                r.bd3(x, y, z);
                f64 ex, ey, ez, ax, ay, az;
                r.bd3(ex, ey, ez);
                r.bd3(ax, ay, az);
                t.width=r.bd();                             // rect width
                if (version >= Version::R2007) r.bd();      // rect height
                const f64 height = r.bd();
                const int attachment=std::clamp(int(r.bs()),1,9)-1;
                t.halign=attachment%3;t.valign=attachment/3==0?3:attachment/3==1?2:1;
                r.bs();                                     // drawing direction
                r.bd();                                     // extents height
                r.bd();                                     // extents width
                t.text = read_string(r, strings, version);
                t.pos = gcm::to_lattice(v, x, y);
                t.height = height;
                t.angle = std::atan2(ay, ax);
            }
            if((type==ObjectType::Attrib||type==ObjectType::AttDef)&&version<Version::R2018) {
                if(version>=Version::R2010) r.rc();
                gdoc::AttributeDef a; a.tag=read_string(r,strings,version); r.bs(); const auto flags=r.rc();
                a.position=t.pos; a.height=t.height; a.rotation=t.angle; a.default_value=t.text;
                a.invisible=flags&1; a.constant=flags&2; a.verify=flags&4; a.preset=flags&8;
                if(version>=Version::R2007) a.lock_position=r.b();
                if(type==ObjectType::AttDef) { if(version>=Version::R2010) r.rc(); a.prompt=read_string(r,strings,version); }
                if(!r.ok()) continue;
                if(type==ObjectType::AttDef) found_attdefs.emplace_back(hs.owner,std::move(a));
                else { FoundAttrib fa; fa.owner=hs.owner; fa.handle=t.handle; fa.tag=std::move(a.tag); fa.text=std::move(t); found_attributes.push_back(std::move(fa)); }
                continue;
            }
            if (!r.ok() || t.text.empty()) continue;
            place_layer(t);
            found_texts.push_back({std::move(t), hs.owner, pf.entmode});
            continue;
        }
        if (type >= ObjectType::DimOrdinate && type <= ObjectType::DimDiameter) {
            if (version >= Version::R2010) r.rc();          // class version
            f64 ex, ey, ez;
            r.bd3(ex, ey, ez);                              // extrusion
            f64 tx, ty;
            r.rd2(tx, ty);                                  // text midpoint (11)
            r.bd();                                         // elevation
            r.rc();                                         // flags
            const std::string user = read_string(r, strings, version);
            r.bd(); r.bd();                                 // text rotation, horizontal direction
            { f64 sx, sy, sz; r.bd3(sx, sy, sz); }          // insertion scale
            r.bd();                                         // insertion rotation
            r.bs(); r.bs(); r.bd(); r.bd();                 // attachment, line spacing style/factor, measurement
            if (version >= Version::R2007) { r.b(); r.b(); r.b(); }
            { f64 cx, cy; r.rd2(cx, cy); }                  // clone insertion point
            const f64 flip = ez < 0.0 ? -1.0 : 1.0;
            const auto pt = [&](f64 x, f64 y) { return gcm::to_lattice(v, x * flip, y); };
            f64 x[7] = {0, tx, 0, 0, 0, 0, 0}, y[7] = {0, ty, 0, 0, 0, 0, 0}, z;
            gdim::Dimension d;
            switch (type) {
                case ObjectType::DimOrdinate:
                    r.bd3(x[0], y[0], z); r.bd3(x[3], y[3], z); r.bd3(x[4], y[4], z); r.rc();
                    d.kind = gdim::Kind::Ordinate; d.p1 = pt(x[3], y[3]); d.p2 = pt(x[4], y[4]); d.p3 = pt(x[0], y[0]);
                    break;
                case ObjectType::DimLinear: {
                    r.bd3(x[3], y[3], z); r.bd3(x[4], y[4], z); r.bd3(x[0], y[0], z);
                    r.bd();                                 // oblique
                    const f64 rot = r.bd() * 180.0 / 3.14159265358979323846;
                    const bool vertical = std::fabs(std::fmod(std::fabs(rot), 180.0) - 90.0) < 1.0;
                    d.kind = vertical ? gdim::Kind::Vertical : gdim::Kind::Horizontal;
                    d.p1 = pt(x[3], y[3]); d.p2 = pt(x[4], y[4]); d.p3 = pt(x[0], y[0]);
                    break;
                }
                case ObjectType::DimAligned:
                    r.bd3(x[3], y[3], z); r.bd3(x[4], y[4], z); r.bd3(x[0], y[0], z); r.bd();
                    d.kind = gdim::Kind::Aligned; d.p1 = pt(x[3], y[3]); d.p2 = pt(x[4], y[4]); d.p3 = pt(x[0], y[0]);
                    break;
                case ObjectType::DimAng3Pt:
                    r.bd3(x[0], y[0], z); r.bd3(x[3], y[3], z); r.bd3(x[4], y[4], z); r.bd3(x[5], y[5], z);
                    d.kind = gdim::Kind::Angular; d.centre = pt(x[5], y[5]);
                    d.p1 = pt(x[3], y[3]); d.p2 = pt(x[4], y[4]); d.p3 = pt(x[0], y[0]);
                    break;
                case ObjectType::DimAng2Ln: {
                    r.rd2(x[0], y[0]);
                    r.bd3(x[3], y[3], z); r.bd3(x[4], y[4], z); r.bd3(x[5], y[5], z); r.bd3(x[6], y[6], z);
                    const f64 den = (x[3] - x[4]) * (y[5] - y[6]) - (y[3] - y[4]) * (x[5] - x[6]);
                    f64 cx = x[3], cy = y[3];
                    if (std::fabs(den) > 1e-12) {
                        const f64 t = ((x[3] - x[5]) * (y[5] - y[6]) - (y[3] - y[5]) * (x[5] - x[6])) / den;
                        cx = x[3] + t * (x[4] - x[3]); cy = y[3] + t * (y[4] - y[3]);
                    }
                    d.kind = gdim::Kind::Angular; d.centre = pt(cx, cy);
                    d.p1 = pt(x[4], y[4]); d.p2 = pt(x[6], y[6]); d.p3 = pt(x[0], y[0]);
                    break;
                }
                case ObjectType::DimRadius:
                    r.bd3(x[0], y[0], z); r.bd3(x[5], y[5], z); r.bd();
                    d.kind = gdim::Kind::Radial; d.centre = pt(x[0], y[0]);
                    d.p1 = pt(x[5], y[5]); d.p2 = d.p1; d.p3 = pt(x[1], y[1]);
                    break;
                case ObjectType::DimDiameter:
                default:
                    r.bd3(x[5], y[5], z); r.bd3(x[0], y[0], z); r.bd();
                    d.kind = gdim::Kind::Diameter; d.p1 = pt(x[5], y[5]); d.p2 = pt(x[0], y[0]);
                    d.centre = pt((x[0] + x[5]) * 0.5, (y[0] + y[5]) * 0.5); d.p3 = pt(x[1], y[1]);
                    break;
            }
            if (!r.ok()) continue;
            if (!user.empty() && user != "<>") {
                d.text_override = user;
                std::string& t2 = d.text_override;
                std::string out2;
                for (std::size_t i2 = 0; i2 < t2.size(); ++i2) {
                    if (t2[i2] == '%' && i2 + 2 < t2.size() && t2[i2 + 1] == '%') {
                        const char c2 = char(std::tolower(static_cast<unsigned char>(t2[i2 + 2])));
                        if (c2 == 'c') { out2 += "\xe2\x8c\x80"; i2 += 2; continue; }
                        if (c2 == 'd') { out2 += "\xc2\xb0"; i2 += 2; continue; }
                        if (c2 == 'p') { out2 += "\xc2\xb1"; i2 += 2; continue; }
                        if (c2 == 'u' || c2 == 'o') { i2 += 2; continue; }
                        if (c2 == '%') { out2 += '%'; i2 += 2; continue; }
                    }
                    out2 += t2[i2];
                }
                t2 = std::move(out2);
            }
            if(d.kind==gdim::Kind::Radial||d.kind==gdim::Kind::Diameter) d.radius=gcm::i64(std::llround(std::hypot(double(d.p1.x)-d.centre.x,double(d.p1.y)-d.centre.y)));
            DimensionRec rec;
            rec.dim = std::move(d);
            rec.layer_handle = hs.layer;
            if(!hs.rest.empty()) rec.style_handle=hs.rest[0].value;
            rec.handle=raw.handle;rec.extension_handle=hs.extension;
            found_dims.push_back({std::move(rec), hs.owner, pf.entmode});
            continue;
        }
        if (type == ObjectType::Insert || type == ObjectType::MInsert) {
            FoundInsert ins; ins.handle=raw.handle; ins.extension=hs.extension;
            f64 z;
            r.bd3(ins.x, ins.y, z);
            const u8 scale_flag = r.bb();
            f64 sx = 1, sy = 1;
            if (scale_flag == 3) { sx = sy = 1.0; }
            else if (scale_flag == 1) { sx = 1.0; sy = r.dd(1.0); r.dd(1.0); }
            else if (scale_flag == 2) { sx = r.rd(); sy = sx; }
            else { sx = r.rd(); sy = r.dd(sx); r.dd(sx); }
            ins.sx = sx; ins.sy = sy;
            ins.rot = r.bd();
            {
                // INSERT stores a full 3BD extrusion, not BE; a negative Z mirrors.
                f64 ex, ey, ez;
                r.bd3(ex, ey, ez);
                if (r.ok() && ez < 0.0) {
                    ins.x = -ins.x;
                    ins.rot = -ins.rot;
                    ins.sx = -ins.sx;
                }
            }
            if (!r.ok()) continue;
            // Block header: the first hard pointer among the entity's own handles.
            for (const Handle& h : hs.rest) if (h.code == 5 || h.code == 4) { ins.block = h.value; break; }
            ins.layer = hs.layer;
            ins.owner = hs.owner;
            ins.entmode = pf.entmode;
            inserts.push_back(ins);
            continue;
        }

        Entity e;
        e.handle = raw.handle;
        e.kind = object_type_name(raw.type);

        std::vector<Entity> made;
        switch (type) {
            case ObjectType::LwPolyline: {
                const u16 flag = r.bs();
                if (flag & 4) r.bd();                       // constant width
                if (flag & 8) r.bd();                       // elevation
                if (flag & 2) r.bd();                       // thickness
                if (flag & 1) { f64 nx, ny, nz; r.bd3(nx, ny, nz); }
                const u32 n = r.bl();
                u32 n_bulges = 0, n_ids = 0, n_widths = 0;
                if (flag & 16) n_bulges = r.bl();
                if (version >= Version::R2010 && (flag & 1024)) n_ids = r.bl();
                if (flag & 32) n_widths = r.bl();
                if (!r.ok() || n == 0 || n > 1000000) continue;
                if (n_bulges > n || n_ids > n || n_widths > n) continue;
                std::vector<std::pair<f64, f64>> pts;
                pts.reserve(n);
                f64 px = 0, py = 0;
                r.rd2(px, py);
                pts.push_back({px, py});
                for (u32 i = 1; i < n; ++i) {
                    const f64 nx = r.dd(px), ny = r.dd(py);
                    px = nx; py = ny;
                    pts.push_back({px, py});
                }
                std::vector<f64> bulges(n, 0.0);
                for (u32 i = 0; i < n_bulges; ++i) { const f64 b = r.bd(); if (i < n) bulges[i] = b; }
                for (u32 i = 0; i < n_ids; ++i) r.bl();
                for (u32 i = 0; i < n_widths; ++i) { r.bd(); r.bd(); }
                if (!r.ok()) continue;
                const bool closed = (flag & 512) != 0;
                if(closed && std::all_of(bulges.begin(),bulges.end(),[](double b){return b==0;})) {
                    auto& clip=clip_polygons[raw.handle]; for(const auto& [x,y]:pts) clip.push_back({x,y});
                }
                const std::size_t last = closed ? n : n - 1;
                for (std::size_t k = 0; k < last; ++k) {
                    const auto& a = pts[k];
                    const auto& b = pts[(k + 1) % n];
                    Entity seg = e;seg.subentity=u32(k+1);
                    if (std::fabs(bulges[k]) < 1e-12) {
                        seg.shape.kind = gsnap::ShapeKind::Segment;
                        seg.shape.a = gcm::to_lattice(v, a.first, a.second);
                        seg.shape.b = gcm::to_lattice(v, b.first, b.second);
                        if (seg.shape.a.x == seg.shape.b.x && seg.shape.a.y == seg.shape.b.y) continue;
                    } else {
                        const f64 bulge = bulges[k];
                        const f64 dx = b.first - a.first, dy = b.second - a.second;
                        const f64 chord = std::hypot(dx, dy);
                        if (chord < 1e-12) continue;
                        const f64 sweep = 4.0 * std::atan(bulge);
                        const f64 radius = chord / (2.0 * std::sin(std::fabs(sweep) * 0.5));
                        const f64 mx = (a.first + b.first) * 0.5, my = (a.second + b.second) * 0.5;
                        const f64 h = std::sqrt(std::max(radius * radius - chord * chord * 0.25, 0.0));
                        const f64 sgn = bulge > 0 ? 1.0 : -1.0;
                        const f64 side = std::fabs(sweep) > 3.14159265358979323846 ? -1.0 : 1.0;
                        const f64 cx = mx - sgn * side * dy / chord * h;
                        const f64 cy = my + sgn * side * dx / chord * h;
                        f64 a0 = std::atan2(a.second - cy, a.first - cx);
                        f64 a1 = std::atan2(b.second - cy, b.first - cx);
                        if (bulge < 0) std::swap(a0, a1);
                        seg.shape.kind = gsnap::ShapeKind::Arc;
                        seg.shape.a = gcm::to_lattice(v, cx, cy);
                        seg.shape.b = seg.shape.a;
                        seg.shape.radius = static_cast<gcm::i64>(std::llround(radius * v.scale()));
                        seg.shape.start_angle = a0;
                        seg.shape.end_angle = a1;
                    }
                    made.push_back(std::move(seg));
                }
                break;
            }
            case ObjectType::Ellipse: {
                f64 cx, cy, cz, mx, my, mz, ex, ey, ez;
                r.bd3(cx, cy, cz);
                r.bd3(mx, my, mz);
                r.bd3(ex, ey, ez);
                const f64 ratio = r.bd();
                e.shape.start_angle=r.bd();
                e.shape.end_angle=r.bd();
                if (!r.ok()) continue;
                e.shape.kind = gsnap::ShapeKind::Ellipse;
                e.shape.a = gcm::to_lattice(v, cx, cy);
                e.shape.b = gcm::to_lattice(v, cx + mx, cy + my);
                e.shape.radius = static_cast<gcm::i64>(std::llround(std::hypot(mx, my) * ratio * v.scale()));
                made.push_back(e);
                break;
            }
            case ObjectType::Leader: {
                r.b();                                      // unknown
                r.bs();                                     // annotation type
                r.bs();                                     // path type: 0 straight, 1 spline
                const u32 n = r.bl();
                if (n == 0 || n > 4096 || !r.ok()) continue;
                std::vector<std::pair<f64, f64>> pts;
                for (u32 i = 0; i < n && r.ok(); ++i) { f64 x, y, z; r.bd3(x, y, z); pts.push_back({x, y}); }
                if (!r.ok()) continue;
                for (std::size_t k = 0; k + 1 < pts.size(); ++k) {
                    Entity s = e;
                    s.shape.kind = gsnap::ShapeKind::Segment;
                    s.shape.a = gcm::to_lattice(v, pts[k].first, pts[k].second);
                    s.shape.b = gcm::to_lattice(v, pts[k + 1].first, pts[k + 1].second);
                    if (s.shape.a.x != s.shape.b.x || s.shape.a.y != s.shape.b.y) made.push_back(s);
                }
                break;
            }
            case ObjectType::Line: {
                // Z-is-zero flag first; coordinates interleaved x-pair then y-pair.
                const bool flat = r.b();
                const f64 x0 = r.rd();
                const f64 x1 = r.dd(x0);
                const f64 y0 = r.rd();
                const f64 y1 = r.dd(y0);
                if (!flat) { const f64 z0 = r.rd(); r.dd(z0); }
                if (!r.ok()) continue;
                e.shape.kind = gsnap::ShapeKind::Segment;
                e.shape.a = gcm::to_lattice(v, x0, y0);
                e.shape.b = gcm::to_lattice(v, x1, y1);
                made.push_back(e);
                break;
            }
            case ObjectType::Circle: {
                f64 cx, cy, cz;
                r.bd3(cx, cy, cz);
                const f64 radius = r.bd();
                if (!r.ok()) continue;
                e.shape.kind = gsnap::ShapeKind::Circle;
                e.shape.a = gcm::to_lattice(v, cx, cy);
                e.shape.b = e.shape.a;
                e.shape.radius = static_cast<gcm::i64>(std::llround(radius * v.scale()));
                made.push_back(e);
                break;
            }
            case ObjectType::Arc: {
                f64 cx, cy, cz;
                r.bd3(cx, cy, cz);
                const f64 radius = r.bd();
                r.bt();                       // thickness
                f64 ex, ey, ez;
                r.be(ex, ey, ez);             // extrusion
                const f64 start = r.bd();
                const f64 end = r.bd();
                if (!r.ok()) continue;
                e.shape.kind = gsnap::ShapeKind::Arc;
                e.shape.a = gcm::to_lattice(v, cx, cy);
                e.shape.b = e.shape.a;
                e.shape.radius = static_cast<gcm::i64>(std::llround(radius * v.scale()));
                // Radians in the file, unlike DXF.
                e.shape.start_angle = start;
                e.shape.end_angle = end;
                made.push_back(e);
                break;
            }
            case ObjectType::Point: {
                f64 x, y, z;
                r.bd3(x, y, z);
                if (!r.ok()) continue;
                e.shape.kind = gsnap::ShapeKind::Point;
                e.shape.a = gcm::to_lattice(v, x, y);
                e.shape.b = e.shape.a;
                made.push_back(e);
                break;
            }
            default:
                continue;
        }
        for (auto& m : made) {
            m.layer_handle = hs.layer;
            found.push_back({std::move(m), hs.owner, pf.entmode});
        }
    }

    // APPIDs may follow their layers in the map; resolve after all tables.
    for (const auto& [handle, values] : layer_eed) {
        auto* layer = out.layers.find(layer_names.at(handle));
        if (!layer) continue;
        for (const auto& [app, value] : values) {
            const auto name = app_names.find(app);
            if (name != app_names.end() && name->second == "AcCmTransparency" &&
                (value & 0xffffff00u) == 0x02000000u)
                layer->transparency = int(std::lround((255 - (value & 255)) * 100.0 / 255));
        }
    }

    const auto layer_name = [&](u64 h) {
        const auto it = layer_names.find(h);
        return it != layer_names.end() ? it->second : std::string("0");
    };

    std::stable_sort(found_layouts.begin(),found_layouts.end(),[](const auto& a,const auto& b){return a.order<b.order;});
    std::unordered_map<u64,std::size_t> paper_spaces;
    u64 implicit_paper=current_paper;
    if(!implicit_paper) for(const auto& [h,bi]:block_headers) if(bi.name=="*Paper_Space") implicit_paper=h;
    for(auto& f:found_layouts) {
        if(paper_spaces.contains(f.block)) continue;
        paper_spaces[f.block]=out.layouts.size();
        if(!model_active && f.block==implicit_paper) out.active_layout=int(out.layouts.size());
        out.layouts.push_back(std::move(f.sheet));
    }
    const auto paper_index=[&](u64 owner,u8 mode)->int {
        const auto it=paper_spaces.find(mode==1?implicit_paper:owner);
        return mode!=2 && it!=paper_spaces.end()?int(it->second):-1;
    };
    const auto paper_factor=[&](int index) { return found_layouts[std::size_t(index)].paper_unit; };
    const auto scaled_point=[&](gcm::Lattice2 p,double factor) {
        double x,y; gcm::to_world(v,p,x,y); return gcm::to_lattice(v,x*factor,y*factor);
    };
    const auto scaled_shape=[&](gsnap::Shape shape,double factor) {
        shape.a=scaled_point(shape.a,factor); shape.b=scaled_point(shape.b,factor);
        shape.radius=gcm::i64(std::llround(double(shape.radius)*factor)); return shape;
    };
    std::unordered_map<u64,bool> viewport_seen;
    std::unordered_map<u64,bool> viewport_boundaries;
    for(auto& f:found_viewports) {
        const int index=paper_index(f.owner,f.entmode); if(index<0) continue;
        const auto block=found_layouts[std::size_t(index)].block;
        // Viewport 1 is the paper-space camera and never shows model geometry.
        if(!viewport_seen[block]) { viewport_seen[block]=true; continue; }
        auto& vp=f.vp; const auto factor=paper_factor(index);
        vp.lo.x*=factor; vp.lo.y*=factor; vp.hi.x*=factor; vp.hi.y*=factor;
        vp.scale/=factor; // model file units per paper metre; importer converts model units
        for(auto layer:f.frozen) vp.frozen_layers.push_back(layer_name(layer));
        if(f.clip) {
            const auto polygon=clip_polygons.find(f.clip);
            if(polygon==clip_polygons.end()) { out.skipped.push_back("unsupported viewport clipping boundary"); continue; }
            for(auto point:polygon->second) {
                point.x*=factor; point.y*=factor;
                if(point.x>=vp.lo.x-1e-10 && point.x<=vp.hi.x+1e-10) point.x=std::clamp(point.x,vp.lo.x,vp.hi.x);
                if(point.y>=vp.lo.y-1e-10 && point.y<=vp.hi.y+1e-10) point.y=std::clamp(point.y,vp.lo.y,vp.hi.y);
                vp.clip_polygon.push_back(point);
            }
            viewport_boundaries[f.clip]=true;
        }
        out.layouts[std::size_t(index)].viewports.push_back(std::move(vp));
    }
    for(auto& f:found_splines) {
        const int index=paper_index(f.owner,f.entmode);
        if(index>=0) {
            for(auto& cp:f.curve.control) cp.p=ggeo::d2::Point<gcm::F64>::from(cp.p.x.to_f64()*paper_factor(index),cp.p.y.to_f64()*paper_factor(index));
            out.layouts[std::size_t(index)].splines.push_back(std::move(f.curve));
        } else if(f.entmode==2 || (block_headers.contains(f.owner) && block_headers.at(f.owner).name=="*Model_Space")) out.splines.push_back(std::move(f.curve));
        else {
            const auto points=f.curve.tessellate(1e-5);
            for(std::size_t i=1;i<points.size();++i) {
                Entity e; e.kind="SPLINE"; e.shape.a=gcm::to_lattice(v,points[i-1].x.to_f64(),points[i-1].y.to_f64()); e.shape.b=gcm::to_lattice(v,points[i].x.to_f64(),points[i].y.to_f64());
                found.push_back({e,f.owner,f.entmode});
            }
        }
    }

    std::unordered_map<u64, std::string> block_name_of;
    for (const auto& [h, bi] : block_headers) {
        if (bi.name.empty() || bi.name[0] == '*') continue;   // *Model_Space, *Paper_Space
        gdoc::BlockDef def;
        def.name = bi.name;
        def.base = gcm::to_lattice(v, bi.bx, bi.by);
        def.is_xref=bi.xref; def.xref_path=bi.path;
        out.blocks.define(std::move(def));
        block_name_of[h] = bi.name;
    }
    if (std::getenv("GLCAD_DWG_DEBUG")) {
        std::size_t em0 = 0, matched = 0;
        for (const auto& f : found)
            if (f.entmode == 0) { ++em0; if (block_name_of.count(f.owner)) ++matched; }
        std::fprintf(stderr, "DBG entities=%zu entmode0=%zu matched=%zu block_headers=%zu named=%zu\n",
                     found.size(), em0, matched, block_headers.size(), block_name_of.size());
        int k = 0;
        for (const auto& f : found)
            if (f.entmode == 0 && !block_name_of.count(f.owner) && k++ < 6)
                std::fprintf(stderr, "DBG unmatched owner=%llx kind=%s\n", (unsigned long long)f.owner, f.entity.kind.c_str());
        k = 0;
        for (const auto& [h, bi] : block_headers)
            if (k++ < 8) std::fprintf(stderr, "DBG header %llx name='%s' xref=%d\n", (unsigned long long)h, bi.name.c_str(), int(bi.xref));
    }
    {
        // From 2004 vertices name their polyline by owner; in R2000 they follow it.
        std::unordered_map<u64, std::vector<std::pair<f64, f64>>> chain, seq_chain;
        for (const auto& vtx : found_verts) {
            if (vtx.flag & 16) continue;
            if (vtx.owner) chain[vtx.owner].push_back({vtx.x, vtx.y});
            if (vtx.seq_poly) seq_chain[vtx.seq_poly].push_back({vtx.x, vtx.y});
        }
        for (const auto& fp : found_polys) {
            const std::vector<std::pair<f64, f64>>* pts_p = nullptr;
            if (const auto it = chain.find(fp.handle); it != chain.end() && it->second.size() >= 2)
                pts_p = &it->second;
            else if (const auto is = seq_chain.find(fp.handle); is != seq_chain.end() && is->second.size() >= 2)
                pts_p = &is->second;
            if (!pts_p) continue;
            const auto& pts = *pts_p;
            const std::size_t n = pts.size();
            const std::size_t last = fp.closed ? n : n - 1;
            for (std::size_t k = 0; k < last; ++k) {
                Entity s = fp.proto;s.subentity=u32(k+1);
                s.shape.kind = gsnap::ShapeKind::Segment;
                s.shape.a = gcm::to_lattice(v, pts[k].first, pts[k].second);
                s.shape.b = gcm::to_lattice(v, pts[(k + 1) % n].first, pts[(k + 1) % n].second);
                if (s.shape.a.x == s.shape.b.x && s.shape.a.y == s.shape.b.y) continue;
                found.push_back({std::move(s), fp.owner, fp.entmode});
            }
        }
    }
    u32 source_key=0;
    std::unordered_map<u64,std::vector<Found*>> association_geometry;
    for(auto& f:found) {
        f.entity.shape.key=++source_key;
        if(f.entity.handle)association_geometry[f.entity.handle].push_back(&f);
    }
    std::unordered_map<u64,std::vector<const DimensionAssociation*>> associations;
    for(const auto& a:out.dimension_associations)associations[a.dimension].push_back(&a);
    for(auto& d:found_dims) {
        const auto entry=associations.find(d.rec.handle);if(entry==associations.end())continue;
        const auto unsupported=[&] {out.skipped.push_back("dimension " + std::to_string(d.rec.handle) + " has unsupported geometry associations");};
        if(entry->second.size()!=1){unsupported();continue;}
        const auto& a=*entry->second.front();
        const auto dictionary=dictionaries.find(a.owner);
        const auto kind=d.rec.dim.kind;
        const bool linear=kind==gdim::Kind::Linear || kind==gdim::Kind::Horizontal ||
                          kind==gdim::Kind::Vertical || kind==gdim::Kind::Aligned;
        if(!linear || a.trans_space || a.rotated_type || a.flags>3 ||
           dictionary==dictionaries.end() || a.owner!=d.rec.extension_handle || dictionary->second.owner!=d.rec.handle ||
           !dictionary->second.entries.contains("ACAD_DIMASSOC") || dictionary->second.entries.at("ACAD_DIMASSOC")!=a.handle) {
            unsupported();continue;
        }
        bool incomplete=false;
        for(const auto& p:a.points) {
            if(p.index>1 || p.last_point || p.objects.size()!=1 || !p.paths.empty() ||
               !p.intersection_objects.empty() || (p.osnap!=1 && p.osnap!=13 && p.osnap!=3 && p.osnap!=4) || p.subentity!=2) {
                incomplete=true;continue;
            }
            const auto geometry=association_geometry.find(p.objects.front());
            if(geometry==association_geometry.end()){incomplete=true;continue;}
            gdim::Dimension::Ref bound;unsigned matches=0;
            const auto point=p.index==0?d.rec.dim.p1:d.rec.dim.p2;
            for(auto* f:geometry->second) {
                const auto& shape=f->entity.shape;
                const auto paper=paper_index(d.owner,d.entmode);
                const bool same_space=paper>=0?paper_index(f->owner,f->entmode)==paper:
                    d.entmode==f->entmode && d.owner==f->owner;
                if(!same_space || f->entity.subentity!=p.marker)continue;
                for(auto anchor:{gdim::Dimension::Anchor::Start,gdim::Dimension::Anchor::End,gdim::Dimension::Anchor::Centre}) {
                    if((p.osnap==3)!=(anchor==gdim::Dimension::Anchor::Centre))continue;
                    if(p.osnap==4 && (shape.kind!=gsnap::ShapeKind::Point || anchor!=gdim::Dimension::Anchor::Start))continue;
                    gcm::Lattice2 at;
                    if(!gdim::Dimension::anchor_point(shape,anchor,at))continue;
                    double unit=paper>=0?paper_factor(paper):gdxf::metres_per(static_cast<gdxf::Units>(out.insunits));
                    if(unit<=0)unit=1;
                    const double tolerance=2/std::min(unit,1.0);
                    if(std::hypot(double(at.x)-point.x,double(at.y)-point.y)>tolerance)continue;
                    ++matches;
                    bound={shape.key,anchor};
                }
            }
            if(matches!=1)incomplete=true;
            else (p.index==0?d.rec.dim.o1:d.rec.dim.o2)=bound;
        }
        if(incomplete)unsupported();
    }
    for(auto& item:found_underlays) {
        const auto definition=underlay_defs.find(item.definition);
        if(definition==underlay_defs.end()||definition->second.kind!=item.underlay.kind){out.skipped.push_back("underlay definition is missing");continue;}
        auto& u=item.underlay;u.path=definition->second.path;u.sheet=definition->second.sheet;u.layer=layer_name(item.layer);
        if(const auto* layer=out.layers.find(u.layer)){u.plottable=layer->plottable;u.locked=layer->locked;}
        if(const int index=paper_index(item.owner,item.entmode);index>=0) {
            const auto factor=paper_factor(index);u.insertion=scaled_point(u.insertion,factor);u.scale*=factor;
            out.layouts[std::size_t(index)].underlays.push_back(std::move(u));continue;
        }
        if(item.entmode==0&&block_name_of.contains(item.owner)) {
            if(auto* def=const_cast<gdoc::BlockDef*>(out.blocks.find(block_name_of.at(item.owner))))def->underlays.push_back(std::move(u));
        } else if(item.entmode!=1)out.underlays.push_back(std::move(u));
    }
    for (auto& fh : found_hatches) {
        fh.rec.layer = layer_name(fh.rec.layer_handle);
        if(const int index=paper_index(fh.owner,fh.entmode);index>=0) {
            gdxf::HatchRec h; const auto factor=paper_factor(index);
            h.pattern=fh.rec.pattern; h.solid=fh.rec.solid; h.scale=fh.rec.scale*factor; h.angle=fh.rec.angle; h.layer=fh.rec.layer;
            h.seed=scaled_point(fh.rec.seed,factor); for(const auto& shape:fh.rec.boundary) h.boundary.push_back(scaled_shape(shape,factor));
            out.layouts[std::size_t(index)].hatches.push_back(std::move(h)); continue;
        }
        if (fh.entmode == 0 && block_name_of.count(fh.owner)) continue;
        out.hatches.push_back(std::move(fh.rec));
    }
    for (auto& f : found) {
        if(const auto at=entity_attributes.find(f.entity.handle);at!=entity_attributes.end())
            f.entity.shape.attrs=at->second;
        f.entity.layer = layer_name(f.entity.layer_handle);
        if(viewport_boundaries.contains(f.entity.handle)) continue;
        if(const int index=paper_index(f.owner,f.entmode);index>=0) {
            auto shape=scaled_shape(f.entity.shape,paper_factor(index)); shape.layer=f.entity.layer;
            out.layouts[std::size_t(index)].entities.push_back({shape,f.entity.layer,f.entity.kind}); continue;
        }
        if(f.entmode==1 || (block_headers.contains(f.owner) && block_headers.at(f.owner).name.starts_with("*D"))) continue;
        if (f.entmode == 0) {
            const auto it = block_name_of.find(f.owner);
            if (it != block_name_of.end()) {
                if (gdoc::BlockDef* def = const_cast<gdoc::BlockDef*>(out.blocks.find(it->second))) {
                    f.entity.shape.layer = f.entity.layer;
                    def->geometry.push_back(f.entity.shape);
                }
                continue;
            }
        }
        f.entity.shape.layer = f.entity.layer;
        out.entities.push_back(std::move(f.entity));
    }
    for(auto& [owner,attribute]:found_attdefs) {
        const auto name=block_name_of.find(owner);
        if(name!=block_name_of.end()) if(auto* def=const_cast<gdoc::BlockDef*>(out.blocks.find(name->second))) def->attributes.push_back(std::move(attribute));
    }
    std::unordered_map<u64,bool> annotation_texts;
    for(auto& d:found_dims) {
        if(dimstyle_names.contains(d.rec.style_handle)) d.rec.dim.style=dimstyle_names.at(d.rec.style_handle);
        if(d.rec.annotation_handle) for(const auto& t:found_texts) if(t.text.handle==d.rec.annotation_handle) {
            d.rec.dim.text_override=t.text.text;
            if(d.rec.dim.kind==gdim::Kind::Leader) {
                d.rec.dim.p3=t.text.pos;d.rec.dim.leader_text_absolute=true;
                d.rec.dim.leader_text_height=t.text.height;d.rec.dim.leader_text_angle=t.text.angle;
                d.rec.dim.leader_text_halign=t.text.halign;d.rec.dim.leader_text_valign=t.text.valign;
            }
            annotation_texts[t.text.handle]=true;break;
        }
    }
    double model_style_unit=gdxf::metres_per(static_cast<gdxf::Units>(out.insunits));
    if(model_style_unit<=0)model_style_unit=1;
    std::unordered_map<std::string,bool> used_style_names;
    for(const auto& [handle,name]:dimstyle_names)used_style_names[name]=true;
    for(const auto& [handle,raw_style]:raw_dimstyles) {
        std::map<double,std::vector<FoundDim*>> uses;
        for(auto& dim:found_dims)if(dim.rec.style_handle==handle) {
            const auto paper=paper_index(dim.owner,dim.entmode);
            uses[paper>=0?paper_factor(paper):model_style_unit].push_back(&dim);
        }
        const double primary=uses.empty() || uses.contains(model_style_unit)?model_style_unit:uses.begin()->first;
        uses.try_emplace(primary);
        for(const auto& [unit,dimensions]:uses) {
            auto style=raw_style;
            // Native sizes are physical millimetres.
            if(unit!=primary) {
                style.name+="_Paper";
                while(used_style_names.contains(style.name))style.name+="_";
                used_style_names[style.name]=true;
            }
            style.dimasz*=unit*1000;style.dimtxt*=unit*1000;style.dimexo*=unit*1000;
            style.dimexe*=unit*1000;style.dimgap*=unit*1000;
            style.unit=gunit::Length::Metre;
            const bool feet_inches=style.format==gunit::Style::Engineering || style.format==gunit::Style::Architectural;
            style.dimlfac*=feet_inches?.0254/unit:1/unit;
            if(!std::isfinite(style.dimasz) || !std::isfinite(style.dimtxt) || !std::isfinite(style.dimexo) ||
               !std::isfinite(style.dimexe) || !std::isfinite(style.dimgap) || !std::isfinite(style.dimlfac)) {
                out.skipped.push_back("dimension style " + style.name + " exceeds supported physical sizes");
                continue;
            }
            for(auto* dim:dimensions)dim->rec.dim.style=style.name;
            out.dim_styles.push_back(std::move(style));
        }
    }
    for (auto& t : found_texts) {
        if(annotation_texts.contains(t.text.handle)) continue;
        if(style_names.contains(t.text.style_handle)) t.text.style=style_names.at(t.text.style_handle);
        t.text.layer = layer_name(t.text.layer_handle);
        if (const auto appearance = entity_attributes.find(t.text.handle); appearance != entity_attributes.end())
            t.text.attrs = appearance->second;
        if(const int index=paper_index(t.owner,t.entmode);index>=0) {
            gdxf::Text note; const auto factor=paper_factor(index);
            note.pos=scaled_point(t.text.pos,factor); note.text=t.text.text; note.height=t.text.height*factor; note.angle=t.text.angle;
            note.layer=t.text.layer; note.halign=t.text.halign; note.valign=t.text.valign;note.width=t.text.width*factor;note.style=t.text.style;note.attrs=t.text.attrs;
            out.layouts[std::size_t(index)].texts.push_back(std::move(note)); continue;
        }
        if(t.entmode==1 || (block_headers.contains(t.owner) && block_headers.at(t.owner).name.starts_with("*D"))) continue;
        if (t.entmode == 0) {
            const auto it = block_name_of.find(t.owner);
            if (it != block_name_of.end()) {
                if (gdoc::BlockDef* def = const_cast<gdoc::BlockDef*>(out.blocks.find(it->second))) {
                    gdoc::BlockDef::Note n;
                    n.pos = t.text.pos; n.text = t.text.text; n.height = t.text.height; n.angle = t.text.angle; n.layer = t.text.layer;
                    n.halign=t.text.halign; n.valign=t.text.valign;
                    n.width=t.text.width; n.style=t.text.style; n.attrs=t.text.attrs;
                    def->notes.push_back(std::move(n));
                }
                continue;
            }
        }
        out.texts.push_back(std::move(t.text));
    }
    for (auto& d : found_dims) {
        d.rec.layer = layer_name(d.rec.layer_handle);
        d.rec.dim.layer=d.rec.layer;
        if(const int index=paper_index(d.owner,d.entmode);index>=0) {
            auto dim=d.rec.dim; const auto factor=paper_factor(index);
            dim.transform_points([&](auto& point){point=scaled_point(point,factor);});
            dim.radius=gcm::i64(std::llround(double(dim.radius)*factor)); dim.layer=d.rec.layer;
            out.layouts[std::size_t(index)].dimensions.push_back(std::move(dim)); continue;
        }
        if (d.entmode == 0 && block_name_of.count(d.owner)) continue;
        out.dimensions.push_back(std::move(d.rec));
    }
    const auto apply_attributes=[&](gdoc::BlockRef& ref,u64 handle) {
        for(const auto& attrib:found_attributes) {
            if(attrib.owner!=handle) continue;
            ref.attributes.push_back({attrib.tag,attrib.text.text});
            gdoc::BlockNote note;
            // ATTRIB is in world space; undo the insertion so it is not applied twice.
            {
                const auto* def=out.blocks.find(ref.name);
                double bx=0,by=0;
                if(def) gcm::to_world(v,def->base,bx,by);
                double wx,wy,ix,iy;
                gcm::to_world(v,attrib.text.pos,wx,wy);
                gcm::to_world(v,ref.insertion,ix,iy);
                const double c=std::cos(ref.rotation),sn=std::sin(ref.rotation);
                const double dx=wx-ix,dy=wy-iy;
                const double sx=std::abs(ref.scale_x)>1e-12?ref.scale_x:1.0;
                const double sy=std::abs(ref.scale_y)>1e-12?ref.scale_y:1.0;
                note.pos=gcm::to_lattice(v,bx+(dx*c+dy*sn)/sx,by+(-dx*sn+dy*c)/sy);
            }
            note.text=attrib.text.text;
            {
                const double sy=std::abs(ref.scale_y)>1e-12?std::abs(ref.scale_y):1.0;
                const double sx=std::abs(ref.scale_x)>1e-12?std::abs(ref.scale_x):1.0;
                note.height=attrib.text.height/sy;
                note.width=attrib.text.width/sx;
            }
            note.angle=attrib.text.angle-ref.rotation;
            note.layer=layer_name(attrib.text.layer_handle);
            note.halign=attrib.text.halign; note.valign=attrib.text.valign;
            note.style=style_names.contains(attrib.text.style_handle)?style_names.at(attrib.text.style_handle):attrib.text.style;
            if(const auto appearance=entity_attributes.find(attrib.handle);appearance!=entity_attributes.end()) note.attrs=appearance->second;
            ref.notes.push_back(std::move(note));
        }
    };
    std::unordered_map<u64,std::pair<std::string,std::size_t>> nested_locations;
    for(const auto& ins:inserts) {
        if(ins.entmode!=0)continue;
        const auto parent=block_name_of.find(ins.owner),child=block_name_of.find(ins.block);
        if(parent==block_name_of.end()||child==block_name_of.end())continue;
        auto* def=const_cast<gdoc::BlockDef*>(out.blocks.find(parent->second));if(!def)continue;
        gdoc::BlockRef ref;ref.name=child->second;ref.insertion=gcm::to_lattice(v,ins.x,ins.y);
        ref.scale_x=ins.sx;ref.scale_y=ins.sy;ref.rotation=ins.rot;ref.layer=layer_name(ins.layer);
        ref.attrs=entity_attributes.at(ins.handle);
        apply_attributes(ref,ins.handle);
        nested_locations.emplace(ins.handle,std::make_pair(parent->second,def->nested_refs.size()));
        def->nested_refs.push_back(std::move(ref));
    }
    const auto placed_reference=[&](const FoundInsert& ins,const std::string& name) {
        gdoc::BlockRef ref;ref.name=name;ref.insertion=gcm::to_lattice(v,ins.x,ins.y);
        ref.scale_x=ins.sx;ref.scale_y=ins.sy;ref.rotation=ins.rot;ref.layer=layer_name(ins.layer);
        ref.attrs=entity_attributes.at(ins.handle);
        apply_attributes(ref,ins.handle);
        if(const int index=paper_index(ins.owner,ins.entmode);index>=0) {
            const auto factor=paper_factor(index);ref.insertion=scaled_point(ref.insertion,factor);
            const double stated_unit=out.insunits>=0?gdxf::metres_per(static_cast<gdxf::Units>(out.insunits)):1.0;
            const double model_unit=stated_unit>0?stated_unit:1.0;
            ref.scale_x*=factor/model_unit;ref.scale_y*=factor/model_unit;
        }
        return ref;
    };
    for (const auto& ins : inserts) {
        const auto it = block_name_of.find(ins.block);
        if (it == block_name_of.end()) continue;
        auto insert_name=it->second;
        const auto& bi=block_headers.at(ins.block);
        if (bi.xref) {
            const ClipInfo* clip=nullptr;
            if(ins.extension) {
                const auto ext=dictionaries.find(ins.extension);
                if(ext!=dictionaries.end() && ext->second.owner==ins.handle && ext->second.entries.size()==1 && ext->second.entries.contains("ACAD_FILTER")) {
                    const auto filter=dictionaries.find(ext->second.entries.at("ACAD_FILTER"));
                    if(filter!=dictionaries.end() && filter->second.owner==ins.extension && filter->second.entries.size()==1 && filter->second.entries.contains("SPATIAL")) {
                        const auto found=clips.find(filter->second.entries.at("SPATIAL"));
                        if(found!=clips.end() && found->second.owner==filter->first) clip=&found->second;
                    }
                }
            }
            if ((ins.extension && !clip) || !std::isfinite(ins.sx) || !std::isfinite(ins.sy) || !std::isfinite(ins.rot) || ins.sx==0 || ins.sy==0) {
                out.skipped.push_back("external reference "+bi.name+" has an unsupported transform or extension dictionary; native re-export is protected");
                std::size_t suffix=1;
                do { insert_name=bi.name+"_unmapped_"+std::to_string(suffix++); } while(out.blocks.find(insert_name));
                auto def=*out.blocks.find(bi.name); def.name=insert_name; out.blocks.define(std::move(def));
            } else {
                gdoc::Xref x; x.name=bi.name; x.path=bi.path;
                if (out.blocks.xref(x.name)) {
                    std::size_t suffix=2;
                    do { x.name=bi.name+"_"+std::to_string(suffix++); } while(out.blocks.find(x.name) || out.blocks.xref(x.name));
                }
                auto placement=placed_reference(ins,x.name);
                x.insertion=placement.insertion;x.scale=placement.scale_x;x.rotation=placement.rotation;
                x.type=bi.overlay?gdoc::XrefType::Overlay:gdoc::XrefType::Attach;
                x.state=bi.unloaded?gdoc::XrefState::Unloaded:gdoc::XrefState::Unresolved;
                x.nested=ins.entmode==0 && block_name_of.count(ins.owner);
                if(clip) { x.clip=clip->points; x.clip_enabled=clip->enabled; }
                if (x.name!=bi.name) {
                    auto def=*out.blocks.find(bi.name); def.name=x.name;
                    out.blocks.define(std::move(def));
                }
                if(const auto location=nested_locations.find(ins.handle);location!=nested_locations.end()) {
                    auto* parent=const_cast<gdoc::BlockDef*>(out.blocks.find(location->second.first));
                    parent->nested_refs[location->second.second].name=x.name;
                }
                if(!x.nested) {
                    if(const int index=paper_index(ins.owner,ins.entmode);index>=0)
                        out.layouts[std::size_t(index)].references.push_back(std::move(placement));
                    else out.blocks.insert(std::move(placement));
                }
                out.blocks.attach(std::move(x));
                continue;
            }
        }
        if (ins.entmode == 0 && block_name_of.count(ins.owner)) {
            if(const auto location=nested_locations.find(ins.handle);location!=nested_locations.end()) {
                auto* parent=const_cast<gdoc::BlockDef*>(out.blocks.find(location->second.first));
                parent->nested_refs[location->second.second].name=insert_name;
            }
            continue;
        }
        auto r=placed_reference(ins,insert_name);
        if(const int index=paper_index(ins.owner,ins.entmode);index>=0) {
            out.layouts[std::size_t(index)].references.push_back(std::move(r)); continue;
        }
        if(ins.entmode==1) continue;
        out.blocks.insert(r);

    }

    out.ok = true;
    if (out.objects.empty())
        out.error = "the file's object map is empty";
    return out;
}

ReadResult read_file(const std::string& path, const gcm::WorkingVolume& v) {
    ReadResult out;
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        out.error = "could not open " + path;
        return out;
    }
    std::fseek(f, 0, SEEK_END);
    const long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (n <= 0) {
        std::fclose(f);
        out.error = path + " is empty";
        return out;
    }
    std::vector<u8> bytes(static_cast<std::size_t>(n));
    const std::size_t got = std::fread(bytes.data(), 1, bytes.size(), f);
    std::fclose(f);
    bytes.resize(got);
    out=read(bytes.data(), bytes.size(), v);
    if (out.ok) for (auto x:out.blocks.xrefs()) {
        if (x.path.empty()) continue;
        auto portable=x.path; std::replace(portable.begin(),portable.end(),'\\','/');
        auto resolved=std::filesystem::path(portable);
        if (resolved.is_absolute() || !(portable.size()>1 && portable[1]==':')) {
            std::error_code ec;
            if (resolved.is_relative()) resolved=std::filesystem::absolute(std::filesystem::path(path).parent_path()/resolved,ec);
            if (!ec) x.resolved_path=resolved.lexically_normal().string();
        }
        out.blocks.attach(std::move(x));
    }
    return out;
}

}
