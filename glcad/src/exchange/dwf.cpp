#include "exchange/dwf.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <zlib.h>
#include "exchange/underlay_image.hpp"

#include "engine/frame.hpp"
#include "engine/stream.hpp"
#include "engine/transport.hpp"

namespace gplot {

using gcm::u8;
using gcm::u16;
using gcm::u32;

namespace {

#include "dwf_default_font.inc"

// XPS works in DIPs (1/96 inch); this file works in metres.
constexpr f64 kDipPerMetre = 96.0 / 0.0254;

void put16(std::vector<u8>& v, u32 x) {
    v.push_back(static_cast<u8>(x & 0xFF));
    v.push_back(static_cast<u8>((x >> 8) & 0xFF));
}
void put32(std::vector<u8>& v, u32 x) {
    for (int i = 0; i < 4; ++i) v.push_back(static_cast<u8>((x >> (i * 8)) & 0xFF));
}
u32 get16(const std::vector<u8>& v, std::size_t at) {
    return static_cast<u32>(v[at]) | (static_cast<u32>(v[at + 1]) << 8);
}
u32 get32(const std::vector<u8>& v, std::size_t at) {
    u32 x = 0;
    for (int i = 0; i < 4; ++i) x |= static_cast<u32>(v[at + i]) << (i * 8);
    return x;
}

std::string xml_escape(std::string_view s) {
    std::string out;
    for (char c : s) {
        switch (c) {
            case '&':  out += "&amp;"; break;
            case '<':  out += "&lt;"; break;
            case '>':  out += "&gt;"; break;
            case '"':  out += "&quot;"; break;
            case '\'': out += "&apos;"; break;
            default:
                if (static_cast<unsigned char>(c) >= 0x20 || c == '\t' ||
                    c == '\n' || c == '\r')
                    out += c;
                break;
        }
    }
    return out;
}

std::string num(f64 v) {
    char buf[48];
    std::snprintf(buf, sizeof buf, "%.6f", v);
    std::string s = buf;
    while (s.size() > 2 && s.back() == '0' && s[s.size() - 2] != '.') s.pop_back();
    return s;
}

std::string colour_hex(const PdfColor& c) {
    auto ch = [](f64 v) {
        const int i = static_cast<int>(std::lround(std::clamp(v, 0.0, 1.0) * 255.0));
        return i;
    };
    char buf[16];
    std::snprintf(buf, sizeof buf, "#%02X%02X%02X%02X", ch(c.a), ch(c.r), ch(c.g), ch(c.b));
    return buf;
}

void append(std::vector<u8>& v, std::string_view s) {
    v.insert(v.end(), s.begin(), s.end());
}

}

std::vector<u8> zip(const std::vector<ZipEntry>& entries) {
    std::vector<u8> out;
    struct Record {
        u32 crc, size, offset;
        std::string name;
    };
    std::vector<Record> records;

    for (const ZipEntry& e : entries) {
        Record r;
        r.offset = static_cast<u32>(out.size());
        r.crc = gio::crc32(e.data.data(), e.data.size());
        r.size = static_cast<u32>(e.data.size());
        r.name = e.name;

        put32(out, 0x04034b50);          // local file header
        put16(out, 20);                  // version needed
        put16(out, 0);                   // flags
        put16(out, 0);                   // stored
        put16(out, 0);                   // time
        put16(out, 0x21);                // date: 1 January 1980
        put32(out, r.crc);
        put32(out, r.size);
        put32(out, r.size);
        put16(out, static_cast<u32>(r.name.size()));
        put16(out, 0);                   // extra length
        append(out, r.name);
        out.insert(out.end(), e.data.begin(), e.data.end());
        records.push_back(std::move(r));
    }

    const u32 directory_at = static_cast<u32>(out.size());
    for (const Record& r : records) {
        put32(out, 0x02014b50);          // central directory header
        put16(out, 20);                  // version made by
        put16(out, 20);                  // version needed
        put16(out, 0);
        put16(out, 0);                   // stored
        put16(out, 0);
        put16(out, 0x21);
        put32(out, r.crc);
        put32(out, r.size);
        put32(out, r.size);
        put16(out, static_cast<u32>(r.name.size()));
        put16(out, 0);                   // extra
        put16(out, 0);                   // comment
        put16(out, 0);                   // disk
        put16(out, 0);                   // internal attributes
        put32(out, 0);                   // external attributes
        put32(out, r.offset);
        append(out, r.name);
    }
    const u32 directory_size = static_cast<u32>(out.size()) - directory_at;

    put32(out, 0x06054b50);              // end of central directory
    put16(out, 0);
    put16(out, 0);
    put16(out, static_cast<u32>(records.size()));
    put16(out, static_cast<u32>(records.size()));
    put32(out, directory_size);
    put32(out, directory_at);
    put16(out, 0);                       // comment length
    return out;
}

std::vector<std::string> zip_names(const std::vector<u8>& archive) {
    std::vector<std::string> out;
    std::size_t at = 0;
    while (at + 30 <= archive.size() && get32(archive, at) == 0x04034b50) {
        const u32 name_len = get16(archive, at + 26);
        const u32 extra_len = get16(archive, at + 28);
        const u32 size = get32(archive, at + 18);
        if (at + 30 + name_len > archive.size()) break;
        out.emplace_back(reinterpret_cast<const char*>(archive.data() + at + 30),
                         name_len);
        at += 30 + name_len + extra_len + size;
    }
    return out;
}

bool unzip_entry(const std::vector<u8>& archive, std::string_view name,
                 std::vector<u8>& out) {
    std::size_t at = 0;
    while (at + 30 <= archive.size() && get32(archive, at) == 0x04034b50) {
        const u32 name_len = get16(archive, at + 26);
        const u32 extra_len = get16(archive, at + 28);
        const u32 size = get32(archive, at + 18);
        if (at + 30 + name_len + extra_len + size > archive.size()) return false;

        const std::string_view here(
            reinterpret_cast<const char*>(archive.data() + at + 30), name_len);
        if (here == name) {
            const std::size_t from = at + 30 + name_len + extra_len;
            out.assign(archive.begin() + static_cast<long>(from),
                       archive.begin() + static_cast<long>(from + size));
            return gio::crc32(out.data(), out.size()) == get32(archive, at + 14);
        }
        at += 30 + name_len + extra_len + size;
    }
    return false;
}

void DwfWriter::begin_page(std::string name, f64 width, f64 height) {
    if (in_page_) end_page();
    DwfPage p;
    p.name = name.empty() ? "Sheet " + std::to_string(pages_.size() + 1)
                          : std::move(name);
    p.width = width;
    p.height = height;
    pages_.push_back(std::move(p));
    in_page_ = true;
}

void DwfWriter::end_page() { in_page_ = false; }

void DwfWriter::add(const PdfPath& p) {
    if (!in_page_ || pages_.empty() || p.points.size() < 2) return;
    pages_.back().paths.push_back(p);
}

void DwfWriter::add(const PdfText& t) {
    if (!in_page_ || pages_.empty() || t.text.empty()) return;
    pages_.back().texts.push_back(t);
}

void DwfWriter::add(const PdfImage& image) {
    if (!in_page_ || pages_.empty() || !image.raster || !image.raster->valid()) return;
    pages_.back().images.push_back(image);
}

void DwfWriter::declare_layer(const std::string& name, bool visible) {
    for (auto& l : layers_)
        if (l.first == name) return;
    layers_.emplace_back(name, visible);
}

std::size_t DwfWriter::path_count() const noexcept {
    std::size_t n = 0;
    for (const DwfPage& p : pages_) n += p.paths.size();
    return n;
}

namespace {

std::string page_markup(const DwfPage& page,
                        const std::vector<std::pair<std::string, bool>>& layers, std::size_t page_index) {
    std::string x;
    x += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    x += "<FixedPage xmlns=\"http://schemas.microsoft.com/xps/2005/06\" "
         "xml:lang=\"en-GB\" Width=\"";
    x += num(page.width * kDipPerMetre);
    x += "\" Height=\"";
    x += num(page.height * kDipPerMetre);
    x += "\">\n";

    std::vector<std::string> order;
    for (const auto& l : layers) order.push_back(l.first);
    for (const PdfPath& p : page.paths)
        if (std::find(order.begin(), order.end(), p.layer) == order.end())
            order.push_back(p.layer);
    for (const PdfText& t : page.texts)
        if (std::find(order.begin(), order.end(), t.layer) == order.end())
            order.push_back(t.layer);

    const auto clip_attribute = [&](const std::optional<PaperClip>& clip) {
        if (!clip) return std::string{};
        const auto& c=*clip;
        std::string path=" Clip=\""; const auto points=c.boundary();
        for(std::size_t i=0;i<points.size();++i) path+=(i?" L ":"M ")+num(points[i].x*kDipPerMetre)+","+num((page.height-points[i].y)*kDipPerMetre);
        return path+" Z\"";
    };
    const auto open_content_clips=[&](const std::vector<ContentClip>& clips) {
        for(const auto& clip:clips) {
            x+="<Canvas Clip=\"F0 ";
            if(clip.inverted)x+="M 0,0 L "+num(page.width*kDipPerMetre)+",0 "+num(page.width*kDipPerMetre)+","+num(page.height*kDipPerMetre)+" 0,"+num(page.height*kDipPerMetre)+" Z ";
            const auto points=clip.boundary.boundary();
            for(std::size_t j=0;j<points.size();++j)x+=(j?" L ":"M ")+num(points[j].x*kDipPerMetre)+","+num((page.height-points[j].y)*kDipPerMetre);
            x+=" Z\">\n";
        }
    };
    const auto close_content_clips=[&](const std::vector<ContentClip>& clips) {
        for(std::size_t i=0;i<clips.size();++i)x+="</Canvas>\n";
    };
    for(std::size_t i=0;i<page.images.size();++i) {
        const auto& image=page.images[i];
        open_content_clips(image.clips);
        x+="  <Canvas"+clip_attribute(image.clip)+">\n   <Canvas RenderTransform=\"";
        const auto& a=image.x_axis;const auto& b=image.y_axis;const auto& o=image.origin;
        x+=num(a.x*kDipPerMetre)+","+num(-a.y*kDipPerMetre)+","+num(-b.x*kDipPerMetre)+","+num(b.y*kDipPerMetre)+","+num((o.x+b.x)*kDipPerMetre)+","+num((page.height-o.y-b.y)*kDipPerMetre);
        x+="\"><Path Data=\"M 0,0 L 1,0 1,1 0,1 Z\"><Path.Fill><ImageBrush ImageSource=\"/Documents/1/Resources/Images/"+std::to_string(page_index+1)+"_"+std::to_string(i+1)+".png\" ViewboxUnits=\"Absolute\" Viewbox=\"0,0,"+std::to_string(image.raster->width)+","+std::to_string(image.raster->height)+"\" ViewportUnits=\"Absolute\" Viewport=\"0,0,1,1\" /></Path.Fill></Path></Canvas>\n  </Canvas>\n";
        close_content_clips(image.clips);
    }
    for (const std::string& layer : order) {
        bool any = false;
        for (const PdfPath& p : page.paths) if (p.layer == layer) any = true;
        for (const PdfText& t : page.texts) if (t.layer == layer) any = true;
        if (!any) continue;

        bool visible = true;
        for (const auto& l : layers)
            if (l.first == layer) visible = l.second;

        x += "  <Canvas Name=\"";
        x += xml_escape(layer.empty() ? "0" : layer);
        x += "\" Opacity=\"";
        x += visible ? "1" : "0";
        x += "\">\n";

        for (const PdfPath& p : page.paths) {
            if (p.layer != layer) continue;
            x += "   <Canvas" + clip_attribute(p.clip) + ">\n";
            x += "    <Path";
            if(!p.filled) {
                x += " Stroke=\"";
                x += colour_hex(p.color);
                x += "\" StrokeThickness=\"";
                x += num(p.width_mm / 1000.0 * kDipPerMetre);
                x += "\"";
                const char* cap=p.endcap==LinecapStyle::Round ? "Round" : p.endcap==LinecapStyle::Square ? "Square" : "Flat";
                const char* join=p.join==LinejoinStyle::Round ? "Round" : p.join==LinejoinStyle::Bevel ? "Bevel" : "Miter";
                x += std::string(" StrokeStartLineCap=\"")+cap+"\" StrokeEndLineCap=\""+cap+"\" StrokeLineJoin=\""+join+"\"";
            }
            if (p.filled) {
                x += " Fill=\"";
                x += colour_hex(p.color);
                x += "\"";
            }
            if (!p.dashes_mm.empty()) {
                x += " StrokeDashArray=\"";
                for (std::size_t i = 0; i < p.dashes_mm.size(); ++i) {
                    if (i) x += " ";
                    // XPS dash lengths are multiples of the stroke width.
                    const f64 w = p.width_mm > 0.0 ? p.width_mm : 0.25;
                    x += num(p.dashes_mm[i] / w);
                }
                x += "\"";
            }
            x += p.filled ? (p.even_odd?" Data=\"F0 ":" Data=\"F1 ") : " Data=\"";
            const auto contour=[&](const auto& points) {
                if(points.empty())return;
                x+="M ";
                for(std::size_t i=0;i<points.size();++i) {
                    if(i)x+=" L ";
                    x+=num(points[i].first*kDipPerMetre)+","+num((page.height-points[i].second)*kDipPerMetre);
                }
                if(p.closed||p.filled)x+=" Z ";
            };
            contour(p.points);for(const auto& points:p.subpaths)contour(points);
            x += "\" />\n   </Canvas>\n";
        }

        for (const PdfText& t : page.texts) {
            if (t.layer != layer) continue;
            open_content_clips(t.clips);
            x += "   <Canvas" + clip_attribute(t.clip) + (t.invisible ? " Opacity=\"0\"" : "") + ">\n";
            x += "    <Glyphs Fill=\"";
            x += colour_hex(t.color);
            if (std::fabs(t.rotation)>1e-9 || t.width_factor!=1 || t.oblique!=0 || t.y_factor!=1) {
                const f64 c=std::cos(t.rotation), s=std::sin(t.rotation);
                const f64 ox=t.x*kDipPerMetre, oy=(page.height-t.y)*kDipPerMetre;
                const f64 sh=std::tan(t.oblique*3.141592653589793/180)*t.width_factor;
                const f64 a=c*t.width_factor,b=-s*t.width_factor,d=c*t.y_factor+s*sh,e=s*t.y_factor-c*sh;
                x += "\" RenderTransform=\"" + num(a) + "," + num(b) + "," + num(e) + "," + num(d)
                    + "," + num(ox*(1-a)-e*oy) + "," + num(oy*(1-d)-b*ox);
            }
            x += "\" FontRenderingEmSize=\"";
            x += num(t.height_mm / 1000.0 * kDipPerMetre);
            x += "\" OriginX=\"";
            x += num(t.x * kDipPerMetre);
            x += "\" OriginY=\"";
            x += num((page.height - t.y) * kDipPerMetre);
            x += "\" UnicodeString=\"";
            x += xml_escape(t.text);
            x += "\" FontUri=\"/Documents/1/Resources/Fonts/default.ttf\" />\n   </Canvas>\n";
            close_content_clips(t.clips);
        }
        x += "  </Canvas>\n";
    }
    x += "</FixedPage>\n";
    return x;
}

std::vector<u8> png_image(const RasterImage& image) {
    if(!image.valid() || std::uint64_t(image.width)*image.height>64ull*1024*1024) return {};
    std::vector<u8> scan;scan.reserve(image.rgba.size()+image.height);
    for(int y=0;y<image.height;++y) {
        scan.push_back(0);
        const auto start=image.rgba.begin()+std::size_t(y)*image.width*4;
        scan.insert(scan.end(),start,start+std::size_t(image.width)*4);
    }
    uLongf size=compressBound(scan.size());std::vector<u8> compressed(size);
    if(compress2(compressed.data(),&size,scan.data(),scan.size(),Z_DEFAULT_COMPRESSION)!=Z_OK)return {};
    compressed.resize(size);
    std::vector<u8> png={137,80,78,71,13,10,26,10};
    const auto big=[](std::vector<u8>& dst,u32 v){for(int s=24;s>=0;s-=8)dst.push_back(u8(v>>s));};
    const auto chunk=[&](const char* type,const std::vector<u8>& data){
        big(png,u32(data.size()));const auto start=png.size();
        png.insert(png.end(),type,type+4);png.insert(png.end(),data.begin(),data.end());
        big(png,gio::crc32(png.data()+start,png.size()-start));
    };
    std::vector<u8> header;big(header,image.width);big(header,image.height);
    header.insert(header.end(),{8,6,0,0,0});chunk("IHDR",header);chunk("IDAT",compressed);chunk("IEND",{});
    return png;
}

std::vector<u8> bytes_of(const std::string& s) {
    return std::vector<u8>(s.begin(), s.end());
}

}

std::vector<u8> DwfWriter::build() const {
    std::vector<ZipEntry> parts;

    std::string types;
    types += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    types += "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/"
             "content-types\">\n";
    types += "  <Default Extension=\"rels\" ContentType=\"application/"
             "vnd.openxmlformats-package.relationships+xml\" />\n";
    types += "  <Default Extension=\"fpage\" ContentType=\"application/"
             "vnd.ms-package.xps-fixedpage+xml\" />\n";
    types += "  <Default Extension=\"fdoc\" ContentType=\"application/"
             "vnd.ms-package.xps-fixeddocument+xml\" />\n";
    types += "  <Default Extension=\"fdseq\" ContentType=\"application/"
             "vnd.ms-package.xps-fixeddocumentsequence+xml\" />\n";
    types += "  <Default Extension=\"xml\" ContentType=\"application/xml\" />\n";
    types += "  <Default Extension=\"ttf\" ContentType=\"application/vnd.ms-opentype\" />\n";
    types += "  <Default Extension=\"png\" ContentType=\"image/png\" />\n";
    types += "</Types>\n";
    parts.push_back({"[Content_Types].xml", bytes_of(types)});

    std::string root_rels;
    root_rels += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    root_rels += "<Relationships xmlns=\"http://schemas.openxmlformats.org/"
                 "package/2006/relationships\">\n";
    root_rels += "  <Relationship Id=\"rId1\" Type=\"http://schemas.microsoft."
                 "com/xps/2005/06/fixedrepresentation\" "
                 "Target=\"/FixedDocSeq.fdseq\" />\n";
    root_rels += "  <Relationship Id=\"rId2\" Type=\"http://schemas.openxml"
                 "formats.org/package/2006/relationships/metadata/core-"
                 "properties\" Target=\"/docProps/core.xml\" />\n";
    root_rels += "</Relationships>\n";
    parts.push_back({"_rels/.rels", bytes_of(root_rels)});

    std::string props;
    props += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    props += "<coreProperties xmlns=\"http://schemas.openxmlformats.org/"
             "package/2006/metadata/core-properties\">\n";
    props += "  <title>" + xml_escape(title_) + "</title>\n";
    props += "  <creator>" + xml_escape(author_) + "</creator>\n";
    props += "</coreProperties>\n";
    parts.push_back({"docProps/core.xml", bytes_of(props)});

    std::string seq;
    seq += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    seq += "<FixedDocumentSequence xmlns=\"http://schemas.microsoft.com/xps/"
           "2005/06\">\n";
    seq += "  <DocumentReference Source=\"/Documents/1/FixedDoc.fdoc\" />\n";
    seq += "</FixedDocumentSequence>\n";
    parts.push_back({"FixedDocSeq.fdseq", bytes_of(seq)});

    std::string doc;
    doc += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    doc += "<FixedDocument xmlns=\"http://schemas.microsoft.com/xps/2005/06\">\n";
    for (std::size_t i = 0; i < pages_.size(); ++i) {
        doc += "  <PageContent Source=\"Pages/" + std::to_string(i + 1) +
               ".fpage\" />\n";
    }
    doc += "</FixedDocument>\n";
    parts.push_back({"Documents/1/FixedDoc.fdoc", bytes_of(doc)});

    const bool has_text = std::any_of(pages_.begin(),pages_.end(),[](const auto& p){return !p.texts.empty();});
    if(has_text) {
        std::vector<u8> bytes = font_ && !font_->empty() ? *font_ :
            std::vector<u8>(std::begin(kDwfDefaultFont),std::end(kDwfDefaultFont));
        parts.push_back({"Documents/1/Resources/Fonts/default.ttf",std::move(bytes)});
    }
    for (std::size_t i = 0; i < pages_.size(); ++i) {
        parts.push_back({"Documents/1/Pages/" + std::to_string(i + 1) + ".fpage",
                         bytes_of(page_markup(pages_[i], layers_,i))});
        if(!pages_[i].texts.empty() || !pages_[i].images.empty()) {
            std::string rels = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">";
            if(!pages_[i].texts.empty()) rels+="<Relationship Id=\"Font1\" Type=\"http://schemas.microsoft.com/xps/2005/06/required-resource\" "
                "Target=\"/Documents/1/Resources/Fonts/default.ttf\" />";
            for(std::size_t j=0;j<pages_[i].images.size();++j) {
                const std::string uri="Documents/1/Resources/Images/"+std::to_string(i+1)+"_"+std::to_string(j+1)+".png";
                auto pixels=png_image(*pages_[i].images[j].raster);if(pixels.empty())return {};
                parts.push_back({uri,std::move(pixels)});
                rels+="<Relationship Id=\"Image"+std::to_string(j+1)+"\" Type=\"http://schemas.microsoft.com/xps/2005/06/required-resource\" Target=\"/"+uri+"\" />";
            }
            rels+="</Relationships>";
            parts.push_back({"Documents/1/Pages/_rels/"+std::to_string(i+1)+".fpage.rels",bytes_of(rels)});
        }
    }

    std::string names;
    names += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    names += "<Sheets>\n";
    for (std::size_t i = 0; i < pages_.size(); ++i) {
        names += "  <Sheet Index=\"" + std::to_string(i + 1) + "\" Name=\"" +
                 xml_escape(pages_[i].name) + "\" />\n";
    }
    names += "</Sheets>\n";
    parts.push_back({"Documents/1/Sheets.xml", bytes_of(names)});

    return zip(parts);
}

bool DwfWriter::write(gio::Writer& w) const {
    const std::vector<u8> data = build();
    return !data.empty() && w.write(data.data(), data.size()) == data.size() && w.flush();
}

bool DwfWriter::write_file(const std::string& path) const {
    return write_url("file://" + path);
}

bool DwfWriter::write_url(std::string_view url) const {
    const auto data=build();if(data.empty())return false;
    gio::Status st;
    auto conn = gio::TransportRegistry::instance().open(url, gio::Mode::Write, st);
    if (!conn || !conn->output()) return false;
    auto& stream=*conn->output();
    const bool ok=stream.write(data.data(),data.size())==data.size() && stream.flush();
    return conn->close().ok && ok;
}

PlotResult publish(const DwfJob& job, DwfWriter& out) {
    PlotResult r;
    if (!job.sheets || job.sheets->size() == 0) {
        r.message = "no sheets to publish";
        return r;
    }

    for (const Sheet& sh : job.sheets->all()) {
        out.begin_page(sh.name, sh.setup.page_width(), sh.setup.page_height());

        PlotJob one;
        one.sheet = &sh;
        one.styles = job.styles;
        one.layers = job.layers;
        one.volume = job.volume;
        one.content = job.content;

        PdfWriter page;
        const PlotResult one_result = plot(one, page);

        if (!one_result.ok) {
            r.message = one_result.message;
            return r;
        }
        for (const PdfPath& p : page.paths_of_page(0)) {
            out.declare_layer(p.layer);
            out.add(p);
        }
        for (const PdfText& t : page.texts_of_page(0)) out.add(t);
        for (const PdfImage& image : page.images_of_page(0)) out.add(image);

        r.paths += one_result.paths;
        r.clipped += one_result.clipped;
        r.scale_name = one_result.scale_name;
        r.scale_ratio = one_result.scale_ratio;
        out.end_page();
    }

    r.ok = true;
    r.message = "published";
    return r;
}

}
