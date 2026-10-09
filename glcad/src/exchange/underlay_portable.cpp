#include "exchange/underlay_image.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <jpeglib.h>
#include <memory>
#include <png.h>
// MANDATORY: replace Poppler (GPL) with PDFium before any Windows/Linux release.
#include <poppler-document.h>
#include <poppler-page-renderer.h>
#include <poppler-page.h>
#include <setjmp.h>
#include <tiffio.h>

namespace gplot {
namespace {
constexpr std::uint64_t source_pixel_limit = 64ull * 1024 * 1024;
FILE *open_file(const std::string &path) {
#ifdef _WIN32
    return _wfopen(std::filesystem::u8path(path).c_str(), L"rb");
#else
    return std::fopen(path.c_str(), "rb");
#endif
}
bool source_size(std::uint64_t w, std::uint64_t h) {
    return w && h && w <= source_pixel_limit && h <= source_pixel_limit / w;
}
void placement(RasterImage &out, unsigned w, unsigned h, double dx, double dy) {
    out.metres_w = w * .0254 / (std::isfinite(dx) && dx > 0 ? dx : 96);
    out.metres_h = h * .0254 / (std::isfinite(dy) && dy > 0 ? dy : 96);
}
void thumbnail(RasterImage &out, int limit) {
    if (std::max(out.width, out.height) <= limit)
        return;
    const double factor = double(limit) / std::max(out.width, out.height);
    const int w = std::max(1, int(std::floor(out.width * factor)));
    const int h = std::max(1, int(std::floor(out.height * factor)));
    std::vector<std::uint8_t> pixels(std::size_t(w) * h * 4);
    // Filter in premultiplied space to avoid dark fringes at alpha edges.
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            double sums[4]{}, weight = 0;
            const double x0 = double(x) * out.width / w, x1 = double(x + 1) * out.width / w;
            const double y0 = double(y) * out.height / h, y1 = double(y + 1) * out.height / h;
            for (int sy = int(y0); sy < std::ceil(y1); ++sy)
                for (int sx = int(x0); sx < std::ceil(x1); ++sx) {
                    const double area = (std::min(x1, double(sx + 1)) - std::max(x0, double(sx))) *
                                        (std::min(y1, double(sy + 1)) - std::max(y0, double(sy)));
                    const auto *p = &out.rgba[(std::size_t(sy) * out.width + sx) * 4];
                    for (int c = 0; c < 3; ++c)
                        sums[c] += area * p[c] * p[3];
                    sums[3] += area * p[3];
                    weight += area;
                }
            auto *p = &pixels[(std::size_t(y) * w + x) * 4];
            for (int c = 0; c < 3; ++c)
                p[c] = sums[3] > 0
                           ? std::uint8_t(std::clamp(std::lround(sums[c] / sums[3]), 0l, 255l))
                           : 0;
            p[3] = std::uint8_t(std::clamp(std::lround(sums[3] / weight), 0l, 255l));
        }
    out.width = w;
    out.height = h;
    out.rgba = std::move(pixels);
}
RasterImage png(const std::string &path, std::string &error) {
    png_image image{};
    image.version = PNG_IMAGE_VERSION;
    RasterImage out;
    std::unique_ptr<FILE, decltype(&std::fclose)> source(open_file(path), std::fclose);
    if (!source || !png_image_begin_read_from_stdio(&image, source.get())) {
        error = image.message;
        png_image_free(&image);
        return out;
    }
    if (!source_size(image.width, image.height)) {
        error = "PNG exceeds the 64 megapixel source limit";
        png_image_free(&image);
        return out;
    }
    image.format = PNG_FORMAT_RGBA;
    out.width = int(image.width);
    out.height = int(image.height);
    out.rgba.resize(PNG_IMAGE_SIZE(image));
    if (!png_image_finish_read(&image, nullptr, out.rgba.data(), 0, nullptr)) {
        error = image.message;
        png_image_free(&image);
        return {};
    }
    png_image_free(&image);
    // libpng's simplified API does not expose pHYs; read the chunk headers.
    double dx = 96, dy = 96;
    if (auto *file = open_file(path)) {
        std::fseek(file, 8, SEEK_SET);
        unsigned char header[8];
        auto u32 = [](const unsigned char *p) {
            return (unsigned(p[0]) << 24) | (unsigned(p[1]) << 16) | (unsigned(p[2]) << 8) | p[3];
        };
        while (std::fread(header, 1, 8, file) == 8) {
            const auto length = u32(header);
            if (!std::memcmp(header + 4, "IDAT", 4) || !std::memcmp(header + 4, "IEND", 4))
                break;
            if (!std::memcmp(header + 4, "pHYs", 4) && length == 9) {
                unsigned char data[9];
                if (std::fread(data, 1, 9, file) == 9 && data[8] == 1) {
                    dx = u32(data) * .0254;
                    dy = u32(data + 4) * .0254;
                }
                break;
            }
            if (length > 0x7ffffffb || std::fseek(file, long(length) + 4, SEEK_CUR))
                break;
        }
        std::fclose(file);
    }
    placement(out, out.width, out.height, dx, dy);
    return out;
}
struct JpegError {
    jpeg_error_mgr base;
    jmp_buf jump;
    char message[JMSG_LENGTH_MAX];
};
struct JpegState {
    jpeg_decompress_struct info{};
    JpegError error{};
    FILE *file{};
    unsigned char *data{};
    bool created{};
};
unsigned jpeg_orientation(jpeg_saved_marker_ptr marker) {
    for (; marker; marker = marker->next) {
        if (marker->marker != JPEG_APP0 + 1 || marker->data_length < 14 ||
            std::memcmp(marker->data, "Exif\0\0", 6))
            continue;
        const auto *p = marker->data + 6;
        const std::size_t n = marker->data_length - 6;
        const bool little = p[0] == 'I' && p[1] == 'I';
        if (!little && !(p[0] == 'M' && p[1] == 'M'))
            continue;
        auto u16 = [&](std::size_t i) -> unsigned {
            if (i > n - 2)
                return 0;
            return little ? unsigned(p[i]) | (unsigned(p[i + 1]) << 8)
                          : (unsigned(p[i]) << 8) | p[i + 1];
        };
        auto u32 = [&](std::size_t i) -> std::uint32_t {
            if (i > n - 4)
                return 0;
            return little ? u16(i) | (std::uint32_t(u16(i + 2)) << 16)
                          : (std::uint32_t(u16(i)) << 16) | u16(i + 2);
        };
        if (u16(2) != 42)
            continue;
        const std::size_t offset = u32(4);
        if (offset > n - 2)
            continue;
        const unsigned count = u16(offset);
        if (count > (n - offset - 2) / 12)
            continue;
        for (unsigned i = 0; i < count; ++i) {
            const std::size_t entry = offset + 2 + i * 12;
            if (u16(entry) == 0x112 && u16(entry + 2) == 3 && u32(entry + 4) == 1) {
                const unsigned value = u16(entry + 8);
                if (value >= 1 && value <= 8)
                    return value;
            }
        }
    }
    return 1;
}
std::pair<unsigned, unsigned> oriented_pixel(unsigned x, unsigned y, unsigned w, unsigned h,
                                             unsigned orientation) {
    switch (orientation) {
    case 2:
        return {w - 1 - x, y};
    case 3:
        return {w - 1 - x, h - 1 - y};
    case 4:
        return {x, h - 1 - y};
    case 5:
        return {y, x};
    case 6:
        return {h - 1 - y, x};
    case 7:
        return {h - 1 - y, w - 1 - x};
    case 8:
        return {y, w - 1 - x};
    default:
        return {x, y};
    }
}
RasterImage jpeg(const std::string &path, std::string &error) {
    // Heap state so libjpeg's longjmp cannot clobber locals or skip destructors.
    auto state = std::make_unique<JpegState>();
    auto &s = *state;
    s.file = open_file(path);
    if (!s.file) {
        error = "cannot open JPEG";
        return {};
    }
    s.info.err = jpeg_std_error(&s.error.base);
    s.error.base.error_exit = [](j_common_ptr p) {
        auto *e = reinterpret_cast<JpegError *>(p->err);
        p->err->format_message(p, e->message);
        longjmp(e->jump, 1);
    };
    if (setjmp(s.error.jump)) {
        error = s.error.message;
        if (s.created)
            jpeg_destroy_decompress(&s.info);
        std::free(s.data);
        std::fclose(s.file);
        return {};
    }
    jpeg_create_decompress(&s.info);
    s.created = true;
    jpeg_stdio_src(&s.info, s.file);
    jpeg_save_markers(&s.info, JPEG_APP0 + 1, 0xffff);
    jpeg_read_header(&s.info, TRUE);
    const unsigned orientation = jpeg_orientation(s.info.marker_list);
    if (!source_size(s.info.image_width, s.info.image_height)) {
        jpeg_destroy_decompress(&s.info);
        std::fclose(s.file);
        error = "JPEG exceeds the 64 megapixel source limit";
        return {};
    }
    const bool cmyk = s.info.jpeg_color_space == JCS_CMYK || s.info.jpeg_color_space == JCS_YCCK;
    s.info.out_color_space = cmyk ? JCS_CMYK : JCS_RGB;
    jpeg_start_decompress(&s.info);
    const std::size_t stride = std::size_t(s.info.output_width) * s.info.output_components;
    s.data = static_cast<unsigned char *>(std::malloc(stride * s.info.output_height));
    if (!s.data) {
        jpeg_destroy_decompress(&s.info);
        std::fclose(s.file);
        error = "cannot allocate JPEG pixels";
        return {};
    }
    while (s.info.output_scanline < s.info.output_height) {
        JSAMPROW row = s.data + stride * s.info.output_scanline;
        jpeg_read_scanlines(&s.info, &row, 1);
    }
    jpeg_finish_decompress(&s.info);
    RasterImage out;
    out.width = s.info.output_width;
    out.height = s.info.output_height;
    const double units = s.info.density_unit == 2 ? 2.54 : 1;
    placement(out, out.width, out.height, s.info.density_unit ? s.info.X_density * units : 96,
              s.info.density_unit ? s.info.Y_density * units : 96);
    if (orientation >= 5) {
        std::swap(out.width, out.height);
        std::swap(out.metres_w, out.metres_h);
    }
    out.rgba.resize(std::size_t(out.width) * out.height * 4);
    for (std::size_t i = 0; i < std::size_t(out.width) * out.height; ++i) {
        const auto *p = s.data + i * s.info.output_components;
        const auto [x, y] = oriented_pixel(i % s.info.output_width, i / s.info.output_width,
                                           s.info.output_width, s.info.output_height, orientation);
        auto *q = out.rgba.data() + (std::size_t(y) * out.width + x) * 4;
        for (int c = 0; c < 3; ++c)
            q[c] = !cmyk                     ? p[c]
                   : s.info.saw_Adobe_marker ? (unsigned(p[c]) * p[3] + 127) / 255
                                             : ((255 - unsigned(p[c])) * (255 - p[3]) + 127) / 255;
        q[3] = 255;
    }
    jpeg_destroy_decompress(&s.info);
    std::free(s.data);
    std::fclose(s.file);
    return out;
}
RasterImage tiff(const std::string &path, int page, std::string &error) {
#ifdef _WIN32
    std::unique_ptr<TIFF, decltype(&TIFFClose)> file(
        TIFFOpenW(std::filesystem::u8path(path).c_str(), "r"), TIFFClose);
#else
    std::unique_ptr<TIFF, decltype(&TIFFClose)> file(TIFFOpen(path.c_str(), "r"), TIFFClose);
#endif
    RasterImage out;
    if (!file) {
        error = "cannot decode TIFF";
        return out;
    }
    out.pages = TIFFNumberOfDirectories(file.get());
    if (page > out.pages || !TIFFSetDirectory(file.get(), tdir_t(page - 1))) {
        error = "TIFF page unavailable";
        return {};
    }
    std::uint32_t w = 0, h = 0;
    TIFFGetField(file.get(), TIFFTAG_IMAGEWIDTH, &w);
    TIFFGetField(file.get(), TIFFTAG_IMAGELENGTH, &h);
    if (!source_size(w, h)) {
        error = "TIFF exceeds the 64 megapixel source limit";
        return {};
    }
    float dx = 96, dy = 96;
    std::uint16_t unit = RESUNIT_INCH, orientation = ORIENTATION_TOPLEFT;
    TIFFGetField(file.get(), TIFFTAG_XRESOLUTION, &dx);
    TIFFGetField(file.get(), TIFFTAG_YRESOLUTION, &dy);
    TIFFGetFieldDefaulted(file.get(), TIFFTAG_RESOLUTIONUNIT, &unit);
    TIFFGetFieldDefaulted(file.get(), TIFFTAG_ORIENTATION, &orientation);
    if (unit == RESUNIT_CENTIMETER) {
        dx *= 2.54;
        dy *= 2.54;
    } else if (unit == RESUNIT_NONE) {
        dx = dy = 96;
    }
    std::vector<std::uint32_t> pixels(std::size_t(w) * h);
    // libtiff's RGBA helper does not transpose orientations 5..8.
    TIFFSetField(file.get(), TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT);
    if (!TIFFReadRGBAImageOriented(file.get(), w, h, pixels.data(), ORIENTATION_TOPLEFT, 1)) {
        error = "invalid TIFF pixels";
        return {};
    }
    const bool transpose = orientation >= ORIENTATION_LEFTTOP;
    out.width = transpose ? h : w;
    out.height = transpose ? w : h;
    placement(out, out.width, out.height, transpose ? dy : dx, transpose ? dx : dy);
    out.rgba.resize(std::size_t(w) * h * 4);
    for (unsigned y = 0; y < h; ++y)
        for (unsigned x = 0; x < w; ++x) {
            const auto p = pixels[std::size_t(y) * w + x];
            const auto [ox, oy] = oriented_pixel(x, y, w, h, orientation);
            auto *q = out.rgba.data() + (std::size_t(oy) * out.width + ox) * 4;
            q[3] = TIFFGetA(p);
            const unsigned rgb[] = {TIFFGetR(p), TIFFGetG(p), TIFFGetB(p)};
            for (int c = 0; c < 3; ++c)
                q[c] = q[3] ? std::min(255u, (rgb[c] * 255 + q[3] / 2) / q[3]) : 0;
        }
    return out;
}
class PortablePdf final : public PdfRasterSource {
    std::unique_ptr<poppler::document> document_;
    std::vector<std::unique_ptr<poppler::page>> pages_;
    poppler::page_renderer renderer_;
    poppler::page* get(int number,std::string& error) {
        if(number<1 || number>page_count()) {error="PDF page unavailable";return nullptr;}
        auto& page=pages_[number-1];
        if(!page) page.reset(document_->create_page(number-1));
        if(!page) error="PDF page unavailable";
        return page.get();
    }
public:
    explicit PortablePdf(std::unique_ptr<poppler::document> document):document_(std::move(document)),pages_(document_->pages()) {
        renderer_.set_paper_color(0xffffffff);
        renderer_.set_image_format(poppler::image::format_rgb24);
        renderer_.set_render_hint(poppler::page_renderer::antialiasing,true);
    }
    int page_count() const override {return document_->pages();}
    std::pair<double,double> page_size(int number,std::string& error) override {
        error.clear();auto* page=get(number,error);if(!page)return {};
        const auto box=page->page_rect();double w=box.width(),h=box.height();
        if(!std::isfinite(w)||!std::isfinite(h)||w<=0||h<=0) {error="invalid PDF page dimensions";return {};}
        if(page->orientation()==poppler::page::landscape || page->orientation()==poppler::page::seascape) std::swap(w,h);
        return {w*.0254/72,h*.0254/72};
    }
    RasterImage tile(int number,double dx,double dy,int x,int y,int w,int h,std::string& error) override {
        error.clear();
        if(!std::isfinite(dx)||!std::isfinite(dy)||dx<=0||dy<=0||dx>1e9||dy>1e9||x<0||y<0||w<1||h<1||w>4096||h>4096) {
            error="invalid PDF tile bounds or resolution";return {};
        }
        const auto [mw,mh]=page_size(number,error);if(mw<=0||mh<=0)return {};
        const double pw=std::ceil(mw/.0254*dx),ph=std::ceil(mh/.0254*dy);
        if(pw>2147480000 || ph>2147480000 || double(x)+w>pw+1 || double(y)+h>ph+1) {error="PDF tile is outside the page";return {};}
        // Keep the clip edge outside the returned pixels to avoid seams between tiles.
        constexpr int bleed=4;
        const int left=std::min(x,bleed),top=std::min(y,bleed);
        const int rw=w+left+std::max(0,std::min(bleed,int(pw)-x-w));
        const int rh=h+top+std::max(0,std::min(bleed,int(ph)-y-h));
        auto image=renderer_.render_page(get(number,error),dx,dy,x-left,y-top,rw,rh);
        if(!image.is_valid() || image.width()!=rw || image.height()!=rh) {error="PDF tile rasterization failed";return {};}
        RasterImage out;out.pages=page_count();out.width=w;out.height=h;
        out.metres_w=w*.0254/dx;out.metres_h=h*.0254/dy;
        out.rgba.resize(std::size_t(w)*h*4);
        for(int row=0;row<h;++row)for(int col=0;col<w;++col) {
            const auto* src=reinterpret_cast<const unsigned char*>(image.const_data())+std::size_t(row+top)*image.bytes_per_row()+(col+left)*3;
            auto* dst=out.rgba.data()+(std::size_t(row)*w+col)*4;
            std::copy_n(src,3,dst);dst[3]=255;
        }
        return out;
    }
};
RasterImage pdf_page(PdfRasterSource& source,int number,int limit,std::string& error) {
    const auto [w,h]=source.page_size(number,error);if(w<=0||h<=0)return {};
    const double dpi=.0254*limit/std::max(w,h);
    const int width=std::clamp(int(std::ceil(w/.0254*dpi)),1,limit);
    const int height=std::clamp(int(std::ceil(h/.0254*dpi)),1,limit);
    auto out=source.tile(number,dpi,dpi,0,0,width,height,error);
    out.metres_w=w;out.metres_h=h;return out;
}
}
std::unique_ptr<PdfRasterSource> open_pdf_raster(const std::string& path,std::string& error) {
    error.clear();auto document=std::unique_ptr<poppler::document>(poppler::document::load_from_file(path));
    if(!document || document->is_locked() || document->pages()<1) {error="cannot decode PDF or password required";return {};}
    return std::make_unique<PortablePdf>(std::move(document));
}
RasterImage rasterize_pdf(const std::string& bytes,int page,std::string& error,int limit) {
    error.clear();
    if(page<1||limit<1||limit>4096||bytes.empty()||bytes.size()>64*1024*1024) {
        error="invalid PDF data, page or raster size";return {};
    }
    auto document=std::unique_ptr<poppler::document>(poppler::document::load_from_raw_data(bytes.data(),int(bytes.size())));
    if(!document||document->is_locked()||document->pages()<1){error="cannot decode PDF or password required";return {};}
    PortablePdf source(std::move(document));
    return pdf_page(source,page,limit,error);
}
RasterImage load_underlay_portable(const std::string &path, int page, std::string &error,
                                   int limit) {
    error.clear();
    if (page < 1 || limit < 1 || limit > 4096) {
        error = "invalid page or raster size";
        return {};
    }
    unsigned char signature[8]{};
    if (auto *file = open_file(path)) {
        std::fread(signature, 1, 8, file);
        std::fclose(file);
    } else {
        error = "cannot open underlay";
        return {};
    }
    RasterImage out;
    if (!std::memcmp(signature, "%PDF-", 5)) {
        auto source=open_pdf_raster(path,error);
        return source?pdf_page(*source,page,limit,error):RasterImage{};
    }
    const bool is_tiff = (!std::memcmp(signature, "II", 2) || !std::memcmp(signature, "MM", 2));
    if (is_tiff)
        out = tiff(path, page, error);
    else if (page != 1) {
        error = "image page unavailable";
        return {};
    } else if (!png_sig_cmp(signature, 0, 8))
        out = png(path, error);
    else if (signature[0] == 0xff && signature[1] == 0xd8)
        out = jpeg(path, error);
    else
        error = "unsupported underlay encoding (PDF, PNG, JPEG and TIFF supported)";
    if (out.valid())
        thumbnail(out, limit);
    return out;
}
}
