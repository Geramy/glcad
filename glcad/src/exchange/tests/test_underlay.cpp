#include "exchange/pdf.hpp"
#include "exchange/underlay_image.hpp"
#include "test.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#ifdef GLCAD_PORTABLE_UNDERLAYS
#include <cstdio>
#include <jpeglib.h>
#include <tiffio.h>
#endif

using namespace gplot;
namespace {
struct Scratch {
    std::filesystem::path dir =
        std::filesystem::temp_directory_path() /
        ("glcad-underlay-test-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Scratch() { std::filesystem::create_directories(dir); }
    ~Scratch() {
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }
    std::string path(const char *name) const { return (dir / name).string(); }
};
}
TEST(underlay_snap_boundaries_follow_placement_and_regular_or_inverted_clips) {
    gcm::WorkingVolume volume;
    volume.extent = 500;
    volume.resolution_bits = 32;
    RasterImage image;
    image.width = image.height = 1;
    image.rgba = {255, 255, 255, 255};
    image.metres_w = 4;
    image.metres_h = 2;
    gdoc::Underlay u;
    u.insertion = gcm::to_lattice(volume, 10, 20);
    u.scale = 2;
    u.rotation = std::acos(-1.) / 2;
    auto shapes = underlay_snap_boundary(u, image, volume);
    CHECK(shapes.size() == 4);
    if (shapes.size() != 4)
        return;
    double x, y;
    gcm::to_world(volume, shapes[0].a, x, y);
    CHECK(std::abs(x - 10) < 1e-5 && std::abs(y - 20) < 1e-5);
    gcm::to_world(volume, shapes[0].b, x, y);
    CHECK(std::abs(x - 10) < 1e-5 && std::abs(y - 28) < 1e-5);
    u.rotation = 0;
    u.scale = 1;
    u.insertion = {};
    u.clip = {gcm::to_lattice(volume, 1, .5), gcm::to_lattice(volume, 3, .5),
              gcm::to_lattice(volume, 3, 1.5), gcm::to_lattice(volume, 1, 1.5)};
    shapes = underlay_snap_boundary(u, image, volume);
    CHECK(shapes.size() == 4);
    for (const auto &sh : shapes) {
        gcm::to_world(volume, sh.a, x, y);
        CHECK(x >= 1 - 1e-5 && x <= 3 + 1e-5 && y >= .5 - 1e-5 && y <= 1.5 + 1e-5);
    }
    u.clip_inverted = true;
    CHECK(underlay_snap_boundary(u, image, volume).size() == 8);
    u.clip = {gcm::to_lattice(volume, -1, -1), gcm::to_lattice(volume, 2, -1),
              gcm::to_lattice(volume, 2, 3), gcm::to_lattice(volume, -1, 3)};
    shapes = underlay_snap_boundary(u, image, volume);
    CHECK(shapes.size() == 4);
    for (const auto &sh : shapes) {
        gcm::to_world(volume, sh.a, x, y);
        CHECK(x >= 2 - 1e-5 && x <= 4 + 1e-5 && y >= -1e-5 && y <= 2 + 1e-5);
    }
    u.visible = false;
    CHECK(underlay_snap_boundary(u, image, volume).empty());
    u.visible = true;
    u.fade = 100;
    CHECK(underlay_snap_boundary(u, image, volume).empty());
}
#if defined(__APPLE__) || defined(GLCAD_PORTABLE_UNDERLAYS)
TEST(pdf_search_text_does_not_change_printed_pixels) {
    Scratch files;
    PdfWriter visible;
    visible.begin_page(.2,.1);
    PdfPath ink;ink.points={{.02,.02},{.08,.02},{.08,.07},{.02,.07}};
    ink.closed=ink.filled=true;ink.color={0,0,1};visible.add(ink);
    PdfWriter searchable=visible;
    PdfText label;label.x=.03;label.y=.04;label.height_mm=12;
    label.text="Searchable drawing note";label.invisible=true;searchable.add(label);
    visible.end_page();searchable.end_page();
    const auto plain=files.path("visible.pdf"),text=files.path("searchable.pdf");
    CHECK(visible.write_file(plain));CHECK(searchable.write_file(text));
    std::string error;
    const auto a=load_underlay(plain,1,error,400),b=load_underlay(text,1,error,400);
    CHECK(a.valid());CHECK(b.valid());CHECK(a.width==b.width&&a.height==b.height);
    CHECK(a.rgba==b.rgba);
    CHECK(searchable.build().find("(Searchable drawing note) Tj")!=std::string::npos);
}

TEST(underlay_decoder_reads_png_alpha_and_bounds_thumbnail_allocation) {
    Scratch files;
    const unsigned char png[] = {137, 80,  78,  71,  13,  10,  26,  10,  0,   0,  0,  13, 73,  72,
                                 68,  82,  0,   0,   0,   2,   0,   0,   0,   2,  8,  6,  0,   0,
                                 0,   114, 182, 13,  36,  0,   0,   0,   23,  73, 68, 65, 84,  120,
                                 156, 99,  248, 207, 192, 208, 192, 240, 31,  8,  25, 24, 254, 3,
                                 1,   3,   3,   0,   63,  217, 7,   122, 204, 16, 7,  68, 0,   0,
                                 0,   0,   73,  69,  78,  68,  174, 66,  96,  130};
    auto path = files.path("scan.data");
    {
        std::ofstream f(path, std::ios::binary);
        f.write(reinterpret_cast<const char *>(png), sizeof(png));
    }
    std::string error;
    auto image = load_underlay(path, 1, error, 64);
    CHECK_MSG(image.valid(), "%s", error.c_str());
    if (!image.valid())
        return;
    CHECK(image.width == 2 && image.height == 2);
    CHECK(image.rgba[0] > 250 && image.rgba[3] == 128);
    CHECK(image.rgba[10] > 250 && image.rgba[15] == 0);
    CHECK(std::abs(image.metres_w - 2 * .0254 / 96) < 1e-8);
    image = load_underlay(path, 1, error, 1);
    CHECK(image.valid() && image.width == 1 && image.height == 1);
    CHECK(!load_underlay(path, 2, error).valid());
    CHECK(!load_underlay(path, 0, error).valid());
    CHECK(!load_underlay(path, 1, error, 4097).valid());
    {
        std::ofstream f(path, std::ios::binary);
        f << "broken";
    }
    CHECK(!load_underlay(path, 1, error).valid());
    CHECK(!error.empty());
}
TEST(underlay_decoder_pdf_pages_preserve_physical_size_and_top_row_order) {
    Scratch files;
    auto path = files.path("pages.pdf");
    PdfWriter writer;
    writer.begin_page(.2, .1);
    PdfPath rect;
    rect.points = {{0, .05}, {.1, .05}, {.1, .1}, {0, .1}};
    rect.closed = true;
    rect.filled = true;
    rect.color = {1, 0, 0};
    writer.add(rect);
    writer.end_page();
    writer.begin_page(.1, .2);
    writer.end_page();
    {
        std::ofstream f(path, std::ios::binary);
        f << writer.build();
    }
    std::string error;
    auto image = load_underlay(path, 1, error, 200);
    CHECK_MSG(image.valid(), "%s", error.c_str());
    if (!image.valid())
        return;
    CHECK(image.pages == 2);
    CHECK(std::abs(image.metres_w - .2) < 1e-6 && std::abs(image.metres_h - .1) < 1e-6);
    auto at = [&](int x, int y, int c) {
        return image.rgba[(std::size_t(y) * image.width + x) * 4 + c];
    };
    CHECK(at(25, 25, 0) > 245 && at(25, 25, 1) < 10);
    CHECK(at(25, 75, 1) > 245);
    image = load_underlay(path, 2, error, 200);
    CHECK(image.valid() && image.height > image.width);
    CHECK(!load_underlay(path, 3, error, 200).valid());
}
TEST(underlay_pdf_crop_box_and_page_rotation_set_raster_size) {
    Scratch files;
    const auto path = files.path("rotated.pdf");
    const std::vector<std::string> objects = {
        "<< /Type /Catalog /Pages 2 0 R >>", "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] /CropBox [10 20 110 70] /Rotate 90 "
        "/Resources << >> /Contents 4 0 R >>",
        "<< /Length 0 >>\nstream\nendstream"};
    std::string bytes = "%PDF-1.4\n";
    std::vector<std::size_t> offsets;
    for (std::size_t i = 0; i < objects.size(); ++i) {
        offsets.push_back(bytes.size());
        bytes += std::to_string(i + 1) + " 0 obj\n" + objects[i] + "\nendobj\n";
    }
    const auto xref = bytes.size();
    bytes += "xref\n0 5\n0000000000 65535 f \n";
    for (auto offset : offsets) {
        char row[32];
        std::snprintf(row, sizeof(row), "%010zu 00000 n \n", offset);
        bytes += row;
    }
    bytes += "trailer\n<< /Size 5 /Root 1 0 R >>\nstartxref\n" + std::to_string(xref) + "\n%%EOF\n";
    {
        std::ofstream f(path, std::ios::binary);
        f << bytes;
    }
    std::string error;
    const auto image = load_underlay(path, 1, error, 200);
    CHECK_MSG(image.valid(), "%s", error.c_str());
    CHECK(image.width == 100 && image.height == 200);
    CHECK(std::abs(image.metres_w - 50 * .0254 / 72) < 1e-9 &&
          std::abs(image.metres_h - 100 * .0254 / 72) < 1e-9);
}
#endif
#ifdef GLCAD_PORTABLE_UNDERLAYS
TEST(portable_underlay_jpeg_density_and_tiff_pages_alpha_orientation) {
    Scratch files;
    std::string error;
    const auto jpg = files.path("scan.jpg");
    FILE *f = std::fopen(jpg.c_str(), "wb");
    CHECK(f != nullptr);
    if (!f)
        return;
    jpeg_compress_struct c{};
    jpeg_error_mgr e{};
    c.err = jpeg_std_error(&e);
    jpeg_create_compress(&c);
    jpeg_stdio_dest(&c, f);
    c.image_width = 8;
    c.image_height = 4;
    c.input_components = 3;
    c.in_color_space = JCS_RGB;
    jpeg_set_defaults(&c);
    c.density_unit = 1;
    c.X_density = 200;
    c.Y_density = 100;
    jpeg_set_quality(&c, 100, TRUE);
    jpeg_start_compress(&c, TRUE);
    const unsigned char exif[] = {'E',  'x', 'i', 'f', 0, 0, 'I', 'I', 42, 0, 8, 0, 0, 0, 1, 0,
                                  0x12, 1,   3,   0,   1, 0, 0,   0,   6,  0, 0, 0, 0, 0, 0, 0};
    jpeg_write_marker(&c, JPEG_APP0 + 1, exif, sizeof(exif));
    unsigned char row[24];
    for (int i = 0; i < 8; ++i) {
        row[3 * i] = 255;
        row[3 * i + 1] = row[3 * i + 2] = 0;
    }
    while (c.next_scanline < c.image_height) {
        JSAMPROW r = row;
        jpeg_write_scanlines(&c, &r, 1);
    }
    jpeg_finish_compress(&c);
    jpeg_destroy_compress(&c);
    std::fclose(f);
    auto image = load_underlay(jpg, 1, error, 4);
    CHECK_MSG(image.valid(), "%s", error.c_str());
    CHECK(image.width == 2 && image.height == 4);
    CHECK(std::abs(image.metres_w - 4 * .0254 / 100) < 1e-9);
    CHECK(std::abs(image.metres_h - 8 * .0254 / 200) < 1e-9);
    if (image.valid())
        CHECK(image.rgba[0] > 245 && image.rgba[1] < 10);
    const auto tif = files.path("scan.tif");
    auto *t = TIFFOpen(tif.c_str(), "w");
    CHECK(t != nullptr);
    if (!t)
        return;
    for (int page = 0; page < 2; ++page) {
        TIFFSetField(t, TIFFTAG_IMAGEWIDTH, 2);
        TIFFSetField(t, TIFFTAG_IMAGELENGTH, 3);
        TIFFSetField(t, TIFFTAG_SAMPLESPERPIXEL, 4);
        TIFFSetField(t, TIFFTAG_BITSPERSAMPLE, 8);
        TIFFSetField(t, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
        TIFFSetField(t, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB);
        TIFFSetField(t, TIFFTAG_ORIENTATION, page ? ORIENTATION_RIGHTTOP : ORIENTATION_TOPLEFT);
        TIFFSetField(t, TIFFTAG_XRESOLUTION, 100.);
        TIFFSetField(t, TIFFTAG_YRESOLUTION, 200.);
        TIFFSetField(t, TIFFTAG_RESOLUTIONUNIT, RESUNIT_INCH);
        std::uint16_t extra = EXTRASAMPLE_UNASSALPHA;
        TIFFSetField(t, TIFFTAG_EXTRASAMPLES, 1, &extra);
        unsigned char pixels[3][8] = {{255, 0, 0, 128, 0, 255, 0, 255},
                                      {0, 0, 255, 255, 0, 0, 0, 0},
                                      {255, 255, 0, 255, 255, 0, 255, 255}};
        for (int y = 0; y < 3; ++y)
            CHECK(TIFFWriteScanline(t, pixels[y], y, 0) >= 0);
        CHECK(TIFFWriteDirectory(t));
    }
    TIFFClose(t);
    image = load_underlay(tif, 1, error, 64);
    CHECK_MSG(image.valid(), "%s", error.c_str());
    if (image.valid()) {
        CHECK(image.pages == 2 && image.width == 2 && image.height == 3);
        CHECK(image.rgba[0] > 250 && image.rgba[3] == 128);
        CHECK(image.rgba[10] > 250);
    }
    image = load_underlay(tif, 2, error, 64);
    CHECK(image.valid() && image.width == 3 && image.height == 2);
    CHECK(std::abs(image.metres_w - 3 * .0254 / 200) < 1e-8 &&
          std::abs(image.metres_h - 2 * .0254 / 100) < 1e-8);
    // Orientation 6 rotates stored rows clockwise: bottom-left is now top-left.
    if (image.valid())
        CHECK(image.rgba[0] > 250 && image.rgba[1] > 250 && image.rgba[2] < 3);
    CHECK(!load_underlay(tif, 3, error).valid());
}
#endif

#ifdef GLCAD_XPS_UNDERLAYS
#include "exchange/dwf.hpp"
TEST(dwfx_underlay_renders_embedded_font_geometry_and_page_order) {
    Scratch files;
    const auto path = files.path("issued.dwfx");
    DwfWriter writer;
    writer.begin_page("Plan", .2, .1);
    PdfPath rect;
    rect.points = {{.01, .06}, {.08, .06}, {.08, .09}, {.01, .09}};
    rect.closed = true;
    rect.filled = true;
    rect.color = {1, 0, 0};
    writer.add(rect);
    PdfText text;
    text.x = .01;
    text.y = .02;
    text.text = "DWFx text";
    text.height_mm = 10;
    writer.add(text);
    writer.end_page();
    writer.begin_page("Detail", .1, .2);
    writer.end_page();
    const auto bytes = writer.build();
    std::vector<gcm::u8> font;
    CHECK(unzip_entry(bytes, "Documents/1/Resources/Fonts/default.ttf", font));
    CHECK(font.size() > 100000);
    {
        std::ofstream f(path, std::ios::binary);
        f.write(reinterpret_cast<const char *>(bytes.data()), bytes.size());
    }
    std::string error;
    auto image = load_underlay(path, 1, error, 400);
    CHECK_MSG(image.valid(), "%s", error.c_str());
    if (!image.valid())
        return;
    CHECK(image.pages == 2);
    CHECK(std::abs(image.metres_w - .2) < 1e-6 && std::abs(image.metres_h - .1) < 1e-6);
    const auto pixel = [&](int x, int y, int c) {
        return image.rgba[(std::size_t(y) * image.width + x) * 4 + c];
    };
    CHECK(pixel(80, 40, 0) > 245 && pixel(80, 40, 1) < 10);
    unsigned ink = 0;
    for (int y = 130; y < 170; ++y)
        for (int x = 15; x < 230; ++x)
            if (pixel(x, y, 0) < 100 && pixel(x, y, 1) < 100 && pixel(x, y, 2) < 100)
                ++ink;
    CHECK(ink > 100);
    image = load_underlay(path, 2, error, 400);
    CHECK(image.valid() && image.height > image.width);
    CHECK(!load_underlay(path, 3, error, 400).valid());
    std::vector<ZipEntry> missing;
    for (const auto &name : zip_names(bytes))
        if (name != "Documents/1/Resources/Fonts/default.ttf") {
            ZipEntry entry;
            entry.name = name;
            CHECK(unzip_entry(bytes, name, entry.data));
            missing.push_back(std::move(entry));
        }
    const auto broken = zip(missing);
    {
        std::ofstream f(path, std::ios::binary);
        f.write(reinterpret_cast<const char *>(broken.data()), broken.size());
    }
    CHECK(!load_underlay(path, 1, error, 400).valid());
    CHECK(!error.empty());
}
#endif
#ifdef GLCAD_XPS_UNDERLAYS
TEST(dwfx_image_resources_preserve_alpha_rotation_clipping_and_draw_order) {
    Scratch files;
    const auto path = files.path("scans.dwfx");
    auto raster = std::make_shared<RasterImage>();
    raster->width = 2;
    raster->height = 2;
    raster->rgba = {255, 0, 0, 128, 0, 255, 0, 255, 0, 0, 255, 255, 0, 0, 0, 0};
    DwfWriter writer;
    writer.begin_page("Scanned plan", .2, .1);
    PdfImage image;
    image.raster = raster;
    image.origin = {.02, .02};
    image.x_axis = {.04, 0};
    image.y_axis = {0, .04};
    image.clip = PaperClip{{.02, .02}, {.04, .06}};
    writer.add(image);
    image.origin = {.14, .02};
    image.x_axis = {0, .04};
    image.y_axis = {-.04, 0};
    image.clip.reset();
    writer.add(image);
    PdfPath line;
    line.points = {{.02, .04}, {.06, .04}};
    line.width_mm = 1;
    writer.add(line);
    writer.end_page();
    CHECK(writer.write_file(path));
    std::string error;
    const auto out = load_underlay(path, 1, error, 400);
    CHECK_MSG(out.valid(), "%s", error.c_str());
    if (!out.valid())
        return;
    auto at = [&](int x, int y, int c) {
        return out.rgba[(std::size_t(y) * out.width + x) * 4 + c];
    };
    CHECK(at(60, 100, 0) > 245 && at(60, 100, 1) > 110 && at(60, 100, 1) < 145);
    CHECK(at(60, 140, 2) > 245 && at(60, 140, 0) < 10);
    CHECK(at(100, 100, 0) > 245 && at(100, 100, 1) > 245 && at(100, 100, 2) > 245);
    CHECK(at(60, 120, 0) < 10 && at(60, 120, 2) < 10);
    CHECK(at(220, 140, 0) > 245 && at(220, 140, 1) > 110 && at(220, 140, 1) < 145);
    CHECK(at(260, 140, 2) > 245 && at(260, 140, 0) < 10);
    std::vector<gcm::u8> png;
    CHECK(unzip_entry(writer.build(), "Documents/1/Resources/Images/1_1.png", png));
    CHECK(png.size() > 8 && png[0] == 137 && png[1] == 'P');
}
#endif

#ifdef GLCAD_PORTABLE_UNDERLAYS
TEST(pdf_printer_tiles_match_full_page_pixels_and_bound_allocations) {
    Scratch scratch;const auto path=(scratch.dir/"printer-tiles.pdf").string();
    constexpr double point=.0254/72;
    PdfWriter pdf;pdf.begin_page(128*point,96*point);
    PdfPath shape;shape.closed=true;shape.filled=true;shape.color={1,0,0};
    shape.points={{10*point,8*point},{100*point,20*point},{80*point,85*point}};pdf.add(shape);pdf.end_page();
    pdf.begin_page(96*point,128*point);pdf.end_page();CHECK(pdf.write_file(path));
    std::string error;auto source=open_pdf_raster(path,error);CHECK_MSG(bool(source),"%s",error.c_str());if(!source)return;
    CHECK(source->page_count()==2);
    auto [w,h]=source->page_size(2,error);CHECK(std::abs(w-96*point)<1e-8);CHECK(std::abs(h-128*point)<1e-8);
    const auto full=source->tile(1,144,144,0,0,256,192,error);CHECK_MSG(full.valid(),"%s",error.c_str());if(!full.valid())return;
    for(int y=0;y<192;y+=32)for(int x=0;x<256;x+=32) {
        const auto tile=source->tile(1,144,144,x,y,32,32,error);CHECK_MSG(tile.valid(),"%s",error.c_str());if(!tile.valid())continue;
        int differences=0,max_delta=0,first_col=-1,first_row=-1;
        bool same_coverage=true;
        for(int row=0;row<32;++row)for(int col=0;col<32;++col)for(int c=0;c<4;++c)
            if(const int delta=std::abs(int(tile.rgba[(row*32+col)*4+c])-int(full.rgba[((y+row)*256+x+col)*4+c]));delta) {
                ++differences;max_delta=std::max(max_delta,delta);
                if(first_col<0){first_col=col;first_row=row;}
                const auto b=full.rgba[((y+row)*256+x+col)*4+c];
                bool on_edge=false;
                for(int yy=std::max(0,y+row-1);yy<=std::min(191,y+row+1);++yy)
                    for(int xx=std::max(0,x+col-1);xx<=std::min(255,x+col+1);++xx)
                        if(full.rgba[(yy*256+xx)*4+c]!=b)on_edge=true;
                if(!on_edge)same_coverage=false;
            }
        // Older Poppler quantizes edge coverage differently; allow one step at edges.
        CHECK_MSG(same_coverage && max_delta<=16,"tile at %d,%d differs in %d channels, max %d, first at %d,%d",x,y,differences,max_delta,first_col,first_row);
    }
    CHECK(!source->tile(0,144,144,0,0,32,32,error).valid());
    CHECK(!source->tile(1,144,144,0,0,8192,32,error).valid());
    CHECK(!source->tile(1,144,144,250,0,32,32,error).valid());
    CHECK(!source->tile(1,0,144,0,0,32,32,error).valid());
    const auto detail=source->tile(1,2400,2400,2048,1024,1024,1024,error);
    CHECK_MSG(detail.valid(),"%s",error.c_str());CHECK(detail.width==1024);
}
#endif

TEST(pdf_and_dwfx_vector_opacity_composites_without_rasterizing_geometry) {
    Scratch files;PdfWriter pdf;pdf.begin_page(.1,.1);
    const auto rectangle=[&](double left,double right,PdfColor color) {
        PdfPath path;path.closed=path.filled=true;path.color=color;
        path.points={{left,.01},{right,.01},{right,.09},{left,.09}};pdf.add(path);
    };
    rectangle(.01,.09,{0,0,1});rectangle(.01,.05,{1,0,0,.5});
    rectangle(.05,.07,{0,1,0,0});rectangle(.07,.09,{1,1,0,1});pdf.end_page();
    CHECK(pdf.write_file(files.path("alpha.pdf")));
    CHECK(pdf.build().find("/ExtGState")!=std::string::npos);
    std::vector<std::string> paths{files.path("alpha.pdf")};
#ifdef GLCAD_XPS_UNDERLAYS
    DwfWriter dwf;dwf.begin_page("Opacity",.1,.1);
    for(const auto& path:pdf.paths_of_page(0))dwf.add(path);dwf.end_page();
    CHECK(dwf.write_file(files.path("alpha.dwfx")));paths.push_back(files.path("alpha.dwfx"));
#endif
    for(const auto& path:paths) {
        std::string error;const auto image=load_underlay(path,1,error,200);
        CHECK_MSG(image.valid(),"%s: %s",path.c_str(),error.c_str());if(!image.valid())continue;
        const auto pixel=[&](double x,int c){return image.rgba[(std::size_t(image.height/2)*image.width+int(x*image.width))*4+c];};
        CHECK(std::abs(int(pixel(.3,0))-128)<=2);CHECK(pixel(.3,1)<2);CHECK(std::abs(int(pixel(.3,2))-128)<=2);
        CHECK(pixel(.6,0)<2);CHECK(pixel(.6,1)<2);CHECK(pixel(.6,2)>253);
        CHECK(pixel(.8,0)>253);CHECK(pixel(.8,1)>253);CHECK(pixel(.8,2)<2);
    }
}

TEST(reference_image_snaps_include_cut_edges_but_not_internal_mesh_diagonals) {
    gcm::WorkingVolume volume;volume.extent=500;volume.resolution_bits=40;
    RasterImage image;image.width=image.height=1;image.rgba={255,255,255,255};image.metres_w=image.metres_h=4;
    gdoc::Underlay underlay;
    const auto p=[&](double x,double y){return gcm::to_lattice(volume,x,y);};
    const std::array<double,6> transform{0,-2,10,3,0,20};
    const std::vector<gclip::Boundary> clips={{{p(3,21),p(9,31)},false},{{p(5,24),p(7,28)},true}};
    const auto edges=underlay_snap_boundary(underlay,image,volume,transform,clips);
    CHECK(edges.size()==8);double perimeter=0;
    for(const auto& edge:edges) {
        double ax,ay,bx,by;gcm::to_world(volume,edge.a,ax,ay);gcm::to_world(volume,edge.b,bx,by);
        CHECK(std::abs(ax-bx)<1e-8||std::abs(ay-by)<1e-8);
        perimeter+=std::hypot(ax-bx,ay-by);
    }
    CHECK(std::abs(perimeter-44)<1e-7);
}

#if defined(__APPLE__) || defined(GLCAD_PORTABLE_UNDERLAYS)
TEST(pdf_and_dwfx_reference_image_and_text_masks_render_nested_inverted_holes) {
    Scratch files;PdfWriter pdf;pdf.begin_page(.1,.1);
    auto raster=std::make_shared<RasterImage>();raster->width=raster->height=100;raster->rgba.resize(40000);
    for(std::size_t i=0;i<raster->rgba.size();i+=4){raster->rgba[i]=255;raster->rgba[i+3]=255;}
    PdfImage image;image.raster=raster;image.origin={0,0};image.x_axis={.1,0};image.y_axis={0,.1};
    image.clips={{{{.02,.02},{.08,.08}},false},{{{.04,.04},{.06,.06}},true}};
    pdf.add(image);
    PdfText text;text.text="MMMMMMMMMM";text.x=.01;text.y=.07;text.height_mm=10;text.color={0,0,1};
    text.clips={{{{.02,.065},{.08,.09}},false},{{{.04,.065},{.06,.09}},true}};pdf.add(text);pdf.end_page();
    CHECK(pdf.write_file(files.path("clipped.pdf")));std::vector<std::string> paths{files.path("clipped.pdf")};
#ifdef GLCAD_XPS_UNDERLAYS
    DwfWriter dwf;dwf.begin_page("Clipped",.1,.1);
    for(const auto& im:pdf.images_of_page(0))dwf.add(im);
    for(const auto& t:pdf.texts_of_page(0))dwf.add(t);
    dwf.end_page();CHECK(dwf.write_file(files.path("clipped.dwfx")));paths.push_back(files.path("clipped.dwfx"));
#endif
    for(const auto& path:paths) {
        std::string error;const auto decoded=load_underlay(path,1,error,400);
        CHECK_MSG(decoded.valid(),"%s: %s",path.c_str(),error.c_str());if(!decoded.valid())continue;
        const auto pixel=[&](double x,double y,int c){return decoded.rgba[(std::size_t((1-y)*decoded.height)*decoded.width+std::size_t(x*decoded.width))*4+c];};
        CHECK(pixel(.3,.5,0)>250);CHECK(pixel(.3,.5,1)<5);CHECK(pixel(.3,.5,2)<5);
        CHECK(pixel(.5,.5,0)>250);CHECK(pixel(.5,.5,1)>250);CHECK(pixel(.5,.5,2)>250);
        CHECK(pixel(.1,.5,0)>250);CHECK(pixel(.1,.5,1)>250);CHECK(pixel(.1,.5,2)>250);
        int blue_left=0,blue_hole=0;
        for(int y=280;y<330;++y)for(int x=80;x<240;++x) {
            const double px=(x+.5)/400,py=(y+.5)/400;
            if(pixel(px,py,2)>150&&pixel(px,py,0)<100&&pixel(px,py,1)<100) {
                if(x<160)++blue_left;else ++blue_hole;
            }
        }
        CHECK(blue_left>20);CHECK(blue_hole==0);
    }
}
#endif

TEST(underlays_with_unresolved_exchange_coordinates_are_retained_without_guessed_rendering) {
    gdoc::Underlay u;u.kind=gdoc::UnderlayKind::Dwf;u.path="page.dwfx";u.source_coordinates_pending=true;
    gcm::WorkingVolume volume;std::string error;
    CHECK(!load_underlay(u,volume,error).valid());CHECK(error.find("unit/page")!=std::string::npos);
    RasterImage raster;raster.width=raster.height=1;raster.rgba={255,255,255,255};raster.metres_w=raster.metres_h=1;
    CHECK(underlay_snap_boundary(u,raster,volume).empty());
}

#if defined(GLCAD_PORTABLE_UNDERLAYS) || defined(__APPLE__)
TEST(underlay_pdf_memory_render_matches_file_and_rejects_invalid_pages) {
    Scratch scratch;PdfWriter writer;writer.begin_page(.2,.1);
    PdfPath path;path.filled=true;path.closed=true;path.color={0,0,1};path.points={{0,.05},{.1,.05},{.1,.1},{0,.1}};writer.add(path);writer.end_page();
    writer.begin_page(.1,.2);writer.end_page();const auto bytes=writer.build();
    const auto file=scratch.path("memory.pdf");{std::ofstream out(file,std::ios::binary);out<<bytes;}
    std::string error;const auto memory=rasterize_pdf(bytes,1,error,200),disk=load_underlay(file,1,error,200);
    CHECK_MSG(memory.valid(),"%s",error.c_str());CHECK(disk.valid());CHECK(memory.width==disk.width);CHECK(memory.height==disk.height);CHECK(memory.rgba==disk.rgba);
    CHECK(memory.pages==2);CHECK(std::abs(memory.metres_w-.2)<1e-6);CHECK(std::abs(memory.metres_h-.1)<1e-6);
    CHECK(rasterize_pdf(bytes,2,error,200).valid());CHECK(!rasterize_pdf(bytes,3,error,200).valid());
    CHECK(!rasterize_pdf(bytes,0,error,200).valid());CHECK(!rasterize_pdf(bytes,1,error,4097).valid());CHECK(!rasterize_pdf("invalid",1,error).valid());
}
#endif
TEST(underlay_offset_footprint_uses_source_origin_before_nested_transform) {
    gcm::WorkingVolume volume;volume.extent=500;volume.resolution_bits=32;
    RasterImage image;image.width=image.height=1;image.rgba={255,255,255,255};image.metres_x=-3;image.metres_y=7;image.metres_w=4;image.metres_h=2;
    gdoc::Underlay u;u.scale=2;u.y_factor=-1;u.rotation=std::acos(-1.)/2;u.insertion=gcm::to_lattice(volume,10,20);
    const auto snaps=underlay_snap_boundary(u,image,volume,{1,0,3,0,1,-4});CHECK(snaps.size()==4);
    if(snaps.size()!=4)return;double x,y;gcm::to_world(volume,snaps.front().a,x,y);CHECK(std::abs(x-27)<1e-5&&std::abs(y-10)<1e-5);
    image.metres_x=1000;CHECK(underlay_snap_boundary(u,image,volume).empty());
}

#if defined(GLCAD_PORTABLE_UNDERLAYS) || defined(__APPLE__)
TEST(compound_fill_contours_have_no_internal_seams_or_extra_strokes) {
    Scratch scratch;PdfWriter pdf;pdf.begin_page(.1,.1);
    PdfPath background;background.filled=true;background.closed=true;background.color={0,0,1};background.points={{0,0},{.1,0},{.1,.1},{0,.1}};pdf.add(background);
    PdfPath fill;fill.filled=true;fill.closed=true;fill.color={1,0,0,.5};fill.width_mm=10;
    fill.points={{.02,.02},{.08,.02},{.08,.08}};fill.subpaths={{{.02,.02},{.08,.08},{.02,.08}}};pdf.add(fill);pdf.end_page();
    CHECK(pdf.paths_of_page(0).back().subpaths.size()==1);
    const auto pdf_path=scratch.path("compound.pdf");CHECK(pdf.write_file(pdf_path));std::vector<std::string> files{pdf_path};
#ifdef GLCAD_XPS_UNDERLAYS
    DwfWriter dwf;dwf.begin_page("Compound",.1,.1);for(const auto& p:pdf.paths_of_page(0))dwf.add(p);dwf.end_page();
    const auto dwfx_path=scratch.path("compound.dwfx");CHECK(dwf.write_file(dwfx_path));files.push_back(dwfx_path);
#endif
    for(const auto& path:files) {
        std::string error;const auto image=load_underlay(path,1,error,100);CHECK_MSG(image.valid(),"%s",error.c_str());if(!image.valid())continue;
        bool uniform=true;
        for(int y=30;y<70;++y)for(int x=30;x<70;++x) {
            const auto i=(std::size_t(y)*image.width+x)*4;
            uniform&=std::abs(int(image.rgba[i])-128)<=2&&image.rgba[i+1]<2&&std::abs(int(image.rgba[i+2])-127)<=2;
        }
        CHECK_MSG(uniform,"fill seams or overlapping opacity in %s",path.c_str());
        const auto outside=(std::size_t(50)*image.width+18)*4;CHECK(image.rgba[outside]<2&&image.rgba[outside+2]>253);
    }
}
#endif

#if defined(GLCAD_PORTABLE_UNDERLAYS) || defined(__APPLE__)
TEST(compound_even_odd_holes_match_pdf_and_dwfx) {
    Scratch scratch;PdfWriter pdf;pdf.begin_page(.1,.1);
    PdfPath background;background.filled=true;background.closed=true;background.color={0,0,1};background.points={{0,0},{.1,0},{.1,.1},{0,.1}};pdf.add(background);
    PdfPath ring;ring.filled=true;ring.closed=true;ring.even_odd=true;ring.color={1,0,0};
    ring.points={{.01,.01},{.09,.01},{.09,.09},{.01,.09}};
    ring.subpaths={{{.04,.04},{.06,.04},{.06,.06},{.04,.06}}};pdf.add(ring);pdf.end_page();
    const auto pdf_path=scratch.path("ring.pdf");CHECK(pdf.write_file(pdf_path));std::vector<std::string> files{pdf_path};
#ifdef GLCAD_XPS_UNDERLAYS
    DwfWriter dwf;dwf.begin_page("Ring",.1,.1);for(const auto& p:pdf.paths_of_page(0))dwf.add(p);dwf.end_page();
    const auto dwfx_path=scratch.path("ring.dwfx");CHECK(dwf.write_file(dwfx_path));files.push_back(dwfx_path);
#endif
    for(const auto& path:files) {
        std::string error;const auto image=load_underlay(path,1,error,100);CHECK_MSG(image.valid(),"%s",error.c_str());if(!image.valid())continue;
        const auto hole=(std::size_t(50)*image.width+50)*4,solid=(std::size_t(70)*image.width+70)*4;
        CHECK(image.rgba[hole]<2&&image.rgba[hole+2]>253);CHECK(image.rgba[solid]>253&&image.rgba[solid+2]<2);
    }
}
#endif
