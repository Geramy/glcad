#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>

#include "render/font.hpp"

namespace gtext {

namespace {
// stb takes no byte count: validate before handing it an offset.
int font_offset(const std::vector<u8>& bytes) {
    if(bytes.size()<16) return -1;
    const int offset=stbtt_GetFontOffsetForIndex(bytes.data(),0);
    if(offset<0 || std::size_t(offset)>bytes.size()-12) return -1;
    const auto start=std::size_t(offset);
    const auto count=(std::size_t(bytes[start+4])<<8)|bytes[start+5];
    if(count>(bytes.size()-start-12)/16) return -1;
    const auto number=[&](std::size_t at) {
        return (u32(bytes[at])<<24)|(u32(bytes[at+1])<<16)|(u32(bytes[at+2])<<8)|bytes[at+3];
    };
    for(std::size_t i=0;i<count;++i) {
        const auto entry=start+12+i*16;
        const std::size_t table=number(entry+8),size=number(entry+12);
        if(table>bytes.size() || size>bytes.size()-table) return -1;
    }
    return offset;
}
}

u32 next_codepoint(std::string_view s, std::size_t& i) {
    if (i >= s.size()) return 0;
    const u8 c = static_cast<u8>(s[i]);
    if (c < 0x80) { ++i; return c; }
    if ((c >> 5) == 0x6 && i + 1 < s.size()) {
        const u32 v = ((c & 0x1Fu) << 6) | (static_cast<u8>(s[i + 1]) & 0x3Fu);
        i += 2;
        return v;
    }
    if ((c >> 4) == 0xE && i + 2 < s.size()) {
        const u32 v = ((c & 0x0Fu) << 12) |
                      ((static_cast<u8>(s[i + 1]) & 0x3Fu) << 6) |
                      (static_cast<u8>(s[i + 2]) & 0x3Fu);
        i += 3;
        return v;
    }
    if ((c >> 3) == 0x1E && i + 3 < s.size()) {
        const u32 v = ((c & 0x07u) << 18) |
                      ((static_cast<u8>(s[i + 1]) & 0x3Fu) << 12) |
                      ((static_cast<u8>(s[i + 2]) & 0x3Fu) << 6) |
                      (static_cast<u8>(s[i + 3]) & 0x3Fu);
        i += 4;
        return v;
    }
    ++i;
    return 0xFFFD;   // replacement character
}

std::vector<u32> decode_utf8(std::string_view s) {
    std::vector<u32> out;
    std::size_t i = 0;
    while (i < s.size()) out.push_back(next_codepoint(s, i));
    return out;
}

bool Font::load(gio::Reader& r, i32 sdf_size, i32 padding, std::string* error) {
    std::vector<u8> data;
    if (!r.read_all(data) || data.empty()) {
        if (error) *error = "the font stream was empty";
        return false;
    }
    return load(data, sdf_size, padding, error);
}

bool Font::load_file(const std::string& path, i32 sdf_size, i32 padding,
                     std::string* error) {
    gio::FileReader f;
    const gio::Status s = f.open(path);
    if (!s.ok) {
        if (error) *error = s.message;
        return false;
    }
    return load(f, sdf_size, padding, error);
}

bool Font::load(const std::vector<u8>& ttf, i32 sdf_size, i32 padding,
                std::string* error) {
    return load_with_icons(ttf, {}, {}, sdf_size, padding, error);
}

bool Font::load_with_icons_files(const std::string& text_path,
                                 const std::string& icon_path,
                                 const std::vector<u32>& icon_codepoints,
                                 i32 sdf_size, i32 padding,
                                 std::string* error) {
    const auto slurp = [&](const std::string& path, std::vector<u8>& out) {
        gio::FileReader f;
        const gio::Status s = f.open(path);
        if (!s.ok) {
            if (error) *error = s.message;
            return false;
        }
        return f.read_all(out);
    };
    std::vector<u8> text_bytes, icon_bytes;
    if (!slurp(text_path, text_bytes)) return false;
    if (!icon_path.empty()) slurp(icon_path, icon_bytes);
    return load_with_icons(text_bytes, icon_bytes, icon_codepoints, sdf_size,
                           padding, error);
}

bool Font::load_with_icons(const std::vector<u8>& ttf,
                           const std::vector<u8>& icons,
                           const std::vector<u32>& icon_codepoints,
                           i32 sdf_size, i32 padding, std::string* error) {
    loaded_ = false;
    source_.reset();
    glyphs_.clear();
    outlines_.clear();
    kerning_.clear();
    atlas_.clear();

    stbtt_fontinfo info{};
    const int text_offset=font_offset(ttf);
    if (text_offset<0 || !stbtt_InitFont(&info, ttf.data(), text_offset)) {
        if (error) *error = "not a valid TrueType font";
        return false;
    }

    sdf_size_ = sdf_size;
    padding_  = padding;

    const float scale = stbtt_ScaleForPixelHeight(&info, static_cast<float>(sdf_size));

    int asc = 0, desc = 0, gap = 0;
    stbtt_GetFontVMetrics(&info, &asc, &desc, &gap);
    const float em = static_cast<float>(sdf_size);
    metrics_.ascent   = static_cast<f32>(asc) * scale / em;
    metrics_.descent  = static_cast<f32>(desc) * scale / em;
    metrics_.line_gap = static_cast<f32>(gap) * scale / em;

    std::vector<u32> want;
    for (u32 c = 32; c < 127; ++c) want.push_back(c);
    for (u32 c : {0x00B0u /*deg*/, 0x00B1u /*+-*/, 0x00D8u /*dia*/, 0x00F8u,
                  0x2205u /*empty set*/, 0x2300u /*diameter sign*/,
                  0x00B5u /*micro*/, 0x2014u /*em dash*/, 0x2013u,
                  0x2190u, 0x2192u, 0x2191u, 0x2193u, 0x00D7u, 0x00F7u,
                  0x00B2u, 0x00B3u, 0x221Eu /*infinity*/})
        want.push_back(c);

    struct Raster { u32 cp; unsigned char* bits; int w, h, xoff, yoff; int advance; };
    std::vector<Raster> rasters;
    rasters.reserve(want.size());

    int max_w = 1;
    for (u32 cp : want) {
        const int gi = stbtt_FindGlyphIndex(&info, static_cast<int>(cp));
        if (gi == 0 && cp != 32) continue;

        int adv = 0, lsb = 0;
        stbtt_GetGlyphHMetrics(&info, gi, &adv, &lsb);

        int w = 0, h = 0, xo = 0, yo = 0;
        unsigned char* bits = nullptr;
        if (cp != 32) {
            bits = stbtt_GetGlyphSDF(&info, scale, gi, padding, sdf_on_edge,
                                     sdf_distance_scale, &w, &h, &xo, &yo);
        }
        rasters.push_back({cp, bits, w, h, xo, yo, adv});
        max_w = std::max(max_w, w);
    }

    if (!icons.empty() && !icon_codepoints.empty()) {
        stbtt_fontinfo icon_info{};
        const int icon_offset=font_offset(icons);
        if (icon_offset>=0 && stbtt_InitFont(&icon_info, icons.data(),icon_offset)) {
            const float icon_scale =
                stbtt_ScaleForPixelHeight(&icon_info, static_cast<float>(sdf_size));
            for (u32 cp : icon_codepoints) {
                if (glyphs_.count(cp)) continue;
                const int gi = stbtt_FindGlyphIndex(&icon_info, static_cast<int>(cp));
                if (gi == 0) continue;
                int adv = 0, lsb = 0;
                stbtt_GetGlyphHMetrics(&icon_info, gi, &adv, &lsb);
                int w = 0, h = 0, xo = 0, yo = 0;
                unsigned char* bits = stbtt_GetGlyphSDF(
                    &icon_info, icon_scale, gi, padding, sdf_on_edge,
                    sdf_distance_scale, &w, &h, &xo, &yo);
                if (!bits) continue;
                // The icon face's own scale, not the text face's.
                const int scaled_adv = static_cast<int>(
                    static_cast<float>(adv) * icon_scale / scale);
                rasters.push_back({cp, bits, w, h, xo, yo, scaled_adv});
                max_w = std::max(max_w, w);
            }
        }
    }

    int atlas_w = 256;
    while (atlas_w < max_w + 2) atlas_w *= 2;

    int pen_x = 1, pen_y = 1, row_h = 0, needed_h = 1;
    for (const auto& r : rasters) {
        if (r.w == 0) continue;
        if (pen_x + r.w + 1 > atlas_w) { pen_x = 1; pen_y += row_h + 1; row_h = 0; }
        pen_x += r.w + 1;
        row_h = std::max(row_h, r.h);
        needed_h = pen_y + row_h + 1;
    }
    int atlas_h = 1;
    while (atlas_h < needed_h) atlas_h *= 2;

    atlas_.assign(static_cast<std::size_t>(atlas_w) * atlas_h, 0);
    atlas_w_ = atlas_w;
    atlas_h_ = atlas_h;

    pen_x = 1; pen_y = 1; row_h = 0;
    for (auto& r : rasters) {
        Glyph g;
        g.codepoint = r.cp;
        g.advance   = static_cast<f32>(r.advance) * scale / em;

        if (r.w > 0 && r.bits) {
            if (pen_x + r.w + 1 > atlas_w) { pen_x = 1; pen_y += row_h + 1; row_h = 0; }
            for (int y = 0; y < r.h; ++y)
                std::memcpy(&atlas_[static_cast<std::size_t>(pen_y + y) * atlas_w + pen_x],
                            &r.bits[static_cast<std::size_t>(y) * r.w],
                            static_cast<std::size_t>(r.w));

            g.ax = pen_x; g.ay = pen_y; g.aw = r.w; g.ah = r.h;
            g.bearing_x = static_cast<f32>(r.xoff) / em;
            g.bearing_y = static_cast<f32>(-r.yoff) / em;
            g.width     = static_cast<f32>(r.w) / em;
            g.height    = static_cast<f32>(r.h) / em;

            pen_x += r.w + 1;
            row_h = std::max(row_h, r.h);
            stbtt_FreeSDF(r.bits, nullptr);
        }
        glyphs_[r.cp] = g;
    }

    // Space mapped to a zero-advance .notdef: use the 'n' width or a quarter em.
    if (auto it = glyphs_.find(32); it != glyphs_.end() && it->second.advance <= 0.001f) {
        const Glyph* n = glyph('n');
        it->second.advance = (n && n->advance > 0.001f) ? n->advance : 0.25f;
    }

    for (u32 a = 32; a < 127; ++a) {
        const int ga = stbtt_FindGlyphIndex(&info, static_cast<int>(a));
        if (!ga) continue;
        for (u32 b = 32; b < 127; ++b) {
            const int gb = stbtt_FindGlyphIndex(&info, static_cast<int>(b));
            if (!gb) continue;
            const int k = stbtt_GetGlyphKernAdvance(&info, ga, gb);
            if (k != 0)
                kerning_[(static_cast<u64>(a) << 32) | b] =
                    static_cast<f32>(k) * scale / em;
        }
    }

    if (const Glyph* H = glyph('H')) metrics_.cap_height = H->height + H->bearing_y - H->height;
    if (const Glyph* H = glyph('H')) metrics_.cap_height = H->bearing_y;
    if (const Glyph* x = glyph('x')) metrics_.x_height = x->bearing_y;

    family_ = "loaded";
    source_ = std::make_shared<const std::vector<u8>>(ttf);
    loaded_ = true;
    return true;
}

std::size_t Font::coverage() const noexcept {
    std::size_t n = 0;
    for (u32 c = 33; c < 127; ++c)
        if (const auto it = glyphs_.find(c); it != glyphs_.end() && it->second.valid())
            ++n;
    return n;
}

bool Font::covers_ascii() const noexcept {
    for (u32 c = 'a'; c <= 'z'; ++c)
        if (const auto it = glyphs_.find(c); it == glyphs_.end() || !it->second.valid())
            return false;
    for (u32 c = 'A'; c <= 'Z'; ++c)
        if (const auto it = glyphs_.find(c); it == glyphs_.end() || !it->second.valid())
            return false;
    for (u32 c = '0'; c <= '9'; ++c)
        if (const auto it = glyphs_.find(c); it == glyphs_.end() || !it->second.valid())
            return false;
    return true;
}

const Glyph* Font::glyph(u32 cp) const {
    const auto it = glyphs_.find(cp);
    return it == glyphs_.end() ? nullptr : &it->second;
}

f32 Font::kerning(u32 a, u32 b) const {
    const auto it = kerning_.find((static_cast<u64>(a) << 32) | b);
    return it == kerning_.end() ? 0.0f : it->second;
}

f32 Font::measure(std::string_view utf8) const {
    f32 w = 0.0f;
    u32 prev = 0;
    std::size_t i = 0;
    while (i < utf8.size()) {
        const u32 cp = next_codepoint(utf8, i);
        if (const Glyph* g = glyph(cp)) {
            if (prev) w += kerning(prev, cp);
            w += g->advance;
        }
        prev = cp;
    }
    return w;
}

const GlyphOutline& Font::outline(u32 codepoint) const {
    if(const auto found=outlines_.find(codepoint);found!=outlines_.end())return found->second;
    GlyphOutline shape;
    if(source_ && !source_->empty()) {
        stbtt_fontinfo info{};
        if(stbtt_InitFont(&info,source_->data(),stbtt_GetFontOffsetForIndex(source_->data(),0))) {
            const double scale=stbtt_ScaleForPixelHeight(&info,1.f);
            stbtt_vertex* vertices=nullptr;
            const int count=stbtt_GetCodepointShape(&info,int(codepoint),&vertices);
            OutlinePoint previous{};
            const auto point=[&](double x,double y){return OutlinePoint{x*scale,y*scale};};
            const auto midpoint=[](OutlinePoint a,OutlinePoint b){return OutlinePoint{(a.x+b.x)*.5,(a.y+b.y)*.5};};
            const auto flat=[](OutlinePoint a,OutlinePoint b,OutlinePoint c) {
                const double dx=c.x-a.x,dy=c.y-a.y,length=std::hypot(dx,dy);
                return length>1e-12 ? std::abs(dx*(b.y-a.y)-dy*(b.x-a.x))/length : std::hypot(b.x-a.x,b.y-a.y);
            };
            const auto quadratic=[&](auto&& self,OutlinePoint a,OutlinePoint b,OutlinePoint c,int depth)->void {
                if(depth==10 || flat(a,b,c)<.0005){shape.contours.back().push_back(c);return;}
                const auto ab=midpoint(a,b),bc=midpoint(b,c),mid=midpoint(ab,bc);
                self(self,a,ab,mid,depth+1);self(self,mid,bc,c,depth+1);
            };
            const auto cubic=[&](auto&& self,OutlinePoint a,OutlinePoint b,OutlinePoint c,OutlinePoint d,int depth)->void {
                if(depth==10 || std::max(flat(a,b,d),flat(a,c,d))<.0005){shape.contours.back().push_back(d);return;}
                const auto ab=midpoint(a,b),bc=midpoint(b,c),cd=midpoint(c,d),abc=midpoint(ab,bc),bcd=midpoint(bc,cd),mid=midpoint(abc,bcd);
                self(self,a,ab,abc,mid,depth+1);self(self,mid,bcd,cd,d,depth+1);
            };
            for(int i=0;i<count;++i) {
                const auto& v=vertices[i];const auto end=point(v.x,v.y);
                if(v.type==STBTT_vmove)shape.contours.push_back({end});
                else if(!shape.contours.empty()) {
                    if(v.type==STBTT_vline)shape.contours.back().push_back(end);
                    else if(v.type==STBTT_vcurve)quadratic(quadratic,previous,point(v.cx,v.cy),end,0);
                    else if(v.type==STBTT_vcubic)cubic(cubic,previous,point(v.cx,v.cy),point(v.cx1,v.cy1),end,0);
                }
                previous=end;
            }
            stbtt_FreeShape(&info,vertices);
            for(auto& loop:shape.contours)if(loop.size()>1 && std::hypot(loop.front().x-loop.back().x,loop.front().y-loop.back().y)<1e-10)loop.pop_back();
        }
    }
    return outlines_.emplace(codepoint,std::move(shape)).first->second;
}

}
