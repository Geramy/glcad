#include "exchange/underlay_image.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <libgxps/gxps.h>
#include <memory>

namespace gplot {
namespace {
struct Unref {
    template <class T> void operator()(T *p) const {
        if (p)
            g_object_unref(p);
    }
};
struct Error {
    GError *value{};
    ~Error() {
        if (value)
            g_error_free(value);
    }
    std::string message(const char *fallback) const { return value ? value->message : fallback; }
};
}
RasterImage load_underlay_xps(const std::string &path, int number, std::string &error, int limit) {
    error.clear();
    if (number < 1 || limit < 1 || limit > 4096) {
        error = "invalid page or raster size";
        return {};
    }
    Error failure;
    std::unique_ptr<GFile, Unref> source(g_file_new_for_path(path.c_str()));
    std::unique_ptr<GXPSFile, Unref> file(gxps_file_new(source.get(), &failure.value));
    if (!file) {
        error = failure.message("cannot decode DWFx/XPS package");
        return {};
    }
    const auto documents = gxps_file_get_n_documents(file.get());
    if (documents > 10000) {
        error = "DWFx exceeds the document count limit";
        return {};
    }
    std::unique_ptr<GXPSPage, Unref> page;
    unsigned count = 0;
    for (unsigned i = 0; i < documents; ++i) {
        std::unique_ptr<GXPSDocument, Unref> doc(
            gxps_file_get_document(file.get(), i, &failure.value));
        if (!doc) {
            error = failure.message("cannot read DWFx document");
            return {};
        }
        const auto pages = gxps_document_get_n_pages(doc.get());
        if (pages > 100000 - count) {
            error = "DWFx exceeds the page count limit";
            return {};
        }
        if (unsigned(number) > count && unsigned(number) <= count + pages) {
            page.reset(
                gxps_document_get_page(doc.get(), unsigned(number) - count - 1, &failure.value));
            if (!page) {
                error = failure.message("cannot read DWFx page");
                return {};
            }
        }
        count += pages;
    }
    if (!page) {
        error = "DWFx page unavailable";
        return {};
    }
    double w = 0, h = 0;
    gxps_page_get_size(page.get(), &w, &h);
    if (!std::isfinite(w) || !std::isfinite(h) || w <= 0 || h <= 0) {
        error = "invalid DWFx page dimensions";
        return {};
    }
    RasterImage out;
    out.pages = int(count);
    out.metres_w = w * .0254 / 96;
    out.metres_h = h * .0254 / 96;
    const double factor = double(limit) / std::max(w, h);
    out.width = std::max(1, int(std::ceil(w * factor)));
    out.height = std::max(1, int(std::ceil(h * factor)));
    std::unique_ptr<cairo_surface_t, decltype(&cairo_surface_destroy)> surface(
        cairo_image_surface_create(CAIRO_FORMAT_ARGB32, out.width, out.height),
        cairo_surface_destroy);
    if (cairo_surface_status(surface.get()) != CAIRO_STATUS_SUCCESS) {
        error = "cannot allocate DWFx bitmap";
        return {};
    }
    std::unique_ptr<cairo_t, decltype(&cairo_destroy)> ctx(cairo_create(surface.get()),
                                                           cairo_destroy);
    cairo_set_source_rgb(ctx.get(), 1, 1, 1);
    cairo_paint(ctx.get());
    cairo_scale(ctx.get(), double(out.width) / w, double(out.height) / h);
    if (!gxps_page_render(page.get(), ctx.get(), &failure.value)) {
        error = failure.message("cannot render DWFx page");
        return {};
    }
    if (cairo_status(ctx.get()) != CAIRO_STATUS_SUCCESS) {
        error = cairo_status_to_string(cairo_status(ctx.get()));
        return {};
    }
    cairo_surface_flush(surface.get());
    const auto *data = cairo_image_surface_get_data(surface.get());
    const auto stride = cairo_image_surface_get_stride(surface.get());
    out.rgba.resize(std::size_t(out.width) * out.height * 4);
    for (int y = 0; y < out.height; ++y)
        for (int x = 0; x < out.width; ++x) {
            std::uint32_t pixel;
            std::memcpy(&pixel, data + std::size_t(y) * stride + x * 4, 4);
            auto *p = out.rgba.data() + (std::size_t(y) * out.width + x) * 4;
            p[3] = pixel >> 24;
            for (int c = 0; c < 3; ++c)
                p[c] =
                    p[3] ? std::min(255u, (((pixel >> (16 - 8 * c)) & 255) * 255 + p[3] / 2) / p[3])
                         : 0;
        }
    return out;
}
}
