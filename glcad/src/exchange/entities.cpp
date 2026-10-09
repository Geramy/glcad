#include <algorithm>
#include <functional>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <set>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

#include "exchange/dxf.hpp"
#include "exchange/dimension_styles.hpp"
#include "exchange/region_export.hpp"

namespace gdxf {
namespace {

// %%c diameter, %%d degrees, %%p plus-minus, %%u underline (dropped), %%%% percent
void decode_percent(std::string& t) {
    std::string out2;
    out2.reserve(t.size());
    for (std::size_t i = 0; i < t.size(); ++i) {
        if (t[i] == '%' && i + 2 < t.size() && t[i + 1] == '%') {
            const char c = char(std::tolower(static_cast<unsigned char>(t[i + 2])));
            if (c == 'c') { out2 += "\xe2\x8c\x80"; i += 2; continue; }
            if (c == 'd') { out2 += "\xc2\xb0"; i += 2; continue; }
            if (c == 'p') { out2 += "\xc2\xb1"; i += 2; continue; }
            if (c == 'u' || c == 'o') { i += 2; continue; }
            if (c == '%') { out2 += '%'; i += 2; continue; }
        }
        out2 += t[i];
    }
    t = std::move(out2);
}


constexpr f64 kPi = 3.14159265358979323846;
constexpr f64 kEps = 1e-12;

std::string number(f64 v) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.10f", v);
    std::string s = buf;
    while (s.size() > 1 && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s += "0";
    return s;
}

std::string upper(std::string_view s) {
    std::string o(s);
    for (char& c : o)
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    return o;
}

f64 degrees(f64 radians) { return radians * 180.0 / kPi; }
f64 radians(f64 deg) { return deg * kPi / 180.0; }

f64 normalise_angle(f64 a) {
    while (a < 0.0) a += 2.0 * kPi;
    while (a >= 2.0 * kPi) a -= 2.0 * kPi;
    return a;
}

struct Out {
    std::ostringstream s;
    void pair(i32 code, std::string_view value) {
        s << code << "\n" << value << "\n";
    }
    void pair(i32 code, f64 value) { s << code << "\n" << number(value) << "\n"; }
    void pair_int(i32 code, i32 value) { s << code << "\n" << value << "\n"; }
};

}

std::string write(const Document& input, const gcm::WorkingVolume& v,
                  const WriteOptions& o) {
    if(!input.underlays.empty()||std::any_of(input.layouts.begin(),input.layouts.end(),[](const auto& layout){return !layout.underlays.empty();}))return {};
    auto d=input;
    std::string region_error;
    if(!gexchange::prepare_region_boundaries(d,region_error))return {};
    if(!gexchange::detail::prepare_extension_offsets(d.dimensions,v))return {};
    for(auto& layout:d.layouts)if(!gexchange::detail::prepare_extension_offsets(layout.dimensions,v))return {};
    if(!gexchange::detail::prepare_leader_styles(d.dimensions,d.dim_styles,d.has_dim_style?d.dim_style:gdim::Style{}))return {};
    const auto leaders_supported=[](const auto& dimensions) {
        for(const auto& dim:dimensions)if(dim.kind==gdim::Kind::Leader) {
            const auto paths=dim.paths();if(paths.size()!=1||paths.front().vertices.size()<2||paths.front().vertices.size()>4096)return false;
        }
        return true;
    };
    if(!leaders_supported(d.dimensions))return {};
    for(const auto& layout:d.layouts)if(!leaders_supported(layout.dimensions))return {};
    for(const auto& block:d.blocks.all()) {
        if(!block.underlays.empty())return {};
        for(const auto& attr:block.attributes)if(attr.multiline||attr.lock_position)return {};
        for(const auto& ref:block.nested_refs)if(!ref.attributes.empty())return {};
    }
    Out out;
    // Handles start at 0x100, as AutoCAD's do.
    u32 next_handle = 0x100;
    const auto handle = [&]() {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%X", next_handle++);
        out.pair(5, buf);
    };

    const f64 unit = metres_per(o.units);
    const f64 to_file = unit > 0.0 ? 1.0 / unit : 1.0;

    auto world = [&](const gcm::Lattice2& p, f64& x, f64& y) {
        gcm::to_world(v, p, x, y);
        x *= to_file;
        y *= to_file;
    };

    out.pair(0, "SECTION");
    out.pair(2, "HEADER");
    out.pair(9, "$ACADVER");
    out.pair(1, o.version);
    out.pair(9, "$INSUNITS");
    out.pair_int(70, static_cast<i32>(o.units));
    out.pair(9, "$EXTMIN");
    out.pair(10, 0.0);
    out.pair(20, 0.0);
    out.pair(9, "$EXTMAX");
    out.pair(10, 0.0);
    out.pair(20, 0.0);
    out.pair(0, "ENDSEC");

    out.pair(0, "SECTION");
    out.pair(2, "TABLES");

    out.pair(0, "TABLE");
    out.pair(2, "LTYPE");
    handle();
    out.pair(100, "AcDbSymbolTable");
    out.pair_int(70, static_cast<i32>(std::max<std::size_t>(1, d.linetypes.size())));
    {
        bool wrote_continuous = false;
        for (const gdoc::Linetype& lt : d.linetypes.all()) {
            if (lt.name == "Continuous") wrote_continuous = true;
            out.pair(0, "LTYPE");
            out.pair(2, lt.name);
            out.pair_int(70, 0);
            out.pair(3, lt.description);
            out.pair_int(72, 65);              // 'A', the alignment code
            out.pair_int(73, static_cast<i32>(lt.pattern.size()));
            out.pair(40, lt.cycle_length());
            for (const gdoc::Dash& dash : lt.pattern) out.pair(49, dash.length);
        }
        if (!wrote_continuous) {
            out.pair(0, "LTYPE");
            out.pair(2, "Continuous");
            out.pair_int(70, 0);
            out.pair(3, "Solid line");
            out.pair_int(72, 65);
            out.pair_int(73, 0);
            out.pair(40, 0.0);
        }
    }
    out.pair(0, "ENDTAB");

    out.pair(0, "TABLE");
    out.pair(2, "STYLE");
    handle();
    out.pair(100, "AcDbSymbolTable");
    out.pair_int(70, static_cast<i32>(std::max<std::size_t>(1, d.text_styles.size())));
    {
        bool wrote_standard = false;
        for (const gtext::TextStyleDef& st : d.text_styles) {
            if (st.name == "Standard") wrote_standard = true;
            out.pair(0, "STYLE");
            out.pair(2, st.name);
            out.pair_int(70, 0);
            out.pair(40, f64(st.height) * to_file);  // 0 = height asked per text
            out.pair(41, f64(st.width_factor));
            out.pair(50, f64(st.oblique));
            out.pair_int(71, (st.backwards ? 2 : 0) | (st.upside_down ? 4 : 0));
            out.pair(42, 2.5);
            out.pair(3, st.font.empty() ? std::string("txt") : st.font);
            out.pair(4, st.big_font);
        }
        if (!wrote_standard) {
            out.pair(0, "STYLE");
            out.pair(2, "Standard");
            out.pair_int(70, 0);
            out.pair(40, 0.0);
            out.pair(41, 1.0);
            out.pair(50, 0.0);
            out.pair_int(71, 0);
            out.pair(42, 2.5);
            out.pair(3, "txt");
            out.pair(4, "");
        }
    }
    out.pair(0, "ENDTAB");

    out.pair(0, "TABLE");
    out.pair(2, "DIMSTYLE");
    handle();
    out.pair(100, "AcDbSymbolTable");
    auto styles = d.dim_styles;
    if (styles.empty()) styles.push_back(d.has_dim_style ? d.dim_style : gdim::Style{});
    out.pair_int(70, static_cast<i32>(styles.size()));
    for (const auto& st : styles) {
        out.pair(0, "DIMSTYLE");
        out.pair(2, st.name.empty() ? std::string("Standard") : st.name);
        out.pair_int(70, 0);
        // Native style sizes are millimetres; DXF stores drawing units.
        const auto size=[&](double value){return value*.001*to_file;};
        out.pair(140, size(st.dimtxt));                       // DIMTXT
        out.pair(41, size(st.dimasz));                       // DIMASZ
        out.pair(147, size(st.dimgap));                       // DIMGAP
        out.pair(42, size(st.dimexo));                        // DIMEXO
        out.pair(44, size(st.dimexe));                        // DIMEXE
        out.pair(40, st.dimscale);                      // DIMSCALE
        out.pair_int(271, static_cast<i32>(st.dimdec)); // DIMDEC
    }
    out.pair(0, "ENDTAB");

    out.pair(0, "TABLE");
    out.pair(2, "LAYER");
    handle();
    out.pair(100, "AcDbSymbolTable");
    out.pair_int(70, static_cast<i32>(std::max<std::size_t>(1, d.layers.size())));
    {
        bool wrote_zero = false;
        for (const gdoc::Layer& l : d.layers.all()) {
            if (l.name == "0") wrote_zero = true;
            out.pair(0, "LAYER");
            out.pair(2, l.name);
            // Bit 1 is frozen; a negative colour means the layer is off.
            out.pair_int(70, (l.frozen ? 1 : 0) | (l.locked ? 4 : 0));
            const i32 colour = l.color.kind == gdoc::Color::Kind::Index
                                   ? l.color.index : 7;
            out.pair_int(62, l.on ? colour : -colour);
            out.pair(6, l.linetype.empty() ? "CONTINUOUS" : l.linetype);
            out.pair_int(370, static_cast<i32>(l.lineweight));
            if (!l.plottable) out.pair_int(290, 0);
            if (l.color.kind == gdoc::Color::Kind::True)
                out.pair_int(420, (i32(l.color.r) << 16) | (i32(l.color.g) << 8) | i32(l.color.b));
        }
        if (!wrote_zero) {
            out.pair(0, "LAYER");
            out.pair(2, "0");
            out.pair_int(70, 0);
            out.pair_int(62, 7);
            out.pair(6, "CONTINUOUS");
        }
    }
    out.pair(0, "ENDTAB");
    out.pair(0, "ENDSEC");

    auto write_attrs = [&](const gdoc::Attributes& at) {
        if(!at.visible)out.pair_int(60,1);
        if (at.color.kind == gdoc::Color::Kind::Index)
            out.pair_int(62, at.color.index);
        else if (at.color.kind == gdoc::Color::Kind::ByBlock)
            out.pair_int(62, 0);
        else if (at.color.kind == gdoc::Color::Kind::True)
            // 420 is a 24-bit true colour packed into one integer.
            out.pair_int(420, (i32(at.color.r) << 16) | (i32(at.color.g) << 8) | i32(at.color.b));
        if (!at.linetype.empty() && at.linetype != "ByLayer")
            out.pair(6, at.linetype);
        if (at.lineweight != gdoc::Lineweight::ByLayer)
            out.pair_int(370, static_cast<i32>(at.lineweight));
        if (at.transparency >= 0)
            // Transparency: alpha in the low byte, 0x02 in the high; inverse of percent.
            out.pair_int(440, 0x02000000 | int(std::lround((100-gdoc::transparency_percent(at))*255.0/100)));
        else if(at.transparency==-2)out.pair_int(440,0x01000000);
        if (at.linetype_scale != 1.0)
            out.pair(48, at.linetype_scale);
    };

    auto write_shape = [&](const gsnap::Shape& s, const std::string& layer) {
        f64 ax, ay, bx, by;
        world(s.a, ax, ay);
        world(s.b, bx, by);

        switch (s.kind) {
            case gsnap::ShapeKind::Segment:
            case gsnap::ShapeKind::Ray:
            case gsnap::ShapeKind::Xline:
                out.pair(0, s.kind == gsnap::ShapeKind::Segment ? "LINE"
                         : s.kind == gsnap::ShapeKind::Ray      ? "RAY"
                                                                : "XLINE");
                handle();
                out.pair(8, layer);
                write_attrs(s.attrs);
                out.pair(10, ax);
                out.pair(20, ay);
                out.pair(30, 0.0);
                if (s.kind == gsnap::ShapeKind::Segment) {
                    out.pair(11, bx);
                    out.pair(21, by);
                    out.pair(31, 0.0);
                } else {
                    // For a ray or xline the second pair is a direction, not a point.
                    const f64 dx = bx - ax, dy = by - ay;
                    const f64 len = std::hypot(dx, dy);
                    out.pair(11, len > kEps ? dx / len : 1.0);
                    out.pair(21, len > kEps ? dy / len : 0.0);
                    out.pair(31, 0.0);
                }
                break;

            case gsnap::ShapeKind::Point:
                out.pair(0, "POINT");
                handle();
                out.pair(8, layer);
                write_attrs(s.attrs);
                out.pair(10, ax);
                out.pair(20, ay);
                out.pair(30, 0.0);
                break;

            case gsnap::ShapeKind::Circle:
                out.pair(0, "CIRCLE");
                handle();
                out.pair(8, layer);
                write_attrs(s.attrs);
                out.pair(10, ax);
                out.pair(20, ay);
                out.pair(30, 0.0);
                out.pair(40, f64(s.radius) / v.scale() * to_file);
                break;

            case gsnap::ShapeKind::Arc:
                out.pair(0, "ARC");
                handle();
                out.pair(8, layer);
                write_attrs(s.attrs);
                out.pair(10, ax);
                out.pair(20, ay);
                out.pair(30, 0.0);
                out.pair(40, f64(s.radius) / v.scale() * to_file);
                // Degrees, counter-clockwise from start to end.
                out.pair(50, degrees(normalise_angle(s.start_angle)));
                out.pair(51, degrees(normalise_angle(s.end_angle)));
                break;

            case gsnap::ShapeKind::Ellipse: {
                out.pair(0, "ELLIPSE");
                handle();
                out.pair(8, layer);
                write_attrs(s.attrs);
                out.pair(10, ax);
                out.pair(20, ay);
                out.pair(30, 0.0);
                // Major axis as a vector from the centre.
                out.pair(11, bx - ax);
                out.pair(21, by - ay);
                out.pair(31, 0.0);
                const f64 major = std::hypot(bx - ax, by - ay);
                const f64 minor = f64(s.radius) / v.scale() * to_file;
                out.pair(40, major > kEps ? minor / major : 1.0);
                out.pair(41, s.start_angle);
                out.pair(42, s.start_angle+s.ellipse_sweep());
                break;
            }

            case gsnap::ShapeKind::Block:
                out.pair(0, "INSERT");
                handle();
                out.pair(8, layer);
                write_attrs(s.attrs);
                out.pair(2, "*U0");
                out.pair(10, ax);
                out.pair(20, ay);
                out.pair(30, 0.0);
                break;
        }
    };

    const auto write_insert=[&](const gdoc::BlockRef& r) {
        f64 x, y;
        world(r.insertion, x, y);
        out.pair(0, "INSERT");
        out.pair(8, r.layer);
        write_attrs(r.attrs);
        out.pair(2, r.name);
        out.pair(10, x);
        out.pair(20, y);
        out.pair(30, 0.0);
        out.pair(41, r.scale_x);
        out.pair(42, r.scale_y);
        out.pair(43, 1.0);
        out.pair(50, degrees(r.rotation));
    };

    const auto write_text = [&](const auto& t) {
        f64 x, y;
        world(t.pos, x, y);
        const bool multi = t.width > 0.0 ||
                           t.text.find("\\P") != std::string::npos ||
                           t.text.find('\n') != std::string::npos ||
                           t.text.find('{') != std::string::npos;
        if (multi) {
            out.pair(0, "MTEXT");
            handle();
            out.pair(8, t.layer);
            write_attrs(t.attrs);
            out.pair(100, "AcDbEntity");
            out.pair(100, "AcDbMText");
            out.pair(10, x);
            out.pair(20, y);
            out.pair(30, 0.0);
            out.pair(40, t.height * to_file);
            out.pair(41, t.width * to_file);         // column width; 0 = no wrap
            // Attachment: DXF numbers them 1..9 from top-left, reading across.
            const i32 row = t.valign == 3 ? 0 : t.valign == 2 ? 1 : 2;
            const i32 col = t.halign == 0 ? 1 : t.halign == 1 ? 2 : 3;
            out.pair_int(71, row * 3 + col);
            out.pair_int(72, 1);                     // by column, top to bottom
            // Code 1 takes at most 250 bytes; the rest go in 3s before it.
            std::string body = t.text;
            for (std::size_t at = 0; body.size() - at > 250;) {
                out.pair(3, body.substr(at, 250));
                at += 250;
                body = body.substr(250);
                at = 0;
            }
            out.pair(1, body);
            out.pair(7, t.style.empty()?"Standard":t.style);
            if (std::fabs(t.angle) > 1e-12) out.pair(50, t.angle * 180.0 / kPi);
            return;
        }
        out.pair(0, "TEXT");
        handle();
        out.pair(8, t.layer);
        write_attrs(t.attrs);
        out.pair(10, x);
        out.pair(20, y);
        out.pair(30, 0.0);
        out.pair(40, t.height * to_file);
        out.pair(1, t.text);
        out.pair(7,t.style.empty()?"Standard":t.style);
        out.pair_int(72,t.halign);out.pair_int(73,t.valign);
        if(t.halign || t.valign) {out.pair(11,x);out.pair(21,y);out.pair(31,0.);}
        if (std::fabs(t.angle) > 1e-12) out.pair(50, t.angle * 180.0 / kPi);
    };

    std::vector<std::string> dimension_blocks(d.dimensions.size());
    std::unordered_set<std::string> block_names;
    for (const auto& block : d.blocks.all()) block_names.insert(upper(block.name));
    std::size_t dimension_number = 1;
    for (std::size_t i = 0; i < d.dimensions.size(); ++i) {
        if (d.dimensions[i].kind == gdim::Kind::Leader) continue;
        std::string name;
        do { name = "*D" + std::to_string(dimension_number++); }
        while (!block_names.insert(upper(name)).second);
        dimension_blocks[i] = std::move(name);
    }
    if (o.write_blocks || !d.dimensions.empty()) {
        out.pair(0, "SECTION");
        out.pair(2, "BLOCKS");
        if (o.write_blocks) for (const gdoc::BlockDef& b : d.blocks.all()) {
            f64 bx, by;
            world(b.base, bx, by);
            out.pair(0, "BLOCK");
            out.pair(8, "0");
            out.pair(2, b.name);
            out.pair_int(70, 0);
            out.pair(10, bx);
            out.pair(20, by);
            out.pair(30, 0.0);
            out.pair(3, b.name);
            out.pair(1, b.is_xref ? b.xref_path : "");
            for (const gsnap::Shape& s : b.geometry) write_shape(s, s.layer);
            for (const auto& note : b.notes) write_text(note);
            for (const auto& attr : b.attributes) {
                f64 x,y;world(attr.position,x,y);
                out.pair(0,"ATTDEF");handle();out.pair(100,"AcDbEntity");out.pair(8,"0");
                out.pair(100,"AcDbText");out.pair(10,x);out.pair(20,y);out.pair(30,0.0);
                out.pair(40,attr.height*to_file);out.pair(1,attr.default_value);
                out.pair(50,attr.rotation*180.0/kPi);out.pair(7,attr.style.empty()?"Standard":attr.style);
                out.pair(100,"AcDbAttributeDefinition");out.pair(3,attr.prompt);out.pair(2,attr.tag);
                out.pair_int(70,(attr.invisible?1:0)|(attr.constant?2:0)|(attr.verify?4:0)|(attr.preset?8:0));
                out.pair_int(73,0);out.pair_int(74,0);
            }
            for(const auto& ref:d.blocks.nested_insertions(b))write_insert(ref);
            out.pair(0, "ENDBLK");
            out.pair(8, "0");
        }
        for (std::size_t i = 0; i < d.dimensions.size(); ++i) {
            if (dimension_blocks[i].empty()) continue;
            const auto& dim = d.dimensions[i];
            const auto style = std::find_if(styles.begin(), styles.end(),
                [&](const auto& value) { return value.name == dim.style; });
            const auto geometry = gdim::build(dim, style == styles.end() ? styles.front() : *style, v);
            if (!geometry.ok) return {};
            out.pair(0, "BLOCK"); handle(); out.pair(100, "AcDbEntity"); out.pair(8, "0");
            out.pair(100, "AcDbBlockBegin"); out.pair(2, dimension_blocks[i]); out.pair_int(70, 1);
            out.pair(10, 0.0); out.pair(20, 0.0); out.pair(30, 0.0);
            out.pair(3, dimension_blocks[i]); out.pair(1, "");
            for (const auto& line : geometry.lines) write_shape(line, "0");
            for (const auto& arrow : geometry.arrows) write_shape(arrow, "0");
            Text text; text.pos = geometry.text_at; text.text = geometry.text;
            text.height = geometry.text_height; text.angle = geometry.text_angle;
            text.halign = geometry.halign; text.valign = geometry.valign; text.layer = "0";
            write_text(text);
            out.pair(0, "ENDBLK"); handle(); out.pair(100, "AcDbEntity"); out.pair(8, "0");
            out.pair(100, "AcDbBlockEnd");
        }
        out.pair(0, "ENDSEC");
    }

    out.pair(0, "SECTION");
    out.pair(2, "ENTITIES");
    {
        std::unordered_map<gcm::u32, std::vector<const Entity*>> runs;
        for (const Entity& e : d.entities)
            if (e.shape.poly != 0) runs[e.shape.poly].push_back(&e);
        for (auto& [id, run] : runs)
            std::sort(run.begin(), run.end(), [](const Entity* a, const Entity* b) {
                return a->shape.poly_index < b->shape.poly_index;
            });

        std::unordered_set<gcm::u32> written;
        for (const Entity& e : d.entities) {
            if (e.shape.poly == 0) { write_shape(e.shape, e.layer); continue; }
            if (!written.insert(e.shape.poly).second) continue;
            const auto& run = runs[e.shape.poly];

            // Bulge = tan(sweep / 4), on the vertex the arc starts at.
            std::vector<std::pair<f64, f64>> pts;
            std::vector<f64> bulges;
            const auto vertex = [&](f64 x, f64 y, f64 bulge) {
                pts.push_back({x, y});
                bulges.push_back(bulge);
            };
            bool ok = true;
            for (const Entity* pe : run) {
                const gsnap::Shape& sp = pe->shape;
                if (sp.kind == gsnap::ShapeKind::Segment) {
                    f64 ax, ay, bx, by;
                    world(sp.a, ax, ay);
                    world(sp.b, bx, by);
                    vertex(ax, ay, 0.0);
                    if (pe == run.back()) vertex(bx, by, 0.0);
                } else if (sp.kind == gsnap::ShapeKind::Arc) {
                    f64 cx, cy;
                    world(sp.a, cx, cy);
                    const f64 r = f64(sp.radius) / v.scale() * to_file;
                    f64 sweep = sp.end_angle - sp.start_angle;
                    while (sweep < 0.0) sweep += 6.28318530717958647692;
                    vertex(cx + r * std::cos(sp.start_angle),
                           cy + r * std::sin(sp.start_angle),
                           std::tan(sweep / 4.0));
                    if (pe == run.back())
                        vertex(cx + r * std::cos(sp.end_angle),
                               cy + r * std::sin(sp.end_angle), 0.0);
                } else {
                    ok = false;      // a circle in a polyline is not a polyline
                    break;
                }
            }
            if (!ok || pts.size() < 2) {
                for (const Entity* pe : run) write_shape(pe->shape, pe->layer);
                continue;
            }
            const bool closed = std::hypot(pts.back().first - pts.front().first,
                                           pts.back().second - pts.front().second) <= std::max(kEps,4*v.inv_scale()*to_file);
            if (closed) { pts.pop_back(); bulges.pop_back(); }

            out.pair(0, "LWPOLYLINE");
            handle();
            out.pair(8, run.front()->layer);
            write_attrs(run.front()->shape.attrs);
            out.pair(100, "AcDbEntity");
            out.pair(100, "AcDbPolyline");
            out.pair_int(90, static_cast<i32>(pts.size()));
            out.pair_int(70, closed ? 1 : 0);
            for (std::size_t k = 0; k < pts.size(); ++k) {
                out.pair(10, pts[k].first);
                out.pair(20, pts[k].second);
                if (std::fabs(bulges[k]) > kEps) out.pair(42, bulges[k]);
            }
        }
    }
    for (const Text& t : d.texts) write_text(t);
    for (const auto& c : d.splines) {
        if (!c.valid()) continue;
        bool rational = false;
        for (const auto& cp : c.control)
            rational = rational || std::fabs(cp.w.to_f64() - 1.0) > 1e-12;
        out.pair(0, "SPLINE");
        handle();
        out.pair(8, "0");
        out.pair(100, "AcDbEntity");
        out.pair(100, "AcDbSpline");
        // 8 = planar; 4 = rational. Closed and periodic are not produced here.
        out.pair_int(70, 8 | (rational ? 4 : 0));
        out.pair_int(71, c.degree);
        out.pair_int(72, static_cast<i32>(c.knots.knots.size()));
        out.pair_int(73, static_cast<i32>(c.control.size()));
        for (const f64 k : c.knots.knots) out.pair(40, k);
        if (rational)
            for (const auto& cp : c.control) out.pair(41, cp.w.to_f64());
        for (const auto& cp : c.control) {
            out.pair(10, cp.p.x.to_f64() * to_file);
            out.pair(20, cp.p.y.to_f64() * to_file);
            out.pair(30, 0.0);
        }
    }

    gdoc::PatternTable hatch_patterns;
    for (const HatchRec& h : d.hatches) {
        struct HatchEdge { gsnap::Shape shape; bool ccw{true}; };
        std::vector<std::vector<HatchEdge>> paths;
        std::vector<HatchEdge> edges;
        for (const auto& shape : h.boundary) {
            if (shape.kind==gsnap::ShapeKind::Circle) paths.push_back({{shape}});
            else if (shape.kind==gsnap::ShapeKind::Segment || shape.kind==gsnap::ShapeKind::Arc)
                edges.push_back({shape});
            else return {};
        }
        const auto ends=[&](const HatchEdge& edge) {
            auto a=edge.shape.a,b=edge.shape.b;
            if (edge.shape.kind==gsnap::ShapeKind::Arc) {
                double x,y; gcm::to_world(v,edge.shape.a,x,y);
                const double radius=double(edge.shape.radius)/v.scale();
                a=gcm::to_lattice(v,x+radius*std::cos(edge.shape.start_angle),y+radius*std::sin(edge.shape.start_angle));
                b=gcm::to_lattice(v,x+radius*std::cos(edge.shape.end_angle),y+radius*std::sin(edge.shape.end_angle));
            }
            if (!edge.ccw) std::swap(a,b);
            return std::pair{a,b};
        };
        const auto near=[](auto a,auto b) {
            return std::abs(double(a.x)-double(b.x))<=2 && std::abs(double(a.y)-double(b.y))<=2;
        };
        while (!edges.empty()) {
            std::vector<HatchEdge> path{edges.back()}; edges.pop_back();
            auto [start,end]=ends(path[0]);
            while (!near(start,end)) {
                bool found=false;
                for (std::size_t i=0;i<edges.size();++i) {
                    auto [a,b]=ends(edges[i]);
                    if (near(end,b)) { edges[i].ccw=!edges[i].ccw; std::swap(a,b); }
                    if (!near(end,a)) continue;
                    path.push_back(edges[i]); edges.erase(edges.begin()+i); end=b; found=true; break;
                }
                if (!found) return {};
            }
            paths.push_back(std::move(path));
        }
        if (paths.empty()) return {};

        out.pair(0, "HATCH");
        handle();
        out.pair(100, "AcDbEntity");
        out.pair(8, h.layer);
        write_attrs(h.attrs);
        out.pair(100, "AcDbHatch");
        out.pair(10, 0.0);            // elevation point
        out.pair(20, 0.0);
        out.pair(30, 0.0);
        out.pair(210, 0.0);           // extrusion
        out.pair(220, 0.0);
        out.pair(230, 1.0);
        out.pair(2, h.solid ? "SOLID" : h.pattern);
        out.pair_int(70, h.solid ? 1 : 0);          // solid fill flag
        out.pair_int(71, 0);                        // not associative on export
        out.pair_int(91, static_cast<i32>(paths.size()));
        for (const auto& path : paths) {
            out.pair_int(92, 0);                        // edge list
            out.pair_int(93, static_cast<i32>(path.size()));
            for (const auto& edge : path) {
                const auto& e=edge.shape;
                f64 ax, ay, bx, by;
                world(e.a, ax, ay);
                world(e.b, bx, by);
                if (e.kind == gsnap::ShapeKind::Arc || e.kind == gsnap::ShapeKind::Circle) {
                    const bool circle = e.kind == gsnap::ShapeKind::Circle;
                    out.pair_int(72, 2);                // circular arc edge
                    out.pair(10, ax);
                    out.pair(20, ay);
                    out.pair(40, f64(e.radius) / v.scale() * to_file);
                    out.pair(50, circle ? 0.0 : degrees(edge.ccw ? e.start_angle : e.end_angle));
                    out.pair(51, circle ? 360.0 : degrees(edge.ccw ? e.end_angle : e.start_angle));
                    out.pair_int(73, edge.ccw ? 1 : 0);                // counter-clockwise
                } else {
                    if (!edge.ccw) { std::swap(ax,bx); std::swap(ay,by); }
                    out.pair_int(72, 1);                // line edge
                    out.pair(10, ax);
                    out.pair(20, ay);
                    out.pair(11, bx);
                    out.pair(21, by);
                }
            }
            out.pair_int(97, 0);          // no source boundary objects
        }
        out.pair_int(75, 0);          // alternate filled and excluded islands
        out.pair_int(76, 1);          // pattern type: predefined
        if (!h.solid) {
            out.pair(52, h.angle);
            out.pair(41, h.scale);
            out.pair_int(77, 0);      // not double
            auto lines=h.pattern_lines;
            if (lines.empty()) {
                const auto* pattern=hatch_patterns.find(h.pattern);
                if (!pattern) return {};
                lines=pattern->lines;
            }
            out.pair_int(78, static_cast<i32>(lines.size()));
            const double rotation=radians(h.angle), scale=h.scale*to_file;
            for (const auto& line : lines) {
                const double angle=radians(line.angle)+rotation;
                out.pair(53, degrees(angle));
                out.pair(43, (line.x*std::cos(rotation)-line.y*std::sin(rotation))*scale);
                out.pair(44, (line.x*std::sin(rotation)+line.y*std::cos(rotation))*scale);
                out.pair(45, (line.dx*std::cos(angle)-line.dy*std::sin(angle))*scale);
                out.pair(46, (line.dx*std::sin(angle)+line.dy*std::cos(angle))*scale);
                out.pair_int(79, static_cast<i32>(line.dashes.size()));
                for (const auto dash : line.dashes) out.pair(49, dash*scale);
            }
        }
        out.pair_int(98, 1);          // one seed point
        f64 sx, sy;
        world(h.seed, sx, sy);
        out.pair(10, sx);
        out.pair(20, sy);
    }

    for (std::size_t i = 0; i < d.dimensions.size(); ++i) {
        const gdim::Dimension& dim = d.dimensions[i];
        f64 x1, y1, x2, y2, x3, y3;
        world(dim.p1, x1, y1);
        world(dim.p2, x2, y2);
        world(dim.p3, x3, y3);
        if (dim.kind == gdim::Kind::Leader) {
            const auto it=std::find_if(styles.begin(),styles.end(),[&](const auto& st){return st.name==dim.style;});
            const auto geom=gdim::build(dim,it==styles.end()?styles.front():*it,v);
            out.pair(0,"LEADER"); handle(); out.pair(100,"AcDbEntity");
            out.pair(8,dim.layer.empty()?std::string("0"):dim.layer); out.pair(100,"AcDbLeader");
            const auto paths=dim.paths();
            out.pair(3,dim.style); out.pair_int(71,paths.front().arrow);out.pair_int(72,0);
            out.pair_int(73,dim.text_override.empty()?3:0);out.pair_int(76,i32(paths.front().vertices.size()));
            for(auto pt:paths.front().vertices) {f64 x,y;world(pt,x,y);out.pair(10,x);out.pair(20,y);out.pair(30,0.0);}
            if(!dim.text_override.empty()) {
                std::ostringstream h; h<<std::hex<<std::uppercase<<next_handle;out.pair(340,h.str());
                f64 tx,ty;world(geom.text_at,tx,ty);
                out.pair(0,"MTEXT");handle();out.pair(100,"AcDbEntity");out.pair(8,dim.layer);
                out.pair(100,"AcDbMText");out.pair(10,tx);out.pair(20,ty);out.pair(30,0.0);
                out.pair(40,geom.text_height*to_file);out.pair_int(71,(geom.valign==3?0:geom.valign==2?3:6)+std::clamp(geom.halign,0,2)+1);
                out.pair(50,degrees(geom.text_angle));
                out.pair(1,dim.text_override);out.pair(7,"Standard");
            }
            continue;
        }
        i32 type = 0;
        switch (dim.kind) {
            case gdim::Kind::Aligned:   type = 1; break;
            case gdim::Kind::Angular:   type = 2; break;
            case gdim::Kind::Diameter:  type = 3; break;
            case gdim::Kind::Radial:    type = 4; break;
            case gdim::Kind::Ordinate:  type = 6; break;
            default:                    type = 0; break;   // linear family
        }
        out.pair(0, "DIMENSION");
        handle();
        out.pair(100, "AcDbEntity");
        out.pair(8, dim.layer.empty() ? std::string("0") : dim.layer);
        out.pair(100, "AcDbDimension");
        out.pair(2, dimension_blocks[i]);
        out.pair(10, x3);             // dimension line location
        out.pair(20, y3);
        out.pair(30, 0.0);
        out.pair(11, x3);             // text midpoint
        out.pair(21, y3);
        out.pair(31, 0.0);
        // Bit 128: the text position is user-set, which is what p3 IS here.
        out.pair_int(70, type | 32 | 128);
        if (!dim.text_override.empty()) out.pair(1, dim.text_override);
        out.pair(3, dim.style.empty() ? std::string("Standard") : dim.style);
        if (type == 3 || type == 4) {
            f64 cx, cy;
            world(dim.centre, cx, cy);
            out.pair(100, "AcDbRadialDimension");
            out.pair(15, x1);
            out.pair(25, y1);
            out.pair(35, 0.0);
            (void)cx; (void)cy;
        } else {
            out.pair(100, "AcDbAlignedDimension");
            out.pair(13, x1);
            out.pair(23, y1);
            out.pair(33, 0.0);
            out.pair(14, x2);
            out.pair(24, y2);
            out.pair(34, 0.0);
            if (type == 0) {
                out.pair(100, "AcDbRotatedDimension");
                const bool vertical=dim.kind==gdim::Kind::Vertical ||
                    (dim.kind==gdim::Kind::Linear && !gdim::linear_is_horizontal(dim,v));
                out.pair(50,vertical?90.0:0.0);
            }
        }
    }

    for(const auto& ref:d.blocks.references())write_insert(ref);
    out.pair(0, "ENDSEC");
    out.pair(0, "EOF");
    return out.s.str();
}

namespace {

struct Pending {
    std::string kind;
    std::string handle, annotation_handle;
    std::string layer{"0"};
    std::string name;                // block name, for INSERT and BLOCK
    std::string style;               // DIMENSION: the dimension style's name
    f64 x{0}, y{0}, z{0};
    f64 x1{0}, y1{0}, z1{0};
    f64 radius{0};
    f64 start_deg{0}, end_deg{360};
    f64 ratio{1.0};
    f64 scale_x{1.0}, scale_y{1.0};
    f64 rotation_deg{0.0};
    f64 column_width{0};
    i32 attachment{1};
    i32 halign{0}, valign{0};        // TEXT 72 / 73
    f64 ex{0}, ey{0}, ez{1};         // extrusion direction
    std::string text;                // TEXT / MTEXT content
    std::string attribute_prompt;
    f64 bulge{0.0};                  // VERTEX
    f64 dx[7] = {}, dy[7] = {};
    bool has_pt[7] = {};
    i32 degree{3}, spline_flags{0};
    std::vector<f64> knots, weights;
    std::vector<std::pair<f64, f64>> fit;
    struct HEdge {
        i32  type{1};                // 1 line, 2 arc, 3 ellipse, 4 spline, 0 polyline vertex
        f64  x0{0}, y0{0}, x1{0}, y1{0};
        f64  cx{0}, cy{0}, r{0}, a0{0}, a1{360};
        bool ccw{true};
        f64  bulge{0};
        int  have{0};                // points received so far
    };
    std::vector<std::vector<HEdge>> hloops;
    i32  hloop_type{0};
    bool hafter_seed{false};
    bool hsolid{false};
    f64  hscale{1.0}, hangle{0.0};
    gdoc::Attributes attrs;
    i32 flags{0};
    bool has_second{false};
    bool has_radius{false};
    bool has_angles{false};
    double ellipse_start{0},ellipse_end{2*kPi};

    // Polyline vertices, with the bulge to the next one.
    struct Vertex { f64 x{0}, y{0}, bulge{0}; };
    std::vector<Vertex> vertices;
    std::vector<std::vector<std::pair<f64, f64>>> ml_lines;
    int  ml_state{0};                // 0 outside, 1 CONTEXT_DATA, 2 LEADER, 3 LEADER_LINE
    bool ml_has_conn{false};
    f64  ml_cx{0}, ml_cy{0}, ml_dx{1}, ml_dy{0}, ml_dogleg{0};
    f64  ml_tx{0}, ml_ty{0}, ml_height{0};
    bool ml_has_text_pos{false};
    std::string ml_text;
    bool closed{false};
};

}

ReadResult read(std::string_view text, const gcm::WorkingVolume& v) {
    ReadResult out;
    const Tape tape = lex(text);
    if (!tape.ok) {
        out.error = tape.error;
        return out;
    }

    Units units = Units::Unspecified;
    for (std::size_t i = 0; i + 1 < tape.size(); ++i) {
        if (tape.groups[i].code != 9 || tape.groups[i].value != "$INSUNITS")
            continue;
        i32 code = 0;
        if (tape.groups[i + 1].as_int(code)) units = static_cast<Units>(code);
        break;
    }
    out.document.units = units;

    f64 to_metres = metres_per(units);
    if (to_metres <= 0.0) {
        to_metres = 1.0;
        out.skipped.push_back(
            "the file does not state its units ($INSUNITS is 0); the "
            "coordinates were taken as they stand");
    }

    for (std::size_t i = 0; i + 1 < tape.size(); ++i) {
        if (tape.groups[i].code == 9 && tape.groups[i].value == "$ACADVER")
            out.document.dxf_version = tape.groups[i + 1].value;
    }

    auto lattice = [&](f64 x, f64 y) {
        return gcm::to_lattice(v, x * to_metres, y * to_metres);
    };

    std::string section;
    std::string table;
    bool in_blocks = false;
    std::vector<std::pair<std::string, gdoc::BlockRef>> nested_inserts;
    std::string current_block;

    std::unordered_map<std::string,std::size_t> text_handles;
    std::vector<std::pair<std::size_t,std::string>> leader_annotations;
    Pending pending;
    bool have_pending = false;

    Pending open_poly;
    bool have_open_poly = false;
    gcm::u32 last_poly = 0;
    std::function<void()> finish;
    finish = [&]() {
        if (!have_pending) return;
        have_pending = false;
        const Pending p = pending;
        pending = Pending{};

        if (p.kind == "POLYLINE") { open_poly = p; have_open_poly = true; return; }
        if (p.kind == "VERTEX") {
            if (have_open_poly) open_poly.vertices.push_back({p.x, p.y, p.bulge});
            return;
        }
        if (p.kind == "SEQEND") {
            if (have_open_poly) {
                have_open_poly = false;
                pending = open_poly;
                pending.kind = "LWPOLYLINE";
                have_pending = true;
                finish();
            }
            return;
        }

        // A negative extrusion Z means the coordinates are in a mirrored system.
        const bool mirrored = p.ez < 0.0;
        if (mirrored) ++out.mirrored_entities;
        const f64 flip = mirrored ? -1.0 : 1.0;

        gcm::u32 poly_run = 0, poly_next = 0;
        auto push = [&](gsnap::Shape s, const std::string& kind) {
            s.attrs = p.attrs;
            if (poly_run != 0) {
                s.poly = poly_run;
                s.poly_index = poly_next++;
            }
            Entity e;
            e.shape = s;
            e.layer = p.layer;
            e.kind = kind;
            if (in_blocks && !current_block.empty()) {
                if (gdoc::BlockDef* def =
                        const_cast<gdoc::BlockDef*>(out.document.blocks.find(current_block)))
                    def->geometry.push_back(s);
                return;
            }
            out.document.entities.push_back(std::move(e));
        };

        if (p.kind == "ATTDEF") {
            if(in_blocks&&!current_block.empty())if(auto* def=const_cast<gdoc::BlockDef*>(out.document.blocks.find(current_block))) {
                gdoc::AttributeDef attr;attr.tag=p.name;attr.prompt=p.attribute_prompt;attr.default_value=p.text;
                attr.position=lattice(p.x*flip,p.y);
                if((p.halign||p.valign)&&p.has_second)attr.position=lattice(p.x1*flip,p.y1);
                attr.height=p.radius*to_metres;attr.rotation=radians(p.start_deg);attr.style=p.style.empty()?"Standard":p.style;
                attr.invisible=p.flags&1;attr.constant=p.flags&2;attr.verify=p.flags&4;attr.preset=p.flags&8;
                def->attributes.push_back(std::move(attr));
            }
            return;
        }
        if (p.kind == "TEXT" || p.kind == "MTEXT" || p.kind == "ATTRIB") {
            if (p.kind == "ATTRIB" && (p.flags & 1)) return;   // invisible attribute
            Text t;
            t.pos = lattice(p.x * flip, p.y);
            t.text = p.text;
            t.height = p.radius * to_metres;
            t.angle = p.has_angles ? p.start_deg * kPi / 180.0 : 0.0;
            // Aligned text anchors at its second point (11/21).
            t.halign = (p.halign == 1 || p.halign == 4) ? 1 : (p.halign == 2 ? 2 : (p.halign >= 3 ? 1 : 0));
            t.valign = p.halign == 4 ? 2 : p.valign;
            if ((p.halign > 0 || p.valign > 0) && p.has_second)
                t.pos = lattice(p.x1 * flip, p.y1);
            t.layer = p.layer; t.style=p.style.empty()?"Standard":p.style; t.attrs=p.attrs;
            if(p.kind=="MTEXT") {
                t.width=p.column_width*to_metres;
                const int at=std::clamp(p.attachment,1,9)-1;
                t.halign=at%3;t.valign=at/3==0?3:at/3==1?2:1;
                t.pos=lattice(p.x*flip,p.y);
            }
            decode_percent(t.text);
            if (t.text.empty()) return;
            if (in_blocks && !current_block.empty()) {
                if (gdoc::BlockDef* def = const_cast<gdoc::BlockDef*>(out.document.blocks.find(current_block))) {
                    gdoc::BlockDef::Note n;
                    n.pos = t.pos; n.text = t.text; n.height = t.height; n.angle = t.angle; n.layer = t.layer;
                    n.halign = t.halign; n.valign = t.valign;
                    n.width = t.width; n.style = t.style; n.attrs = t.attrs;
                    def->notes.push_back(std::move(n));
                }
                return;
            }
            if(!p.handle.empty()) text_handles[p.handle]=out.document.texts.size();
            out.document.texts.push_back(std::move(t));
            return;
        }
        if (p.kind == "DIMENSION") {
            gdim::Dimension d;
            d.layer=p.layer;
            if (!p.style.empty()) d.style=p.style;
            const int type = p.flags & 7;
            const auto pt = [&](int k) { return lattice(p.dx[k] * flip, p.dy[k]); };
            switch (type) {
                case 1: d.kind = gdim::Kind::Aligned; d.p1 = pt(3); d.p2 = pt(4); d.p3 = pt(0); break;
                case 2: {
                    // Two lines: 13-14 and 15-16; the vertex is where they meet.
                    d.kind = gdim::Kind::Angular;
                    const f64 x1 = p.dx[3], y1 = p.dy[3], x2 = p.dx[4], y2 = p.dy[4];
                    const f64 x3 = p.dx[5], y3 = p.dy[5], x4 = p.dx[6], y4 = p.dy[6];
                    const f64 den = (x1 - x2) * (y3 - y4) - (y1 - y2) * (x3 - x4);
                    f64 cx = x1, cy = y1;
                    if (std::fabs(den) > 1e-12) {
                        const f64 t = ((x1 - x3) * (y3 - y4) - (y1 - y3) * (x3 - x4)) / den;
                        cx = x1 + t * (x2 - x1); cy = y1 + t * (y2 - y1);
                    }
                    d.centre = lattice(cx * flip, cy);
                    d.p1 = pt(4); d.p2 = pt(6); d.p3 = pt(0);
                    break;
                }
                case 3: d.kind = gdim::Kind::Diameter; d.p1 = pt(0); d.p2 = pt(5);
                        d.centre = lattice((p.dx[0] + p.dx[5]) * 0.5 * flip, (p.dy[0] + p.dy[5]) * 0.5); d.p3 = pt(1); break;
                case 4: d.kind = gdim::Kind::Radial; d.centre = pt(0); d.p1 = pt(5); d.p2 = pt(5); d.p3 = pt(1); break;
                case 5: d.kind = gdim::Kind::Angular; d.centre = pt(5); d.p1 = pt(3); d.p2 = pt(4); d.p3 = pt(0); break;
                case 6: d.kind = gdim::Kind::Ordinate; d.p1 = pt(3); d.p2 = pt(4); d.p3 = pt(0); break;
                default: {
                    const f64 rot = p.has_angles ? p.start_deg : 0.0;
                    const bool vertical = std::fabs(std::fmod(std::fabs(rot), 180.0) - 90.0) < 1.0;
                    d.kind = vertical ? gdim::Kind::Vertical : gdim::Kind::Horizontal;
                    d.p1 = pt(3); d.p2 = pt(4); d.p3 = pt(0);
                    break;
                }
            }
            if (!p.text.empty() && p.text != "<>") {
                d.text_override = p.text;
                decode_percent(d.text_override);
            }
            if (!in_blocks) out.document.dimensions.push_back(std::move(d));
            return;
        }
        if (p.kind == "SOLID" || p.kind == "TRACE" || p.kind == "3DFACE") {
            // SOLID and TRACE give corners in bow-tie order (1 2 4 3); 3DFACE goes round.
            const bool bowtie = p.kind != "3DFACE";
            const int order[4] = {0, 1, bowtie ? 3 : 2, bowtie ? 2 : 3};
            int n = 4;
            if (!p.has_pt[3] || (p.dx[3] == p.dx[2] && p.dy[3] == p.dy[2])) n = 3;
            for (int k = 0; k < n; ++k) {
                const int a = order[k], b = order[(k + 1) % n];
                gsnap::Shape s;
                s.kind = gsnap::ShapeKind::Segment;
                s.a = lattice(p.dx[a] * flip, p.dy[a]);
                s.b = lattice(p.dx[b] * flip, p.dy[b]);
                if (s.a.x != s.b.x || s.a.y != s.b.y) push(s, p.kind);
            }
            return;
        }
        if (p.kind == "MULTILEADER" || p.kind == "MLEADER") {
            gdim::Dimension leader;leader.kind=gdim::Kind::Leader;leader.layer=p.layer;
            if(p.ml_lines.size()>256||!std::isfinite(p.ml_height)||p.ml_height<0){out.skipped.push_back("invalid multileader paths or text height");return;}
            std::size_t count=0;
            for(const auto& line:p.ml_lines) {
                count+=line.size();if(count>4096){out.skipped.push_back("multileader exceeds 4096 vertices");return;}
                gdim::Dimension::LeaderPath path;
                for(auto [x,y]:line) {
                    const auto point=lattice(x*flip,y);
                    if(path.vertices.empty()||path.vertices.back().x!=point.x||path.vertices.back().y!=point.y)path.vertices.push_back(point);
                }
                if(path.vertices.size()>1)leader.leader_paths.push_back(std::move(path));
            }
            if(leader.leader_paths.empty())return;
            leader.p1=leader.leader_paths.front().vertices.front();leader.p2=leader.leader_paths.front().vertices[1];
            leader.p3=p.ml_has_text_pos?lattice(p.ml_tx*flip,p.ml_ty):leader.leader_paths.front().vertices.back();
            leader.text_override=p.ml_text;leader.leader_text_absolute=true;leader.leader_text_height=p.ml_height*to_metres;
            out.document.dimensions.push_back(std::move(leader));return;
        }
        if (p.kind == "LEADER") {
            if(p.vertices.size()<2 || p.vertices.size()>4096 || p.halign!=0) {out.skipped.push_back("unsupported or invalid leader path");return;}
            gdim::Dimension d;d.kind=gdim::Kind::Leader;d.layer=p.layer;
            d.style=p.style.empty()?"Standard":p.style;
            const auto pt=[&](std::size_t i){return lattice(p.vertices[i].x*flip,p.vertices[i].y);};
            d.p1=pt(0);d.p2=pt(1);d.p3=pt(p.vertices.size()-1);
            gdim::Dimension::LeaderPath path;path.arrow=p.attachment!=0;
            for(std::size_t i=0;i<p.vertices.size();++i)path.vertices.push_back(pt(i));
            d.leader_paths.push_back(std::move(path));
            leader_annotations.push_back({out.document.dimensions.size(),p.annotation_handle});
            out.document.dimensions.push_back(std::move(d));return;
        }
        if (p.kind == "SPLINE") {
            ggeo::NurbsCurve2<gcm::F64> c;
            c.degree = std::max(1, p.degree);
            const bool rational = (p.spline_flags & 4) != 0 && p.weights.size() == p.vertices.size();
            for (std::size_t k = 0; k < p.vertices.size(); ++k) {
                ggeo::ControlPoint2<gcm::F64> cp;
                cp.p = ggeo::d2::Point<gcm::F64>::from(p.vertices[k].x * flip * to_metres, p.vertices[k].y * to_metres);
                cp.w = gcm::F64::from(rational ? p.weights[k] : 1.0);
                c.control.push_back(cp);
            }
            if (c.control.empty() && p.fit.size() >= 2) {
                std::vector<ggeo::d2::Point<gcm::F64>> pts;
                for (const auto& q : p.fit) pts.push_back(ggeo::d2::Point<gcm::F64>::from(q.first * flip * to_metres, q.second * to_metres));
                c = ggeo::interpolate_points<gcm::F64>(pts, std::min<int>(3, static_cast<int>(pts.size()) - 1));
            } else {
                if (p.knots.size() == c.control.size() + static_cast<std::size_t>(c.degree) + 1) c.knots.knots = p.knots;
                else c.knots = ggeo::KnotVector::clamped_uniform(c.degree, c.control.size());
            }
            if (c.valid() && !in_blocks) out.document.splines.push_back(std::move(c));
            else if (c.valid()) {
                if (gdoc::BlockDef* def = const_cast<gdoc::BlockDef*>(out.document.blocks.find(current_block))) {
                    const int steps = 24;
                    for (int k = 0; k < steps; ++k) {
                        const f64 t0 = c.knots.first() + (c.knots.last() - c.knots.first()) * k / steps;
                        const f64 t1 = c.knots.first() + (c.knots.last() - c.knots.first()) * (k + 1) / steps;
                        const auto a = c.evaluate(t0), b = c.evaluate(t1);
                        gsnap::Shape s;
                        s.kind = gsnap::ShapeKind::Segment;
                        s.a = gcm::to_lattice(v, a.x.to_f64(), a.y.to_f64());
                        s.b = gcm::to_lattice(v, b.x.to_f64(), b.y.to_f64());
                        def->geometry.push_back(s);
                    }
                }
            }
            return;
        }
        if (p.kind == "HATCH") {
            std::vector<std::pair<f64, f64>> first_loop;
            std::vector<gsnap::Shape> boundary;
            const auto push_edge = [&](const gsnap::Shape& s) { boundary.push_back(s); push(s, p.kind); };
            for (std::size_t li = 0; li < p.hloops.size(); ++li) {
                const auto& loop = p.hloops[li];
                if (loop.empty()) continue;
                const bool polyline = loop[0].type == 0;
                if (polyline) {
                    const std::size_t n = loop.size();
                    for (std::size_t k = 0; k < n; ++k) {
                        const auto& a = loop[k];
                        const auto& b = loop[(k + 1) % n];
                        if (k + 1 == n && n < 3) break;
                        const Bulge arc = arc_from_bulge(a.x0 * flip, a.y0, b.x0 * flip, b.y0, mirrored ? -a.bulge : a.bulge);
                        gsnap::Shape s;
                        if (!arc.is_arc) {
                            s.kind = gsnap::ShapeKind::Segment;
                            s.a = lattice(a.x0 * flip, a.y0);
                            s.b = lattice(b.x0 * flip, b.y0);
                        } else {
                            s.kind = gsnap::ShapeKind::Arc;
                            s.a = lattice(arc.cx, arc.cy);
                            s.b = s.a;
                            s.radius = static_cast<gcm::i64>(std::llround(arc.radius * to_metres * v.scale()));
                            s.start_angle = arc.start_angle;
                            s.end_angle = arc.end_angle;
                        }
                        if (s.kind == gsnap::ShapeKind::Arc || s.a.x != s.b.x || s.a.y != s.b.y) push_edge(s);
                        if (li == 0) first_loop.push_back({a.x0 * flip, a.y0});
                    }
                } else {
                    for (const auto& e : loop) {
                        gsnap::Shape s;
                        if (e.type == 2) {
                            s.kind = std::fabs(std::fabs(e.a1-e.a0)-360.0)<1e-8
                                ? gsnap::ShapeKind::Circle : gsnap::ShapeKind::Arc;
                            s.a = lattice(e.cx * flip, e.cy);
                            s.b = s.a;
                            s.radius = static_cast<gcm::i64>(std::llround(e.r * to_metres * v.scale()));
                            f64 a0 = radians(e.a0), a1 = radians(e.a1);
                            if (!e.ccw) std::swap(a0, a1);
                            if (mirrored) { const f64 s0 = kPi - a1, s1 = kPi - a0; a0 = s0; a1 = s1; }
                            s.start_angle = normalise_angle(a0);
                            s.end_angle = normalise_angle(a1);
                            if (li == 0) {
                                for (int k = 0; k <= 8; ++k) {
                                    const f64 t = a0 + (a1 - a0) * k / 8.0;
                                    first_loop.push_back({e.cx * flip + e.r * std::cos(t), e.cy + e.r * std::sin(t)});
                                }
                            }
                        } else {
                            s.kind = gsnap::ShapeKind::Segment;
                            s.a = lattice(e.x0 * flip, e.y0);
                            s.b = lattice(e.x1 * flip, e.y1);
                            if (s.a.x == s.b.x && s.a.y == s.b.y) continue;
                            if (li == 0) first_loop.push_back({e.x0 * flip, e.y0});
                        }
                        push_edge(s);
                    }
                }
            }
            if (first_loop.size() >= 3 && !in_blocks) {
                HatchRec h;
                h.pattern = p.name.empty() ? "ANSI31" : p.name;
                h.solid = p.hsolid || h.pattern == "SOLID";
                h.scale = p.hscale;
                h.angle = p.hangle;
                h.layer = p.layer;
                h.attrs = p.attrs;
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
                h.seed = lattice(cx, cy);
                h.boundary = std::move(boundary);
                out.document.hatches.push_back(std::move(h));
            }
            return;
        }
        if (p.kind == "LINE") {
            gsnap::Shape s;
            s.kind = gsnap::ShapeKind::Segment;
            s.a = lattice(p.x * flip, p.y);
            s.b = lattice(p.x1 * flip, p.y1);
            push(s, p.kind);
        } else if (p.kind == "POINT") {
            gsnap::Shape s;
            s.kind = gsnap::ShapeKind::Point;
            s.a = lattice(p.x * flip, p.y);
            s.b = s.a;
            push(s, p.kind);
        } else if (p.kind == "CIRCLE") {
            gsnap::Shape s;
            s.kind = gsnap::ShapeKind::Circle;
            s.a = lattice(p.x * flip, p.y);
            s.b = s.a;
            s.radius = static_cast<gcm::i64>(
                std::llround(p.radius * to_metres * v.scale()));
            push(s, p.kind);
        } else if (p.kind == "ARC") {
            gsnap::Shape s;
            s.kind = gsnap::ShapeKind::Arc;
            s.a = lattice(p.x * flip, p.y);
            s.b = s.a;
            s.radius = static_cast<gcm::i64>(
                std::llround(p.radius * to_metres * v.scale()));
            f64 a0 = radians(p.start_deg), a1 = radians(p.end_deg);
            if (mirrored) {
                // Mirroring reverses the sweep as well as the position.
                const f64 s0 = kPi - a1, s1 = kPi - a0;
                a0 = s0;
                a1 = s1;
            }
            s.start_angle = normalise_angle(a0);
            s.end_angle = normalise_angle(a1);
            push(s, p.kind);
        } else if (p.kind == "ELLIPSE") {
            gsnap::Shape s;
            s.kind = gsnap::ShapeKind::Ellipse;
            s.a = lattice(p.x * flip, p.y);
            s.b = lattice((p.x + p.x1) * flip, p.y + p.y1);
            const f64 major = std::hypot(p.x1, p.y1);
            s.start_angle=mirrored?-p.ellipse_end:p.ellipse_start;s.end_angle=mirrored?-p.ellipse_start:p.ellipse_end;
            s.radius = static_cast<gcm::i64>(
                std::llround(major * p.ratio * to_metres * v.scale()));
            push(s, p.kind);
        } else if (p.kind == "RAY" || p.kind == "XLINE") {
            gsnap::Shape s;
            s.kind = p.kind == "RAY" ? gsnap::ShapeKind::Ray
                                     : gsnap::ShapeKind::Xline;
            s.a = lattice(p.x * flip, p.y);
            // The second pair is a direction.
            s.b = lattice((p.x + p.x1) * flip, p.y + p.y1);
            push(s, p.kind);
        } else if (p.kind == "LWPOLYLINE" || p.kind == "POLYLINE") {
            const std::size_t n = p.vertices.size();
            if (n >= 2) {
                poly_run = ++last_poly;
                poly_next = 0;
                const std::size_t last = p.closed ? n : n - 1;
                for (std::size_t k = 0; k < last; ++k) {
                    const auto& a = p.vertices[k];
                    const auto& b = p.vertices[(k + 1) % n];
                    const f64 ax = a.x * flip, bx = b.x * flip;
                    const Bulge arc = arc_from_bulge(ax, a.y, bx, b.y,
                                                     mirrored ? -a.bulge : a.bulge);
                    if (!arc.is_arc) {
                        gsnap::Shape s;
                        s.kind = gsnap::ShapeKind::Segment;
                        s.a = lattice(ax, a.y);
                        s.b = lattice(bx, b.y);
                        push(s, p.kind);
                    } else {
                        gsnap::Shape s;
                        s.kind = gsnap::ShapeKind::Arc;
                        s.a = lattice(arc.cx, arc.cy);
                        s.b = s.a;
                        s.radius = static_cast<gcm::i64>(
                            std::llround(arc.radius * to_metres * v.scale()));
                        s.start_angle = arc.start_angle;
                        s.end_angle = arc.end_angle;
                        push(s, p.kind);
                    }
                }
                poly_run = 0;
            }
        } else if (p.kind == "INSERT") {
            gdoc::BlockRef r;
            r.name = p.name;
            r.insertion = lattice(p.x * flip, p.y);
            r.scale_x = p.scale_x;
            r.scale_y = p.scale_y;
            r.rotation = radians(p.rotation_deg);
            if (mirrored) {
                // Mirror-then-rotate: negate the angle and flip the hand.
                r.rotation = -r.rotation;
                r.scale_x = -r.scale_x;
            }
            r.layer = p.layer;
            r.attrs = p.attrs;
            if (in_blocks && !current_block.empty()) {
                nested_inserts.push_back({current_block, std::move(r)});
                return;
            }
            out.document.blocks.insert(r);

        } else if (!p.kind.empty()) {
            ++out.unknown_entities;
            bool seen = false;
            for (auto& [k, n] : out.unknown_kinds) if (k == p.kind) { ++n; seen = true; }
            if (!seen) out.unknown_kinds.push_back({p.kind, 1});
        }
    };

    for (std::size_t i = 0; i < tape.size(); ++i) {
        const Group& g = tape.groups[i];

        if (g.code == 0) {
            finish();
            const std::string what = upper(g.value);

            if (what == "SECTION") {
                if (i + 1 < tape.size() && tape.groups[i + 1].code == 2) {
                    section = upper(tape.groups[i + 1].value);
                    in_blocks = section == "BLOCKS";
                }
                continue;
            }
            if (what == "ENDSEC") {
                section.clear();
                in_blocks = false;
                continue;
            }
            if (what == "TABLE") {
                if (i + 1 < tape.size() && tape.groups[i + 1].code == 2)
                    table = upper(tape.groups[i + 1].value);
                continue;
            }
            if (what == "ENDTAB") { table.clear(); continue; }
            if (what == "EOF") break;

            if (section == "TABLES" && table == "LTYPE" && what == "LTYPE") {
                gdoc::Linetype lt;
                for (std::size_t k = i + 1; k < tape.size(); ++k) {
                    if (tape.groups[k].code == 0) break;
                    f64 rv = 0;
                    switch (tape.groups[k].code) {
                        case 2: lt.name = tape.groups[k].value; break;
                        case 3: lt.description = tape.groups[k].value; break;
                        // 49: positive draws, negative is a gap, zero is a dot.
                        case 49:
                            if (tape.groups[k].as_real(rv)) lt.pattern.push_back({rv, "", 1.0});
                            break;
                        default: break;
                    }
                }
                if (!lt.name.empty()) out.document.linetypes.add(std::move(lt));
                continue;
            }
            if (section == "TABLES" && table == "STYLE" && what == "STYLE") {
                gtext::TextStyleDef st;
                for (std::size_t k = i + 1; k < tape.size(); ++k) {
                    if (tape.groups[k].code == 0) break;
                    f64 rv = 0;
                    i32 iv = 0;
                    switch (tape.groups[k].code) {
                        case 2: st.name = tape.groups[k].value; break;
                        case 40: if (tape.groups[k].as_real(rv)) st.height = static_cast<float>(rv * to_metres); break;
                        case 41: if (tape.groups[k].as_real(rv) && rv > 0) st.width_factor = static_cast<float>(rv); break;
                        case 50: if (tape.groups[k].as_real(rv)) st.oblique = static_cast<float>(rv); break;
                        case 71:
                            if (tape.groups[k].as_int(iv)) {
                                st.backwards = (iv & 2) != 0;
                                st.upside_down = (iv & 4) != 0;
                            }
                            break;
                        case 3: st.font = tape.groups[k].value; break;
                        case 4: st.big_font = tape.groups[k].value; break;
                        default: break;
                    }
                }
                if (!st.name.empty()) out.document.text_styles.push_back(std::move(st));
                continue;
            }
            if (section == "TABLES" && table == "DIMSTYLE" && what == "DIMSTYLE") {
                gdim::Style ds;
                ds.name = "Standard";
                bool any = false;
                for (std::size_t k = i + 1; k < tape.size(); ++k) {
                    if (tape.groups[k].code == 0) break;
                    f64 rv = 0;
                    i32 iv = 0;
                    switch (tape.groups[k].code) {
                        case 2: ds.name=tape.groups[k].value;break;
                        case 40: if(tape.groups[k].as_real(rv)&&rv>0)ds.dimscale=rv;break;
                        case 41:  if (tape.groups[k].as_real(rv) && rv > 0) { ds.dimasz = rv; any = true; } break;
                        case 42:  if (tape.groups[k].as_real(rv) && rv > 0) { ds.dimexo = rv; any = true; } break;
                        case 44:  if (tape.groups[k].as_real(rv) && rv > 0) { ds.dimexe = rv; any = true; } break;
                        case 140: if (tape.groups[k].as_real(rv) && rv > 0) { ds.dimtxt = rv; any = true; } break;
                        case 147: if (tape.groups[k].as_real(rv) && rv > 0) { ds.dimgap = rv; any = true; } break;
                        case 271: if (tape.groups[k].as_int(iv) && iv >= 0) ds.dimdec = iv; break;
                        default: break;
                    }
                }
                const double size_to_mm=to_metres*1000;
                ds.dimtxt*=size_to_mm;ds.dimasz*=size_to_mm;ds.dimgap*=size_to_mm;ds.dimexo*=size_to_mm;ds.dimexe*=size_to_mm;
                out.document.dim_styles.push_back(ds);
                if (any && !out.document.has_dim_style) {
                    out.document.dim_style = ds;
                    out.document.has_dim_style = true;
                }
                continue;
            }
            if (section == "TABLES" && table == "LAYER" && what == "LAYER") {
                gdoc::Layer l;
                for (std::size_t k = i + 1; k < tape.size(); ++k) {
                    if (tape.groups[k].code == 0) break;
                    i32 iv = 0;
                    switch (tape.groups[k].code) {
                        case 2: l.name = tape.groups[k].value; break;
                        case 70:
                            if (tape.groups[k].as_int(iv)) {
                                l.frozen = (iv & 1) != 0;
                                l.locked = (iv & 4) != 0;
                            }
                            break;
                        case 62:
                            if (tape.groups[k].as_int(iv)) {
                                // A negative colour means the layer is off.
                                l.on = iv >= 0;
                                l.color = gdoc::Color::aci(std::abs(iv));
                            }
                            break;
                        case 6: l.linetype = tape.groups[k].value; break;
                        // Bit 4 is locked.
                        case 290:
                            if (tape.groups[k].as_int(iv)) l.plottable = iv != 0;
                            break;
                        case 370:
                            if (tape.groups[k].as_int(iv))
                                l.lineweight = static_cast<gdoc::Lineweight>(iv);
                            break;
                        case 420:
                            if (tape.groups[k].as_int(iv))
                                l.color = gdoc::Color::rgb(gcm::u8((iv >> 16) & 0xFF),
                                                           gcm::u8((iv >> 8) & 0xFF),
                                                           gcm::u8(iv & 0xFF));
                            break;
                        default: break;
                    }
                }
                if (!l.name.empty()) out.document.layers.add(l);
                continue;
            }

            if (in_blocks && what == "BLOCK") {
                gdoc::BlockDef def;double base_x=0,base_y=0;
                for (std::size_t k = i + 1; k < tape.size(); ++k) {
                    if (tape.groups[k].code == 0) break;
                    f64 rv = 0;
                    switch (tape.groups[k].code) {
                        case 2: def.name = tape.groups[k].value; break;
                        case 1:
                            def.xref_path = tape.groups[k].value;
                            def.is_xref = !def.xref_path.empty();
                            break;
                        case 10: if (tape.groups[k].as_real(rv)) base_x=rv; break;
                        case 20: if (tape.groups[k].as_real(rv)) base_y=rv; break;
                        default: break;
                    }
                }
                def.base=gcm::to_lattice(v,base_x*to_metres,base_y*to_metres);
                if (!def.name.empty()) {
                    current_block = def.name;
                    out.document.blocks.define(def);
                }
                continue;
            }
            if (in_blocks && what == "ENDBLK") {
                current_block.clear();
                continue;
            }

            if (section == "ENTITIES" || in_blocks) {
                pending = Pending{};
                pending.kind = what;
                have_pending = true;
            }
            continue;
        }

        if (!have_pending) continue;

        f64 rv = 0;
        i32 iv = 0;
        // Hatch boundaries and spline data reuse low group codes; take them first.
        if (pending.kind == "HATCH" && g.code != 62 && g.code != 420 && g.code != 440 && g.code != 60 && g.code != 8 && g.code != 2 && g.code != 210 && g.code != 220 && g.code != 230) {
            auto& H = pending;
            const bool polyline = (H.hloop_type & 2) != 0;
            auto& loops = H.hloops;
            switch (g.code) {
                case 92:
                    if (g.as_int(iv)) { H.hloop_type = iv; loops.emplace_back(); }
                    break;
                case 72:
                    if (g.as_int(iv)) {
                        if (!polyline && !loops.empty()) { Pending::HEdge e; e.type = iv; loops.back().push_back(e); }
                    }
                    break;
                case 10:
                    if (g.as_real(rv) && !H.hafter_seed && !loops.empty()) {
                        if (polyline) { Pending::HEdge e; e.type = 0; e.x0 = rv; loops.back().push_back(e); }
                        else if (!loops.back().empty()) {
                            auto& e = loops.back().back();
                            if (e.type == 2 || e.type == 3) e.cx = rv; else e.x0 = rv;
                        }
                    }
                    break;
                case 20:
                    if (g.as_real(rv) && !H.hafter_seed && !loops.empty() && !loops.back().empty()) {
                        auto& e = loops.back().back();
                        if (polyline) e.y0 = rv;
                        else if (e.type == 2 || e.type == 3) e.cy = rv; else e.y0 = rv;
                    }
                    break;
                case 11: if (g.as_real(rv) && !loops.empty() && !loops.back().empty()) loops.back().back().x1 = rv; break;
                case 21: if (g.as_real(rv) && !loops.empty() && !loops.back().empty()) loops.back().back().y1 = rv; break;
                case 40: if (g.as_real(rv) && !loops.empty() && !loops.back().empty()) loops.back().back().r = rv; break;
                case 42: if (g.as_real(rv) && !loops.empty() && !loops.back().empty()) loops.back().back().bulge = rv; break;
                case 50: if (g.as_real(rv) && !loops.empty() && !loops.back().empty()) loops.back().back().a0 = rv; break;
                case 51: if (g.as_real(rv) && !loops.empty() && !loops.back().empty()) loops.back().back().a1 = rv; break;
                case 73:
                    if (g.as_int(iv)) {
                        if (polyline) H.closed = iv != 0;
                        else if (!loops.empty() && !loops.back().empty()) loops.back().back().ccw = iv != 0;
                    }
                    break;
                case 70: if (g.as_int(iv)) H.hsolid = (iv & 1) != 0; break;
                case 41: if (g.as_real(rv)) H.hscale = rv; break;
                case 52: if (g.as_real(rv)) H.hangle = rv; break;
                case 98: H.hafter_seed = true; break;
                default: break;
            }
            continue;
        }
        if ((pending.kind == "MULTILEADER" || pending.kind == "MLEADER") && g.code != 8) {
            // CONTEXT_DATA{ note, LEADER{ connection and dogleg, LEADER_LINE{ points
            auto& m = pending;
            switch (g.code) {
                case 300: if (g.value == "CONTEXT_DATA{") m.ml_state = 1; break;
                case 301: if (g.value == "}") m.ml_state = 0; break;
                case 302: if (g.value == "LEADER{") { m.ml_state = 2; m.ml_has_conn = false; } break;
                case 303: if (g.value == "}") m.ml_state = 1; break;
                case 304:
                    if (g.value == "LEADER_LINE{") { m.ml_state = 3; m.ml_lines.emplace_back(); }
                    else if (m.ml_state == 1) m.ml_text = g.value;
                    break;
                case 305:
                    if (g.value == "}" && m.ml_state == 3) {
                        if (m.ml_has_conn) {
                            m.ml_lines.back().push_back({m.ml_cx, m.ml_cy});
                            if (m.ml_dogleg != 0.0)
                                m.ml_lines.back().push_back({m.ml_cx + m.ml_dx * m.ml_dogleg, m.ml_cy + m.ml_dy * m.ml_dogleg});
                        }
                        m.ml_state = 2;
                    }
                    break;
                case 10:
                    if (g.as_real(rv)) {
                        if (m.ml_state == 3) m.ml_lines.back().push_back({rv, 0.0});
                        else if (m.ml_state == 2) { m.ml_cx = rv; m.ml_has_conn = true; }
                    }
                    break;
                case 20:
                    if (g.as_real(rv)) {
                        if (m.ml_state == 3 && !m.ml_lines.back().empty()) m.ml_lines.back().back().second = rv;
                        else if (m.ml_state == 2) m.ml_cy = rv;
                    }
                    break;
                case 11: if (m.ml_state == 2 && g.as_real(rv)) m.ml_dx = rv; break;
                case 21: if (m.ml_state == 2 && g.as_real(rv)) m.ml_dy = rv; break;
                case 40: if (m.ml_state == 2 && g.as_real(rv)) m.ml_dogleg = rv; break;
                case 41: if (m.ml_state == 1 && m.ml_height == 0.0 && g.as_real(rv)) m.ml_height = rv; break;
                case 12: if (m.ml_state == 1 && g.as_real(rv)) { m.ml_tx = rv; m.ml_has_text_pos = true; } break;
                case 22: if (m.ml_state == 1 && g.as_real(rv)) m.ml_ty = rv; break;
                default: break;
            }
            continue;
        }
        if(pending.kind=="ELLIPSE"&&(g.code==41||g.code==42)) {
            if(g.as_real(rv)){if(g.code==41)pending.ellipse_start=rv;else pending.ellipse_end=rv;}continue;
        }
        if (pending.kind == "SPLINE" && g.code != 8 && g.code != 210 && g.code != 220 && g.code != 230) {
            switch (g.code) {
                case 70: if (g.as_int(iv)) pending.spline_flags = iv; break;
                case 71: if (g.as_int(iv)) pending.degree = iv; break;
                case 40: if (g.as_real(rv)) pending.knots.push_back(rv); break;
                case 41: if (g.as_real(rv)) pending.weights.push_back(rv); break;
                case 10: if (g.as_real(rv)) pending.vertices.push_back({rv, 0.0, 0.0}); break;
                case 20: if (g.as_real(rv) && !pending.vertices.empty()) pending.vertices.back().y = rv; break;
                case 11: if (g.as_real(rv)) pending.fit.push_back({rv, 0.0}); break;
                case 21: if (g.as_real(rv) && !pending.fit.empty()) pending.fit.back().second = rv; break;
                default: break;
            }
            continue;
        }
        if (g.code >= 10 && g.code <= 16 && g.as_real(rv)) { pending.dx[g.code - 10] = rv; pending.has_pt[g.code - 10] = true; }
        if (g.code >= 20 && g.code <= 26 && g.as_real(rv)) pending.dy[g.code - 20] = rv;
        switch (g.code) {
            case 8:  pending.layer = g.value; break;
            case 62:
                // 0 is ByBlock and 256 is ByLayer; anything else is an index.
                if (g.as_int(iv)) {
                    if (iv == 0) pending.attrs.color = gdoc::Color::by_block();
                    else if (iv == 256) pending.attrs.color = gdoc::Color::by_layer();
                    else pending.attrs.color = gdoc::Color::aci(std::abs(iv));
                }
                break;
            case 420:
                if (g.as_int(iv))
                    pending.attrs.color = gdoc::Color::rgb(gcm::u8((iv >> 16) & 0xFF),
                                                           gcm::u8((iv >> 8) & 0xFF),
                                                           gcm::u8(iv & 0xFF));
                break;
            case 6:  pending.attrs.linetype = g.value; break;
            case 60: if(g.as_int(iv))pending.attrs.visible=iv==0;break;
            case 370:
                if (g.as_int(iv)) pending.attrs.lineweight = static_cast<gdoc::Lineweight>(iv);
                break;
            case 440:
                // Alpha in the low byte, inverse of the drafter's percentage.
                if (g.as_int(iv)) {
                    const auto mode=(gcm::u32(iv)>>24)&255;
                    if(mode==0)pending.attrs.transparency=-1;
                    else if(mode==1)pending.attrs.transparency=-2;
                    else if(mode==2)pending.attrs.transparency=int(std::lround((255-(iv&255))*100.0/255));
                }
                break;
            case 48:
                if (g.as_real(rv) && rv > 0.0) pending.attrs.linetype_scale = rv;
                break;
            case 5: pending.handle=g.value;break;
            case 340: pending.annotation_handle=g.value;break;
            case 2:  pending.name = g.value; break;
            case 7: pending.style=g.value;break;
            case 1: if(pending.kind=="MTEXT") pending.text+=g.value;else pending.text=g.value;break;
            // Code 3 is MTEXT continuation, except on DIMENSION where it is the style name.
            case 3:
                if (pending.kind == "DIMENSION" || pending.kind == "LEADER") pending.style = g.value;
                else if(pending.kind=="ATTDEF")pending.attribute_prompt=g.value;
                else pending.text += g.value;
                break;
            case 10:
                if (g.as_real(rv)) {
                    if (pending.kind == "LWPOLYLINE" || pending.kind == "LEADER") {
                        pending.vertices.push_back({rv, 0.0, 0.0});
                    } else {
                        pending.x = rv;
                    }
                }
                break;
            case 20:
                if (g.as_real(rv)) {
                    if ((pending.kind == "LWPOLYLINE" || pending.kind == "LEADER") && !pending.vertices.empty())
                        pending.vertices.back().y = rv;
                    else
                        pending.y = rv;
                }
                break;
            case 30: if (g.as_real(rv)) pending.z = rv; break;
            case 11: if (g.as_real(rv)) { pending.x1 = rv; pending.has_second = true; } break;
            case 21: if (g.as_real(rv)) pending.y1 = rv; break;
            case 31: if (g.as_real(rv)) pending.z1 = rv; break;
            case 40:
                if (g.as_real(rv)) {
                    if (pending.kind == "ELLIPSE") pending.ratio = rv;
                    else { pending.radius = rv; pending.has_radius = true; }
                }
                break;
            case 41: if (g.as_real(rv)) {pending.scale_x=rv;if(pending.kind=="MTEXT") pending.column_width=rv;} break;
            case 71: if(g.as_int(iv)) pending.attachment=iv;break;
            case 42:
                if (g.as_real(rv)) {
                    // Code 42 is SCALE on INSERT and BULGE on a polyline vertex.
                    if (pending.kind == "LWPOLYLINE" && !pending.vertices.empty())
                        pending.vertices.back().bulge = rv;
                    else if (pending.kind == "VERTEX")
                        pending.bulge = rv;
                    else
                        pending.scale_y = rv;
                }
                break;
            case 50:
                if (g.as_real(rv)) {
                    if (pending.kind == "INSERT") pending.rotation_deg = rv;
                    else { pending.start_deg = rv; pending.has_angles = true; }
                }
                break;
            case 51: if (g.as_real(rv)) pending.end_deg = rv; break;
            case 72: if (g.as_int(iv)) pending.halign = iv; break;
            case 73: if (pending.kind!="ATTDEF"&&g.as_int(iv)) pending.valign = iv; break;
            case 74: if (pending.kind=="ATTDEF"&&g.as_int(iv)) pending.valign = iv; break;
            case 70:
                if (g.as_int(iv)) {
                    pending.flags = iv;
                    if (pending.kind == "LWPOLYLINE" || pending.kind == "POLYLINE")
                        pending.closed = (iv & 1) != 0;
                }
                break;
            case 210: if (g.as_real(rv)) pending.ex = rv; break;
            case 220: if (g.as_real(rv)) pending.ey = rv; break;
            case 230: if (g.as_real(rv)) pending.ez = rv; break;
            default: break;
        }
    }
    finish();
    const auto boundary_key=[](const gsnap::Shape& shape) {
        auto a=shape.a,b=shape.b;
        const bool segment=shape.kind==gsnap::ShapeKind::Segment;
        if (segment && std::tie(b.x,b.y)<std::tie(a.x,a.y)) std::swap(a,b);
        if (!segment) b={};
        return std::tuple{unsigned(shape.kind),a.x,a.y,b.x,b.y,
            segment?gcm::i64(0):shape.radius,
            shape.kind==gsnap::ShapeKind::Arc?shape.start_angle:0.0,
            shape.kind==gsnap::ShapeKind::Arc?shape.end_angle:0.0};
    };
    std::set<decltype(boundary_key(gsnap::Shape{}))> outlines;
    for (const auto& e:out.document.entities)
        if (e.kind!="HATCH") outlines.insert(boundary_key(e.shape));
    std::erase_if(out.document.entities,[&](const Entity& e) {
        return e.kind=="HATCH" && !outlines.insert(boundary_key(e.shape)).second;
    });
    std::vector<std::size_t> consumed;
    for(const auto& [i,h]:leader_annotations) if(auto it=text_handles.find(h);it!=text_handles.end()) {
        auto& leader=out.document.dimensions[i];const auto& note=out.document.texts[it->second];
        leader.text_override=note.text;leader.p3=note.pos;leader.leader_text_absolute=true;
        leader.leader_text_height=note.height;leader.leader_text_angle=note.angle;
        leader.leader_text_halign=note.halign;leader.leader_text_valign=note.valign;consumed.push_back(it->second);
    }
    std::sort(consumed.begin(),consumed.end());consumed.erase(std::unique(consumed.begin(),consumed.end()),consumed.end());
    for(auto it=consumed.rbegin();it!=consumed.rend();++it)out.document.texts.erase(out.document.texts.begin()+*it);

    if (!out.document.layers.find("0")) {
        gdoc::Layer zero;
        zero.name = "0";
        out.document.layers.add(zero);
    }

    out.ok = true;
    out.junk_lines = tape.junk_lines;
    if (out.document.entities.empty() && out.document.blocks.size() == 0)
        out.error = "the file contained no geometry this reader could use";
    for(auto& [parent,ref]:nested_inserts) {
        if(auto* def=const_cast<gdoc::BlockDef*>(out.document.blocks.find(parent)))def->nested_refs.push_back(std::move(ref));
    }
    return out;
}

}
