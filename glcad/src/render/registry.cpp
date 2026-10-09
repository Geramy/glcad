#include "render/registry.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>

namespace gtext {

namespace fs = std::filesystem;

static FontKind kind_of(const std::string& ext) {
    std::string e;
    for (char c : ext) e.push_back(static_cast<char>(std::tolower((unsigned char)c)));
    if (e == ".ttf" || e == ".ttc") return FontKind::TrueType;
    if (e == ".otf") return FontKind::OpenType;
    if (e == ".shx") return FontKind::SHX;
    return FontKind::Unknown;
}

static std::string stem_name(const fs::path& p) { return p.stem().string(); }

static bool icontains(std::string_view hay, std::string_view needle) {
    if (needle.empty()) return true;
    if (needle.size() > hay.size()) return false;
    for (std::size_t i = 0; i + needle.size() <= hay.size(); ++i) {
        bool ok = true;
        for (std::size_t j = 0; j < needle.size() && ok; ++j)
            ok = std::tolower((unsigned char)hay[i + j]) ==
                 std::tolower((unsigned char)needle[j]);
        if (ok) return true;
    }
    return false;
}

void FontRegistry::add(FontEntry e) {
    for (auto& f : fonts_)
        if (f.name == e.name && f.kind == e.kind) { f = std::move(e); return; }
    fonts_.push_back(std::move(e));
}

std::size_t FontRegistry::scan_system(const std::vector<std::string>& extra) {
    std::vector<std::string> dirs = {
#if defined(__APPLE__)
        "/System/Library/Fonts", "/System/Library/Fonts/Supplemental", "/Library/Fonts",
#elif defined(_WIN32)
        "C:/Windows/Fonts",
#else
        "/usr/share/fonts", "/usr/local/share/fonts",
#endif
    };
    dirs.insert(dirs.end(), extra.begin(), extra.end());

    const std::size_t before = fonts_.size();
    for (const auto& d : dirs) {
        std::error_code ec;
        if (!fs::exists(d, ec)) continue;
        for (auto it = fs::recursive_directory_iterator(
                 d, fs::directory_options::skip_permission_denied, ec);
             it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (ec) break;
            if (!it->is_regular_file(ec)) continue;

            const fs::path& p = it->path();
            const FontKind k = kind_of(p.extension().string());
            if (k == FontKind::Unknown) continue;

            FontEntry e;
            e.name = stem_name(p);
            e.path = p.string();
            e.kind = k;
            e.italic = icontains(e.name, "italic") || icontains(e.name, "oblique");
            e.monospace = icontains(e.name, "mono") || icontains(e.name, "courier") ||
                          icontains(e.name, "consol");
            if (icontains(e.name, "bold")) e.weight = 700;
            else if (icontains(e.name, "light")) e.weight = 300;
            else if (icontains(e.name, "thin")) e.weight = 100;
            add(std::move(e));
        }
    }
    std::sort(fonts_.begin(), fonts_.end(),
              [](const FontEntry& a, const FontEntry& b) { return a.name < b.name; });
    return fonts_.size() - before;
}

const FontEntry* FontRegistry::find(std::string_view name) const {
    for (const auto& f : fonts_)
        if (f.name.size() == name.size() &&
            std::equal(f.name.begin(), f.name.end(), name.begin(),
                       [](char a, char b) {
                           return std::tolower((unsigned char)a) ==
                                  std::tolower((unsigned char)b);
                       }))
            return &f;
    return nullptr;
}

std::vector<const FontEntry*> FontRegistry::search(std::string_view partial) const {
    std::vector<const FontEntry*> out;
    for (const auto& f : fonts_)
        if (icontains(f.name, partial)) out.push_back(&f);
    return out;
}

std::vector<const FontEntry*> FontRegistry::monospace() const {
    std::vector<const FontEntry*> out;
    for (const auto& f : fonts_)
        if (f.monospace) out.push_back(&f);
    return out;
}

void FontRegistry::map_substitute(std::string_view missing, std::string_view repl) {
    for (auto& s : subs_)
        if (s.first == missing) { s.second = std::string(repl); return; }
    subs_.emplace_back(std::string(missing), std::string(repl));
}

const FontEntry* FontRegistry::resolve(std::string_view requested,
                                       bool& substituted) const {
    substituted = false;
    if (const FontEntry* e = find(requested)) return e;

    for (const auto& s : subs_)
        if (s.first == requested)
            if (const FontEntry* e = find(s.second)) { substituted = true; return e; }

    for (const auto& fb : fallbacks_)
        if (const FontEntry* e = find(fb)) { substituted = true; return e; }

    if (!fonts_.empty()) { substituted = true; return &fonts_.front(); }
    return nullptr;
}

TextStyleTable::TextStyleTable() {
    TextStyleDef s;
    s.name = "Standard";
    s.font = "Helvetica";
    styles_.push_back(s);
}

const TextStyleDef* TextStyleTable::find(std::string_view n) const {
    for (const auto& s : styles_)
        if (s.name == n) return &s;
    return nullptr;
}

TextStyleDef& TextStyleTable::add(TextStyleDef s) {
    for (auto& e : styles_)
        if (e.name == s.name) { e = std::move(s); return e; }
    styles_.push_back(std::move(s));
    return styles_.back();
}

bool TextStyleTable::remove(std::string_view n) {
    if (n == "Standard" || n == current_) return false;
    const auto it = std::find_if(styles_.begin(), styles_.end(),
                                 [&](const TextStyleDef& s) { return s.name == n; });
    if (it == styles_.end()) return false;
    styles_.erase(it);
    return true;
}

void TextStyleTable::set_current(std::string_view n) {
    if (find(n)) current_ = std::string(n);
}

}
