#include "engine/layer.hpp"

#include <cmath>

#include <algorithm>
#include <cctype>

namespace gdoc {

f64 lineweight_mm(Lineweight w) noexcept {
    const i32 v = static_cast<i32>(w);
    return v < 0 ? 0.25 : static_cast<f64>(v) / 100.0;   // sentinels plot as default
}

Lineweight nearest_lineweight(f64 mm) noexcept {
    static const Lineweight ladder[] = {
        Lineweight::W000, Lineweight::W005, Lineweight::W009, Lineweight::W013,
        Lineweight::W015, Lineweight::W018, Lineweight::W020, Lineweight::W025,
        Lineweight::W030, Lineweight::W035, Lineweight::W040, Lineweight::W050,
        Lineweight::W053, Lineweight::W060, Lineweight::W070, Lineweight::W080,
        Lineweight::W090, Lineweight::W100, Lineweight::W106, Lineweight::W120,
        Lineweight::W140, Lineweight::W158, Lineweight::W200, Lineweight::W211,
    };
    Lineweight best = Lineweight::W025;
    f64 best_d = 1e300;
    for (const Lineweight w : ladder) {
        const f64 d = std::fabs(lineweight_mm(w) - mm);
        if (d < best_d) { best_d = d; best = w; }
    }
    return best;
}

bool Layer::frozen_in(u32 viewport) const noexcept {
    return std::find(vp_frozen.begin(), vp_frozen.end(), viewport) != vp_frozen.end();
}

bool glob_match(std::string_view pat, std::string_view txt) {
    std::size_t p = 0, t = 0, star = std::string_view::npos, mark = 0;
    auto low = [](char c) { return static_cast<char>(std::tolower((unsigned char)c)); };

    while (t < txt.size()) {
        if (p < pat.size() && (pat[p] == '?' || low(pat[p]) == low(txt[t]))) {
            ++p; ++t;
        } else if (p < pat.size() && pat[p] == '*') {
            star = p++;
            mark = t;
        } else if (star != std::string_view::npos) {
            p = star + 1;
            t = ++mark;
        } else {
            return false;
        }
    }
    while (p < pat.size() && pat[p] == '*') ++p;
    return p == pat.size();
}

LayerTable::LayerTable() {
    Layer zero;
    zero.name = "0";
    zero.description = "Default layer";
    layers_.push_back(zero);
}

Layer* LayerTable::find(std::string_view name) {
    for (auto& l : layers_)
        if (glob_match(l.name, name) && l.name.size() == name.size()) return &l;
    for (auto& l : layers_)
        if (l.name == name) return &l;
    return nullptr;
}

const Layer* LayerTable::find(std::string_view name) const {
    for (const auto& l : layers_)
        if (l.name == name) return &l;
    return nullptr;
}

Layer& LayerTable::add(Layer l) {
    for (auto& e : layers_)
        if (e.name == l.name) { e = std::move(l); return e; }
    layers_.push_back(std::move(l));
    return layers_.back();
}

bool LayerTable::remove(std::string_view name) {
    if (name == "0") return false;
    if (name == current_) return false;
    const auto it = std::find_if(layers_.begin(), layers_.end(),
                                 [&](const Layer& l) { return l.name == name; });
    if (it == layers_.end()) return false;
    layers_.erase(it);
    return true;
}

bool LayerTable::rename(std::string_view from, std::string_view to) {
    if (from == "0") return false;
    if (find(to)) return false;
    Layer* l = find(from);
    if (!l) return false;
    const bool was_current = (current_ == l->name);
    l->name = std::string(to);
    if (was_current) current_ = l->name;
    return true;
}

void LayerTable::set_current(std::string_view name) {
    if (const Layer* l = find(name)) current_ = l->name;
}

void LayerTable::isolate(std::string_view name) {
    for (auto& l : layers_) l.on = (l.name == name);
}

void LayerTable::all_on() {
    for (auto& l : layers_) l.on = true;
}

void LayerTable::thaw_all() {
    for (auto& l : layers_) l.frozen = false;
}

std::vector<const Layer*> LayerTable::filter_by_name(std::string_view pattern) const {
    std::vector<const Layer*> out;
    for (const auto& l : layers_)
        if (glob_match(pattern, l.name)) out.push_back(&l);
    return out;
}

std::vector<const Layer*> LayerTable::visible_layers() const {
    std::vector<const Layer*> out;
    for (const auto& l : layers_)
        if (l.visible()) out.push_back(&l);
    return out;
}

void LayerTable::save_state(std::string_view name) {
    for (auto& s : states_)
        if (s.name == name) { s.layers = layers_; return; }
    states_.push_back({std::string(name), layers_});
}

bool LayerTable::restore_state(std::string_view name) {
    for (const auto& s : states_)
        if (s.name == name) { layers_ = s.layers; return true; }
    return false;
}

std::vector<std::string> LayerTable::state_names() const {
    std::vector<std::string> n;
    for (const auto& s : states_) n.push_back(s.name);
    return n;
}



namespace {

constexpr gcm::f64 kShadeValue[10]      = {1.00, 1.00, 0.80, 0.80, 0.60,
                                      0.60, 0.50, 0.50, 0.30, 0.30};
constexpr gcm::f64 kShadeSaturation[10] = {1.00, 0.65, 1.00, 0.65, 1.00,
                                      0.65, 1.00, 0.65, 1.00, 0.65};

void hsv_bytes(gcm::f64 h_deg, gcm::f64 s, gcm::f64 v, gcm::u8& r, gcm::u8& g, gcm::u8& b) noexcept {
    const gcm::f64 c = v * s;
    const gcm::f64 hp = h_deg / 60.0;
    const gcm::f64 x = c * (1.0 - std::fabs(std::fmod(hp, 2.0) - 1.0));
    gcm::f64 r1 = 0, g1 = 0, b1 = 0;
    switch (static_cast<int>(hp) % 6) {
        case 0: r1 = c; g1 = x; break;
        case 1: r1 = x; g1 = c; break;
        case 2: g1 = c; b1 = x; break;
        case 3: g1 = x; b1 = c; break;
        case 4: r1 = x; b1 = c; break;
        default: r1 = c; b1 = x; break;
    }
    const gcm::f64 m = v - c;
    r = static_cast<gcm::u8>(std::lround((r1 + m) * 255.0));
    g = static_cast<gcm::u8>(std::lround((g1 + m) * 255.0));
    b = static_cast<gcm::u8>(std::lround((b1 + m) * 255.0));
}

}

i32 aci_of_name(std::string_view name) noexcept {
    if (name == "red")     return 1;
    if (name == "yellow")  return 2;
    if (name == "green")   return 3;
    if (name == "cyan")    return 4;
    if (name == "blue")    return 5;
    if (name == "magenta") return 6;
    if (name == "white" || name == "black") return 7;
    return 0;
}

void aci_rgb(gcm::i32 index, gcm::u8& r, gcm::u8& g, gcm::u8& b) noexcept {
    // 0 is ByBlock and 256 ByLayer; 7 is the foreground, reported as white.
    struct Fixed { gcm::i32 i; gcm::u8 r, g, b; };
    static constexpr Fixed kFixed[] = {
        {1, 255, 0, 0},     {2, 255, 255, 0},   {3, 0, 255, 0},
        {4, 0, 255, 255},   {5, 0, 0, 255},     {6, 255, 0, 255},
        {7, 255, 255, 255}, {8, 128, 128, 128}, {9, 192, 192, 192},
        {250, 51, 51, 51},  {251, 91, 91, 91},  {252, 132, 132, 132},
        {253, 173, 173, 173}, {254, 214, 214, 214}, {255, 255, 255, 255},
    };
    for (const Fixed& f : kFixed)
        if (f.i == index) { r = f.r; g = f.g; b = f.b; return; }

    if (index < 10 || index > 249) { r = g = b = 255; return; }
    const gcm::i32 n = index - 10;
    const gcm::i32 hue = n / 10;              // 0..23, every 15 degrees
    const gcm::i32 shade = n % 10;
    hsv_bytes(static_cast<gcm::f64>(hue) * 15.0, kShadeSaturation[shade],
              kShadeValue[shade], r, g, b);
}

}
