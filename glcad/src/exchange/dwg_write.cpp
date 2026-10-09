#include "exchange/dwg_write.hpp"
#include "exchange/dwg_text.hpp"
#include "exchange/dwg_clip.hpp"
#include "exchange/dimension_styles.hpp"
#include "exchange/region_export.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <map>
#include <filesystem>
#include <tuple>

namespace gdwg {
namespace {

using gcm::f64;
using gcm::i64;

// Each section is bracketed by a 16-byte marker and its bitwise complement.
constexpr u8 kHeaderSentinel[16] = {0x95, 0xA0, 0x4E, 0x28, 0x99, 0x82, 0x1A, 0xE5,
                                    0x5E, 0x41, 0xE0, 0x5F, 0x9D, 0x3A, 0x4D, 0x00};
constexpr u8 kVarsStart[16] = {0xCF, 0x7B, 0x1F, 0x23, 0xFD, 0xDE, 0x38, 0xA9,
                               0x5F, 0x7C, 0x68, 0xB8, 0x4E, 0x6D, 0x33, 0x5F};
constexpr u8 kVarsEnd[16] = {0x30, 0x84, 0xE0, 0xDC, 0x02, 0x21, 0xC7, 0x56,
                             0xA0, 0x83, 0x97, 0x47, 0xB1, 0x92, 0xCC, 0xA0};
constexpr u8 kClassStart[16] = {0x8D, 0xA1, 0xC4, 0xB8, 0xC4, 0xA9, 0xF8, 0xC5,
                                0xC0, 0xDC, 0xF4, 0x5F, 0xE7, 0xCF, 0xB6, 0x8A};
constexpr u8 kClassEnd[16] = {0x72, 0x5E, 0x3B, 0x47, 0x3B, 0x56, 0x07, 0x3A,
                              0x3F, 0x23, 0x0B, 0xA0, 0x18, 0x30, 0x49, 0x75};
constexpr u8 kSecondStart[16] = {0xD4, 0x7B, 0x21, 0xCE, 0x28, 0x93, 0x9F, 0xBF,
                                 0x53, 0x24, 0x40, 0x09, 0x12, 0x3C, 0xAA, 0x01};
constexpr u8 kSecondEnd[16] = {0x2B, 0x84, 0xDE, 0x31, 0xD7, 0x6C, 0x60, 0x40,
                               0xAC, 0xDB, 0xBF, 0xF6, 0xED, 0xC3, 0x55, 0xFE};

enum Type : u16 {
    kText = 1, kAttrib=2, kAttdef=3, kSeqend=6, kBlock = 4, kEndblk = 5, kInsert = 7, kEllipse = 35, kSpline = 36,
    kArc = 17, kCircle = 18, kLine = 19, kPoint = 27, kViewport = 34, kVpEntHdr = 71,
    kDictionary = 42, kMText = 44, kLeader = 45,
    kBlockControl = 48, kBlockHeader = 49,
    kLayerControl = 50, kLayer = 51,
    kStyleControl = 52, kStyle = 53,
    kLtypeControl = 56, kLtype = 57,
    kViewControl = 60, kUcsControl = 62, kVportControl = 64,
    kAppidControl = 66, kAppid = 67,
    kDimstyleControl = 68, kDimstyle = 69, kVpEntHdrControl = 70,
    kLwPolyline = 77,
};

enum Fixed : u64 {
    hBlockControl = 0x01, hLayerControl = 0x02, hStyleControl = 0x03,
    hLtypeControl = 0x05, hViewControl = 0x06, hUcsControl = 0x07,
    hVportControl = 0x08, hAppidControl = 0x09, hDimstyleControl = 0x0A,
    hVpEntHdrControl = 0x0B, hNamedDict = 0x0C, hGroupDict = 0x0D,
    hMlineDict = 0x0E,
    hModelSpace = 0x0F, hPaperSpace = 0x10,
    hLtypeByLayer = 0x11, hLtypeByBlock = 0x12, hLtypeContinuous = 0x13,
    hStyleStandard = 0x14, hAppidAcad = 0x15,
    hModelBlock = 0x16, hModelEndblk = 0x17,
    hPaperBlock = 0x18, hPaperEndblk = 0x19,
    hPlotstyles=0x1A, hPlotstyleNormal=0x1B,
    hFirstFree = 0x20,
};

Handle hard(u64 v) { return {3, v}; }        // hard owner
Handle soft(u64 v) { return {5, v}; }        // hard pointer
Handle owner(u64 v) { return {4, v}; }       // soft pointer
Handle null_h() { return {5, 0}; }

struct Obj {
    u64       handle{0};
    u16       type{0};
    BitWriter data;
    BitWriter refs;
};

struct Written {
    u64 handle;
    u32 offset;
};

void put_object(std::vector<u8>& out, std::vector<Written>& map, const Obj& o) {
    BitWriter probe;
    probe.bs(o.type);
    // Bitsize counts from the type field, not from the data or the size field.
    const u32 bitsize = static_cast<u32>(probe.bit_position() + 32 + o.data.bit_position());

    BitWriter body;
    body.bs(o.type);
    body.rl(bitsize);
    body.append(o.data);
    body.append(o.refs);
    body.align_byte();

    BitWriter whole;
    whole.ms(static_cast<u32>(body.bytes().size()));       // excludes the following CRC
    whole.append(body);

    map.push_back({o.handle, static_cast<u32>(out.size())});
    const std::vector<u8>& bytes = whole.bytes();
    out.insert(out.end(), bytes.begin(), bytes.end());
    const u16 crc = crc8(0xC0C1, bytes.data(), bytes.size());
    out.push_back(static_cast<u8>(crc & 0xFF));
    out.push_back(static_cast<u8>(crc >> 8));
}

void object_head(Obj& o, u64 handle, u16 type, u32 reactors=0,
                 const std::vector<std::pair<u64,u32>>& extended_longs={}) {
    o.handle = handle;
    o.type = type;
    o.data.handle({0, handle});
    for (const auto& [app,value] : extended_longs) {
        o.data.bs(5);
        o.data.handle(hard(app));
        o.data.rc(71);
        o.data.rl(value);
    }
    o.data.bs(0);        // end of extended entity data
    o.data.bl(reactors);
}
void object_tail(Obj& o, u64 parent, const std::vector<u64>& reactors={}) {
    o.refs.handle(owner(parent));
    for(auto ref:reactors)o.refs.handle(owner(ref));
    o.refs.handle({3, 0});     // no extension dictionary
}

// entmode 2: model space, no owner handle; 0: owner handle written (block).
u16 aci_color(const gdoc::Color& color) {
    if(color.kind==gdoc::Color::Kind::ByLayer) return 256;
    if(color.kind==gdoc::Color::Kind::ByBlock) return 0;
    if(color.kind==gdoc::Color::Kind::Index) return u16(color.index);
    u16 best=7; int distance=3*256*256;
    for(u16 i=1;i<256;++i) {
        u8 r,g,b; gdoc::aci_rgb(i,r,g,b);
        int dr=int(r)-color.r,dg=int(g)-color.g,db=int(b)-color.b;
        int d=dr*dr+dg*dg+db*db;
        if(d<distance) {distance=d; best=i;}
    }
    return best;
}
u8 weight_code(gdoc::Lineweight weight) {
    if (weight==gdoc::Lineweight::ByLayer) return 29;
    if (weight==gdoc::Lineweight::ByBlock) return 30;
    if (weight==gdoc::Lineweight::Default) return 31;
    const int ladder[]={0,5,9,13,15,18,20,25,30,35,40,50,53,60,70,80,90,100,106,120,140,158,200,211};
    for (u8 i=0;i<24;++i) if (ladder[i]==int(weight)) return i;
    return 31;
}
void entity_head(Obj& o, u64 handle, u16 type, u8 entmode, const gdoc::Attributes& attrs = {}, bool linked=false, u32 reactors=0) {
    o.handle = handle;
    o.type = type;
    o.data.handle({0, handle});
    o.data.bs(0);            // no extended entity data
    o.data.b(false);         // no cached graphic
    o.data.bb(entmode);
    o.data.bl(reactors);
    o.data.b(!linked);       // nolinks: linked spaces supply previous/next handles
    o.data.bs(aci_color(attrs.color));
    o.data.bd(attrs.linetype_scale);
    o.data.bb(attrs.linetype=="Continuous" ? 2 : attrs.linetype=="ByBlock" ? 1 : (attrs.linetype.empty()||attrs.linetype=="ByLayer") ? 0 : 3);
    o.data.bb(attrs.user.contains("plot_style") ? 3 : 0);
    o.data.bs(attrs.visible ? 0 : 1);
    o.data.rc(weight_code(attrs.lineweight));
}
void entity_tail(Obj& o, u8 entmode, u64 block, u64 layer, u64 extension=0, const std::pair<u64,u64>* links=nullptr,
                 const std::vector<u64>& reactors={}) {
    if (entmode == 0) o.refs.handle(owner(block));
    for(auto reactor:reactors)o.refs.handle(owner(reactor));
    o.refs.handle({3, extension});
    if(links) { o.refs.handle(owner(links->first)); o.refs.handle(owner(links->second)); }
    o.refs.handle(hard(layer));
}

struct Vars {
    BitWriter w;
    void bds(int n, f64 v = 0.0) { for (int i = 0; i < n; ++i) w.bd(v); }
    void bss(int n, u16 v = 0) { for (int i = 0; i < n; ++i) w.bs(v); }
    void bits(int n, bool v = false) { for (int i = 0; i < n; ++i) w.b(v); }
    void bd3(f64 x = 0, f64 y = 0, f64 z = 0) { w.bd3(x, y, z); }
    void rd2(f64 x = 0, f64 y = 0) { w.rd2(x, y); }
    void text(const std::string& s = {}) { w.tv(s); }
    void h(const Handle& v) { w.handle(v); }
    void cmc(u16 index = 256) { w.bs(index); }
};

std::vector<u8> header_vars(u16 insunits, u64 handseed, u64 clayer, const std::string& style_table, bool named, u64 dimstyle, u64 layout_dict, u64 paper_space, bool model_active) {
    Vars v;
    v.bds(4, 1.0);                       // unit conversion ratios
    v.text(); v.text(); v.text(); v.text();   // unit names
    v.w.bl(0); v.w.bl(0);                // unknown 8, 9
    v.h(null_h());                       // the current VX record: R2000 only
    v.bits(2);                           // DIMASO DIMSHO
    v.bits(7);                           // PLINEGEN .. LIMCHECK
    v.bits(4);                           // USRTIMER .. SPLFRAME
    v.bits(2);                           // MIRRTEXT WORLDVIEW
    v.w.b(model_active); v.w.b(false); v.w.b(true);   // TILEMODE PLIMCHECK VISRETAIN
    v.bits(2);                           // DISPSILH PELLIPSE
    v.bss(1);                            // PROXYGRAPHICS
    v.bss(1, 3);                         // TREEDEPTH
    v.w.bs(2); v.w.bs(4); v.w.bs(0); v.w.bs(0);   // LUNITS LUPREC AUNITS AUPREC
    v.bss(1);                            // ATTMODE
    v.bss(1);                            // PDMODE
    v.bss(5);                            // USERI1-5
    v.bss(14);                           // SPLINESEGS .. TEXTQLTY
    v.bds(9, 1.0);                       // LTSCALE .. PLINEWID
    v.bds(5);                            // USERR1-5
    v.bds(4);                            // CHAMFERA-D
    v.w.bd(0.5); v.w.bd(1.0); v.w.bd(1.0);   // FACETRES CMLSCALE CELTSCALE
    v.text();                            // MENU
    for (int i = 0; i < 4; ++i) v.w.bl(0);   // TDUCREATE TDUUPDATE
    for (int i = 0; i < 4; ++i) v.w.bl(0);   // TDINDWG TDUSRTIMER
    v.cmc();                             // CECOLOR
    v.h({0, handseed});                  // HANDSEED: inline in every version
    v.h(hard(clayer));                   // CLAYER
    v.h(hard(hStyleStandard));           // TEXTSTYLE
    v.h(hard(hLtypeByLayer));            // CELTYPE
    v.h(soft(dimstyle));                 // DIMSTYLE
    v.h(null_h());                       // CMLSTYLE
    v.w.bd(1.0);                         // PSVPSCALE
    v.bd3(); v.bd3(); v.bd3(); v.rd2(); v.rd2(); v.w.bd(0.0);   // paper space
    v.bd3(); v.bd3(1, 0, 0); v.bd3(0, 1, 0); v.h(null_h());     // PUCS
    v.h(null_h()); v.w.bs(0); v.h(null_h());
    for (int i = 0; i < 6; ++i) v.bd3();
    v.bd3(); v.bd3(); v.bd3(); v.rd2(); v.rd2(); v.w.bd(0.0);   // model space
    v.bd3(); v.bd3(1, 0, 0); v.bd3(0, 1, 0); v.h(null_h());     // UCS
    v.h(null_h()); v.w.bs(0); v.h(null_h());
    for (int i = 0; i < 6; ++i) v.bd3();
    v.text(); v.text();                  // DIMPOST DIMAPOST
    v.bds(9, 1.0);                       // DIMSCALE .. DIMTM
    v.bits(6);                           // DIMTOL .. DIMSE2
    v.bss(3);                            // DIMTAD DIMZIN DIMAZIN
    v.bds(8, 1.0);                       // DIMTXT .. DIMGAP
    v.w.bd(0.0);                         // DIMALTRND
    v.w.b(false); v.w.bs(2); v.bits(4);  // DIMALT DIMALTD DIMTOFL DIMSAH DIMTIX DIMSOXD
    v.cmc(0); v.cmc(0); v.cmc(0);        // DIMCLRD DIMCLRE DIMCLRT
    v.bss(11);                           // DIMADEC .. DIMJUST
    v.bits(2);                           // DIMSD1 DIMSD2
    v.bss(4);                            // DIMTOLJ DIMTZIN DIMALTZ DIMALTTZ
    v.w.b(false); v.w.bs(3);             // DIMUPT DIMATFIT
    for (int i = 0; i < 5; ++i) v.h(null_h());   // DIMTXSTY DIMLDRBLK DIMBLK 1 2
    v.w.bs(0xFFFF); v.w.bs(0xFFFF);      // DIMLWD DIMLWE
    v.h(hard(hBlockControl));
    v.h(hard(hLayerControl));
    v.h(hard(hStyleControl));
    v.h(hard(hLtypeControl));
    v.h(hard(hViewControl));
    v.h(hard(hUcsControl));
    v.h(hard(hVportControl));
    v.h(hard(hAppidControl));
    v.h(hard(hDimstyleControl));
    v.h(hard(hVpEntHdrControl));         // the VX control: R2000 only
    v.h(hard(hGroupDict));
    v.h(hard(hMlineDict));
    v.h(hard(hNamedDict));
    v.w.bs(0); v.w.bs(0);                // TSTACKALIGN TSTACKSIZE
    v.text(); v.w.tv(detail::encode_text(style_table)); // HYPERLINKBASE STYLESHEET
    v.h(soft(layout_dict)); v.h(null_h()); v.h(soft(hPlotstyles)); // layout, plotsettings, plotstyle
    v.w.bl(named?0:0x2000);              // FLAGS / PSTYLEMODE
    v.w.bs(insunits);                    // $INSUNITS
    v.w.bs(0);                           // CEPSNTYPE
    // Pre-2007 the two GUIDs follow inline; the reader verifies their shape.
    v.w.tv("{00000000-0000-0000-0000-000000000000}");
    v.w.tv("{00000000-0000-0000-0000-000000000000}");
    v.h(hard(paper_space)); v.h(hard(hModelSpace));
    v.h(hard(hLtypeByLayer)); v.h(hard(hLtypeByBlock)); v.h(hard(hLtypeContinuous));
    v.bss(4);
    v.w.align_byte();
    return v.w.bytes();
}

void put_sentinel(std::vector<u8>& out, const u8 (&s)[16]) {
    out.insert(out.end(), s, s + 16);
}
void put_u32(std::vector<u8>& out, u32 v) {
    out.push_back(u8(v)); out.push_back(u8(v >> 8));
    out.push_back(u8(v >> 16)); out.push_back(u8(v >> 24));
}
void patch_u32(std::vector<u8>& out, std::size_t at, u32 v) {
    out[at] = u8(v); out[at + 1] = u8(v >> 8);
    out[at + 2] = u8(v >> 16); out[at + 3] = u8(v >> 24);
}

// Marker, size, data, CRC, closing marker.
void put_section(std::vector<u8>& out, const u8 (&start)[16], const u8 (&end)[16],
                 const std::vector<u8>& data) {
    put_sentinel(out, start);
    const auto crc_start=out.size();
    put_u32(out, static_cast<u32>(data.size()));
    out.insert(out.end(), data.begin(), data.end());
    const u16 crc = crc8(0xC0C1, out.data()+crc_start, out.size()-crc_start);
    out.push_back(u8(crc & 0xFF));
    out.push_back(u8(crc >> 8));
    put_sentinel(out, end);
}

// Sections of at most 2032 bytes holding deltas.
std::vector<u8> object_map(const std::vector<Written>& objs) {
    std::vector<u8> out;
    std::size_t i = 0;
    u64 handle = 0;
    while (i < objs.size()) {
        BitWriter sec;
        // Only the handle carries across sections; offsets restart at zero.
        u32 offset = 0;
        std::size_t taken = 0;
        while (i + taken < objs.size()) {
            const Written& w = objs[i + taken];
            BitWriter probe;
            probe.umc(static_cast<u32>(w.handle - handle));
            probe.mc(static_cast<i32>(static_cast<i64>(w.offset) - static_cast<i64>(offset)));
            if ((sec.bit_position() + probe.bit_position()) / 8 + 2 > 2030) break;
            sec.append(probe);
            handle = w.handle;
            offset = w.offset;
            ++taken;
        }
        if (taken == 0) break;
        // The size counts itself and the data; the CRC follows outside it.
        const u16 size = static_cast<u16>(sec.bytes().size() + 2);
        std::vector<u8> section;
        section.push_back(u8(size >> 8));       // big-endian, alone in this format
        section.push_back(u8(size & 0xFF));
        section.insert(section.end(), sec.bytes().begin(), sec.bytes().end());
        const u16 crc = crc8(0xC0C1, section.data(), section.size());
        section.push_back(u8(crc >> 8));
        section.push_back(u8(crc & 0xFF));
        out.insert(out.end(), section.begin(), section.end());
        i += taken;
    }
    std::vector<u8> tail = {0x00, 0x02};
    const u16 crc = crc8(0xC0C1, tail.data(), tail.size());
    tail.push_back(u8(crc >> 8));
    tail.push_back(u8(crc & 0xFF));
    out.insert(out.end(), tail.begin(), tail.end());
    return out;
}

}

static std::vector<u8> write_impl(const gdxf::Document& input, const gcm::WorkingVolume& v,
                      const WriteOptions& o, std::string* error) {
    auto d=input;
    if (o.version != "AC1015") {
        if (error) *error = "this build writes AutoCAD 2000 (AC1015) only, not " + o.version;
        return {};
    }

    if (error) error->clear();
    bool offsets_supported=gexchange::detail::prepare_extension_offsets(d.dimensions,v);
    for(auto& layout:d.layouts)offsets_supported=gexchange::detail::prepare_extension_offsets(layout.dimensions,v)&&offsets_supported;
    if(!offsets_supported) {
        if(error)*error="R2000 export: unsupported or out-of-range dimension extension offset; save the native drawing to preserve it";
        return {};
    }
    const auto static_edge_dimension=[](gdim::Dimension& dim) {
        if(dim.o1.at==gdim::Dimension::Anchor::Edge || dim.o2.at==gdim::Dimension::Anchor::Edge)
            {dim.o1={};dim.o2={};}
    };
    for(auto& dim:d.dimensions)static_edge_dimension(dim);
    for(auto& layout:d.layouts)for(auto& dim:layout.dimensions)static_edge_dimension(dim);
    const auto fail=[&](const std::string& why) { if(error) *error="R2000 export: "+why+"; save the native drawing to preserve this content"; return std::vector<u8>{}; };

    std::string region_error;
    if(!gexchange::prepare_region_boundaries(d,region_error))return fail("invalid region boundary: "+region_error);
    const auto ascii=[](const std::string& value) { try { detail::encode_text(value); return true; } catch(...) { return false; } };
    for (const auto& x : input.blocks.xrefs()) {
        if (!o.write_blocks) return fail("external references require block output");
        if (x.name.empty() || x.name.front()=='*' || x.path.empty() || !ascii(x.name) || !ascii(x.path))
            return fail("invalid external reference name or path");
        auto suffix=x.path.substr(x.path.find_last_of('.')==std::string::npos?x.path.size():x.path.find_last_of('.'));
        for (char& ch:suffix) if(ch>='A' && ch<='Z') ch=char(ch-'A'+'a');
        if (suffix!=".dwg") return fail("external references must point to DWG drawings");
        if(x.nested) {
            bool owned=false;
            for(const auto& parent:input.blocks.all())
                for(const auto& ref:input.blocks.nested_insertions(parent))
                    if(ref.name==x.name)owned=true;
            if(!owned)return fail("nested external reference has no parent insertion");
        }
        if (x.clip_inverted) return fail("inverted external reference clipping is not supported by this writer");
        if (!detail::valid_clip(x.clip)) return fail("invalid external reference clipping boundary");
        if (!std::isfinite(x.scale) || x.scale==0 || !std::isfinite(x.rotation)) return fail("invalid external reference transform");
        const auto* existing=d.blocks.find(x.name);
        if (existing && !existing->is_xref) return fail("external reference name conflicts with a local block");
        gdoc::BlockDef def;
        def.name=x.name; def.is_xref=true; def.xref_path=x.path;
        if(existing) {def=*existing;def.xref_path=x.path;}
        d.blocks.define(std::move(def));
        bool placed=std::any_of(input.blocks.references().begin(),input.blocks.references().end(),[&](const auto& ref){return ref.name==x.name;});
        for(const auto& layout:input.layouts)
            placed|=std::any_of(layout.references.begin(),layout.references.end(),[&](const auto& ref){return ref.name==x.name;});
        if(!x.nested&&!placed) {
            gdoc::BlockRef ref; ref.name=x.name; ref.insertion=x.insertion;
            ref.scale_x=ref.scale_y=x.scale; ref.rotation=x.rotation;
            d.blocks.insert(std::move(ref));
        }
        const auto cached_layer="XREF|"+x.name;
        std::erase_if(d.entities,[&](const auto& e){return e.layer==cached_layer || e.shape.layer==cached_layer;});
        std::erase_if(d.texts,[&](const auto& t){return t.layer==cached_layer;});
        std::erase_if(d.dimensions,[&](const auto& dim){return dim.layer==cached_layer;});
        std::erase_if(d.hatches,[&](const auto& h){return h.layer==cached_layer;});
    }
    std::unordered_map<std::string,bool> dependent_blocks;
    std::vector<std::string> pending_dependencies;
    for(const auto& def:d.blocks.all())if(def.is_xref)
        for(const auto& ref:d.blocks.nested_insertions(def))pending_dependencies.push_back(ref.name);
    while(!pending_dependencies.empty()) {
        auto name=std::move(pending_dependencies.back());pending_dependencies.pop_back();
        if(!dependent_blocks.emplace(name,true).second)continue;
        if(const auto* def=d.blocks.find(name))
            for(const auto& ref:d.blocks.nested_insertions(*def))pending_dependencies.push_back(ref.name);
    }
    // Paper coordinates are millimetres, model coordinates INSUNITS.
    std::vector<int> entity_space(d.entities.size(),-1), text_space(d.texts.size(),-1),
        dim_space(d.dimensions.size(),-1), spline_space(d.splines.size(),-1),
        hatch_space(d.hatches.size(),-1), ref_space(d.blocks.references().size(),-1), underlay_space(d.underlays.size(),-1);
    for(std::size_t i=0;i<d.layouts.size();++i) {
        const auto& sheet=d.layouts[i];
        if(sheet.name.empty() || sheet.name=="Model" || !ascii(sheet.name) ||
           !std::isfinite(sheet.width) || !std::isfinite(sheet.height) || sheet.width<=0 || sheet.height<=0)
            return fail("invalid layout name or paper dimensions");
        for(std::size_t j=0;j<i;++j) if(d.layouts[j].name==sheet.name) return fail("duplicate layout name");
        const auto append=[&](auto& dst,auto& owners,const auto& src) {
            dst.insert(dst.end(),src.begin(),src.end()); owners.insert(owners.end(),src.size(),int(i));
        };
        append(d.entities,entity_space,sheet.entities); append(d.texts,text_space,sheet.texts);
        append(d.dimensions,dim_space,sheet.dimensions); append(d.splines,spline_space,sheet.splines);
        append(d.hatches,hatch_space,sheet.hatches);append(d.underlays,underlay_space,sheet.underlays);
        for(const auto& ref:sheet.references) { d.blocks.insert(ref); ref_space.push_back(int(i)); }
        for(const auto& vp:sheet.viewports) {
            if(!std::isfinite(vp.scale) || vp.scale<=0 || !std::isfinite(vp.rotation) ||
               !std::isfinite(vp.centre_x) || !std::isfinite(vp.centre_y) ||
               !std::isfinite(vp.lo.x) || !std::isfinite(vp.lo.y) || !std::isfinite(vp.hi.x) || !std::isfinite(vp.hi.y) ||
               vp.hi.x<=vp.lo.x || vp.hi.y<=vp.lo.y) return fail("invalid layout viewport");
            if(!vp.clip_polygon.empty()) {
                std::vector<gcm::Lattice2> clip;
                for(const auto point:vp.clip_polygon) clip.push_back(gcm::to_lattice(v,point.x,point.y));
                if(!detail::valid_clip(clip) || clip.size()<3 || clip.size()>256) return fail("invalid viewport clipping polygon");
            }
        }
    }
    const auto underlay_ok=[&](const gdoc::Underlay& u) {
        return u.kind!=gdoc::UnderlayKind::Raster&&u.kind!=gdoc::UnderlayKind::Unknown&&(u.kind==gdoc::UnderlayKind::Pdf||u.source_unit_known)&&!u.path.empty()&&ascii(u.path)&&ascii(u.sheet)&&
            std::isfinite(u.scale)&&u.scale!=0&&std::isfinite(u.y_factor)&&u.y_factor!=0&&std::isfinite(u.rotation)&&
            std::isfinite(u.source_unit)&&u.source_unit>0&&u.fade>=0&&u.fade<=80&&std::isfinite(u.contrast_neutral)&&u.contrast_neutral>0&&u.contrast*100/u.contrast_neutral>=20&&u.contrast*100/u.contrast_neutral<=100&&
            (u.clip.empty()||gclip::valid_boundary(u.clip))&&(u.clip_hole.empty()||gclip::valid_boundary(u.clip_hole))&&
            (!u.clip_inverted||(u.extent_hi.x>u.extent_lo.x&&u.extent_hi.y>u.extent_lo.y));
    };
    for(const auto& u:d.underlays)if(!underlay_ok(u))return fail("unsupported underlay source, appearance, placement or clipping");
    for(const auto& b:d.blocks.all())for(const auto& u:b.underlays)if(!underlay_ok(u))return fail("unsupported block underlay source, appearance, placement or clipping");
    const auto underlay_layer=[&](gdoc::Underlay& u) {
        const auto* original=d.layers.find(u.layer);
        if(original&&original->plottable==u.plottable&&original->locked==u.locked)return;
        gdoc::Layer layer=original?*original:gdoc::Layer{};
        const auto base=u.layer+"_Underlay";layer.name=base;
        for(unsigned suffix=2;d.layers.find(layer.name);++suffix)layer.name=base+std::to_string(suffix);
        layer.plottable=u.plottable;layer.locked=u.locked;u.layer=layer.name;d.layers.add(std::move(layer));
    };
    for(auto& u:d.underlays)underlay_layer(u);
    for(auto block:d.blocks.all()){for(auto& u:block.underlays)underlay_layer(u);d.blocks.define(std::move(block));}
    const auto attrs_ok=[&](const gdoc::Attributes& a) { return (o.quantize_true_color||a.color.kind!=gdoc::Color::Kind::True) && a.transparency==-1 && !a.has_foreign_data() && (a.linetype=="ByLayer"||a.linetype=="ByBlock"||a.linetype.empty()||d.linetypes.find(a.linetype)); };
    const auto shape_ok=[&](const gsnap::Shape& shape) { return attrs_ok(shape.attrs) && (shape.kind==gsnap::ShapeKind::Segment||shape.kind==gsnap::ShapeKind::Circle||shape.kind==gsnap::ShapeKind::Arc||shape.kind==gsnap::ShapeKind::Point||(shape.kind==gsnap::ShapeKind::Ellipse && (shape.a.x!=shape.b.x||shape.a.y!=shape.b.y))); };
    for (const auto& e:d.entities) if (!shape_ok(e.shape)) return fail("an entity type, true colour, transparency, linetype or foreign payload cannot be represented");
    for (const auto& t:d.texts) if (!ascii(t.text)||!attrs_ok(t.attrs)) return fail("invalid text or unsupported text appearance");
    for (const auto& l:d.layers.all()) if (!ascii(l.name)||(!o.quantize_true_color&&l.color.kind==gdoc::Color::Kind::True)||l.transparency<0||l.transparency>100||(!l.linetype.empty()&&!d.linetypes.find(l.linetype))) return fail("a layer name or appearance cannot be represented");
    for (const auto& b:d.blocks.all()) {

        if (b.is_xref && !input.blocks.xref(b.name)) return fail("external reference definition has no reference metadata");
        if (!ascii(b.name)||d.blocks.is_dynamic(b.name)) return fail("dynamic blocks require a later DWG version");
        for(const auto& a:b.attributes) if(a.multiline||a.lock_position||!ascii(a.tag)||!ascii(a.prompt)||!ascii(a.default_value)) return fail("multiline, position-locked or invalid block attribute");
        for(const auto& n:b.notes) if(!ascii(n.text)||!attrs_ok(n.attrs)) return fail("invalid block text or unsupported text appearance");
        for(const auto& ref:d.blocks.nested_insertions(b)) {
            const auto* child=d.blocks.find(ref.name);if(!child)return fail("missing nested block definition");
            if(!attrs_ok(ref.attrs)||!std::isfinite(ref.scale_x)||!std::isfinite(ref.scale_y)||!std::isfinite(ref.rotation)||ref.scale_x==0||ref.scale_y==0)return fail("unsupported nested insertion appearance or transform");
            for(const auto& value:ref.attributes)if(!ascii(value.value)||!child->attribute(value.tag))return fail("invalid nested insertion attribute");
        }
        for (const auto& shape:b.geometry) if (!shape_ok(shape)) return fail("unsupported block geometry or appearance");
    }
    for (const auto& curve:d.splines) if (!curve.valid()) return fail("invalid spline");
    for(const auto& dim:d.dimensions)if(dim.kind==gdim::Kind::Leader) {
        const auto paths=dim.paths();
        if(paths.size()!=1||paths.front().vertices.size()<2||paths.front().vertices.size()>4096)return fail("leader requires a supported path with 2 to 4096 vertices");
    }
    for (const auto& lt:d.linetypes.all()) {
        std::size_t text_size=1;
        for (const auto& dash:lt.pattern) { if(!ascii(dash.text)) return fail("invalid or oversized linetype label"); text_size+=dash.text.empty()?0:detail::encode_text(dash.text).size()+1; }
        if(lt.pattern.size()>255||text_size>256) return fail("linetype exceeds R2000 limits");
    }
    for (const auto& r:d.blocks.references()) {
        if (!o.write_blocks||!d.blocks.find(r.name)) return fail("a block insertion cannot be represented");
        if(!attrs_ok(r.attrs))return fail("block insertion appearance cannot be represented");
        if(!std::isfinite(r.scale_x) || !std::isfinite(r.scale_y) || !std::isfinite(r.rotation) || r.scale_x==0 || r.scale_y==0) return fail("invalid block insertion transform");
    }
    for(const auto& ref:d.blocks.references()) for(const auto& value:ref.attributes) {
        const auto* def=d.blocks.find(ref.name);
        if(!ascii(value.value)||std::none_of(def->attributes.begin(),def->attributes.end(),[&](const auto& a){return a.tag==value.tag;})) return fail("block attribute has no definition or invalid text");
    }

    struct AssociationPointPlan {
        u8 index{0}, osnap{0};
        std::size_t entity{0};
        double parameter{0};
        gcm::Lattice2 point{};
    };
    struct AssociationPlan {
        u64 dictionary{0}, object{0};
        std::vector<AssociationPointPlan> points;
    };
    std::vector<AssociationPlan> dimension_associations(d.dimensions.size());
    std::map<std::pair<int,u32>,std::vector<std::size_t>> keyed_geometry;
    for(std::size_t i=0;i<d.entities.size();++i)if(d.entities[i].shape.key)
        keyed_geometry[{entity_space[i],d.entities[i].shape.key}].push_back(i);
    for(std::size_t i=0;i<d.dimensions.size();++i) {
        auto& dim=d.dimensions[i];if(!dim.associative())continue;
        if(dim.kind==gdim::Kind::Radial || dim.kind==gdim::Kind::Diameter) {
            if(!dim.o1.bound() || dim.o1.at!=gdim::Dimension::Anchor::Centre || dim.o2.bound())
                return fail("radial dimension has unsupported geometry anchors");
            const auto found=keyed_geometry.find({dim_space[i],dim.o1.shape});
            if(found==keyed_geometry.end())return fail("radial dimension association references missing geometry in its drawing space");
            if(found->second.size()!=1)return fail("radial dimension association references a duplicate geometry identity");
            const auto entity=found->second.front();const auto& shape=d.entities[entity].shape;
            if(!shape.is_curve() || shape.radius<=0)return fail("radial dimension association requires a circle or circular arc");
            dim.centre=shape.a;dim.radius=shape.radius;dim.orphaned=false;
            const auto points=dim.radial_points();dim.p1=points.first;dim.p2=points.second;
            for(u8 index=0;index<2;++index) {
                AssociationPointPlan point;point.index=index;point.entity=entity;
                point.point=index==0?points.first:points.second;
                point.osnap=index==1 && dim.kind==gdim::Kind::Radial?3:10;
                if(point.osnap==10) {
                    const double tau=2*std::acos(-1.);
                    point.parameter=std::atan2(double(point.point.y)-shape.a.y,double(point.point.x)-shape.a.x);
                    if(point.parameter<0)point.parameter+=tau;
                    if(shape.kind==gsnap::ShapeKind::Arc &&
                       !gsnap::angle_within_arc(point.parameter,shape.start_angle,shape.end_angle))
                        return fail("radial dimension chord is outside its associated arc");
                }
                dimension_associations[i].points.push_back(point);
            }
            continue;
        }
        if(dim.kind!=gdim::Kind::Linear && dim.kind!=gdim::Kind::Horizontal &&
           dim.kind!=gdim::Kind::Vertical && dim.kind!=gdim::Kind::Aligned)
            return fail("this dimension kind has unsupported DWG geometry associations");
        for(u8 index=0;index<2;++index) {
            const auto& ref=index==0?dim.o1:dim.o2;if(!ref.bound())continue;
            const auto found=keyed_geometry.find({dim_space[i],ref.shape});
            if(found==keyed_geometry.end())return fail("dimension association references missing geometry in its drawing space");
            if(found->second.size()!=1)return fail("dimension association references a duplicate geometry identity");
            AssociationPointPlan point;point.index=index;point.entity=found->second.front();
            const auto& shape=d.entities[point.entity].shape;
            if(!gdim::Dimension::anchor_point(shape,ref.at,point.point))
                return fail("dimension association references an unavailable geometry anchor");
            point.osnap=ref.at==gdim::Dimension::Anchor::Centre?3:ref.at==gdim::Dimension::Anchor::Start?13:1;
            if(shape.kind==gsnap::ShapeKind::Point)point.osnap=4;
            point.parameter=ref.at==gdim::Dimension::Anchor::End?1:0;
            if(ref.at!=gdim::Dimension::Anchor::Centre && (shape.kind==gsnap::ShapeKind::Arc || shape.is_elliptical()))
                point.parameter=ref.at==gdim::Dimension::Anchor::Start?shape.start_angle:shape.end_angle;
            dimension_associations[i].points.push_back(point);
            (index==0?dim.p1:dim.p2)=point.point;
        }
        dim.orphaned=false;
    }

    const f64 unit = gdxf::metres_per(o.units);
    const f64 model_to_file = unit > 0.0 ? 1.0 / unit : 1.0;
    f64 to_file = model_to_file;
    std::map<std::string,std::string> paper_dim_styles;
    if(!o.write_blocks&&!d.dimensions.empty()) return fail("dimensions require their display blocks");
    if(!gexchange::detail::prepare_leader_styles(d.dimensions,d.dim_styles,d.has_dim_style?d.dim_style:gdim::Style{}))return fail("invalid leader arrow size or style scale");
    const auto dim_style_for=[&](const gdim::Dimension& dim)->const gdim::Style& {
        for(const auto& style:d.dim_styles) if(style.name==dim.style) return style;
        return d.dim_styles.front();
    };
    // Paper dimensions use millimetres even when model space does not.
    if(model_to_file!=1000.0) for(std::size_t i=0;i<d.dimensions.size();++i) {
        if(dim_space[i]<0) continue;
        auto& dim=d.dimensions[i]; const auto original=dim.style;
        if(!paper_dim_styles.contains(original)) {
            auto style=dim_style_for(dim); style.name+="_Paper";
            while(std::any_of(d.dim_styles.begin(),d.dim_styles.end(),[&](const auto& s){return s.name==style.name;})) style.name+="_";
            paper_dim_styles[original]=style.name; d.dim_styles.push_back(std::move(style));
        }
        dim.style=paper_dim_styles.at(original);
    }
    std::vector<std::string> dimension_blocks;
    for(const auto& dim:d.dimensions) {
        if(dim.kind==gdim::Kind::Leader) {dimension_blocks.push_back("");continue;}
        if(dim.kind==gdim::Kind::ArcLength) return fail("arc-length dimensions require a later DWG version");
        auto geometry=gdim::build(dim,dim_style_for(dim),v);
        if(!geometry.ok) return fail("invalid dimension geometry");
        std::string name="*D"+std::to_string(dimension_blocks.size()+1);
        while(d.blocks.find(name)) name+="_";
        gdoc::BlockDef block; block.name=name; block.geometry=geometry.lines;
        block.geometry.insert(block.geometry.end(),geometry.arrows.begin(),geometry.arrows.end());
        block.notes.push_back({geometry.text_at,geometry.text,geometry.text_height,geometry.text_angle,dim.layer});
        d.blocks.define(std::move(block)); dimension_blocks.push_back(std::move(name));
    }


    const auto world = [&](const gcm::Lattice2& p, f64& x, f64& y) {
        gcm::to_world(v, p, x, y);
        x *= to_file;
        y *= to_file;
    };
    const auto radius_of = [&](i64 r) { return (double(r) / v.scale()) * to_file; };

    struct HatchEdge { gsnap::Shape shape; bool ccw{true}; };
    std::vector<std::vector<std::vector<HatchEdge>>> hatch_paths;
    gdoc::PatternTable patterns;
    for(auto& hatch:d.hatches) {
        if(hatch.boundary.empty()||!std::isfinite(hatch.scale)||hatch.scale<=0||!std::isfinite(hatch.angle)) return fail("invalid hatch");
        if(!hatch.solid&&hatch.pattern_lines.empty()) { auto* pattern=patterns.find(hatch.pattern); if(!pattern) return fail("hatch pattern definition is missing"); hatch.pattern_lines=pattern->lines; }
        std::vector<std::vector<HatchEdge>> paths;
        std::vector<HatchEdge> edges;
        for(const auto& shape:hatch.boundary) {
            if(shape.kind==gsnap::ShapeKind::Circle||shape.kind==gsnap::ShapeKind::Ellipse) paths.push_back({{shape}});
            else if(shape.kind==gsnap::ShapeKind::Segment||shape.kind==gsnap::ShapeKind::Arc) edges.push_back({shape});
            else return fail("unsupported hatch boundary");
        }
        const auto ends=[&](const HatchEdge& edge) {
            auto a=edge.shape.a,b=edge.shape.b;
            if(edge.shape.kind==gsnap::ShapeKind::Arc) {
                double x,y; gcm::to_world(v,edge.shape.a,x,y); double radius=double(edge.shape.radius)/v.scale();
                a=gcm::to_lattice(v,x+radius*std::cos(edge.shape.start_angle),y+radius*std::sin(edge.shape.start_angle));
                b=gcm::to_lattice(v,x+radius*std::cos(edge.shape.end_angle),y+radius*std::sin(edge.shape.end_angle));
            }
            if(!edge.ccw) std::swap(a,b); return std::pair{a,b};
        };
        const auto near=[](auto a,auto b){return std::abs(double(a.x)-double(b.x))<=2&&std::abs(double(a.y)-double(b.y))<=2;};
        while(!edges.empty()) {
            std::vector<HatchEdge> path{edges.back()}; edges.pop_back();
            auto [start,end]=ends(path[0]);
            while(!near(start,end)) {
                bool found=false;
                for(std::size_t i=0;i<edges.size();++i) {
                    auto [a,b]=ends(edges[i]);
                    if(near(end,b)) { edges[i].ccw=!edges[i].ccw; std::swap(a,b); }
                    if(!near(end,a)) continue;
                    path.push_back(edges[i]); edges.erase(edges.begin()+i); end=b; found=true; break;
                }
                if(!found) return fail("hatch boundary is not closed");
            }
            paths.push_back(std::move(path));
        }
        hatch_paths.push_back(std::move(paths));
    }

    // Handles are assigned first: reference widths are part of the bit stream.
    u64 next = hFirstFree;
    const bool transparent_layers=std::any_of(d.layers.all().begin(),d.layers.all().end(),
        [](const auto& layer){return layer.transparency>0;});
    const u64 transparency_appid=transparent_layers?next++:0;
    std::unordered_map<std::string, u64> layer_handle;
    std::vector<const gdoc::Layer*> layers;
    for (const gdoc::Layer& l : d.layers.all()) {
        layers.push_back(&l);
        layer_handle[l.name] = next++;
    }
    if (layer_handle.empty()) { layer_handle["0"] = next++; }
    const auto layer_of = [&](const std::string& name) {
        const auto it = layer_handle.find(name);
        return it != layer_handle.end() ? it->second : layer_handle.begin()->second;
    };

    std::unordered_map<std::string,u64> ltype_handle{{"Continuous",hLtypeContinuous},{"ByLayer",hLtypeByLayer},{"ByBlock",hLtypeByBlock}};
    for(const auto& lt:d.linetypes.all()) if(!ltype_handle.contains(lt.name)) ltype_handle[lt.name]=next++;
    const auto ltype_of=[&](const std::string& name) { auto it=ltype_handle.find(name); return it==ltype_handle.end()?u64(hLtypeContinuous):it->second; };

    std::unordered_map<std::string,u64> style_handles{{"Standard",hStyleStandard}};
    auto text_styles=d.text_styles;
    if(std::none_of(text_styles.begin(),text_styles.end(),[](const auto& style){return style.name=="Standard";})) text_styles.push_back({});
    for(const auto& style:text_styles) if(style.name!="Standard") style_handles[style.name]=next++;
    const auto style_of=[&](const std::string& name) { auto it=style_handles.find(name); return it==style_handles.end()?u64(hStyleStandard):it->second; };

    std::map<std::string,u64> plotstyle_handles{{"Normal",hPlotstyleNormal}};
    const auto register_plotstyle=[&](const std::string& name) { if(!name.empty()&&!plotstyle_handles.contains(name)) plotstyle_handles[name]=next++; };
    for(const auto& layer:d.layers.all()) register_plotstyle(layer.plot_style);
    for(const auto& entity:d.entities) if(auto it=entity.shape.attrs.user.find("plot_style");it!=entity.shape.attrs.user.end()) register_plotstyle(it->second);
    for(const auto& block:d.blocks.all()) for(const auto& shape:block.geometry) if(auto it=shape.attrs.user.find("plot_style");it!=shape.attrs.user.end()) register_plotstyle(it->second);
    for(const auto& ref:d.blocks.references()) if(auto it=ref.attrs.user.find("plot_style");it!=ref.attrs.user.end()) register_plotstyle(it->second);
    for(const auto& block:d.blocks.all())for(const auto& ref:d.blocks.nested_insertions(block))if(auto it=ref.attrs.user.find("plot_style");it!=ref.attrs.user.end())register_plotstyle(it->second);
    for(const auto& text:d.texts) if(auto it=text.attrs.user.find("plot_style");it!=text.attrs.user.end()) register_plotstyle(it->second);
    for(const auto& block:d.blocks.all()) for(const auto& text:block.notes) if(auto it=text.attrs.user.find("plot_style");it!=text.attrs.user.end()) register_plotstyle(it->second);
    const auto plotstyle_of=[&](const std::string& name) { auto it=plotstyle_handles.find(name); return it==plotstyle_handles.end()?u64(hPlotstyleNormal):it->second; };

    struct BlockPlan {
        const gdoc::BlockDef* def{nullptr};
        u64 header{0}, block{0}, endblk{0};
        std::vector<u64> entities;
        std::vector<gdoc::BlockRef> nested;
        std::vector<std::vector<u64>> attributes;
        std::vector<u64> seqends;
    };
    std::vector<BlockPlan> blocks;
    if (o.write_blocks) {
        for (const gdoc::BlockDef& b : d.blocks.all()) {
            BlockPlan p;
            p.def = &b;
            p.header = next++;
            p.block = next++;
            p.endblk = next++;
            p.nested=d.blocks.nested_insertions(b);
            for (std::size_t i = 0; i < b.geometry.size()+b.notes.size()+b.attributes.size()+p.nested.size(); ++i) p.entities.push_back(next++);
            for(const auto& ref:p.nested) {
                std::vector<u64> attrs;for(const auto& attr:d.blocks.find(ref.name)->attributes)if(!attr.constant)attrs.push_back(next++);
                p.seqends.push_back(attrs.empty()?0:next++);p.attributes.push_back(std::move(attrs));
            }
            blocks.push_back(std::move(p));
        }
    }

    std::vector<u64> model_entities;
    for (std::size_t i = 0; i < d.entities.size(); ++i) model_entities.push_back(next++);
    std::vector<u64> model_texts;
    for (std::size_t i = 0; i < d.texts.size(); ++i) model_texts.push_back(next++);
    std::vector<u64> model_refs;
    for (const auto& ref:d.blocks.references()) { (void)ref; model_refs.push_back(next++); }
    std::vector<u64> model_splines;
    for(const auto& curve:d.splines) { (void)curve; model_splines.push_back(next++); }
    std::vector<u64> model_dims; for(const auto& dim:d.dimensions) { (void)dim; model_dims.push_back(next++); }
    std::unordered_map<u64,std::vector<u64>> association_reactors;
    for(std::size_t i=0;i<dimension_associations.size();++i) {
        auto& plan=dimension_associations[i];if(plan.points.empty())continue;
        plan.dictionary=next++;plan.object=next++;
        association_reactors[model_dims[i]].push_back(plan.object);
        for(const auto& point:plan.points) {
            auto& reactors=association_reactors[model_entities[point.entity]];
            if(std::find(reactors.begin(),reactors.end(),plan.object)==reactors.end())reactors.push_back(plan.object);
        }
    }
    for(const auto& [handle,reactors]:association_reactors)if(reactors.size()>4096)
        return fail("geometry has more than 4096 dimension association reactors");
    std::vector<u64> leader_texts;for(const auto& dim:d.dimensions) leader_texts.push_back(dim.kind==gdim::Kind::Leader&&!dim.text_override.empty()?next++:0);
    std::unordered_map<std::string,u64> dimstyle_handles; for(const auto& style:d.dim_styles) dimstyle_handles[style.name]=next++;
    std::vector<u64> model_hatches; for(const auto& h:d.hatches) { (void)h; model_hatches.push_back(next++); }
    std::vector<std::vector<u64>> insert_attributes; std::vector<u64> insert_seqend;
    for(const auto& ref:d.blocks.references()) {
        std::vector<u64> attrs; for(const auto& def:d.blocks.find(ref.name)->attributes) if(!def.constant) attrs.push_back(next++);
        insert_seqend.push_back(attrs.empty()?0:next++); insert_attributes.push_back(std::move(attrs));
    }
    struct ClipPlan { u64 extension{0}, dictionary{0}, filter{0}; };
    std::unordered_map<u64,ClipPlan> clip_plans;
    const auto plan_clip=[&](u64 handle,const gdoc::BlockRef& ref) {
        const auto* x=input.blocks.xref(ref.name);
        if(x&&!x->clip.empty())clip_plans.emplace(handle,ClipPlan{next++,next++,next++});
    };
    for(std::size_t i=0;i<model_refs.size();++i) {
        plan_clip(model_refs[i],d.blocks.references()[i]);
    }
    for(const auto& p:blocks)for(std::size_t i=0;i<p.nested.size();++i)
        plan_clip(p.entities[p.def->geometry.size()+p.def->notes.size()+p.def->attributes.size()+i],p.nested[i]);
    struct ViewportPlan { u64 entity, header, clip; };
    struct SpacePlan {
        u64 layout, header, block, endblk;
        std::vector<u64> owned;
        std::vector<ViewportPlan> viewports;
    };
    const u64 layout_dict=next++, model_layout=next++;
    std::vector<SpacePlan> spaces;
    if(d.layouts.empty()) d.layouts.emplace_back();
    for(std::size_t i=0;i<d.layouts.size();++i) {
        SpacePlan p{next++,i?next++:u64(hPaperSpace),i?next++:u64(hPaperBlock),i?next++:u64(hPaperEndblk),{}, {}};
        // The first viewport describes the paper-space camera (viewport 1).
        p.viewports.push_back({next++,next++,0});
        for(const auto& vp:d.layouts[i].viewports) p.viewports.push_back({next++,next++,vp.clip_polygon.empty()?0:next++});
        for(const auto& vp:p.viewports) { p.owned.push_back(vp.entity); if(vp.clip) p.owned.push_back(vp.clip); }
        spaces.push_back(std::move(p));
    }
    const auto context=[&](int space) {
        to_file=space<0?model_to_file:1000.0;
        return std::pair<u8,u64>{space<0?u8(2):u8(0),space<0?u64(hModelSpace):spaces[std::size_t(space)].header};
    };
    const u64 current_paper=d.active_layout>=0 && std::size_t(d.active_layout)<spaces.size()?spaces[std::size_t(d.active_layout)].header:hPaperSpace;
    struct UnderlayPlan {const gdoc::Underlay* value;u64 handle,definition,owner;int space;};
    struct DefinitionPlan {const gdoc::Underlay* value;u64 handle;std::vector<u64> references;};
    std::map<int,u64> underlay_dicts;
    std::map<std::tuple<int,std::string,std::string>,DefinitionPlan> definitions;
    std::vector<UnderlayPlan> underlays;
    const auto plan_underlay=[&](const gdoc::Underlay& u,u64 owner,int space) {
        const auto kind=int(u.kind);if(!underlay_dicts.contains(kind))underlay_dicts[kind]=next++;
        const auto key=std::tuple{kind,u.path,u.sheet};
        if(!definitions.contains(key))definitions.emplace(key,DefinitionPlan{&u,next++,{}});
        auto& definition=definitions.at(key);const auto handle=next++;
        definition.references.push_back(handle);underlays.push_back({&u,handle,definition.handle,owner,space});
        return handle;
    };
    for(std::size_t i=0;i<d.underlays.size();++i)plan_underlay(d.underlays[i],underlay_space[i]<0?hModelSpace:spaces[std::size_t(underlay_space[i])].header,underlay_space[i]);
    for(auto& block:blocks)for(const auto& u:block.def->underlays)block.entities.push_back(plan_underlay(u,block.header,-2));
    const u64 handseed = next;

    std::vector<u64> owned_model;
    const auto own=[&](const auto& handles,const auto& owners) {
        for(std::size_t i=0;i<handles.size();++i) if(handles[i])
            (owners[i]<0?owned_model:spaces[std::size_t(owners[i])].owned).push_back(handles[i]);
    };
    own(model_entities,entity_space); own(model_texts,text_space); own(model_refs,ref_space);
    own(model_splines,spline_space); own(model_dims,dim_space); own(leader_texts,dim_space); own(model_hatches,hatch_space);
    for(const auto& u:underlays)if(u.space>=-1)(u.space<0?owned_model:spaces[std::size_t(u.space)].owned).push_back(u.handle);
    std::unordered_map<u64,std::pair<u64,u64>> entity_links;
    const auto link=[&](const std::vector<u64>& owned) {
        for(std::size_t i=0;i<owned.size();++i) entity_links[owned[i]]={i?owned[i-1]:0,i+1<owned.size()?owned[i+1]:0};
    };
    link(owned_model); for(const auto& space:spaces) link(space.owned);
    for(const auto& block:blocks)link(block.entities);
    const std::vector<u64> no_reactors;
    const auto reactors_for=[&](u64 handle)->const std::vector<u64>& {
        const auto found=association_reactors.find(handle);
        return found==association_reactors.end()?no_reactors:found->second;
    };
    const auto entity_head=[&](Obj& e,u64 handle,u16 type,u8 mode,const gdoc::Attributes& attrs=gdoc::Attributes{}) {
        gdwg::entity_head(e,handle,type,mode,attrs,entity_links.contains(handle),u32(reactors_for(handle).size()));
    };
    const auto entity_tail=[&](Obj& e,u8 mode,u64 block,u64 layer,u64 extension=0) {
        const auto links=entity_links.find(e.handle);
        gdwg::entity_tail(e,mode,block,layer,extension,links==entity_links.end()?nullptr:&links->second,reactors_for(e.handle));
    };

    std::vector<u8> body;
    std::vector<Written> map;

    {
        Obj c; object_head(c, hBlockControl, kBlockControl);
        c.data.bl(static_cast<u32>(blocks.size()+spaces.size()-1));
        object_tail(c, hNamedDict);
        for (const BlockPlan& b : blocks) c.refs.handle(hard(b.header));
        for(const auto& space:spaces) if(space.header!=current_paper) c.refs.handle(hard(space.header));
        c.refs.handle(hard(hModelSpace));
        c.refs.handle(hard(current_paper));
        put_object(body, map, c);
    }
    {
        Obj c; object_head(c, hLayerControl, kLayerControl);
        c.data.bl(static_cast<u32>(layer_handle.size()));
        object_tail(c, hNamedDict);
        for (const gdoc::Layer* l : layers) c.refs.handle(hard(layer_of(l->name)));
        put_object(body, map, c);
    }
    {
        Obj c; object_head(c, hStyleControl, kStyleControl);
        c.data.bl(u32(text_styles.size()));
        object_tail(c, hNamedDict);
        for(const auto& style:text_styles) c.refs.handle(hard(style_of(style.name)));
        put_object(body, map, c);
    }
    {
        Obj c; object_head(c, hLtypeControl, kLtypeControl);
        c.data.bl(static_cast<u32>(ltype_handle.size()-2));
        object_tail(c, hNamedDict);
        c.refs.handle(hard(hLtypeContinuous));
        for(const auto& lt:d.linetypes.all()) if(lt.name!="Continuous"&&lt.name!="ByLayer"&&lt.name!="ByBlock") c.refs.handle(hard(ltype_of(lt.name)));
        c.refs.handle(hard(hLtypeByBlock));
        c.refs.handle(hard(hLtypeByLayer));
        put_object(body, map, c);
    }
    for (const auto& [h, t] : std::initializer_list<std::pair<u64, u16>>{
             {hViewControl, kViewControl}, {hUcsControl, kUcsControl},
             {hVportControl, kVportControl}, {hDimstyleControl, kDimstyleControl},
             {hVpEntHdrControl, kVpEntHdrControl}}) {
        Obj c; object_head(c, h, t);
        u32 count=0; if(t==kVpEntHdrControl) for(const auto& p:spaces) count+=u32(p.viewports.size());
        c.data.bl(t==kDimstyleControl?u32(d.dim_styles.size()):count);
        if (t==kDimstyleControl) c.data.rc(0);
        object_tail(c, hNamedDict);
        if(t==kDimstyleControl) for(const auto& style:d.dim_styles) c.refs.handle(hard(dimstyle_handles.at(style.name)));
        if(t==kVpEntHdrControl) for(const auto& p:spaces) for(const auto& vp:p.viewports) c.refs.handle(hard(vp.header));
        put_object(body, map, c);
    }
    {
        Obj c; object_head(c, hAppidControl, kAppidControl);
        c.data.bl(transparency_appid?2:1);
        object_tail(c, hNamedDict);
        c.refs.handle(hard(hAppidAcad));
        if(transparency_appid)c.refs.handle(hard(transparency_appid));
        put_object(body, map, c);
    }

    for (const auto& [h, parent] : std::initializer_list<std::pair<u64, u64>>{
             {hNamedDict, 0}, {hGroupDict, hNamedDict}, {hMlineDict, hNamedDict}}) {
        Obj c; object_head(c, h, kDictionary);
        c.data.bl(h==hNamedDict?u32(2+underlay_dicts.size()):0); // plot style dictionary belongs to the NOD
        c.data.bs(0);              // cloning: keep existing
        c.data.rc(0);              // not a hard owner
        if(h==hNamedDict) { c.data.tv("ACAD_PLOTSTYLENAME"); c.data.tv("ACAD_LAYOUT"); for(const auto& [kind,id]:underlay_dicts)c.data.tv(kind==0?"ACAD_PDFDEFINITIONS":kind==1?"ACAD_DWFDEFINITIONS":"ACAD_DGNDEFINITIONS"); }
        object_tail(c, parent);
        if(h==hNamedDict) { c.refs.handle(hard(hPlotstyles)); c.refs.handle(hard(layout_dict)); for(const auto& [kind,id]:underlay_dicts)c.refs.handle(hard(id)); }
        put_object(body, map, c);
    }

    {
        Obj c; object_head(c,hPlotstyles,502); c.data.bl(u32(plotstyle_handles.size())); c.data.bs(0); c.data.rc(1);
        for(const auto& [kind,id]:underlay_dicts) {
        Obj dict;object_head(dict,id,kDictionary);u32 count=0;for(const auto& [key,def]:definitions)if(int(def.value->kind)==kind)++count;
        dict.data.bl(count);dict.data.bs(0);dict.data.rc(1);
        for(const auto& [key,def]:definitions)if(int(def.value->kind)==kind)dict.data.tv("Underlay_"+std::to_string(def.handle));
        object_tail(dict,hNamedDict);
        for(const auto& [key,def]:definitions)if(int(def.value->kind)==kind)dict.refs.handle(hard(def.handle));
        put_object(body,map,dict);
    }
    for(const auto& [key,def]:definitions) {
        const auto kind=int(def.value->kind);const auto parent=underlay_dicts.at(kind);
        auto reactors=def.references;reactors.insert(reactors.begin(),parent);
        Obj object;object_head(object,def.handle,u16(505+2*kind),u32(reactors.size()));
        object.data.tv(detail::encode_text(def.value->path));object.data.tv(detail::encode_text(def.value->sheet));
        object_tail(object,parent,reactors);put_object(body,map,object);
    }
    for(const auto& [name,handle]:plotstyle_handles) c.data.tv(detail::encode_text(name));
        object_tail(c,hNamedDict); for(const auto& [name,handle]:plotstyle_handles) c.refs.handle(hard(handle)); c.refs.handle(soft(hPlotstyleNormal));
        put_object(body,map,c);
    }
    for(const auto& [name,handle]:plotstyle_handles) { Obj c; object_head(c,handle,501); object_tail(c,hPlotstyles); put_object(body,map,c); }

    for (const auto& [h, name] : std::initializer_list<std::pair<u64, const char*>>{
             {hLtypeByLayer, "ByLayer"}, {hLtypeByBlock, "ByBlock"},
             {hLtypeContinuous, "Continuous"}}) {
        Obj c; object_head(c, h, kLtype);
        c.data.tv(detail::encode_text(name));
        c.data.b(false); c.data.bs(0); c.data.b(false);   // xref flags
        c.data.tv("Solid line");
        c.data.bd(0.0);            // pattern length
        c.data.rc('A');            // alignment
        c.data.rc(0);              // no dashes
        for (int i = 0; i < 256; ++i) c.data.rc(0);       // the text area
        object_tail(c, hLtypeControl);
        c.refs.handle(null_h()); // xref
        put_object(body, map, c);
    }
    for(const auto& lt:d.linetypes.all()) {
        if(lt.name=="Continuous"||lt.name=="ByLayer"||lt.name=="ByBlock") continue;
        Obj c; object_head(c,ltype_of(lt.name),kLtype);
        c.data.tv(detail::encode_text(lt.name)); c.data.b(false); c.data.bs(0); c.data.b(false);
        c.data.tv(detail::encode_text(lt.description)); c.data.bd(lt.cycle_length()*to_file); c.data.rc('A'); c.data.rc(u8(lt.pattern.size()));
        std::vector<u8> strings(1,0);
        for(const auto& dash:lt.pattern) {
            c.data.bd(dash.length*to_file); c.data.bs(dash.text.empty()?0:u16(strings.size()));
            c.data.rd(0); c.data.rd(0); c.data.bd(dash.text.empty()?1:dash.text_scale*to_file); c.data.bd(0); c.data.bs(dash.text.empty()?0:4);
            if(!dash.text.empty()) { auto encoded=detail::encode_text(dash.text); strings.insert(strings.end(),encoded.begin(),encoded.end()); strings.push_back(0); }
        }
        strings.resize(256,0); for(u8 ch:strings) c.data.rc(ch);
        object_tail(c,hLtypeControl); c.refs.handle(null_h());
        for(const auto& dash:lt.pattern) c.refs.handle(dash.text.empty()?null_h():soft(hStyleStandard));
        put_object(body,map,c);
    }
    for(const auto& style:text_styles) {
        Obj c; object_head(c,style_of(style.name),kStyle);
        c.data.tv(detail::encode_text(style.name)); c.data.b(false); c.data.bs(0); c.data.b(false);
        c.data.b(false); c.data.b(style.vertical); c.data.bd(style.height*to_file); c.data.bd(style.width_factor);
        c.data.bd(style.oblique*3.14159265358979323846/180); c.data.rc((style.backwards?2:0)|(style.upside_down?4:0)); c.data.bd(2.5);
        c.data.tv(detail::encode_text(style.font.empty()?"txt":style.font)); c.data.tv(detail::encode_text(style.big_font));
        object_tail(c,hStyleControl); c.refs.handle(null_h()); put_object(body,map,c);
    }
    for(const auto& [handle,name] : std::vector<std::pair<u64,std::string>>{
            {hAppidAcad,"ACAD"},{transparency_appid,"AcCmTransparency"}}) {
        if(!handle)continue;
        Obj c; object_head(c, handle, kAppid);
        c.data.tv(name);
        c.data.b(false); c.data.bs(0); c.data.b(false);
        c.data.rc(0);
        object_tail(c, hAppidControl);
        c.refs.handle(null_h()); // xref
        put_object(body, map, c);
    }

    for(const auto& style:d.dim_styles) {
        const bool paper_style=std::any_of(paper_dim_styles.begin(),paper_dim_styles.end(),[&](const auto& pair){return pair.second==style.name;});
        to_file=paper_style?1000.0:model_to_file;
        Obj c; object_head(c,dimstyle_handles.at(style.name),kDimstyle);
        auto& b=c.data; b.tv(detail::encode_text(style.name)); b.b(false); b.bs(0); b.b(false);
        b.tv(detail::encode_text(style.prefix+"<>"+style.suffix)); b.tv("");
        const auto size=[&](double x){return x*.001*to_file;};
        b.bd(style.dimscale); b.bd(size(style.dimasz)); b.bd(size(style.dimexo)); b.bd(0); b.bd(size(style.dimexe)); b.bd(0); b.bd(0); b.bd(style.dimtp); b.bd(style.dimtm);
        b.b(style.dimtol); b.b(false); b.b(style.dimtih); b.b(style.dimtoh); b.b(style.dimse1); b.b(style.dimse2);
        b.bs(style.text_pos==gdim::TextPos::Centred?0:style.text_pos==gdim::TextPos::Below?4:1); b.bs((style.dimzin_leading?4:0)|(style.dimzin_trailing?8:0)); b.bs(0);
        b.bd(size(style.dimtxt)); b.bd(0); b.bd(style.arrow==gdim::ArrowType::Tick?size(style.dimasz):0); b.bd(25.4);
        const bool feet_inches=style.format==gunit::Style::Engineering || style.format==gunit::Style::Architectural;
        b.bd(gunit::from_metres(paper_style?.001:unit,feet_inches?gunit::Length::Inch:style.unit)*style.dimlfac); b.bd(style.dimtvp); b.bd(1); b.bd(size(style.dimgap)); b.bd(0);
        b.b(false); b.bs(2); b.b(true); b.b(false); b.b(false); b.b(false);
        b.bs(0); b.bs(0); b.bs(0); b.bs(style.dimdec); b.bs(style.dimdec); b.bs(style.dimdec); b.bs(2); b.bs(2); b.bs(0); b.bs(0); b.bs(style.format==gunit::Style::Scientific?1:style.format==gunit::Style::Engineering?3:style.format==gunit::Style::Architectural?4:style.format==gunit::Style::Fractional?5:2); b.bs('.'); b.bs(0); b.bs(0);
        b.b(style.dimsd1); b.b(style.dimsd2); b.bs(1); b.bs(0); b.bs(0); b.bs(0); b.b(false); b.bs(3); b.bs(65534); b.bs(65534); b.b(false);
        object_tail(c,hDimstyleControl); c.refs.handle(null_h()); c.refs.handle(soft(hStyleStandard));
        for(int i=0;i<4;++i) c.refs.handle(null_h()); put_object(body,map,c);
    }

    to_file=model_to_file;
    for (const gdoc::Layer* l : layers) {
        std::vector<std::pair<u64,u32>> eed;
        if(l->transparency>0)
            eed.emplace_back(transparency_appid,0x02000000u |
                u32(std::lround((100-l->transparency)*255.0/100)));
        Obj c; object_head(c, layer_of(l->name), kLayer, 0, eed);
        c.data.tv(detail::encode_text(l->name));
        c.data.b(false); c.data.bs(0); c.data.b(false);   // xref flags
        u16 flags = 0;
        if (l->frozen) flags |= 1;
        if (!l->on) flags |= 2;
        if (l->locked) flags |= 8;
        flags|=u16(weight_code(l->lineweight)<<5);
        if (l->plottable) flags|=16;
        c.data.bs(flags);
        c.data.bs(aci_color(l->color));
        object_tail(c, hLayerControl);
        c.refs.handle(null_h());              // no external reference block
        c.refs.handle(soft(plotstyle_of(l->plot_style))); // named plot style
        c.refs.handle(hard(ltype_of(l->linetype)));
        put_object(body, map, c);
    }

    const auto put_shape = [&](u64 handle, const gsnap::Shape& s, u8 entmode,
                               u64 block, u64 layer) -> bool {
        f64 ax, ay, bx, by;
        world(s.a, ax, ay);
        world(s.b, bx, by);
        Obj e;
        switch (s.kind) {
            case gsnap::ShapeKind::Segment: {
                entity_head(e, handle, kLine, entmode, s.attrs);
                e.data.b(true);                 // z is zero
                e.data.rd(ax);
                e.data.dd(bx, ax);
                e.data.rd(ay);
                e.data.dd(by, ay);
                e.data.bt(0.0);
                e.data.be(0.0, 0.0, 1.0);
                break;
            }
            case gsnap::ShapeKind::Circle: {
                entity_head(e, handle, kCircle, entmode, s.attrs);
                e.data.bd3(ax, ay, 0.0);
                e.data.bd(radius_of(s.radius));
                e.data.bt(0.0);
                e.data.be(0.0, 0.0, 1.0);
                break;
            }
            case gsnap::ShapeKind::Arc: {
                entity_head(e, handle, kArc, entmode, s.attrs);
                e.data.bd3(ax, ay, 0.0);
                e.data.bd(radius_of(s.radius));
                e.data.bt(0.0);
                e.data.be(0.0, 0.0, 1.0);
                // Radians, unlike DXF.
                e.data.bd(s.start_angle);
                e.data.bd(s.end_angle);
                break;
            }
            case gsnap::ShapeKind::Point: {
                entity_head(e, handle, kPoint, entmode, s.attrs);
                e.data.bd3(ax, ay, 0.0);
                e.data.bt(0.0);
                e.data.be(0.0, 0.0, 1.0);
                e.data.bd(0.0);                 // x-axis angle
                break;
            }
            case gsnap::ShapeKind::Ellipse: {
                entity_head(e,handle,kEllipse,entmode,s.attrs);
                e.data.bd3(ax,ay,0); e.data.bd3(bx-ax,by-ay,0); e.data.bd3(0,0,1);
                e.data.bd(radius_of(s.radius)/std::hypot(bx-ax,by-ay));
                e.data.bd(s.start_angle); e.data.bd(s.start_angle+s.ellipse_sweep()); break;
            }
            default:
                return false;
        }
        entity_tail(e, entmode, block, layer);
        if(!s.attrs.linetype.empty()&&s.attrs.linetype!="ByLayer"&&s.attrs.linetype!="ByBlock"&&s.attrs.linetype!="Continuous") e.refs.handle(soft(ltype_of(s.attrs.linetype)));
        if(auto it=s.attrs.user.find("plot_style");it!=s.attrs.user.end()) e.refs.handle(soft(plotstyle_of(it->second)));
        put_object(body, map, e);
        return true;
    };

    const auto put_text = [&](u64 handle, const gdxf::Text& t, u8 entmode, u64 block, u64 layer, const gdoc::AttributeDef* attribute=nullptr, bool instance=false) {
        f64 x, y;
        world(t.pos, x, y);
        Obj e;
        if (!attribute && (t.width > 0.0 || t.text.find("\\P") != std::string::npos ||
                           t.text.find('\n') != std::string::npos || t.text.find('{') != std::string::npos)) {
            entity_head(e, handle, kMText, entmode, t.attrs);
            e.data.bd3(x, y, 0.0);
            e.data.bd3(0.0, 0.0, 1.0);                            // extrusion
            e.data.bd3(std::cos(t.angle), std::sin(t.angle), 0.0);// x axis
            e.data.bd(t.width * to_file);                         // rectangle width
            e.data.bd(t.height * to_file);
            e.data.bs(u16((t.valign==3?0:t.valign==2?3:6)+std::clamp(t.halign,0,2)+1)); // attachment
            e.data.bs(1);                                         // left to right
            e.data.bd(t.height * to_file);                        // extents height
            e.data.bd(t.width * to_file);                         // extents width
            e.data.tv(detail::encode_text(t.text));
            e.data.bs(0);                                         // line spacing style
            e.data.bd(1.0);                                       // line spacing factor
            e.data.b(false);                                      // unknown
            entity_tail(e, entmode, block, layer);
            if(!t.attrs.linetype.empty()&&t.attrs.linetype!="ByLayer"&&t.attrs.linetype!="ByBlock"&&t.attrs.linetype!="Continuous") e.refs.handle(soft(ltype_of(t.attrs.linetype)));
            if(auto it=t.attrs.user.find("plot_style");it!=t.attrs.user.end()) e.refs.handle(soft(plotstyle_of(it->second)));
            e.refs.handle(hard(style_of(attribute?attribute->style:t.style)));
        } else {
            entity_head(e, handle, attribute?(instance?kAttrib:kAttdef):kText, entmode, t.attrs);
            // Bits 0x20/0x40/0x80 stay clear; readers step over those fields unconditionally.
            e.data.rc(0x01);                                      // elevation is zero
            e.data.rd2(x, y);
            e.data.dd(x, x); e.data.dd(y, y);                     // alignment point
            e.data.be(0.0, 0.0, 1.0);
            e.data.bt(0.0);
            e.data.rd(0.0);                                       // oblique
            e.data.rd(t.angle);
            e.data.rd(t.height * to_file);
            e.data.rd(1.0);                                       // width factor
            e.data.tv(detail::encode_text(t.text));
            e.data.bs(0);                                         // generation
            e.data.bs(static_cast<u16>(t.halign));
            e.data.bs(static_cast<u16>(t.valign));
            if(attribute) {
                e.data.tv(detail::encode_text(attribute->tag)); e.data.bs(0); e.data.rc((attribute->invisible?1:0)|(attribute->constant?2:0)|(attribute->verify?4:0)|(attribute->preset?8:0));
                if(!instance) e.data.tv(detail::encode_text(attribute->prompt));
            }
            entity_tail(e, entmode, block, layer);
            if(!t.attrs.linetype.empty()&&t.attrs.linetype!="ByLayer"&&t.attrs.linetype!="ByBlock"&&t.attrs.linetype!="Continuous") e.refs.handle(soft(ltype_of(t.attrs.linetype)));
            if(auto it=t.attrs.user.find("plot_style");it!=t.attrs.user.end()) e.refs.handle(soft(plotstyle_of(it->second)));
            e.refs.handle(hard(style_of(attribute?attribute->style:t.style)));
        }
        put_object(body, map, e);
    };

    const auto put_space = [&](u64 header, u64 blk, u64 end, const char* name,
                               const std::vector<u64>& owned, u64 layout) {
        {
            Obj c; object_head(c, header, kBlockHeader);
            c.data.tv(detail::encode_text(name));
            c.data.b(false); c.data.bs(0); c.data.b(false);   // xref flags
            c.data.b(false);        // not anonymous
            c.data.b(false);        // no attribute definitions
            c.data.b(false);        // not an xref
            c.data.b(false);        // not overlaid
            c.data.b(false);        // xref not loaded
            c.data.bd3(0.0, 0.0, 0.0);
            c.data.tv("");          // xref path name
            c.data.rc(0);           // the insert-count list ends immediately
            c.data.tv("");          // block description
            c.data.bl(0);           // no preview
            object_tail(c, hBlockControl);
            c.refs.handle(null_h());
            c.refs.handle(hard(blk));
            c.refs.handle(owner(owned.empty() ? 0 : owned.front()));
            c.refs.handle(owner(owned.empty() ? 0 : owned.back()));
            c.refs.handle(hard(end));
            c.refs.handle(soft(layout)); // layout
            put_object(body, map, c);
        }
        {
            Obj c; entity_head(c, blk, kBlock, 0);
            c.data.tv(detail::encode_text(name));
            entity_tail(c, 0, header, layer_of("0"));
            put_object(body, map, c);
        }
        {
            Obj c; entity_head(c, end, kEndblk, 0);
            entity_tail(c, 0, header, layer_of("0"));
            put_object(body, map, c);
        }
    };

    for(const auto& item:underlays) {
        const auto& u=*item.value;to_file=item.space<0?model_to_file:1000.;
        const auto source_unit=u.kind==gdoc::UnderlayKind::Pdf?.0254:u.source_unit;
        Obj entity;const u8 mode=item.space==-1?2:0;
        entity_head(entity,item.handle,u16(506+2*int(u.kind)),mode);
        double x,y;world(u.insertion,x,y);entity.data.bd3(0,0,1);entity.data.bd3(x,y,0);
        entity.data.bd(u.rotation);entity.data.bd3(u.scale*source_unit*to_file,u.scale*u.y_factor*source_unit*to_file,u.scale*source_unit*to_file);
        auto boundary=u.clip,hole=u.clip_hole;
        if(u.clip_inverted){boundary={u.extent_lo,{u.extent_hi.x,u.extent_lo.y},u.extent_hi,{u.extent_lo.x,u.extent_hi.y}};hole=u.clip;}
        const u8 flags=(u.has_clip()?1:0)|(u.visible?2:0)|(u.monochrome?4:0)|(u.adjust_background?8:0)|(!hole.empty()?16:0);
        entity.data.rc(flags);entity.data.rc(u8(std::lround(u.contrast*100/u.contrast_neutral)));entity.data.rc(u8(u.fade));entity.data.bl(u32(boundary.size()));
        const auto point=[&](gcm::Lattice2 p){double px,py;gcm::to_world(v,p,px,py);entity.data.rd2(px/source_unit,py/source_unit);};
        for(auto p:boundary)point(p);
        if(flags&16){entity.data.bs(u16(hole.size()));for(auto p:hole)point(p);}
        entity_tail(entity,mode,item.owner,layer_of(u.layer));entity.refs.handle(soft(item.definition));put_object(body,map,entity);
    }

    put_space(hModelSpace,hModelBlock,hModelEndblk,"*Model_Space",owned_model,model_layout);
    for(std::size_t i=0;i<spaces.size();++i) {
        auto& p=spaces[i];
        const auto name=p.header==current_paper?"*Paper_Space":"*Paper_Space"+std::to_string(i);
        put_space(p.header,p.block,p.endblk,name.c_str(),p.owned,p.layout);
    }

    const auto put_clip=[&](u64 insertion,const std::string& name) {
        const auto found=clip_plans.find(insertion);if(found==clip_plans.end())return;
        const auto& plan=found->second;const auto& x=*input.blocks.xref(name);
        const auto dictionary=[&](u64 handle,u64 parent,const char* key,u64 child) {
            Obj c; object_head(c,handle,kDictionary); c.data.bl(1); c.data.bs(1); c.data.rc(1); c.data.tv(key);
            object_tail(c,parent); c.refs.handle(hard(child)); put_object(body,map,c);
        };
        dictionary(plan.extension,insertion,"ACAD_FILTER",plan.dictionary);
        dictionary(plan.dictionary,plan.extension,"SPATIAL",plan.filter);
        Obj filter; object_head(filter,plan.filter,503);
        filter.data.bs(u16(x.clip.size()));
        for(auto point:x.clip) { double px,py; gcm::to_world(v,point,px,py); filter.data.rd2(px*model_to_file,py*model_to_file); }
        filter.data.bd3(0,0,1); filter.data.bd3(0,0,0);
        filter.data.bs(x.clip_enabled?1:0); filter.data.bs(0); filter.data.bs(0);
        // Boundaries are in block coordinates, so both 3x4 matrices are identities.
        for(int matrix=0;matrix<2;++matrix) for(int col=0;col<4;++col) for(int row=0;row<3;++row) filter.data.bd(col==row?1:0);
        object_tail(filter,plan.dictionary); put_object(body,map,filter);
    };

    for (const BlockPlan& p : blocks) {
        const auto dim=std::find(dimension_blocks.begin(),dimension_blocks.end(),p.def->name);
        to_file=dim!=dimension_blocks.end() && dim_space[std::size_t(dim-dimension_blocks.begin())]>=0?1000.0:model_to_file;
        {
            Obj c; object_head(c, p.header, kBlockHeader);
            c.data.tv(detail::encode_text(p.def->name));
            c.data.b(false); c.data.bs(0); c.data.b(dependent_blocks.contains(p.def->name));
            const auto* x=input.blocks.xref(p.def->name);
            c.data.b(p.def->name.starts_with("*D")); c.data.b(p.def->has_attributes());
            c.data.b(x!=nullptr); c.data.b(x && x->type==gdoc::XrefType::Overlay);
            c.data.b(x && x->state==gdoc::XrefState::Unloaded); // zero means loaded
            f64 bx, by;
            world(p.def->base, bx, by);
            c.data.bd3(bx, by, 0.0);
            c.data.tv(detail::encode_text(p.def->xref_path));
            for (const auto& ref:d.blocks.references()) if(ref.name==p.def->name) c.data.rc(1);
            for(const auto& parent:blocks)for(const auto& ref:parent.nested)if(ref.name==p.def->name)c.data.rc(1);
            c.data.rc(0);
            c.data.tv(detail::encode_text(p.def->description));
            c.data.bl(0);
            object_tail(c, hBlockControl);
            c.refs.handle(null_h());
            c.refs.handle(hard(p.block));
            if (!x) {
                c.refs.handle(owner(p.entities.empty() ? 0 : p.entities.front()));
                c.refs.handle(owner(p.entities.empty() ? 0 : p.entities.back()));
            }
            c.refs.handle(hard(p.endblk));
            for (std::size_t i=0;i<model_refs.size();++i) if(d.blocks.references()[i].name==p.def->name) c.refs.handle(owner(model_refs[i]));
            for(const auto& parent:blocks)for(std::size_t i=0;i<parent.nested.size();++i)if(parent.nested[i].name==p.def->name)c.refs.handle(owner(parent.entities[parent.def->geometry.size()+parent.def->notes.size()+parent.def->attributes.size()+i]));
            c.refs.handle(null_h()); // layout
            put_object(body, map, c);
        }
        {
            Obj c; entity_head(c, p.block, kBlock, 0);
            c.data.tv(detail::encode_text(p.def->name));
            entity_tail(c, 0, p.header, layer_of("0"));
            put_object(body, map, c);
        }
        for (std::size_t i = 0; i < p.def->geometry.size(); ++i)
            put_shape(p.entities[i], p.def->geometry[i], 0, p.header, layer_of(p.def->geometry[i].layer));
        for(std::size_t i=0;i<p.def->notes.size();++i) {
            const auto& n=p.def->notes[i]; gdxf::Text t;
            t.pos=n.pos; t.text=n.text; t.height=n.height; t.angle=n.angle; t.layer=n.layer; t.halign=n.halign; t.valign=n.valign;
            t.width=n.width; t.style=n.style; t.attrs=n.attrs;
            put_text(p.entities[p.def->geometry.size()+i],t,0,p.header,layer_of(n.layer));
        }
        for(std::size_t i=0;i<p.def->attributes.size();++i) {
            const auto& a=p.def->attributes[i]; gdxf::Text t; t.pos=a.position; t.text=a.default_value; t.height=a.height; t.angle=a.rotation;
            put_text(p.entities[p.def->geometry.size()+p.def->notes.size()+i],t,0,p.header,layer_of("0"),&a);
        }
        for(std::size_t i=0;i<p.nested.size();++i) {
            const auto& ref=p.nested[i];const auto child=std::find_if(blocks.begin(),blocks.end(),[&](const auto& plan){return plan.def->name==ref.name;});
            const auto handle=p.entities[p.def->geometry.size()+p.def->notes.size()+p.def->attributes.size()+i];
            Obj e;entity_head(e,handle,kInsert,0,ref.attrs);
            double x,y;world(ref.insertion,x,y);e.data.bd3(x,y,0);
            e.data.bb(0);e.data.rd(ref.scale_x);e.data.dd(ref.scale_y,ref.scale_x);e.data.dd(1,ref.scale_x);
            e.data.bd(ref.rotation);e.data.bd3(0,0,1);e.data.b(!p.attributes[i].empty());
            entity_tail(e,0,p.header,layer_of(ref.layer),clip_plans.contains(handle)?clip_plans.at(handle).extension:0);
            if(!ref.attrs.linetype.empty()&&ref.attrs.linetype!="ByLayer"&&ref.attrs.linetype!="ByBlock"&&ref.attrs.linetype!="Continuous")e.refs.handle(soft(ltype_of(ref.attrs.linetype)));
            if(auto it=ref.attrs.user.find("plot_style");it!=ref.attrs.user.end())e.refs.handle(soft(plotstyle_of(it->second)));
            e.refs.handle(soft(child->header));
            if(!p.attributes[i].empty()){e.refs.handle(owner(p.attributes[i].front()));e.refs.handle(owner(p.attributes[i].back()));e.refs.handle(hard(p.seqends[i]));}
            put_object(body,map,e);
            put_clip(handle,ref.name);
            std::size_t ai=0;
            for(const auto& attr:child->def->attributes) {
                if(attr.constant)continue;
                // ATTRIB is a world-space entity.
                gsnap::Shape marker;marker.a=attr.position;
                const auto placed=gdoc::place_in_block(marker,ref,child->def->base,v);
                gdxf::Text text;text.pos=placed.a;text.text=ref.attribute(attr.tag).value_or(attr.default_value);
                text.height=attr.height*std::abs(ref.scale_y);text.angle=attr.rotation+ref.rotation;
                put_text(p.attributes[i][ai++],text,0,handle,layer_of(ref.layer),&attr,true);
            }
            if(p.seqends[i]){Obj e;entity_head(e,p.seqends[i],kSeqend,0);entity_tail(e,0,handle,layer_of(ref.layer));put_object(body,map,e);}
        }
        {
            Obj c; entity_head(c, p.endblk, kEndblk, 0);
            entity_tail(c, 0, p.header, layer_of("0"));
            put_object(body, map, c);
        }
    }

    for (std::size_t i = 0; i < d.entities.size(); ++i) {
        const auto [mode,owner]=context(entity_space[i]);
        put_shape(model_entities[i],d.entities[i].shape,mode,owner,layer_of(d.entities[i].layer));
    }
    for (std::size_t i = 0; i < d.texts.size(); ++i) {
        const auto [mode,owner]=context(text_space[i]);
        put_text(model_texts[i],d.texts[i],mode,owner,layer_of(d.texts[i].layer));
    }

    for (std::size_t i=0;i<model_refs.size();++i) {
        const auto [mode,space_owner]=context(ref_space[i]);
        const auto& r=d.blocks.references()[i];
        const auto p=std::find_if(blocks.begin(),blocks.end(),[&](const auto& b){return b.def->name==r.name;});
        Obj e; entity_head(e,model_refs[i],kInsert,mode,r.attrs);
        double x,y; world(r.insertion,x,y); e.data.bd3(x,y,0);
        const double sx=r.scale_x*to_file/model_to_file,sy=r.scale_y*to_file/model_to_file;
        e.data.bb(0); e.data.rd(sx); e.data.dd(sy,sx); e.data.dd(1,sx);
        e.data.bd(r.rotation); e.data.bd3(0,0,1); e.data.b(!insert_attributes[i].empty());
        entity_tail(e,mode,space_owner,layer_of(r.layer),clip_plans.contains(model_refs[i])?clip_plans.at(model_refs[i]).extension:0);
        if(!r.attrs.linetype.empty()&&r.attrs.linetype!="ByLayer"&&r.attrs.linetype!="ByBlock"&&r.attrs.linetype!="Continuous")e.refs.handle(soft(ltype_of(r.attrs.linetype)));
        if(auto it=r.attrs.user.find("plot_style");it!=r.attrs.user.end())e.refs.handle(soft(plotstyle_of(it->second)));
        e.refs.handle({5,p->header});
        if(!insert_attributes[i].empty()) { e.refs.handle(owner(insert_attributes[i].front())); e.refs.handle(owner(insert_attributes[i].back())); e.refs.handle(hard(insert_seqend[i])); }
        put_object(body,map,e);
        put_clip(model_refs[i],r.name);
        std::size_t ai=0;
        for(const auto& a:p->def->attributes) {
            if(a.constant) continue;
            // World space.
            gsnap::Shape marker;marker.a=a.position;
            const auto placed=gdoc::place_in_block(marker,r,p->def->base,v);
            gdxf::Text t; t.pos=placed.a; t.text=r.attribute(a.tag).value_or(a.default_value);
            t.height=a.height*std::abs(r.scale_y); t.angle=a.rotation+r.rotation;
            put_text(insert_attributes[i][ai++],t,0,model_refs[i],layer_of(r.layer),&a,true);
        }
        if(insert_seqend[i]) { Obj end; entity_head(end,insert_seqend[i],kSeqend,0); entity_tail(end,0,model_refs[i],layer_of(r.layer)); put_object(body,map,end); }
    }

    for(std::size_t i=0;i<d.splines.size();++i) {
        const auto [mode,space_owner]=context(spline_space[i]);
        const auto& curve=d.splines[i]; Obj e; entity_head(e,model_splines[i],kSpline,mode);
        e.data.bl(1); e.data.bl(curve.degree); e.data.b(curve.rational()); e.data.b(false); e.data.b(false);
        e.data.bd(1e-7); e.data.bd(1e-7); e.data.bl(u32(curve.knots.knots.size())); e.data.bl(u32(curve.control.size())); e.data.b(true);
        for(double knot:curve.knots.knots) e.data.bd(knot);
        for(const auto& point:curve.control) { e.data.bd3(point.p.x.to_f64()*to_file,point.p.y.to_f64()*to_file,0); e.data.bd(point.w.to_f64()); }
        entity_tail(e,mode,space_owner,layer_of("0")); put_object(body,map,e);
    }

    for(std::size_t i=0;i<d.dimensions.size();++i) {
        const auto [mode,space_owner]=context(dim_space[i]);
        const auto& dim=d.dimensions[i]; const auto& style=dim_style_for(dim);
        const auto geometry=gdim::build(dim,style,v);
        if(dim.kind==gdim::Kind::Leader) {
            const auto paths=dim.paths();
            Obj e;entity_head(e,model_dims[i],kLeader,mode);auto& b=e.data;
            b.b(false);b.bs(leader_texts[i]?0:3);b.bs(0);b.bl(u32(paths.front().vertices.size()));
            const auto point=[&](gcm::Lattice2 p){double x,y;world(p,x,y);b.bd3(x,y,0);};
            for(auto vertex:paths.front().vertices)point(vertex);point(dim.p1);
            b.bd3(0,0,1);b.bd3(1,0,0);b.bd3(0,0,0);b.bd3(0,0,0);
            b.bd(geometry.text_height*to_file);b.bd(geometry.text_height*.62*dim.text_override.size()*to_file);
            b.b(false);b.b(paths.front().arrow&&style.arrow!=gdim::ArrowType::None);b.bs(0);b.b(false);b.b(false);
            entity_tail(e,mode,space_owner,layer_of(dim.layer));e.refs.handle(hard(leader_texts[i]));e.refs.handle(hard(dimstyle_handles.at(style.name)));
            put_object(body,map,e);
            if(leader_texts[i]) {
                gdxf::Text text;text.pos=geometry.text_at;text.text=geometry.text;text.height=geometry.text_height;
                text.angle=geometry.text_angle;
                text.width=std::max(geometry.text_height,geometry.text_height*dim.text_override.size());
                text.halign=geometry.halign;text.valign=geometry.valign;text.layer=dim.layer;
                put_text(leader_texts[i],text,mode,space_owner,layer_of(dim.layer));
            }
            continue;
        }
        u16 type=21;
        if(dim.kind==gdim::Kind::Aligned) type=22;
        if(dim.kind==gdim::Kind::Angular) type=23;
        if(dim.kind==gdim::Kind::Radial) type=25;
        if(dim.kind==gdim::Kind::Diameter) type=26;
        if(dim.kind==gdim::Kind::Ordinate) type=20;
        Obj e; entity_head(e,model_dims[i],type,mode); auto& b=e.data;
        const auto point=[&](gcm::Lattice2 p) { double x,y; world(p,x,y); b.bd3(x,y,0); };
        double tx,ty; world(geometry.text_at,tx,ty);
        b.bd3(0,0,1); b.rd2(tx,ty); b.bd(0); b.rc(2); b.tv(detail::encode_text(dim.text_override));
        b.bd(geometry.text_angle); b.bd(0); b.bd3(1,1,1); b.bd(0); b.bs(5); b.bs(1); b.bd(1);
        b.bd(gdim::measure(dim,v)*(type==23?1:to_file)); b.rd2(0,0);
        if(type==20) { point(dim.p3); point(dim.p1); point(dim.p2); b.rc(std::abs(double(dim.p2.y)-dim.p1.y)>=std::abs(double(dim.p2.x)-dim.p1.x)?1:0); }
        else if(type==21||type==22) {
            point(dim.p1); point(dim.p2); point(dim.p3); b.bd(0);
            if(type==21) {
                bool vertical=dim.kind==gdim::Kind::Vertical;
                if(dim.kind==gdim::Kind::Linear) vertical=!gdim::linear_is_horizontal(dim,v);
                b.bd(vertical?1.5707963267948966:0);
            }
        } else if(type==23) { point(dim.p3); point(dim.p1); point(dim.p2); point(dim.centre); }
        else {
            const auto points=dim.radial_points();
            if(type==25) {point(points.second);point(points.first);}
            else {point(points.first);point(points.second);}
            b.bd(0);
        }
        entity_tail(e,mode,space_owner,layer_of(dim.layer),dimension_associations[i].dictionary); e.refs.handle(soft(dimstyle_handles.at(style.name)));
        const auto bp=std::find_if(blocks.begin(),blocks.end(),[&](const auto& p){return p.def->name==dimension_blocks[i];});
        e.refs.handle(soft(bp->header)); put_object(body,map,e);
    }

    for(std::size_t i=0;i<dimension_associations.size();++i) {
        const auto& plan=dimension_associations[i];if(plan.points.empty())continue;
        context(dim_space[i]);
        Obj dictionary;object_head(dictionary,plan.dictionary,kDictionary);
        dictionary.data.bl(1);dictionary.data.bs(1);dictionary.data.rc(1);dictionary.data.tv("ACAD_DIMASSOC");
        object_tail(dictionary,model_dims[i]);dictionary.refs.handle({2,plan.object});put_object(body,map,dictionary);
        Obj association;object_head(association,plan.object,511,1);
        object_tail(association,plan.dictionary,{plan.dictionary});
        u32 flags=0;for(const auto& point:plan.points)flags|=1u<<point.index;
        auto& b=association.data;b.bl(flags);b.b(false);b.rc(0);
        association.refs.handle(owner(model_dims[i]));
        for(const auto& point:plan.points) {
            b.tv("AcDbOsnapPointRef");b.rc(point.osnap);b.bl(1);
            association.refs.handle(owner(model_entities[point.entity]));
            b.bl(2);b.bl(0);b.bl(0); // edge, marker zero, no external path
            b.bd(point.parameter);
            double x,y;world(point.point,x,y);b.bd3(x,y,0);
            b.b(false); // no chained point reference
        }
        put_object(body,map,association);
    }

    for(std::size_t i=0;i<d.hatches.size();++i) {
        const auto [mode,space_owner]=context(hatch_space[i]);
        const auto& hatch=d.hatches[i]; Obj e; entity_head(e,model_hatches[i],500,mode); auto& b=e.data;
        b.bd(0); b.bd3(0,0,1); b.tv(detail::encode_text(hatch.solid?"SOLID":hatch.pattern)); b.b(hatch.solid); b.b(false); b.bl(u32(hatch_paths[i].size()));
        for(const auto& path:hatch_paths[i]) {
            b.bl(0); b.bl(u32(path.size()));
            for(const auto& edge:path) {
                const auto& s=edge.shape; double ax,ay,bx,by; world(s.a,ax,ay); world(s.b,bx,by);
                if(s.kind==gsnap::ShapeKind::Segment) { b.rc(1); if(edge.ccw) { b.rd2(ax,ay); b.rd2(bx,by); } else { b.rd2(bx,by); b.rd2(ax,ay); } }
                else if(s.kind==gsnap::ShapeKind::Ellipse) { b.rc(3); b.rd2(ax,ay); b.rd2(bx-ax,by-ay); b.bd(radius_of(s.radius)/std::hypot(bx-ax,by-ay)); b.bd(0); b.bd(6.283185307179586); b.b(true); }
                else { b.rc(2); b.rd2(ax,ay); b.bd(radius_of(s.radius)); const double start=s.kind==gsnap::ShapeKind::Circle?0:s.start_angle,end=s.kind==gsnap::ShapeKind::Circle?6.283185307179586:s.end_angle; b.bd(edge.ccw?start:end); b.bd(edge.ccw?end:start); b.b(edge.ccw); }
            }
            b.bl(0);
        }
        b.bs(0); b.bs(1);
        if(!hatch.solid) {
            const double angle=hatch.angle*3.14159265358979323846/180,scale=hatch.scale*to_file,c=std::cos(angle),sn=std::sin(angle);
            b.bd(angle); b.bd(hatch.scale*to_file); b.b(false); b.bs(u16(hatch.pattern_lines.size()));
            for(const auto& line:hatch.pattern_lines) {
                const double a=line.angle*3.14159265358979323846/180;
                b.bd(a+angle); b.bd((line.x*c-line.y*sn)*scale); b.bd((line.x*sn+line.y*c)*scale);
                // PAT offsets are in the line's coordinate system.
                b.bd((line.dx*std::cos(a+angle)-line.dy*std::sin(a+angle))*scale); b.bd((line.dx*std::sin(a+angle)+line.dy*std::cos(a+angle))*scale);
                b.bs(u16(line.dashes.size())); for(double dash:line.dashes) b.bd(dash*scale);
            }
        }
        double x,y; world(hatch.seed,x,y); b.bl(1); b.rd2(x,y);
        entity_tail(e,mode,space_owner,layer_of(hatch.layer)); put_object(body,map,e);
    }

    // Layout tab order is independent of object handles.
    {
        Obj c; object_head(c,layout_dict,kDictionary); c.data.bl(u32(spaces.size()+1)); c.data.bs(1); c.data.rc(1);
        c.data.tv("Model"); for(const auto& sheet:d.layouts) c.data.tv(detail::encode_text(sheet.name));
        object_tail(c,hNamedDict); c.refs.handle(hard(model_layout));
        for(const auto& p:spaces) c.refs.handle(hard(p.layout));
        put_object(body,map,c);
    }
    const auto put_layout=[&](u64 handle,u64 block,u64 viewport,const gdxf::Layout& sheet,u32 order) {
        Obj c; object_head(c,handle,504); auto& b=c.data;
        b.tv(""); b.tv(""); b.bs(0);
        b.bd(sheet.margin_left*1000); b.bd(sheet.margin_bottom*1000); b.bd(sheet.margin_right*1000); b.bd(sheet.margin_top*1000);
        b.bd(sheet.width*1000); b.bd(sheet.height*1000); b.tv(detail::encode_text(sheet.paper_name));
        b.bd(0); b.bd(0); b.bs(1); b.bs(0); b.bs(5);
        b.bd(0); b.bd(0); b.bd(sheet.width*1000); b.bd(sheet.height*1000); b.tv("");
        b.bd(1); b.bd(1); b.tv(detail::encode_text(sheet.plot_style_table)); b.bs(16); b.bd(1); b.bd(0); b.bd(0);
        b.tv(detail::encode_text(sheet.name)); b.bl(order); b.bs(1);
        b.bd3(0,0,0); b.rd2(0,0); b.rd2(sheet.width*1000,sheet.height*1000); b.bd3(0,0,0);
        b.bd3(1,0,0); b.bd3(0,1,0); b.bd(0); b.bs(0); b.bd3(0,0,0); b.bd3(sheet.width*1000,sheet.height*1000,0);
        object_tail(c,layout_dict); c.refs.handle(owner(block)); c.refs.handle(owner(viewport)); c.refs.handle(null_h()); c.refs.handle(null_h());
        put_object(body,map,c);
    };
    gdxf::Layout model_sheet; model_sheet.name="Model";
    put_layout(model_layout,hModelSpace,0,model_sheet,0);
    for(std::size_t i=0;i<spaces.size();++i) {
        const auto& p=spaces[i]; const auto& sheet=d.layouts[i];
        put_layout(p.layout,p.header,p.viewports.front().entity,sheet,u32(i+1));
        for(std::size_t j=0;j<p.viewports.size();++j) {
            const auto& plan=p.viewports[j];
            gplot::Viewport vp;
            if(j) vp=sheet.viewports[j-1];
            else { vp.lo={0,0}; vp.hi={sheet.width,sheet.height}; vp.centre_x=sheet.width*.5; vp.centre_y=sheet.height*.5; vp.scale=1; }
            const double view_units=j?model_to_file:1000.0;
            Obj e; entity_head(e,plan.entity,kViewport,0); auto& b=e.data;
            b.bd3((vp.lo.x+vp.hi.x)*500,(vp.lo.y+vp.hi.y)*500,0);
            b.bd((vp.hi.x-vp.lo.x)*1000); b.bd((vp.hi.y-vp.lo.y)*1000);
            // A WCS target and zero DCS centre avoid rotating about the origin when twisted.
            b.bd3(vp.centre_x*view_units,vp.centre_y*view_units,0); b.bd3(0,0,1);
            b.bd(-vp.rotation); b.bd((vp.hi.y-vp.lo.y)*vp.scale*view_units); b.bd(50); b.bd(0); b.bd(0); b.bd(0);
            b.rd2(0,0); b.rd2(0,0); b.rd2(10,10); b.rd2(10,10); b.bs(100);
            b.bl(u32(vp.frozen_layers.size())); b.bl(0x8000|(vp.locked?0x4000:0)|(plan.clip?0x10000:0)|(vp.on?0:0x20000));
            b.tv(detail::encode_text(sheet.plot_style_table)); b.rc(0); b.b(false); b.b(false);
            b.bd3(0,0,0); b.bd3(1,0,0); b.bd3(0,1,0); b.bd(0); b.bs(0);
            entity_tail(e,0,p.header,layer_of("0"));
            for(const auto& layer:vp.frozen_layers) e.refs.handle(soft(layer_of(layer)));
            e.refs.handle(owner(plan.clip)); e.refs.handle(soft(plan.header)); e.refs.handle(null_h()); e.refs.handle(null_h());
            put_object(body,map,e);
            Obj h; object_head(h,plan.header,kVpEntHdr); h.data.tv(""); h.data.b(false); h.data.bs(0); h.data.b(false); h.data.b(vp.on);
            object_tail(h,hVpEntHdrControl); h.refs.handle(null_h()); h.refs.handle({2,plan.entity}); h.refs.handle(soft(j?p.viewports[j-1].header:0));
            put_object(body,map,h);
            if(plan.clip) {
                Obj clip; entity_head(clip,plan.clip,kLwPolyline,0); clip.data.bs(512); clip.data.bl(u32(vp.clip_polygon.size()));
                auto previous=vp.clip_polygon.front(); clip.data.rd2(previous.x*1000,previous.y*1000);
                for(std::size_t k=1;k<vp.clip_polygon.size();++k) {
                    const auto point=vp.clip_polygon[k]; clip.data.dd(point.x*1000,previous.x*1000); clip.data.dd(point.y*1000,previous.y*1000); previous=point;
                }
                entity_tail(clip,0,p.header,layer_of("0")); put_object(body,map,clip);
            }
        }
    }
    to_file=model_to_file;

    // Six section locators; offsets are patched in once everything is laid out.
    constexpr u32 kSections = 6;
    std::vector<u8> out;
    out.insert(out.end(), {'A', 'C', '1', '0', '1', '5'});
    for (int i = 0; i < 5; ++i) out.push_back(0);
    out.push_back(0);                       // maintenance release
    out.push_back(0x01);                    // "one", always
    put_u32(out, 0);                        // no preview image
    out.push_back(23);                      // application DWG version: R2000
    out.push_back(0);                       // application maintenance version
    out.push_back(30); out.push_back(0);    // codepage: ANSI_1252
    put_u32(out, kSections);
    const std::size_t locators = out.size();
    for (u32 i = 0; i < kSections; ++i) {
        out.push_back(u8(i));
        put_u32(out, 0);
        put_u32(out, 0);
    }
    // The CRC goes in last, after the locators are patched.
    const std::size_t head_crc_at = out.size();
    out.push_back(0); out.push_back(0);
    put_sentinel(out, kHeaderSentinel);

    const auto locate = [&](u32 section, u32 seeker, u32 size) {
        patch_u32(out, locators + section * 9 + 1, seeker);
        patch_u32(out, locators + section * 9 + 5, size);
    };

    // 0: the header variables.
    {
        const std::size_t at = out.size();
        put_section(out, kVarsStart, kVarsEnd, header_vars(static_cast<u16>(o.units), handseed, layer_of("0"), d.plot_style_table, plotstyle_handles.size()>1 || d.plot_style_table.ends_with(".stb"),dimstyle_handles.at(d.dim_styles.front().name),layout_dict,current_paper,d.active_layout<0));
        locate(0, u32(at), u32(out.size() - at));
    }
    // 1: class declarations for hatches and named plot-style objects.
    {
        const std::size_t at = out.size();
        BitWriter classes;
        { classes.bs(500); classes.bs(0); classes.tv("ObjectDBX Classes"); classes.tv("AcDbHatch"); classes.tv("HATCH"); classes.b(false); classes.bs(0x1f2); }
        const auto object_class=[&](u16 number,const char* cpp,const char* dxf) { classes.bs(number); classes.bs(0); classes.tv("ObjectDBX Classes"); classes.tv(cpp); classes.tv(dxf); classes.b(false); classes.bs(0x1f3); };
        object_class(501,"AcDbPlaceHolder","ACDBPLACEHOLDER"); object_class(502,"AcDbDictionaryWithDefault","ACDBDICTIONARYWDFLT");
        object_class(503,"AcDbSpatialFilter","SPATIAL_FILTER");
        object_class(504,"AcDbLayout","LAYOUT");
        // Class array is indexed by type minus 500; keep earlier reserved slots.
        const int last_underlay=!association_reactors.empty()?2:underlay_dicts.empty()?-1:underlay_dicts.rbegin()->first;
        for(int kind=0;kind<=last_underlay;++kind) {
            const char* prefix=kind==0?"Pdf":kind==1?"Dwf":"Dgn";
            const auto cpp="AcDb"+std::string(prefix)+"Definition";const auto dxf=kind==0?"PDFDEFINITION":kind==1?"DWFDEFINITION":"DGNDEFINITION";
            object_class(u16(505+2*kind),cpp.c_str(),dxf);
            classes.bs(u16(506+2*kind));classes.bs(0);classes.tv("ObjectDBX Classes");classes.tv("AcDb"+std::string(prefix)+"Reference");
            classes.tv(kind==0?"PDFUNDERLAY":kind==1?"DWFUNDERLAY":"DGNUNDERLAY");classes.b(false);classes.bs(0x1f2);
        }
        if(!association_reactors.empty())object_class(511,"AcDbDimAssoc","DIMASSOC");
        classes.align_byte();
        put_section(out, kClassStart, kClassEnd, classes.bytes());
        locate(1, u32(at), u32(out.size() - at));
    }
    // Objects have no locator; their offsets shift by where they land.
    const u32 objects_at = static_cast<u32>(out.size());
    out.insert(out.end(), body.begin(), body.end());
    for (Written& w : map) w.offset += objects_at;

    // 2: the object map.
    {
        std::sort(map.begin(),map.end(),[](auto a,auto b) { return a.handle<b.handle; });
        const std::vector<u8> mapped = object_map(map);
        locate(2, u32(out.size()), u32(mapped.size()));
        out.insert(out.end(), mapped.begin(), mapped.end());
    }
    // 3: object free space, empty.
    locate(3, 0, 0);
    // 4: the template.
    {
        locate(4, u32(out.size()), 4);
        out.push_back(0); out.push_back(0);      // description length
        out.push_back(1); out.push_back(0);      // MEASUREMENT: metric
    }
    // 5: the auxiliary header, a fixed 115-byte block.
    {
        const std::size_t at = out.size();
        const auto rs = [&](u16 x) { out.push_back(u8(x)); out.push_back(u8(x >> 8)); };
        const auto rl = [&](u32 x) { put_u32(out, x); };
        const auto rd = [&](f64 x) {
            u64 raw; std::memcpy(&raw, &x, sizeof raw);
            for (int i = 0; i < 8; ++i) out.push_back(u8(raw >> (i * 8)));
        };
        out.push_back(0xFF); out.push_back(0x77); out.push_back(0x01);   // the marker
        rs(0x17); rs(0);                    // dwg and maintenance version
        rl(0);                              // number of saves
        rl(0xFFFFFFFFu);                    // minus one
        rs(0); rs(0);                       // save counts again
        rl(0);
        rs(0x17); rs(0); rs(0x17); rs(0);   // the versions once more
        rs(4); rs(0x0565); rs(0); rs(0); rs(2); rs(1);   // six unknowns
        for (int i = 0; i < 5; ++i) rl(0);  // five more
        rd(0.0); rd(0.0);                   // TDCREATE, TDUPDATE
        rl(static_cast<u32>(handseed));
        rl(0);                              // no educational plot stamp
        rs(0); rs(0);
        for (int i = 0; i < 8; ++i) rl(0);
        locate(5, u32(at), u32(out.size() - at));
    }

    // Redundant R2000 recovery header, with the same section addresses and handles.
    {
        const auto address=static_cast<u32>(out.size());
        put_sentinel(out,kSecondStart);
        const auto size_at=out.size(); put_u32(out,0);
        BitWriter w; w.bl(address);
        for (char ch:std::string("AC1015")) w.rc(u8(ch));
        for (int i=0;i<5;++i) w.rc(0);
        w.rc(0); w.rc(1); w.bs(23); w.rs(30);
        w.bs(kSections);
        const auto at_u32=[&](std::size_t pos) { return u32(out[pos])|(u32(out[pos+1])<<8)|(u32(out[pos+2])<<16)|(u32(out[pos+3])<<24); };
        for (u32 i=0;i<kSections;++i) { w.rc(u8(i)); w.bl(at_u32(locators+i*9+1)); w.bl(at_u32(locators+i*9+5)); }
        const u64 handles[]={handseed,hBlockControl,hLayerControl,hStyleControl,hLtypeControl,hViewControl,hUcsControl,hVportControl,hAppidControl,hDimstyleControl,hVpEntHdrControl,hNamedDict,hMlineDict,hGroupDict};
        w.bs(14);
        for (u8 i=0;i<14;++i) {
            u8 count=1; while (count<8 && (handles[i]>>(count*8))) ++count;
            w.rc(count); w.rc(i);
            for (int j=count-1;j>=0;--j) w.rc(u8(handles[i]>>(8*j)));
        }
        w.align_byte(); out.insert(out.end(),w.bytes().begin(),w.bytes().end());
        patch_u32(out,size_at,u32(out.size()-size_at));
        const auto crc=crc8(0xC0C1,out.data()+size_at,out.size()-size_at);
        out.push_back(u8(crc)); out.push_back(u8(crc>>8));
        put_sentinel(out,kSecondEnd);
    }

    {
        const u16 head_crc = crc8(0xC0C1, out.data(), head_crc_at);
        out[head_crc_at] = u8(head_crc & 0xFF);
        out[head_crc_at + 1] = u8(head_crc >> 8);
    }
    return out;
}

std::vector<u8> write(const gdxf::Document& d,const gcm::WorkingVolume& v,const WriteOptions& o,std::string* error) {
    try { return write_impl(d,v,o,error); }
    catch(const std::exception& e) { if(error) *error=std::string("R2000 export: ")+e.what(); return {}; }
}

bool write_file(const std::string& path, const gdxf::Document& d,
                const gcm::WorkingVolume& v, const WriteOptions& o,
                std::string* error) {
    auto document=d;
    std::error_code ec;
    const auto folder=std::filesystem::absolute(path,ec).parent_path().lexically_normal();
    if (ec) { if(error) *error="could not resolve output folder: "+ec.message(); return false; }
    for (auto x : document.blocks.xrefs()) {
        const auto target=std::filesystem::path(x.resolved_path);
        if (std::filesystem::path(x.path).is_relative() && target.is_absolute()) {
            const auto relative=target.lexically_normal().lexically_relative(folder);
            x.path=relative.empty()?target.string():relative.generic_string();
            document.blocks.attach(std::move(x));
        }
    }
    const std::vector<u8> bytes = write(document, v, o, error);
    if (bytes.empty()) return false;
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) {
        if (error) *error = "could not open " + path + " for writing";
        return false;
    }
    bool ok = std::fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
    if(std::fclose(f)!=0) ok=false;
    if (!ok && error) *error = "could not write all of " + path;
    return ok;
}

}
