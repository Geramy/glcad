#include "test.hpp"

#include <cmath>
#include <cstdio>

#include "exchange/pdf.hpp"
#include "exchange/dwf.hpp"

using namespace gplot;

static gcm::WorkingVolume vol() {
    gcm::WorkingVolume v;
    v.extent = 500.0;
    v.resolution_bits = 32;
    return v;
}
static gsnap::Shape seg(const gcm::WorkingVolume& v, f64 x0, f64 y0, f64 x1, f64 y1) {
    gsnap::Shape s;
    s.kind = gsnap::ShapeKind::Segment;
    s.a = gcm::to_lattice(v, x0, y0);
    s.b = gcm::to_lattice(v, x1, y1);
    return s;
}

TEST(plot_standard_papers_are_present) {
    CHECK(find_paper("A4") != nullptr);
    CHECK(find_paper("A1") != nullptr);
    CHECK(find_paper("ANSI D") != nullptr);
    CHECK(find_paper("ARCH E") != nullptr);
    CHECK(find_paper("Nonexistent") == nullptr);

    const auto* a4 = find_paper("A4");
    const auto* a3 = find_paper("A3");
    const f64 ratio = (a3->width * a3->height) / (a4->width * a4->height);
    CHECK_MSG(std::fabs(ratio - 2.0) < 0.02, "A3/A4 area ratio %.4f", ratio);
}

TEST(plot_printable_area_excludes_margins) {
    PaperSize p = *find_paper("A4");
    CHECK(p.printable_width() < p.width);
    CHECK(std::fabs(p.printable_width() - (p.width - 0.020)) < 1e-9);
}

TEST(plot_orientation_swaps_the_effective_page) {
    PageSetup s;
    s.paper = *find_paper("A4");
    s.orientation = Orientation::Landscape;
    const f64 lw = s.effective_width(), lh = s.effective_height();
    s.orientation = Orientation::Portrait;
    CHECK(std::fabs(s.effective_width() - lh) < 1e-9);
    CHECK(std::fabs(s.effective_height() - lw) < 1e-9);
}

TEST(plot_ctb_has_255_entries) {
    const PlotStyleTable t(StyleMode::ColorDependent);
    CHECK(t.mode() == StyleMode::ColorDependent);
    CHECK(t.by_index(1) != nullptr);
    CHECK(t.by_index(255) != nullptr);

    CHECK(t.by_index(0) != nullptr);
    CHECK(t.by_index(9999) != nullptr);
}

TEST(plot_stb_maps_by_name) {
    PlotStyleTable t(StyleMode::Named);
    CHECK(t.find("Normal") != nullptr);

    PlotStyle heavy;
    heavy.name = "Heavy";
    heavy.use_object_lineweight = false;
    heavy.lineweight = gdoc::Lineweight::W100;
    t.add(heavy);

    CHECK(t.find("Heavy") != nullptr);
    CHECK(t.find("Heavy")->lineweight == gdoc::Lineweight::W100);
    CHECK(t.resolve(7, "Heavy").lineweight == gdoc::Lineweight::W100);
}

TEST(plot_monochrome_table_forces_black) {
    const PlotStyleTable t = PlotStyleTable::monochrome();
    for (i32 aci : {1, 5, 100, 255}) {
        const PlotStyle* s = t.by_index(aci);
        CHECK_MSG(!s->use_object_color, "ACI %d still uses its object colour", aci);
        CHECK(s->r == 0 && s->g == 0 && s->b == 0);
    }
}

TEST(plot_style_can_suppress_an_object) {
    PlotStyleTable t(StyleMode::ColorDependent);
    t.by_index(8).plot = false;
    CHECK(!t.resolve(8, "").plot);
    CHECK(t.resolve(7, "").plot);
}

TEST(plot_viewport_maps_model_to_paper_and_back) {
    Viewport v;
    v.lo = {0, 0};
    v.hi = {1000, 1000};
    v.centre_x = 50.0;
    v.centre_y = 20.0;
    v.scale = 100.0;              // 100 drawing units per paper unit

    f64 px, py;
    v.to_paper(50.0, 20.0, px, py);
    CHECK_MSG(std::fabs(px - 500.0) < 1e-6, "viewport centre maps to %.4f", px);
    CHECK(std::fabs(py - 500.0) < 1e-6);

    for (f64 mx : {0.0, 50.0, 120.0})
        for (f64 my : {-30.0, 20.0, 80.0}) {
            f64 a, b, ra, rb;
            v.to_paper(mx, my, a, b);
            v.to_model(a, b, ra, rb);
            CHECK(std::fabs(ra - mx) < 1e-6);
            CHECK(std::fabs(rb - my) < 1e-6);
        }
}

TEST(plot_viewport_scale_shrinks_the_drawing) {
    Viewport a, b;
    a.lo = b.lo = {0, 0};
    a.hi = b.hi = {1000, 1000};
    a.scale = 1.0;
    b.scale = 100.0;

    f64 ax, ay, bx, by;
    a.to_paper(10.0, 0.0, ax, ay);
    b.to_paper(10.0, 0.0, bx, by);
    CHECK_MSG(std::fabs(bx - 500.0) < std::fabs(ax - 500.0),
              "a larger scale should place the same point nearer the centre");
}

TEST(plot_viewport_freeze_hides_a_layer_on_one_sheet_only) {
    Viewport v;
    v.frozen_layers = {"A-GRID"};
    CHECK(!v.layer_visible("A-GRID"));
    CHECK(v.layer_visible("A-WALL"));
}

TEST(plot_viewport_rotation) {
    Viewport v;
    v.lo = {0, 0};
    v.hi = {200, 200};
    v.scale = 1.0;
    v.rotation = 3.14159265358979323846 * 0.5;

    f64 px, py;
    v.to_paper(1.0, 0.0, px, py);
    CHECK(std::fabs(px - 100.0) < 1e-6);
    CHECK_MSG(std::fabs(py - 101.0) < 1e-6, "rotated to (%.4f, %.4f)", px, py);
}

TEST(plot_sheet_set_add_find_remove_reorder) {
    SheetSet set;
    for (const char* n : {"A-101", "A-102", "A-103"}) {
        Sheet s;
        s.name = n;
        s.setup.paper = *find_paper("A1");
        set.add(s);
    }
    CHECK(set.size() == 3);
    CHECK(set.find("A-102") != nullptr);
    CHECK(set.find("A-999") == nullptr);

    CHECK(set.reorder("A-103", 0));
    CHECK(set.all()[0].name == "A-103");

    CHECK(set.remove("A-101"));
    CHECK(set.size() == 2);
    CHECK(!set.remove("A-101"));
}

TEST(pdf_produces_a_valid_document) {
    PdfWriter w;
    w.set_title("Test Sheet");
    w.begin_page(0.297, 0.210);

    PdfPath p;
    p.points = {{0.01, 0.01}, {0.28, 0.01}, {0.28, 0.20}, {0.01, 0.20}};
    p.closed = true;
    p.width_mm = 0.5;
    w.add(p);
    w.end_page();

    const std::string pdf = w.build();
    CHECK_MSG(pdf.rfind("%PDF-1.4", 0) == 0, "missing PDF header");
    CHECK_MSG(pdf.find("%%EOF") != std::string::npos, "missing EOF marker");
    CHECK_MSG(pdf.find("/Type /Catalog") != std::string::npos, "no catalog");
    CHECK_MSG(pdf.find("/Type /Pages") != std::string::npos, "no page tree");
    CHECK_MSG(pdf.find("xref") != std::string::npos, "no cross-reference table");
    CHECK(pdf.find("Test Sheet") != std::string::npos);
    CHECK(w.page_count() == 1);
    CHECK(w.path_count() == 1);
}

TEST(pdf_output_is_vector_not_raster) {
    PdfWriter w;
    w.begin_page(0.297, 0.210);
    PdfPath p;
    p.points = {{0.05, 0.05}, {0.25, 0.15}};
    w.add(p);
    w.end_page();

    const std::string pdf = w.build();
    CHECK_MSG(pdf.find(" m\n") != std::string::npos, "no moveto operator");
    CHECK_MSG(pdf.find(" l\n") != std::string::npos, "no lineto operator");
    CHECK_MSG(pdf.find("\nS\n") != std::string::npos, "no stroke operator");
    CHECK_MSG(pdf.find("/Image") == std::string::npos,
              "the page contains a raster image");
}

TEST(pdf_text_stays_searchable) {
    PdfWriter w;
    w.begin_page(0.297, 0.210);
    PdfText t;
    t.x = 0.05;
    t.y = 0.10;
    t.text = "SECTION A-A";
    t.height_mm = 3.5;
    w.add(t);
    w.end_page();

    const std::string pdf = w.build();
    CHECK_MSG(pdf.find("BT") != std::string::npos, "no text block");
    CHECK_MSG(pdf.find("(SECTION A-A) Tj") != std::string::npos,
              "text was not written as text");
    CHECK(pdf.find("/BaseFont /Helvetica") != std::string::npos);
}

TEST(plot_search_text_does_not_expand_drawing_extents) {
    PlotJob::SpaceContent content;
    content.shapes.push_back(seg(vol(),1,2,4,6));
    PlotJob::Note note;note.x=100;note.y=200;note.text="Outline label";
    note.invisible=true;content.notes.push_back(note);
    double lx,ly,hx,hy;
    CHECK(content_extents({content},vol(),lx,ly,hx,hy));
    CHECK(std::abs(lx-1)<1e-6&&std::abs(ly-2)<1e-6);
    CHECK(std::abs(hx-4)<1e-6&&std::abs(hy-6)<1e-6);
}

TEST(pdf_escapes_reserved_characters) {
    PdfWriter w;
    w.begin_page(0.297, 0.210);
    PdfText t;
    t.text = "DETAIL (TYP) \\ 2";
    w.add(t);
    w.end_page();

    const std::string pdf = w.build();
    CHECK(pdf.find("\\(TYP\\)") != std::string::npos);
    CHECK(pdf.find("\\\\") != std::string::npos);
}

TEST(pdf_multiple_pages) {
    PdfWriter w;
    for (int i = 0; i < 3; ++i) {
        w.begin_page(0.420, 0.297);
        PdfPath p;
        p.points = {{0.01, 0.01}, {0.40, 0.28}};
        w.add(p);
        w.end_page();
    }
    CHECK(w.page_count() == 3);
    const std::string pdf = w.build();
    CHECK(pdf.find("/Count 3") != std::string::npos);
}

TEST(pdf_writes_to_disk) {
    const std::string path = "/tmp/glcad_plot_test.pdf";
    std::remove(path.c_str());

    PdfWriter w;
    w.begin_page(0.297, 0.210);
    PdfPath p;
    p.points = {{0.01, 0.01}, {0.28, 0.20}};
    w.add(p);
    w.end_page();

    CHECK(w.write_file(path));
    std::FILE* f = std::fopen(path.c_str(), "rb");
    CHECK_MSG(f != nullptr, "file was not created");
    if (f) {
        char head[9] = {};
        std::fread(head, 1, 8, f);
        std::fclose(f);
        CHECK(std::string(head) == "%PDF-1.4");
    }
    std::remove(path.c_str());
}

TEST(plot_job_renders_model_through_a_viewport) {
    const auto v = vol();

    Sheet sheet;
    sheet.name = "A-101";
    sheet.setup.paper = *find_paper("A3");

    Viewport vp;
    vp.lo = {0, 0};
    vp.hi = {1, 1};
    vp.source_space = 0;
    vp.scale = 100.0;
    sheet.viewports.push_back(vp);

    gdoc::LayerTable layers;
    gdoc::Layer wall;
    wall.name = "A-WALL";
    wall.lineweight = gdoc::Lineweight::W050;
    layers.add(wall);

    const PlotStyleTable styles = PlotStyleTable::monochrome();

    PlotJob job;
    job.sheet = &sheet;
    job.styles = &styles;
    job.layers = &layers;
    job.volume = v;

    PlotJob::SpaceContent c;
    c.space = 0;
    c.shapes = {seg(v, 0, 0, 10, 0), seg(v, 10, 0, 10, 10)};
    c.shape_layers = {"A-WALL", "A-WALL"};
    job.content.push_back(c);

    PdfWriter w;
    const PlotResult r = plot(job, w);
    CHECK_MSG(r.ok, "%s", r.message.c_str());
    CHECK_MSG(r.paths == 2, "expected 2 plotted paths, got %zu", r.paths);
    CHECK(w.page_count() == 1);

    const std::string pdf = w.build();
    CHECK(pdf.find("%PDF") == 0);
    CHECK_MSG(pdf.find(" w\n") != std::string::npos, "no line width set");
}

TEST(plot_respects_layer_visibility_and_plottability) {
    const auto v = vol();
    Sheet sheet;
    sheet.setup.paper = *find_paper("A4");
    Viewport vp;
    vp.hi = {1, 1};
    vp.scale = 1.0;
    sheet.viewports.push_back(vp);

    gdoc::LayerTable layers;
    gdoc::Layer off;
    off.name = "HIDDEN";
    off.on = false;
    layers.add(off);
    gdoc::Layer noplot;
    noplot.name = "NOPLOT";
    noplot.plottable = false;
    layers.add(noplot);
    gdoc::Layer ok;
    ok.name = "VISIBLE";
    layers.add(ok);

    const PlotStyleTable styles;
    PlotJob job;
    job.sheet = &sheet;
    job.styles = &styles;
    job.layers = &layers;
    job.volume = v;

    PlotJob::SpaceContent c;
    c.space = 0;
    c.shapes = {seg(v, 0, 0, 1, 0), seg(v, 0, 1, 1, 1), seg(v, 0, 2, 1, 2)};
    c.shape_layers = {"HIDDEN", "NOPLOT", "VISIBLE"};
    job.content.push_back(c);

    PdfWriter w;
    const PlotResult r = plot(job, w);
    CHECK_MSG(r.paths == 1, "expected only the visible layer, got %zu", r.paths);
    CHECK_MSG(r.clipped == 2, "expected 2 suppressed, got %zu", r.clipped);
}

TEST(plot_viewport_freeze_suppresses_only_that_viewport) {
    const auto v = vol();
    Sheet sheet;
    sheet.setup.paper = *find_paper("A4");
    Viewport vp;
    vp.hi = {1, 1};
    vp.scale = 1.0;
    vp.frozen_layers = {"A-GRID"};
    sheet.viewports.push_back(vp);

    gdoc::LayerTable layers;
    gdoc::Layer grid;
    grid.name = "A-GRID";
    layers.add(grid);

    const PlotStyleTable styles;
    PlotJob job;
    job.sheet = &sheet;
    job.styles = &styles;
    job.layers = &layers;
    job.volume = v;

    PlotJob::SpaceContent c;
    c.space = 0;
    c.shapes = {seg(v, 0, 0, 1, 0)};
    c.shape_layers = {"A-GRID"};
    job.content.push_back(c);

    PdfWriter w;
    const PlotResult r = plot(job, w);
    CHECK_MSG(r.paths == 0, "a frozen layer was plotted");
    CHECK(r.clipped == 1);
}

TEST(plot_circles_reach_the_page_as_curves) {
    const auto v = vol();
    Sheet sheet;
    sheet.setup.paper = *find_paper("A4");
    Viewport vp;
    vp.hi = {1, 1};
    vp.scale = 1.0;
    sheet.viewports.push_back(vp);

    gdoc::LayerTable layers;
    const PlotStyleTable styles;
    PlotJob job;
    job.sheet = &sheet;
    job.styles = &styles;
    job.layers = &layers;
    job.volume = v;

    gsnap::Shape circle;
    circle.kind = gsnap::ShapeKind::Circle;
    circle.a = gcm::to_lattice(v, 0, 0);
    circle.radius = static_cast<gcm::i64>(std::llround(1.0 * v.scale()));

    PlotJob::SpaceContent c;
    c.space = 0;
    c.shapes = {circle};
    c.shape_layers = {"0"};
    job.content.push_back(c);

    PdfWriter w;
    const PlotResult r = plot(job, w);
    CHECK(r.paths == 1);
    const std::string pdf = w.build();
    std::size_t linetos = 0, pos = 0;
    while ((pos = pdf.find(" l\n", pos)) != std::string::npos) { ++linetos; pos += 3; }
    CHECK_MSG(linetos > 20, "circle tessellated to only %zu segments", linetos);
}

TEST(plot_with_no_sheet_reports_cleanly) {
    PlotJob job;
    PdfWriter w;
    const PlotResult r = plot(job, w);
    CHECK(!r.ok);
    CHECK(!r.message.empty());
}

TEST(pdf_writes_to_a_stream) {
    PdfWriter w;
    w.begin_page(0.297, 0.210);
    PdfPath p;
    p.points = {{0.01, 0.01}, {0.28, 0.20}};
    w.add(p);
    w.end_page();

    gio::MemoryWriter m;
    CHECK(w.write(m));
    CHECK(m.bytes().size() > 100);
    const std::string s(reinterpret_cast<const char*>(m.bytes().data()),
                        m.bytes().size());
    CHECK(s.rfind("%PDF-1.4", 0) == 0);
    CHECK(s.find("%%EOF") != std::string::npos);

    CHECK(s == w.build());
}

TEST(pdf_write_url_reaches_the_transport_layer) {
    gio::MemoryTransport::reset();
    PdfWriter w;
    w.begin_page(0.297, 0.210);
    PdfPath p;
    p.points = {{0.01, 0.01}, {0.28, 0.20}};
    w.add(p);
    w.end_page();

    CHECK(w.write_url("memory://plot"));
    const auto& b = gio::MemoryTransport::buffer("plot");
    CHECK(b.size() > 100);
    CHECK(b[0] == '%' && b[1] == 'P' && b[2] == 'D' && b[3] == 'F');

    CHECK_MSG(!w.write_url("ipv4://127.0.0.1:9100"),
              "a plot claimed to reach a transport that is not registered");
}

TEST(plot_the_full_iso_a_series_is_present) {
    for (const char* n : {"A0", "A1", "A2", "A3", "A4", "A5", "A6"})
        CHECK_MSG(find_paper(n) != nullptr, "%s is missing", n);
}

TEST(plot_iso_sizes_halve_correctly) {
    const char* series[] = {"A0", "A1", "A2", "A3", "A4", "A5", "A6"};
    for (int i = 0; i + 1 < 7; ++i) {
        const PaperSize* a = find_paper(series[i]);
        const PaperSize* b = find_paper(series[i + 1]);
        CHECK(a && b);
        CHECK_MSG(std::fabs(b->width - a->height) < 1.5e-3,
                  "%s width %.4f vs %s height %.4f", series[i + 1], b->width,
                  series[i], a->height);
        CHECK_MSG(std::fabs(b->height - a->width * 0.5) < 1.5e-3,
                  "%s is not half of %s", series[i + 1], series[i]);
    }
}

TEST(plot_iso_aspect_is_root_two) {
    for (const char* n : {"A0", "A1", "A2", "A3", "A4", "B0", "B1", "B2"}) {
        const PaperSize* p = find_paper(n);
        CHECK(p != nullptr);
        CHECK_MSG(std::fabs(p->width / p->height - std::sqrt(2.0)) < 0.01,
                  "%s is %.4f, not root two", n, p->width / p->height);
    }
}

TEST(plot_oversize_and_ansi_and_arch_are_present) {
    for (const char* n : {"A0+", "A1+", "2A0", "ANSI D", "ANSI E", "ANSI F",
                          "ARCH A", "ARCH D", "ARCH E", "ARCH E1"})
        CHECK_MSG(find_paper(n) != nullptr, "%s is missing", n);

    CHECK(find_paper("A0+")->width > find_paper("A0")->width);
    CHECK(find_paper("A1+")->height > find_paper("A1")->height);
}

TEST(plot_every_paper_is_landscape_and_named_once) {
    std::vector<std::string> seen;
    for (const PaperSize& p : standard_papers()) {
        CHECK_MSG(p.width >= p.height, "%s is stored portrait", p.name.c_str());
        CHECK(p.printable_width() > 0.0 && p.printable_height() > 0.0);
        for (const std::string& s : seen)
            CHECK_MSG(s != p.name, "%s appears twice", p.name.c_str());
        seen.push_back(p.name);
    }
}

TEST(plot_rotating_a_sheet_turns_its_margins_with_it) {
    PaperSize a = *find_paper("A1");
    a.margin_left = 0.020;
    a.margin_bottom = 0.005;
    a.margin_right = 0.010;
    a.margin_top = 0.015;

    const PaperSize r = rotated(a);
    CHECK(std::fabs(r.width - a.height) < 1e-12);
    CHECK(std::fabs(r.height - a.width) < 1e-12);
    CHECK(std::fabs(r.margin_left - a.margin_bottom) < 1e-12);
    CHECK(std::fabs(r.margin_bottom - a.margin_right) < 1e-12);
    CHECK(std::fabs(r.printable_width() - a.printable_height()) < 1e-12);
    CHECK(std::fabs(r.printable_height() - a.printable_width()) < 1e-12);
    const PaperSize rr = rotated(r);
    CHECK(std::fabs(rr.width - a.width) < 1e-12);
    CHECK(std::fabs(rr.printable_width() - a.printable_width()) < 1e-12);
}

TEST(plot_standard_scales_cover_metric_and_imperial) {
    for (const char* n : {"1:1", "1:2", "1:5", "1:10", "1:20", "1:50", "1:100",
                          "1:200", "1:500", "1:1000", "2:1", "5:1", "10:1"})
        CHECK_MSG(find_scale(n) != nullptr, "%s is missing", n);
    for (const char* n : {"1/4\"=1'-0\"", "1/8\"=1'-0\"", "1\"=1'-0\"",
                          "1\"=50'", "1\"=100'"})
        CHECK_MSG(find_scale(n) != nullptr, "%s is missing", n);
}

TEST(plot_scale_ratio_is_drawing_units_per_paper_metre) {
    CHECK(std::fabs(find_scale("1:50")->ratio() - 50.0) < 1e-12);
    CHECK(std::fabs(find_scale("1:1")->ratio() - 1.0) < 1e-12);
    CHECK(std::fabs(find_scale("2:1")->ratio() - 0.5) < 1e-12);
    CHECK(find_scale("2:1")->is_enlargement());
    CHECK(!find_scale("1:50")->is_enlargement());
    CHECK(find_scale("1:1")->is_full_size());
}

TEST(plot_imperial_scales_are_not_read_as_bare_numbers) {
    // 1"=50' is 1:600, not 1:50.
    NamedScale s;
    CHECK(parse_scale("1\"=50'", s));
    CHECK_MSG(std::fabs(s.ratio() - 600.0) < 1e-9, "got 1:%g", s.ratio());

    CHECK(parse_scale("1/4\"=1'-0\"", s));
    CHECK_MSG(std::fabs(s.ratio() - 48.0) < 1e-9, "got 1:%g", s.ratio());

    CHECK(parse_scale("1/8\"=1'", s));
    CHECK(std::fabs(s.ratio() - 96.0) < 1e-9);

    CHECK(parse_scale("1-1/2\"=1'-0\"", s));
    CHECK_MSG(std::fabs(s.ratio() - 8.0) < 1e-9, "got 1:%g", s.ratio());

    CHECK(parse_scale("3\"=1'-0\"", s));
    CHECK(std::fabs(s.ratio() - 4.0) < 1e-9);
}

TEST(plot_scale_parsing_accepts_what_a_drafter_writes) {
    NamedScale s;
    CHECK(parse_scale("1:50", s) && std::fabs(s.ratio() - 50.0) < 1e-12);
    CHECK(parse_scale(" 1 : 50 ", s) && std::fabs(s.ratio() - 50.0) < 1e-12);
    CHECK(parse_scale("1=50", s) && std::fabs(s.ratio() - 50.0) < 1e-12);
    CHECK(parse_scale("2:1", s) && std::fabs(s.ratio() - 0.5) < 1e-12);
    CHECK(parse_scale("full", s) && s.is_full_size());
    CHECK(parse_scale("half", s) && std::fabs(s.ratio() - 2.0) < 1e-12);
    CHECK(parse_scale("double", s) && std::fabs(s.ratio() - 0.5) < 1e-12);

    CHECK(parse_scale(" 1 : 50 ", s));
    CHECK_MSG(s.name == "1:50", "printed as '%s'", s.name.c_str());
}

TEST(plot_scale_parsing_refuses_rather_than_guesses) {
    NamedScale s;
    for (const char* bad : {"", "  ", "banana", "1:", ":50", "1:0", "0:1",
                            "1:abc", "1/0=1'"})
        CHECK_MSG(!parse_scale(bad, s), "'%s' was accepted", bad);
}

TEST(plot_fit_lands_on_a_scale_a_rule_can_measure) {
    for (f64 want : {73.4, 1.3, 12.0, 340.0, 0.7}) {
        const NamedScale* n = nearest_standard_scale(want);
        CHECK_MSG(n != nullptr, "no scale for %g", want);
        CHECK_MSG(n->ratio() >= want - 1e-9,
                  "1:%g is finer than the fit %g -- the drawing runs off the "
                  "sheet", n->ratio(), want);
    }
    CHECK(std::fabs(nearest_standard_scale(73.4)->ratio() - 100.0) < 1e-9);
    CHECK(std::fabs(nearest_standard_scale(12.0)->ratio() - 20.0) < 1e-9);
    CHECK(std::fabs(nearest_standard_scale(1.3)->ratio() - 2.0) < 1e-9);
    CHECK(nearest_standard_scale(-1.0) == nullptr);
}

TEST(plot_fitting_does_not_cross_scale_families) {
    const NamedScale* m = nearest_standard_scale(73.4, ScaleFamily::Metric);
    CHECK(m && m->family == ScaleFamily::Metric);
    CHECK_MSG(std::fabs(m->ratio() - 100.0) < 1e-9, "metric fit gave %s",
              m->name.c_str());

    const NamedScale* i = nearest_standard_scale(73.4, ScaleFamily::Imperial);
    CHECK(i != nullptr);
    CHECK(i->family == ScaleFamily::Imperial || i->is_full_size());
    CHECK_MSG(std::fabs(i->ratio() - 96.0) < 1e-9, "imperial fit gave %s",
              i->name.c_str());

    CHECK(nearest_standard_scale(0.9, ScaleFamily::Imperial)->is_full_size() ||
          nearest_standard_scale(0.9, ScaleFamily::Imperial)->ratio() >= 0.9);
}

TEST(plot_setting_an_imperial_scale_switches_the_family) {
    PageSetup ps;
    CHECK(ps.scale_family == ScaleFamily::Metric);
    CHECK(ps.set_scale("1/4\"=1'-0\""));
    CHECK_MSG(ps.scale_family == ScaleFamily::Imperial,
              "an imperial scale left the sheet marked metric");
    CHECK(ps.set_scale("1:50"));
    CHECK(ps.scale_family == ScaleFamily::Metric);
}

TEST(plot_page_setup_keeps_the_name_and_the_ratio_together) {
    PageSetup ps;
    ps.paper = *find_paper("A1");
    CHECK(ps.set_scale("1:50"));
    CHECK(ps.scale_name() == "1:50");
    CHECK_MSG(std::fabs(ps.drawing_per_paper() - 50.0) < 1e-9,
              "got %g", ps.drawing_per_paper());
    CHECK(!ps.fit_to_paper);

    CHECK(ps.set_scale("1/4\"=1'-0\""));
    CHECK(std::fabs(ps.drawing_per_paper() - 48.0) < 1e-9);
    CHECK(!ps.set_scale("nonsense"));
    CHECK(std::fabs(ps.drawing_per_paper() - 48.0) < 1e-9);
}

TEST(plot_fitted_ratio_uses_the_binding_dimension) {
    PageSetup ps;
    ps.paper = *find_paper("A1");
    ps.orientation = Orientation::Landscape;
    const f64 pw = ps.effective_width(), ph = ps.effective_height();

    CHECK(std::fabs(ps.fitted_ratio(pw * 10.0, ph * 2.0) - 10.0) < 1e-9);
    CHECK(std::fabs(ps.fitted_ratio(pw * 2.0, ph * 10.0) - 10.0) < 1e-9);
}

static void bbox_of(const std::string& pdf, const char* /*unused*/) { (void)pdf; }

TEST(plot_a_direct_plot_lands_at_the_stated_scale) {
    const auto v = vol();
    Sheet sh;
    sh.setup.paper = *find_paper("A0");
    sh.setup.orientation = Orientation::Landscape;
    sh.setup.set_scale("1:50");
    sh.setup.centred = true;
    sh.setup.area = PlotArea::Extents;

    PlotJob job;
    job.sheet = &sh;
    job.volume = v;
    PlotJob::SpaceContent c;
    c.space = 0;
    c.shapes = {seg(v, 0, 0, 50, 0)};
    c.shape_layers = {"0"};
    job.content = {c};

    PdfWriter w;
    const PlotResult r = plot(job, w);
    CHECK_MSG(r.ok, "%s", r.message.c_str());
    CHECK_MSG(r.paths == 1, "%zu paths", r.paths);
    CHECK(r.scale_name == "1:50");
    CHECK(std::fabs(r.scale_ratio - 50.0) < 1e-9);

    f64 x0, y0, x1, y1;
    sh.setup.to_paper(0, 0, 25, 0, x0, y0);
    sh.setup.to_paper(50, 0, 25, 0, x1, y1);
    CHECK_MSG(std::fabs((x1 - x0) - 1.0) < 1e-9,
              "fifty metres at 1:50 measured %.6f m on paper", x1 - x0);
    CHECK_MSG(std::fabs(y1 - y0) < 1e-12, "a horizontal wall gained rise");
}

TEST(plot_the_same_wall_at_a_different_scale_measures_differently) {
    Sheet sh;
    sh.setup.paper = *find_paper("A1");
    auto paper_length = [&](const char* scale) {
        sh.setup.set_scale(scale);
        f64 x0, y0, x1, y1;
        sh.setup.to_paper(0, 0, 5, 0, x0, y0);
        sh.setup.to_paper(10, 0, 5, 0, x1, y1);
        return x1 - x0;
    };
    CHECK(std::fabs(paper_length("1:1") - 10.0) < 1e-9);
    CHECK(std::fabs(paper_length("1:100") - 0.1) < 1e-9);
    CHECK(std::fabs(paper_length("1:20") - 0.5) < 1e-9);
    CHECK(std::fabs(paper_length("2:1") - 20.0) < 1e-9);
}

TEST(plot_centred_geometry_sits_in_the_middle_of_the_printable_area) {
    Sheet sh;
    sh.setup.paper = *find_paper("A2");
    sh.setup.set_scale("1:100");
    sh.setup.centred = true;

    f64 px, py;
    sh.setup.to_paper(7.0, 3.0, 7.0, 3.0, px, py);
    const PaperSize& p = sh.setup.paper;
    CHECK(std::fabs(px - (p.margin_left + p.printable_width() * 0.5)) < 1e-12);
    CHECK(std::fabs(py - (p.margin_bottom + p.printable_height() * 0.5)) < 1e-12);

    sh.setup.centred = false;
    sh.setup.to_paper(7.0, 3.0, 7.0, 3.0, px, py);
    CHECK(std::fabs(px - p.margin_left) < 1e-12);
    CHECK(std::fabs(py - p.margin_bottom) < 1e-12);
}

TEST(plot_portrait_pages_are_sized_portrait) {
    Sheet sh;
    sh.setup.paper = *find_paper("A1");
    sh.setup.orientation = Orientation::Portrait;
    CHECK(sh.setup.page_width() < sh.setup.page_height());
    CHECK(std::fabs(sh.setup.page_width() - 0.594) < 1e-9);
    CHECK(std::fabs(sh.setup.page_height() - 0.841) < 1e-9);

    PlotJob job;
    const auto v = vol();
    job.sheet = &sh;
    job.volume = v;
    PlotJob::SpaceContent c;
    c.space = 0;
    c.shapes = {seg(v, 0, 0, 1, 1)};
    c.shape_layers = {"0"};
    job.content = {c};

    PdfWriter w;
    CHECK(plot(job, w).ok);
    const std::string pdf = w.build();
    // The MediaBox is in points: 0.594 m is 1683.8 pt.
    CHECK_MSG(pdf.find("1683") != std::string::npos ||
              pdf.find("1684") != std::string::npos,
              "the page is not 594 mm wide");
}

TEST(plot_fit_to_paper_chooses_a_standard_scale) {
    const auto v = vol();
    Sheet sh;
    sh.setup.paper = *find_paper("A3");
    sh.setup.fit_to_paper = true;
    sh.setup.area = PlotArea::Extents;

    PlotJob job;
    job.sheet = &sh;
    job.volume = v;
    PlotJob::SpaceContent c;
    c.space = 0;
    c.shapes = {seg(v, 0, 0, 30, 0), seg(v, 30, 0, 30, 20)};
    c.shape_layers = {"0", "0"};
    job.content = {c};

    PdfWriter w;
    const PlotResult r = plot(job, w);
    CHECK(r.ok && r.paths == 2);
    CHECK_MSG(find_scale(r.scale_name) != nullptr,
              "fitted to '%s', which is not a standard scale",
              r.scale_name.c_str());
    CHECK_MSG(30.0 / r.scale_ratio <= sh.setup.effective_width() + 1e-9,
              "30 m at 1:%g is %.4f m, wider than the %.4f m printable area",
              r.scale_ratio, 30.0 / r.scale_ratio, sh.setup.effective_width());
    CHECK(20.0 / r.scale_ratio <= sh.setup.effective_height() + 1e-9);
}

TEST(plot_a_direct_plot_still_honours_layer_plottability) {
    const auto v = vol();
    gdoc::LayerTable layers;
    gdoc::Layer hidden;
    hidden.name = "NOPLOT";
    hidden.plottable = false;
    layers.add(hidden);

    Sheet sh;
    sh.setup.paper = *find_paper("A3");
    sh.setup.set_scale("1:100");

    PlotJob job;
    job.sheet = &sh;
    job.volume = v;
    job.layers = &layers;
    PlotJob::SpaceContent c;
    c.space = 0;
    c.shapes = {seg(v, 0, 0, 1, 0), seg(v, 0, 1, 1, 1)};
    c.shape_layers = {"0", "NOPLOT"};
    job.content = {c};

    PdfWriter w;
    const PlotResult r = plot(job, w);
    CHECK_MSG(r.paths == 1, "%zu paths", r.paths);
    CHECK(r.clipped == 1);
}

TEST(plot_notes_become_page_text_at_scale) {
    gcm::WorkingVolume v;
    v.extent = 500.0;
    v.resolution_bits = 32;
    Sheet sheet;
    if (const PaperSize* a3 = find_paper("A3")) sheet.setup.paper = *a3;
    Viewport vp;
    vp.lo = {0.0, 0.0};
    vp.hi = {0.4, 0.28};
    vp.source_space = 0;
    vp.scale = 100.0;   // 1:100
    sheet.viewports.push_back(vp);
    const PlotStyleTable styles = PlotStyleTable::monochrome();
    PlotJob job;
    job.sheet = &sheet;
    job.styles = &styles;
    job.volume = v;
    PlotJob::SpaceContent c;
    c.space = 0;
    PlotJob::Note n;
    n.x = 5.0; n.y = 3.0;
    n.text = "FFL +0.150";
    n.height = 0.25;   // 250 mm on the model plots as 2.5 mm at 1:100
    c.notes.push_back(n);
    job.content.push_back(c);
    PdfWriter w;
    const PlotResult r = plot(job, w);
    CHECK_MSG(r.ok, "%s", r.message.c_str());
    CHECK_MSG(r.texts == 1, "%zu texts", r.texts);
    const auto& texts = w.texts_of_page(0);
    CHECK_MSG(texts.size() == 1, "%zu texts on the page", texts.size());
    if (texts.empty()) return;
    CHECK(texts[0].text == "FFL +0.150");
    CHECK_MSG(std::fabs(texts[0].height_mm - 2.5) < 1e-9, "height %g mm", texts[0].height_mm);
    f64 px, py;
    vp.to_paper(5.0, 3.0, px, py);
    CHECK(std::fabs(texts[0].x - px) < 1e-12 && std::fabs(texts[0].y - py) < 1e-12);
    const std::string pdf = w.build();
    CHECK(pdf.find("(FFL +0.150) Tj") != std::string::npos);
}

TEST(plot_viewport_clips_crossing_geometry_and_text_but_not_sheet_ink) {
    Sheet sheet; sheet.setup.paper=*find_paper("A3");
    Viewport vp; vp.lo={0.02,0.03}; vp.hi={0.18,0.14}; vp.scale=1;
    sheet.viewports.push_back(vp);
    PlotJob job; job.sheet=&sheet; job.volume=vol();
    PlotJob::SpaceContent c; c.shapes={seg(vol(),-2,0,2,0)};
    PlotJob::Note n; n.text="crossing"; n.height=0.1; c.notes.push_back(n);
    job.content.push_back(c);
    job.sheet_shapes={seg(vol(),0,0,0.4,0)};
    PdfWriter writer; CHECK(plot(job,writer).ok);
    CHECK(writer.build().find(" re W n\n")!=std::string::npos);
    CHECK(writer.paths_of_page(0).size()==2);
    CHECK(writer.texts_of_page(0).size()==1);
    CHECK(writer.paths_of_page(0)[0].clip.has_value());
    CHECK(!writer.paths_of_page(0)[1].clip.has_value());
    CHECK(writer.texts_of_page(0)[0].clip.has_value());
    DwfWriter dwf; dwf.begin_page("Plan",0.42,0.297);
    for (const auto& p:writer.paths_of_page(0)) dwf.add(p);
    for (const auto& t:writer.texts_of_page(0)) dwf.add(t);
    dwf.end_page(); const auto bytes=dwf.build();
    CHECK(std::string(bytes.begin(),bytes.end()).find("<Canvas Clip=\"M ")!=std::string::npos);
}

TEST(plot_rectangle_clips_crossing_and_disconnected_segments) {
    PaperClip clip{{0.02,0.03},{0.18,0.14}};
    double x0=-1,y0=0.1,x1=1,y1=0.1;
    CHECK(clip.clip_segment(x0,y0,x1,y1));
    CHECK(std::fabs(x0-0.02)<1e-12); CHECK(std::fabs(x1-0.18)<1e-12);
    x0=0; x1=0.01; y0=y1=0.1; CHECK(!clip.clip_segment(x0,y0,x1,y1));
    x0=x1=0.1; y0=-1; y1=1; CHECK(clip.clip_segment(x0,y0,x1,y1));
    CHECK(std::fabs(y0-0.03)<1e-12); CHECK(std::fabs(y1-0.14)<1e-12);
}

TEST(polygon_clip_splits_concave_crossings_and_triangulates) {
    gplot::PaperClip clip{{0,0},{3,3},{{0,0},{3,0},{3,3},{2,3},{2,1},{1,1},{1,3},{0,3}}};
    CHECK(clip.valid()); CHECK(clip.contains({.5,2})); CHECK(!clip.contains({1.5,2}));
    auto spans=clip.segments({-1,2},{4,2}); CHECK(spans.size()==2);
    if(spans.size()==2) { CHECK(std::abs(spans[0][0].x)<1e-12); CHECK(std::abs(spans[0][1].x-1)<1e-12); CHECK(std::abs(spans[1][0].x-2)<1e-12); }
    double area=0; for(auto t:clip.triangles()) area+=std::abs((t[1].x-t[0].x)*(t[2].y-t[0].y)-(t[1].y-t[0].y)*(t[2].x-t[0].x))/2;
    CHECK(std::abs(area-7)<1e-12);
    clip.polygon={{0,0},{3,3},{0,3},{3,0}}; CHECK(!clip.valid());
}

TEST(plot_transparency_survives_screening_and_explicit_opaque_overrides) {
    const auto v=vol();Sheet sheet;sheet.name="Opacity";
    gdoc::LayerTable layers;gdoc::Layer layer;layer.name="Faded";layer.transparency=60;layers.add(layer);
    auto styles=PlotStyleTable::monochrome();
    PlotJob job;job.sheet=&sheet;job.layers=&layers;job.styles=&styles;job.volume=v;
    for(const int transparency:{-1,0,95,100}) {
        auto shape=seg(v,.01,.01,.03,.03);shape.attrs.transparency=transparency;
        job.sheet_shapes.push_back(shape);job.sheet_layers.push_back("Faded");
    }
    PdfWriter pdf;const auto result=plot(job,pdf);CHECK(result.ok);
    const auto& paths=pdf.paths_of_page(0);CHECK(paths.size()==4);
    if(paths.size()==4) {
        CHECK(std::abs(paths[0].color.a-.4)<1e-9);CHECK(paths[1].color.a==1);
        CHECK(std::abs(paths[2].color.a-.05)<1e-9);CHECK(paths[3].color.a==0);
    }
}

TEST(plot_text_uses_entity_appearance_in_model_viewport_and_sheet_spaces) {
    Sheet sheet;sheet.setup.paper=*find_paper("A4");
    gdoc::LayerTable layers;gdoc::Layer layer;layer.name="Notes";layer.color=gdoc::Color::aci(1);layer.transparency=80;layers.add(layer);
    PlotJob::Note note;note.text="Opaque blue";note.height=.003;note.layer="Notes";
    note.attrs.color=gdoc::Color::rgb(0,0,255);note.attrs.transparency=0;
    for(int mode=0;mode<3;++mode) {
        PlotJob job;job.sheet=&sheet;job.layers=&layers;job.volume=vol();
        if(mode==2)job.sheet_notes.push_back(note);
        else {PlotJob::SpaceContent content;content.notes.push_back(note);job.content.push_back(content);}
        sheet.viewports.clear();
        if(mode==1) {Viewport viewport;viewport.lo={0,0};viewport.hi={.2,.2};viewport.scale=1;sheet.viewports.push_back(viewport);}
        PdfWriter writer;const auto result=plot(job,writer);CHECK(result.ok);
        const auto& texts=writer.texts_of_page(0);CHECK(texts.size()==1);if(texts.empty())continue;
        CHECK(texts[0].color.r==0);CHECK(texts[0].color.g==0);CHECK(texts[0].color.b==1);CHECK(texts[0].color.a==1);
    }
}

TEST(plot_reference_clip_chains_follow_model_viewport_and_sheet_mapping) {
    Sheet sheet;sheet.setup.paper=*find_paper("A4");
    const auto volume=vol();const auto p=[&](double x,double y){return gcm::to_lattice(volume,x,y);};
    PlotJob::Note note;note.text="Nested clip";note.height=.02;
    note.clips={{{p(-.04,-.04),p(.04,.04)},false},{{p(-.01,-.01),p(.01,.01)},true}};
    for(int mode=0;mode<3;++mode) {
        PlotJob job;job.sheet=&sheet;job.volume=volume;sheet.viewports.clear();
        if(mode==2)job.sheet_notes.push_back(note);
        else {PlotJob::SpaceContent content;content.notes.push_back(note);job.content.push_back(content);}
        if(mode==1){Viewport vp;vp.lo={0,0};vp.hi={.2,.2};vp.scale=2;vp.rotation=.4;sheet.viewports.push_back(vp);}
        PdfWriter writer;CHECK(plot(job,writer).ok);
        const auto& notes=writer.texts_of_page(0);CHECK(notes.size()==1);if(notes.empty())continue;
        const auto& text=notes[0];CHECK(text.clips.size()==2);if(text.clips.size()!=2)continue;
        CHECK(!text.clips[0].inverted);CHECK(text.clips[1].inverted);
        CHECK(text.clips[0].contains({text.x,text.y}));CHECK(!text.clips[1].contains({text.x,text.y}));
        CHECK(writer.build().find("h W* n")!=std::string::npos);
        DwfWriter dwf;dwf.begin_page("Clipped",.297,.21);dwf.add(text);dwf.end_page();
        const auto bytes=dwf.build();CHECK(std::string(bytes.begin(),bytes.end()).find("<Canvas Clip=\"F0 ")!=std::string::npos);
    }
}

TEST(clipped_reference_content_does_not_inflate_plot_extents) {
    const auto volume=vol();const auto p=[&](double x,double y){return gcm::to_lattice(volume,x,y);};
    PlotJob::SpaceContent content;
    PlotJob::Note note;note.text="wide label";note.height=2;note.clips={{{p(1,.5),p(2,1.5)},false}};
    content.notes.push_back(note);double lx=0,ly=0,hx=0,hy=0;
    CHECK(content_extents({content},volume,lx,ly,hx,hy));
    CHECK(std::abs(lx-1)<1e-6);CHECK(std::abs(hx-2)<1e-6);CHECK(std::abs(ly-.5)<1e-6);CHECK(std::abs(hy-1.5)<1e-6);
    content.notes[0].clips={{{p(-1,-1),p(20,20)},true}};
    CHECK(!content_extents({content},volume,lx,ly,hx,hy));
    content.notes.clear();PdfImage image;image.origin={0,0};image.x_axis={10,0};image.y_axis={0,10};
    image.clips={{{{1,2},{3,4}},false}};content.images.push_back(image);
    CHECK(content_extents({content},volume,lx,ly,hx,hy));
    CHECK(std::abs(lx-1)<1e-6);CHECK(std::abs(hx-3)<1e-6);CHECK(std::abs(ly-2)<1e-6);CHECK(std::abs(hy-4)<1e-6);
}
