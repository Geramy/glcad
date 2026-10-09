#include "exchange/page.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace gplot {

const std::vector<PaperSize>& standard_papers() {
    // In metres, landscape.
    static const std::vector<PaperSize> p = {
        {"A6",  0.148, 0.105}, {"A5",  0.210, 0.148},
        {"A4",  0.297, 0.210}, {"A3",  0.420, 0.297},
        {"A2",  0.594, 0.420}, {"A1",  0.841, 0.594},
        {"A0",  1.189, 0.841},
        {"A0+", 1.230, 0.880}, {"A1+", 0.880, 0.625},
        {"2A0", 1.682, 1.189}, {"4A0", 2.378, 1.682},
        {"B5",  0.250, 0.176}, {"B4",  0.353, 0.250},
        {"B3",  0.500, 0.353}, {"B2",  0.707, 0.500},
        {"B1",  1.000, 0.707}, {"B0",  1.414, 1.000},
        {"LETTER", 0.2794, 0.2159}, {"LEGAL", 0.3556, 0.2159},
        {"TABLOID", 0.4318, 0.2794},
        {"ANSI A", 0.2794, 0.2159}, {"ANSI B", 0.4318, 0.2794},
        {"ANSI C", 0.5588, 0.4318}, {"ANSI D", 0.8636, 0.5588},
        {"ANSI E", 1.1176, 0.8636}, {"ANSI F", 1.0160, 0.7112},
        {"ARCH A", 0.3048, 0.2286}, {"ARCH B", 0.4572, 0.3048},
        {"ARCH C", 0.6096, 0.4572}, {"ARCH D", 0.9144, 0.6096},
        {"ARCH E", 1.2192, 0.9144}, {"ARCH E1", 1.0668, 0.7620},
        {"ARCH E2", 0.9652, 0.6604}, {"ARCH E3", 1.0922, 0.6858},
    };
    return p;
}

const PaperSize* find_paper(std::string_view name) {
    for (const auto& p : standard_papers())
        if (p.name == name) return &p;
    return nullptr;
}

PaperSize rotated(const PaperSize& p) {
    PaperSize o = p;
    std::swap(o.width, o.height);
    o.margin_left   = p.margin_bottom;
    o.margin_bottom = p.margin_right;
    o.margin_right  = p.margin_top;
    o.margin_top    = p.margin_left;
    return o;
}

const std::vector<NamedScale>& standard_scales() {
    static const std::vector<NamedScale> s = {
        {"100:1", 1.0, 100.0}, {"50:1", 1.0, 50.0}, {"20:1", 1.0, 20.0},
        {"10:1", 1.0, 10.0}, {"5:1", 1.0, 5.0}, {"2:1", 1.0, 2.0},
        {"1:1", 1.0, 1.0},
        // Metric reductions -- the ISO 5455 preferred series.
        {"1:2", 2.0, 1.0}, {"1:5", 5.0, 1.0}, {"1:10", 10.0, 1.0},
        {"1:20", 20.0, 1.0}, {"1:25", 25.0, 1.0}, {"1:50", 50.0, 1.0},
        {"1:100", 100.0, 1.0}, {"1:200", 200.0, 1.0}, {"1:250", 250.0, 1.0},
        {"1:500", 500.0, 1.0}, {"1:1000", 1000.0, 1.0},
        {"1:1250", 1250.0, 1.0}, {"1:2000", 2000.0, 1.0},
        {"1:2500", 2500.0, 1.0}, {"1:5000", 5000.0, 1.0},
        {"1:10000", 10000.0, 1.0},
        // Imperial architectural: 1/4"=1'-0" is 48:1.
        {"3\"=1'-0\"", 4.0, 1.0, ScaleFamily::Imperial},
        {"1-1/2\"=1'-0\"", 8.0, 1.0, ScaleFamily::Imperial},
        {"1\"=1'-0\"", 12.0, 1.0, ScaleFamily::Imperial},
        {"3/4\"=1'-0\"", 16.0, 1.0, ScaleFamily::Imperial},
        {"1/2\"=1'-0\"", 24.0, 1.0, ScaleFamily::Imperial},
        {"3/8\"=1'-0\"", 32.0, 1.0, ScaleFamily::Imperial},
        {"1/4\"=1'-0\"", 48.0, 1.0, ScaleFamily::Imperial},
        {"3/16\"=1'-0\"", 64.0, 1.0, ScaleFamily::Imperial},
        {"1/8\"=1'-0\"", 96.0, 1.0, ScaleFamily::Imperial},
        {"3/32\"=1'-0\"", 128.0, 1.0, ScaleFamily::Imperial},
        {"1/16\"=1'-0\"", 192.0, 1.0, ScaleFamily::Imperial},
        {"1/32\"=1'-0\"", 384.0, 1.0, ScaleFamily::Imperial},
        {"1\"=10'", 120.0, 1.0, ScaleFamily::Imperial}, {"1\"=20'", 240.0, 1.0, ScaleFamily::Imperial},
        {"1\"=30'", 360.0, 1.0, ScaleFamily::Imperial}, {"1\"=40'", 480.0, 1.0, ScaleFamily::Imperial},
        {"1\"=50'", 600.0, 1.0, ScaleFamily::Imperial}, {"1\"=60'", 720.0, 1.0, ScaleFamily::Imperial},
        {"1\"=100'", 1200.0, 1.0, ScaleFamily::Imperial}, {"1\"=200'", 2400.0, 1.0, ScaleFamily::Imperial},
    };
    return s;
}

const NamedScale* find_scale(std::string_view name) {
    for (const auto& s : standard_scales())
        if (s.name == name) return &s;
    return nullptr;
}

namespace {

std::string squeeze(std::string_view t) {
    std::string out;
    for (char c : t)
        if (!std::isspace(static_cast<unsigned char>(c)))
            out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

bool number(std::string_view t, f64& out) {
    if (t.empty()) return false;
    const auto dash = t.find('-');
    if (dash != std::string_view::npos && dash > 0 &&
        t.find('/') != std::string_view::npos && t.find('/') > dash) {
        f64 whole = 0.0, frac = 0.0;
        if (!number(t.substr(0, dash), whole)) return false;
        if (!number(t.substr(dash + 1), frac)) return false;
        out = whole + frac;
        return true;
    }
    const auto slash = t.find('/');
    if (slash != std::string_view::npos) {
        f64 n = 0.0, d = 0.0;
        if (!number(t.substr(0, slash), n)) return false;
        if (!number(t.substr(slash + 1), d)) return false;
        if (d == 0.0) return false;
        out = n / d;
        return true;
    }
    f64 v = 0.0;
    std::size_t i = 0;
    bool any = false, dot = false;
    f64 place = 0.1;
    for (; i < t.size(); ++i) {
        const char c = t[i];
        if (c >= '0' && c <= '9') {
            if (dot) { v += (c - '0') * place; place *= 0.1; }
            else       v = v * 10.0 + (c - '0');
            any = true;
        } else if (c == '.' && !dot) {
            dot = true;
        } else {
            return false;
        }
    }
    if (!any) return false;
    out = v;
    return true;
}

// Returns the length in inches.
bool imperial(std::string_view t, f64& inches) {
    if (t.empty()) return false;
    const auto foot = t.find('\'');
    if (foot != std::string_view::npos) {
        f64 ft = 0.0;
        if (!number(t.substr(0, foot), ft)) return false;
        std::string_view rest = t.substr(foot + 1);
        if (!rest.empty() && rest.front() == '-') rest.remove_prefix(1);
        if (!rest.empty() && rest.back() == '"') rest.remove_suffix(1);
        f64 in = 0.0;
        if (!rest.empty() && !number(rest, in)) return false;
        inches = ft * 12.0 + in;
        return true;
    }
    std::string_view v = t;
    if (!v.empty() && v.back() == '"') v.remove_suffix(1);
    return number(v, inches);
}

}

bool parse_scale(std::string_view text, NamedScale& out) {
    if (const NamedScale* s = find_scale(text)) { out = *s; return true; }

    const std::string t = squeeze(text);
    if (t.empty()) return false;
    if (t == "full" || t == "fullsize" || t == "1=1") { out = {"1:1", 1.0, 1.0}; return true; }
    if (t == "half") { out = {"1:2", 2.0, 1.0}; return true; }
    if (t == "quarter") { out = {"1:4", 4.0, 1.0}; return true; }
    if (t == "double") { out = {"2:1", 1.0, 2.0}; return true; }

    const auto sep = t.find_first_of(":=");
    if (sep == std::string::npos || sep == 0 || sep + 1 >= t.size()) return false;
    const std::string_view lhs(t.data(), sep);
    const std::string_view rhs(t.data() + sep + 1, t.size() - sep - 1);

    const bool imperial_form =
        lhs.find('"') != std::string_view::npos ||
        lhs.find('\'') != std::string_view::npos ||
        rhs.find('"') != std::string_view::npos ||
        rhs.find('\'') != std::string_view::npos;

    if (imperial_form) {
        // Both sides are lengths: 1"=50' is 1:600, not 1:50.
        f64 paper_in = 0.0, model_in = 0.0;
        if (!imperial(lhs, paper_in) || !imperial(rhs, model_in)) return false;
        if (paper_in <= 0.0 || model_in <= 0.0) return false;
        out.drawing = model_in / paper_in;
        out.paper = 1.0;
    } else {
        f64 a = 0.0, b = 0.0;
        if (!number(lhs, a) || !number(rhs, b)) return false;
        if (a <= 0.0 || b <= 0.0) return false;
        out.drawing = b;
        out.paper = a;
    }

    for (const NamedScale& s : standard_scales())
        if (std::abs(s.ratio() - out.ratio()) < 1e-9 * std::max(1.0, s.ratio())) {
            out = s;
            return true;
        }

    out.name = std::string(text);
    return true;
}

const NamedScale* nearest_standard_scale(f64 ratio, ScaleFamily family) {
    if (!(ratio > 0.0)) return nullptr;
    const NamedScale* best = nullptr;
    for (const NamedScale& s : standard_scales()) {
        if (s.family != family && !s.is_full_size()) continue;
        if (s.ratio() + 1e-12 < ratio) continue;
        if (!best || s.ratio() < best->ratio()) best = &s;
    }
    if (!best)
        for (const NamedScale& s : standard_scales()) {
            if (s.family != family && !s.is_full_size()) continue;
            if (!best || s.ratio() > best->ratio()) best = &s;
        }
    return best;
}

bool PageSetup::set_scale(std::string_view name) {
    NamedScale s;
    if (!parse_scale(name, s)) return false;
    set_scale(s);
    return true;
}

void PageSetup::set_scale(const NamedScale& s) {
    scale_numerator = s.paper;
    scale_denominator = s.drawing;
    scale_name_ = s.name;
    scale_family = s.family;
    fit_to_paper = false;
}

void PageSetup::to_paper(f64 mx, f64 my, f64 cx, f64 cy,
                         f64& px, f64& py) const {
    const f64 dpp = drawing_per_paper();
    const f64 sx = (mx - cx) / (dpp != 0.0 ? dpp : 1.0);
    const f64 sy = (my - cy) / (dpp != 0.0 ? dpp : 1.0);

    const PaperSize ps =
        orientation == Orientation::Landscape ? paper : rotated(paper);

    if (centred) {
        px = ps.margin_left + ps.printable_width() * 0.5 + sx + offset_x;
        py = ps.margin_bottom + ps.printable_height() * 0.5 + sy + offset_y;
    } else {
        px = ps.margin_left + sx + offset_x;
        py = ps.margin_bottom + sy + offset_y;
    }
}

f64 PageSetup::fitted_ratio(f64 model_width, f64 model_height) const {
    const f64 pw = effective_width(), ph = effective_height();
    if (pw <= 0.0 || ph <= 0.0) return 1.0;
    f64 r = 0.0;
    if (model_width > 0.0)  r = std::max(r, model_width / pw);
    if (model_height > 0.0) r = std::max(r, model_height / ph);
    return r > 0.0 ? r : 1.0;
}

PlotStyleTable::PlotStyleTable(StyleMode m) : mode_(m) {
    if (m == StyleMode::ColorDependent) {
        // 255 entries, one per ACI, each defaulting to "use the object's own".
        styles_.resize(255);
        for (std::size_t i = 0; i < styles_.size(); ++i)
            styles_[i].name = "Color " + std::to_string(i + 1);
    } else {
        PlotStyle normal;
        normal.name = "Normal";
        styles_.push_back(normal);
    }
}

PlotStyle& PlotStyleTable::by_index(i32 aci) {
    const i32 i = std::clamp(aci, 1, 255) - 1;
    if (styles_.size() < 255) styles_.resize(255);
    return styles_[static_cast<std::size_t>(i)];
}

const PlotStyle* PlotStyleTable::by_index(i32 aci) const {
    const i32 i = std::clamp(aci, 1, 255) - 1;
    if (static_cast<std::size_t>(i) >= styles_.size()) return nullptr;
    return &styles_[static_cast<std::size_t>(i)];
}

PlotStyle& PlotStyleTable::by_name(std::string_view n) {
    for (auto& s : styles_)
        if (s.name == n) return s;
    PlotStyle s;
    s.name = std::string(n);
    styles_.push_back(std::move(s));
    return styles_.back();
}

const PlotStyle* PlotStyleTable::find(std::string_view n) const {
    for (const auto& s : styles_)
        if (s.name == n) return &s;
    return nullptr;
}

void PlotStyleTable::add(PlotStyle s) {
    for (auto& e : styles_)
        if (e.name == s.name) { e = std::move(s); return; }
    styles_.push_back(std::move(s));
}

PlotStyle PlotStyleTable::resolve(i32 aci, std::string_view name) const {
    if (mode_ == StyleMode::ColorDependent) {
        if (const PlotStyle* s = by_index(aci)) return *s;
    } else if (const PlotStyle* s = find(name)) {
        return *s;
    }
    return PlotStyle{};
}

PlotStyleTable PlotStyleTable::monochrome() {
    PlotStyleTable t(StyleMode::ColorDependent);
    for (i32 aci = 1; aci <= 255; ++aci) {
        PlotStyle& s = t.by_index(aci);
        s.use_object_color = false;
        s.r = s.g = s.b = 0;
    }
    return t;
}

bool Viewport::layer_visible(std::string_view layer) const {
    for (const auto& f : frozen_layers)
        if (f == layer) return false;
    return true;
}

void Viewport::to_paper(f64 mx, f64 my, f64& px, f64& py) const {
    const f64 dx = (mx - centre_x) / (scale != 0.0 ? scale : 1.0);
    const f64 dy = (my - centre_y) / (scale != 0.0 ? scale : 1.0);
    const f64 c = std::cos(rotation), s = std::sin(rotation);

    const f64 cx = (lo.x + hi.x) * 0.5, cy = (lo.y + hi.y) * 0.5;

    px = cx + dx * c - dy * s;
    py = cy + dx * s + dy * c;
}

void Viewport::to_model(f64 px, f64 py, f64& mx, f64& my) const {
    const f64 cx = (lo.x + hi.x) * 0.5;
    const f64 cy = (lo.y + hi.y) * 0.5;
    const f64 dx = px - cx, dy = py - cy;
    const f64 c = std::cos(-rotation), s = std::sin(-rotation);
    const f64 rx = dx * c - dy * s, ry = dx * s + dy * c;
    mx = centre_x + rx * scale;
    my = centre_y + ry * scale;
}

Viewport* Sheet::viewport(std::size_t i) {
    return i < viewports.size() ? &viewports[i] : nullptr;
}
const Viewport* Sheet::viewport(std::size_t i) const {
    return i < viewports.size() ? &viewports[i] : nullptr;
}

Sheet& SheetSet::add(Sheet s) {
    for (auto& e : sheets_)
        if (e.name == s.name) { e = std::move(s); return e; }
    sheets_.push_back(std::move(s));
    return sheets_.back();
}

Sheet* SheetSet::find(std::string_view n) {
    for (auto& s : sheets_)
        if (s.name == n) return &s;
    return nullptr;
}
const Sheet* SheetSet::find(std::string_view n) const {
    for (const auto& s : sheets_)
        if (s.name == n) return &s;
    return nullptr;
}

bool SheetSet::remove(std::string_view n) {
    const auto it = std::find_if(sheets_.begin(), sheets_.end(),
                                 [&](const Sheet& s) { return s.name == n; });
    if (it == sheets_.end()) return false;
    sheets_.erase(it);
    return true;
}

bool SheetSet::reorder(std::string_view n, std::size_t to) {
    const auto it = std::find_if(sheets_.begin(), sheets_.end(),
                                 [&](const Sheet& s) { return s.name == n; });
    if (it == sheets_.end() || to >= sheets_.size()) return false;
    Sheet moved = *it;
    sheets_.erase(it);
    sheets_.insert(sheets_.begin() + static_cast<long>(to), std::move(moved));
    return true;
}

}
