#include "geometry/annotation.hpp"

#include <algorithm>
#include <cmath>

namespace gdim {

namespace {

constexpr f64 kPi = 3.14159265358979323846;

struct W { f64 x, y; };
W world(const Volume& v, Point p) {
    f64 x, y;
    gcm::to_world(v, p, x, y);
    return {x, y};
}
Point lat(const Volume& v, f64 x, f64 y) { return gcm::to_lattice(v, x, y); }

gsnap::Shape seg(const Volume& v, W a, W b) {
    gsnap::Shape s;
    s.kind = gsnap::ShapeKind::Segment;
    s.a = lat(v, a.x, a.y);
    s.b = lat(v, b.x, b.y);
    return s;
}

void box(std::vector<gsnap::Shape>& out, const Volume& v, f64 x0, f64 y0,
         f64 x1, f64 y1) {
    out.push_back(seg(v, {x0, y0}, {x1, y0}));
    out.push_back(seg(v, {x1, y0}, {x1, y1}));
    out.push_back(seg(v, {x1, y1}, {x0, y1}));
    out.push_back(seg(v, {x0, y1}, {x0, y0}));
}

void arrow(std::vector<gsnap::Shape>& out, const Volume& v, W tip, W dir,
           f64 size, ArrowType type) {
    const f64 len = std::hypot(dir.x, dir.y);
    if (len < 1e-12 || type == ArrowType::None) return;
    const W u{dir.x / len, dir.y / len};
    const W n{-u.y, u.x};

    if (type == ArrowType::Tick) {
        const f64 h = size * 0.5;
        out.push_back(seg(v, {tip.x - (u.x + n.x) * h, tip.y - (u.y + n.y) * h},
                             {tip.x + (u.x + n.x) * h, tip.y + (u.y + n.y) * h}));
        return;
    }
    if (type == ArrowType::Dot) {
        gsnap::Shape c;
        c.kind = gsnap::ShapeKind::Circle;
        c.a = lat(v, tip.x, tip.y);
        c.radius = i64(std::llround(size * 0.25 * v.scale()));
        out.push_back(c);
        return;
    }
    const f64 w = size * 0.28;
    const W back{tip.x + u.x * size, tip.y + u.y * size};
    const W l{back.x + n.x * w, back.y + n.y * w};
    const W r{back.x - n.x * w, back.y - n.y * w};
    out.push_back(seg(v, tip, l));
    out.push_back(seg(v, tip, r));
    if (type == ArrowType::ClosedFilled) out.push_back(seg(v, l, r));
}

f64 text_width(const std::string& s, f64 height) {
    // Count codepoints, not bytes: GD&T glyphs are multi-byte.
    std::size_t n = 0;
    for (unsigned char c : s)
        if ((c & 0xC0) != 0x80) ++n;
    return static_cast<f64>(n) * height * 0.62;
}

}

const std::vector<AnnotationScale>& standard_scales() {
    static const std::vector<AnnotationScale> s = {
        {"1:1", 1.0, 1.0},       {"1:2", 1.0, 2.0},
        {"1:5", 1.0, 5.0},       {"1:10", 1.0, 10.0},
        {"1:20", 1.0, 20.0},     {"1:25", 1.0, 25.0},
        {"1:50", 1.0, 50.0},     {"1:100", 1.0, 100.0},
        {"1:200", 1.0, 200.0},   {"1:500", 1.0, 500.0},
        {"1:1000", 1.0, 1000.0},
        {"2:1", 2.0, 1.0},       {"5:1", 5.0, 1.0},  {"10:1", 10.0, 1.0},
        {"1\" = 1'-0\"", 1.0, 12.0},
        {"3/4\" = 1'-0\"", 0.75, 12.0},
        {"1/2\" = 1'-0\"", 0.5, 12.0},
        {"1/4\" = 1'-0\"", 0.25, 12.0},
        {"1/8\" = 1'-0\"", 0.125, 12.0},
        {"1/16\" = 1'-0\"", 0.0625, 12.0},
    };
    return s;
}

const AnnotationScale* find_scale(std::string_view name) {
    for (const auto& s : standard_scales())
        if (s.name == name) return &s;
    return nullptr;
}

ScaleList::ScaleList() : scales_(standard_scales()) {}

const AnnotationScale* ScaleList::find(std::string_view n) const {
    for (const auto& s : scales_)
        if (s.name == n) return &s;
    return nullptr;
}

AnnotationScale& ScaleList::add(AnnotationScale s) {
    for (auto& e : scales_)
        if (e.name == s.name) { e = std::move(s); return e; }
    scales_.push_back(std::move(s));
    return scales_.back();
}

bool ScaleList::remove(std::string_view n) {
    if (n == "1:1") return false;
    const auto it = std::find_if(scales_.begin(), scales_.end(),
                                 [&](const AnnotationScale& s) { return s.name == n; });
    if (it == scales_.end()) return false;
    if (current_ == n) current_ = "1:1";
    scales_.erase(it);
    return true;
}

void ScaleList::set_current(std::string_view n) {
    if (find(n)) current_ = std::string(n);
}

f64 ScaleList::current_factor() const {
    const AnnotationScale* s = find(current_);
    return s ? s->factor() : 1.0;
}

bool Annotative::appears_at(std::string_view scale) const noexcept {
    for (const auto& s : scales)
        if (s == scale) return true;
    return false;
}

void Annotative::add_scale(std::string_view scale) {
    if (!appears_at(scale)) scales.emplace_back(scale);
    enabled = true;
}

bool Annotative::remove_scale(std::string_view scale) {
    const auto it = std::find(scales.begin(), scales.end(), std::string(scale));
    if (it == scales.end()) return false;
    scales.erase(it);
    return true;
}

f64 annotative_size(f64 paper_height, f64 scale_factor) noexcept {
    return paper_height * (scale_factor > 0.0 ? scale_factor : 1.0);
}

namespace {

// DIMDLI default: AutoCAD's 3.75 mm, scaled by the style.
f64 chain_spacing(const Chain& c, const Style& s) {
    const f64 raw = c.spacing > 0.0 ? c.spacing : 3.75;
    return s.scaled(raw) * 0.001;      // style sizes are millimetres
}

Dimension make(const Chain& c, const Volume& v, Point a, Point b, W line) {
    const W wa = world(v, a), wb = world(v, b);
    const W at = c.horizontal ? W{(wa.x + wb.x) * 0.5, line.y}
                              : W{line.x, (wa.y + wb.y) * 0.5};
    Dimension d;
    d.kind  = c.kind;
    d.style = c.style;
    d.p1 = a;
    d.p2 = b;
    d.p3 = lat(v, at.x, at.y);
    return d;
}

}

ChainResult baseline(const Chain& c, const Style& s, const Volume& v) {
    ChainResult r;
    if (c.points.empty()) {
        r.reason = "a baseline chain needs at least one further point";
        return r;
    }

    const f64 step = chain_spacing(c, s);
    const W loc = world(v, c.location);
    const W org = world(v, c.origin);

    for (std::size_t i = 0; i < c.points.size(); ++i) {
        const f64 offset = step * static_cast<f64>(i);
        W line = loc;
        if (c.horizontal) line.y += (loc.y >= org.y ? offset : -offset);
        else              line.x += (loc.x >= org.x ? offset : -offset);
        r.dimensions.push_back(make(c, v, c.origin, c.points[i], line));
    }

    r.ok = true;
    return r;
}

ChainResult continued(const Chain& c, const Style& s, const Volume& v) {
    (void)s;
    ChainResult r;
    if (c.points.empty()) {
        r.reason = "a continued chain needs at least one further point";
        return r;
    }

    const W line = world(v, c.location);
    Point from = c.origin;
    for (const Point& p : c.points) {
        r.dimensions.push_back(make(c, v, from, p, line));
        from = p;
    }

    r.ok = true;
    return r;
}

MLeaderStyleTable::MLeaderStyleTable() { styles_.push_back(MLeaderStyle{}); }

const MLeaderStyle* MLeaderStyleTable::find(std::string_view n) const {
    for (const auto& s : styles_)
        if (s.name == n) return &s;
    return nullptr;
}

MLeaderStyle& MLeaderStyleTable::add(MLeaderStyle s) {
    for (auto& e : styles_)
        if (e.name == s.name) { e = std::move(s); return e; }
    styles_.push_back(std::move(s));
    return styles_.back();
}

void MLeaderStyleTable::set_current(std::string_view n) {
    if (find(n)) current_ = std::string(n);
}

MLeaderGeometry build(const MLeader& m, const MLeaderStyle& s, const Volume& v) {
    MLeaderGeometry g;
    if (m.lines.empty()) {
        g.reason = "a multileader needs at least one leader line";
        return g;
    }

    const f64 asz  = s.scaled(s.arrow_size) * 0.001;
    const f64 gap  = s.scaled(s.landing_gap) * 0.001;
    const f64 shelf = s.scaled(s.landing_length) * 0.001;
    g.text_height = s.scaled(s.text_height) * 0.001;
    g.text = m.text;

    const W land = world(v, m.landing);

    for (const auto& line : m.lines) {
        if (line.size() < 2) continue;

        std::vector<W> pts;
        pts.reserve(line.size() + 1);
        for (const Point& p : line) pts.push_back(world(v, p));
        pts.back() = land;

        if (s.leader != LeaderType::None)
            for (std::size_t i = 0; i + 1 < pts.size(); ++i)
                g.lines.push_back(seg(v, pts[i], pts[i + 1]));

        const W tip = pts[0];
        const W dir{pts[1].x - tip.x, pts[1].y - tip.y};
        arrow(g.arrows, v, tip, dir, asz, s.arrow);
    }

    W text_at = land;
    if (s.landing) {
        const f64 dx = m.text_left ? -shelf : shelf;
        g.lines.push_back(seg(v, land, {land.x + dx, land.y}));
        text_at = {land.x + dx, land.y};
    }

    const f64 w = text_width(m.text, g.text_height);
    text_at.x += m.text_left ? -(gap + w) : gap;
    text_at.y += g.text_height * 0.25;
    g.text_at = lat(v, text_at.x, text_at.y);

    g.ok = true;
    return g;
}

const char* symbol_name(GeoSymbol s) noexcept {
    switch (s) {
        case GeoSymbol::Straightness:     return "Straightness";
        case GeoSymbol::Flatness:         return "Flatness";
        case GeoSymbol::Circularity:      return "Circularity";
        case GeoSymbol::Cylindricity:     return "Cylindricity";
        case GeoSymbol::ProfileLine:      return "Profile of a Line";
        case GeoSymbol::ProfileSurface:   return "Profile of a Surface";
        case GeoSymbol::Angularity:       return "Angularity";
        case GeoSymbol::Perpendicularity: return "Perpendicularity";
        case GeoSymbol::Parallelism:      return "Parallelism";
        case GeoSymbol::Position:         return "Position";
        case GeoSymbol::Concentricity:    return "Concentricity";
        case GeoSymbol::Symmetry:         return "Symmetry";
        case GeoSymbol::CircularRunout:   return "Circular Runout";
        case GeoSymbol::TotalRunout:      return "Total Runout";
    }
    return "";
}

const char* symbol_glyph(GeoSymbol s) noexcept {
    switch (s) {
        case GeoSymbol::Straightness:     return "\xE2\x8E\xAF";  // ⎯
        case GeoSymbol::Flatness:         return "\xE2\x8C\x93";  // ⌓
        case GeoSymbol::Circularity:      return "\xE2\x97\x8B";  // ○
        case GeoSymbol::Cylindricity:     return "\xE2\x8C\xAD";  // ⌭
        case GeoSymbol::ProfileLine:      return "\xE2\x8C\x92";  // ⌒
        case GeoSymbol::ProfileSurface:   return "\xE2\x8C\x93";  // ⌓
        case GeoSymbol::Angularity:       return "\xE2\x88\xA0";  // ∠
        case GeoSymbol::Perpendicularity: return "\xE2\x8A\xA5";  // ⊥
        case GeoSymbol::Parallelism:      return "\xE2\x88\xA5";  // ∥
        case GeoSymbol::Position:         return "\xE2\x8C\x96";  // ⌖
        case GeoSymbol::Concentricity:    return "\xE2\x97\x8E";  // ◎
        case GeoSymbol::Symmetry:         return "\xE2\x8C\x96";  // ⌖ (stacked bars)
        case GeoSymbol::CircularRunout:   return "\xE2\x86\x97";  // ↗
        case GeoSymbol::TotalRunout:      return "\xE2\x86\x97";  // ↗↗
    }
    return "";
}

const char* material_glyph(Material m) noexcept {
    switch (m) {
        case Material::MMC: return "\xE2\x93\x82";   // Ⓜ
        case Material::LMC: return "\xE2\x93\x81";   // Ⓛ
        case Material::RFS: return "";
    }
    return "";
}

namespace {

std::string datum_text(const DatumRef& d) {
    if (d.empty()) return {};
    return d.letter + material_glyph(d.material);
}

}

FrameGeometry build(const FeatureControlFrame& f, const Volume& v) {
    FrameGeometry g;
    if (f.segments.empty()) {
        g.reason = "a feature control frame needs at least one segment";
        return g;
    }
    if (f.height <= 0.0) {
        g.reason = "the frame height must be positive";
        return g;
    }

    const W at = world(v, f.at);
    const f64 h = f.height;
    const f64 pad = h * 0.35;
    const f64 th = h * 0.62;

    std::vector<std::vector<std::string>> rows;
    for (const auto& s : f.segments) {
        std::string tol;
        if (s.diameter) tol += "\xE2\x8C\x80";     // ⌀
        tol += s.tolerance;
        tol += material_glyph(s.material);
        rows.push_back({symbol_glyph(s.symbol), tol, datum_text(s.primary),
                        datum_text(s.secondary), datum_text(s.tertiary)});
    }

    constexpr std::size_t kCols = 5;
    std::vector<f64> width(kCols, 0.0);
    std::vector<bool> used(kCols, false);
    for (const auto& r : rows)
        for (std::size_t c = 0; c < kCols; ++c) {
            if (r[c].empty()) continue;
            used[c] = true;
            width[c] = std::max(width[c], text_width(r[c], th) + pad * 2.0);
        }
    if (used[0]) width[0] = std::max(width[0], h);

    g.height = h * static_cast<f64>(rows.size());
    for (std::size_t c = 0; c < kCols; ++c)
        if (used[c]) g.width += width[c];

    for (std::size_t i = 0; i < rows.size(); ++i) {
        const f64 y0 = at.y + g.height - h * static_cast<f64>(i + 1);
        const f64 y1 = y0 + h;
        f64 x = at.x;
        for (std::size_t c = 0; c < kCols; ++c) {
            if (!used[c]) continue;
            box(g.lines, v, x, y0, x + width[c], y1);
            if (!rows[i][c].empty()) {
                FrameCell cell;
                cell.text = rows[i][c];
                cell.width = width[c];
                cell.height = h;
                cell.text_height = th;
                cell.x = x + (width[c] - text_width(rows[i][c], th)) * 0.5;
                cell.y = y0 + (h - th) * 0.5;
                g.cells.push_back(cell);
            }
            x += width[c];
        }
    }

    g.ok = true;
    return g;
}

FrameGeometry build(const DatumFeature& d, const Volume& v) {
    FrameGeometry g;
    if (d.letter.empty()) {
        g.reason = "a datum feature needs a letter";
        return g;
    }
    if (d.height <= 0.0) {
        g.reason = "the datum height must be positive";
        return g;
    }

    const W at = world(v, d.at);
    const f64 h = d.height;
    const f64 th = h * 0.62;
    const f64 w = std::max(h, text_width(d.letter, th) + h * 0.7);
    const f64 dir = d.below ? -1.0 : 1.0;
    const f64 lead = d.leader_length > 0.0 ? d.leader_length : h * 2.0;

    const f64 tw = h * 0.5;
    const W tip{at.x, at.y};
    const W b1{at.x - tw * 0.5, at.y + dir * tw};
    const W b2{at.x + tw * 0.5, at.y + dir * tw};
    g.lines.push_back(seg(v, tip, b1));
    g.lines.push_back(seg(v, tip, b2));
    g.lines.push_back(seg(v, b1, b2));

    const f64 y0 = at.y + dir * lead + (d.below ? -h : 0.0);
    g.lines.push_back(seg(v, {at.x, at.y + dir * tw},
                             {at.x, d.below ? y0 + h : y0}));
    box(g.lines, v, at.x - w * 0.5, y0, at.x + w * 0.5, y0 + h);

    FrameCell cell;
    cell.text = d.letter;
    cell.width = w;
    cell.height = h;
    cell.text_height = th;
    cell.x = at.x - text_width(d.letter, th) * 0.5;
    cell.y = y0 + (h - th) * 0.5;
    g.cells.push_back(cell);

    g.width = w;
    g.height = h;
    g.ok = true;
    return g;
}


ChainResult ordinate_run(const OrdinateRun& r, const Style& s, const Volume& v) {
    ChainResult out;
    if (r.features.empty()) {
        out.reason = "an ordinate run needs at least one feature";
        return out;
    }

    const f64 leader = r.leader_length > 0.0 ? r.leader_length
                                             : s.scaled(10.0) * 0.001;

    for (const Point& f : r.features) {
        Dimension d;
        d.kind = Kind::Ordinate;
        d.style = r.style;
        d.p1 = r.datum;
        d.p2 = f;
        d.p3 = f;

        const W wf = world(v, f);
        const W wd = world(v, r.datum);
        W at = wf;
        if (r.along_x) {
            at.y = (r.base_offset != 0.0 ? r.base_offset : wd.y) +
                   (wf.y >= wd.y ? leader : -leader);
        } else {
            at.x = (r.base_offset != 0.0 ? r.base_offset : wd.x) +
                   (wf.x >= wd.x ? leader : -leader);
        }
        d.p3 = lat(v, at.x, at.y);
        out.dimensions.push_back(d);
    }

    out.ok = true;
    return out;
}

Geometry build(const JoggedRadius& j, const Style& s, const Volume& v) {
    Geometry g;
    if (j.radius <= 0) {
        g.reason = "a jogged radius needs a positive radius";
        return g;
    }

    const W origin = world(v, j.origin);
    const W jog = world(v, j.jog);
    const W drawn = world(v, j.centre_override);
    const f64 asz = s.scaled(s.dimasz) * 0.001;
    g.text_height = s.scaled(s.dimtxt) * 0.001;

    Dimension d;
    d.kind = Kind::Radial;
    d.style = j.style;
    d.centre = j.centre;
    d.radius = j.radius;
    d.p3 = j.jog;
    g.text = format(d, s, v);

    const f64 dx = jog.x - drawn.x, dy = jog.y - drawn.y;
    const f64 len = std::hypot(dx, dy);
    if (len < 1e-12) {
        g.reason = "the jog coincides with the drawn centre";
        return g;
    }
    const f64 ux = dx / len, uy = dy / len;
    const f64 nx = -uy, ny = ux;

    const f64 half = asz * 0.8;
    const W a{drawn.x + ux * (len * 0.45), drawn.y + uy * (len * 0.45)};
    const W b{drawn.x + ux * (len * 0.55), drawn.y + uy * (len * 0.55)};
    const W za{a.x + nx * half, a.y + ny * half};
    const W zb{b.x - nx * half, b.y - ny * half};

    g.lines.push_back(seg(v, drawn, a));
    g.lines.push_back(seg(v, a, za));
    g.lines.push_back(seg(v, za, zb));
    g.lines.push_back(seg(v, zb, b));
    g.lines.push_back(seg(v, b, jog));
    g.lines.push_back(seg(v, jog, origin));

    arrow(g.arrows, v, origin, {jog.x - origin.x, jog.y - origin.y}, asz, s.arrow);

    g.text_at = lat(v, jog.x, jog.y + g.text_height * 0.6);
    g.ok = true;
    return g;
}

InspectionGeometry build(const Inspection& i, const Style& s, const Volume& v) {
    InspectionGeometry out;
    out.base = build(i.dimension, s, v);
    if (!out.base.ok) {
        out.reason = out.base.reason;
        return out;
    }

    const f64 h = out.base.text_height > 0.0 ? out.base.text_height
                                             : s.scaled(s.dimtxt) * 0.001;
    const f64 pad = h * 0.4;
    const W at = world(v, out.base.text_at);

    struct Cell { std::string text; f64 width; };
    std::vector<Cell> cells;
    if (!i.label.empty()) cells.push_back({i.label, 0.0});
    cells.push_back({out.base.text, 0.0});
    if (!i.rate.empty()) cells.push_back({i.rate, 0.0});

    f64 total = 0.0;
    for (auto& c : cells) {
        c.width = text_width(c.text, h) + pad * 2.0;
        total += c.width;
    }
    const f64 y0 = at.y - h * 0.6, y1 = y0 + h * 1.6;
    f64 x = at.x - total * 0.5;
    const f64 left = x;

    for (std::size_t k = 0; k < cells.size(); ++k) {
        FrameCell fc;
        fc.text = cells[k].text;
        fc.width = cells[k].width;
        fc.height = y1 - y0;
        fc.text_height = h;
        fc.x = x + (cells[k].width - text_width(cells[k].text, h)) * 0.5;
        fc.y = y0 + (fc.height - h) * 0.5;
        out.cells.push_back(fc);

        if (k > 0) out.frame.push_back(seg(v, {x, y0}, {x, y1}));
        x += cells[k].width;
    }

    const f64 right = left + total;
    if (i.shape == InspectShape::Angular) {
        const f64 tip = (y1 - y0) * 0.5;
        out.frame.push_back(seg(v, {left, y0}, {right, y0}));
        out.frame.push_back(seg(v, {left, y1}, {right, y1}));
        out.frame.push_back(seg(v, {left, y0}, {left - tip, (y0 + y1) * 0.5}));
        out.frame.push_back(seg(v, {left - tip, (y0 + y1) * 0.5}, {left, y1}));
        out.frame.push_back(seg(v, {right, y0}, {right + tip, (y0 + y1) * 0.5}));
        out.frame.push_back(seg(v, {right + tip, (y0 + y1) * 0.5}, {right, y1}));
    } else if (i.shape == InspectShape::Round) {
        const f64 r = (y1 - y0) * 0.5;
        out.frame.push_back(seg(v, {left, y0}, {right, y0}));
        out.frame.push_back(seg(v, {left, y1}, {right, y1}));
        for (int side = 0; side < 2; ++side) {
            const f64 cx = side ? right : left;
            gsnap::Shape a;
            a.kind = gsnap::ShapeKind::Arc;
            a.a = lat(v, cx, (y0 + y1) * 0.5);
            a.radius = i64(std::llround(r * v.scale()));
            a.start_angle = side ? -kPi * 0.5 : kPi * 0.5;
            a.end_angle = side ? kPi * 0.5 : kPi * 1.5;
            out.frame.push_back(a);
        }
    } else {
        box(out.frame, v, left, y0, right, y1);
    }

    out.ok = true;
    return out;
}

const char* field_name(FieldKind k) noexcept {
    switch (k) {
        case FieldKind::SheetNumber:    return "SheetNumber";
        case FieldKind::SheetCount:     return "SheetCount";
        case FieldKind::SheetName:      return "SheetName";
        case FieldKind::SheetSetName:   return "SheetSetName";
        case FieldKind::DrawingTitle:   return "DrawingTitle";
        case FieldKind::FileName:       return "FileName";
        case FieldKind::FilePath:       return "FilePath";
        case FieldKind::Author:         return "Author";
        case FieldKind::LastSavedBy:    return "LastSavedBy";
        case FieldKind::CreateDate:     return "CreateDate";
        case FieldKind::SaveDate:       return "SaveDate";
        case FieldKind::PlotDate:       return "PlotDate";
        case FieldKind::Scale:          return "Scale";
        case FieldKind::CurrentLayer:   return "CurrentLayer";
        case FieldKind::SystemVariable: return "SystemVariable";
        case FieldKind::ObjectProperty: return "ObjectProperty";
        case FieldKind::Formula:        return "Formula";
        case FieldKind::Custom:         return "Custom";
    }
    return "";
}

void FieldContext::set(std::string_view key, std::string value) {
    for (auto& kv : values_)
        if (kv.first == key) {
            kv.second = std::move(value);
            return;
        }
    values_.emplace_back(std::string(key), std::move(value));
}

const std::string* FieldContext::get(std::string_view key) const {
    for (const auto& kv : values_)
        if (kv.first == key) return &kv.second;
    return nullptr;
}

namespace {

FieldKind kind_of(std::string_view name) {
    for (int i = 0; i <= static_cast<int>(FieldKind::Custom); ++i) {
        const FieldKind k = static_cast<FieldKind>(i);
        if (name == field_name(k)) return k;
    }
    return FieldKind::Custom;
}

bool next_field(std::string_view s, std::size_t from, std::size_t& begin,
                std::size_t& end, std::string& body) {
    const std::size_t open = s.find("%<", from);
    if (open == std::string_view::npos) return false;
    const std::size_t close = s.find(">%", open);
    if (close == std::string_view::npos) return false;

    begin = open;
    end = close + 2;
    std::string_view inner = s.substr(open + 2, close - open - 2);
    if (inner.rfind("\\AcVar", 0) == 0) {
        inner.remove_prefix(6);
        while (!inner.empty() && inner.front() == ' ') inner.remove_prefix(1);
    }
    body = std::string(inner);
    return true;
}

}

std::vector<Field> parse_fields(std::string_view text) {
    std::vector<Field> out;
    std::size_t at = 0, begin = 0, end = 0;
    std::string body;

    while (next_field(text, at, begin, end, body)) {
        Field f;
        const std::size_t sep = body.find('\\');
        std::string name = sep == std::string::npos ? body : body.substr(0, sep);
        if (sep != std::string::npos) f.format = body.substr(sep + 1);
        while (!name.empty() && name.back() == ' ') name.pop_back();

        const std::size_t space = name.find(' ');
        if (space != std::string::npos) {
            f.argument = name.substr(space + 1);
            name = name.substr(0, space);
        }
        f.kind = kind_of(name);
        if (f.kind == FieldKind::Custom && f.argument.empty()) f.argument = name;
        out.push_back(std::move(f));
        at = end;
    }
    return out;
}

std::string evaluate_fields(std::string_view text, const FieldContext& ctx) {
    std::string out;
    std::size_t at = 0, begin = 0, end = 0;
    std::string body;

    while (next_field(text, at, begin, end, body)) {
        out.append(text.substr(at, begin - at));

        const std::size_t sep = body.find('\\');
        std::string name = sep == std::string::npos ? body : body.substr(0, sep);
        while (!name.empty() && name.back() == ' ') name.pop_back();

        std::string key = name;
        const std::size_t space = name.find(' ');
        if (space != std::string::npos) key = name.substr(space + 1);

        const std::string* v = ctx.get(name);
        if (!v) v = ctx.get(key);
        out += v ? *v : "####";
        at = end;
    }
    out.append(text.substr(at));
    return out;
}

}
