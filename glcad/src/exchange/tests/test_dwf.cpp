#include "test.hpp"

#include <cmath>
#include <cstdio>
#include <string>

#include "exchange/dwf.hpp"

using namespace gplot;

namespace {

gcm::WorkingVolume vol() {
    gcm::WorkingVolume v;
    v.extent = 500.0;
    v.resolution_bits = 32;
    return v;
}

gsnap::Shape seg(const gcm::WorkingVolume& v, f64 x0, f64 y0, f64 x1, f64 y1) {
    gsnap::Shape s;
    s.kind = gsnap::ShapeKind::Segment;
    s.a = gcm::to_lattice(v, x0, y0);
    s.b = gcm::to_lattice(v, x1, y1);
    return s;
}

std::string text_of(const std::vector<gcm::u8>& v) {
    return std::string(v.begin(), v.end());
}

PdfPath line(f64 x0, f64 y0, f64 x1, f64 y1, const char* layer = "0") {
    PdfPath p;
    p.points = {{x0, y0}, {x1, y1}};
    p.layer = layer;
    p.width_mm = 0.5;
    return p;
}

}

TEST(zip_round_trips_an_entry) {
    std::vector<ZipEntry> entries = {
        {"a.txt", {'h', 'e', 'l', 'l', 'o'}},
        {"dir/b.bin", {0x00, 0xff, 0x7f, 0x80}},
    };
    const auto archive = zip(entries);
    CHECK(archive.size() > 40);

    std::vector<gcm::u8> out;
    CHECK_MSG(unzip_entry(archive, "a.txt", out), "the first entry is missing");
    CHECK(text_of(out) == "hello");
    CHECK(unzip_entry(archive, "dir/b.bin", out));
    CHECK(out == entries[1].data);
    CHECK(!unzip_entry(archive, "nothing", out));
}

TEST(zip_is_a_recognisable_archive) {
    const auto archive = zip({{"x", {'1'}}});
    CHECK(archive[0] == 'P' && archive[1] == 'K' && archive[2] == 3 &&
          archive[3] == 4);
    bool has_end = false;
    for (std::size_t i = 0; i + 4 <= archive.size(); ++i)
        if (archive[i] == 'P' && archive[i + 1] == 'K' && archive[i + 2] == 5 &&
            archive[i + 3] == 6)
            has_end = true;
    CHECK_MSG(has_end, "no end-of-central-directory record");
}

TEST(zip_names_come_back_in_order) {
    const auto archive = zip({{"first", {'1'}}, {"second", {'2'}}, {"third", {'3'}}});
    const auto names = zip_names(archive);
    CHECK(names.size() == 3);
    CHECK(names[0] == "first" && names[1] == "second" && names[2] == "third");
}

TEST(zip_detects_a_corrupt_entry) {
    auto archive = zip({{"a.txt", {'h', 'e', 'l', 'l', 'o'}}});
    std::vector<gcm::u8> out;
    CHECK(unzip_entry(archive, "a.txt", out));

    for (std::size_t i = 0; i + 5 <= archive.size(); ++i)
        if (archive[i] == 'h' && archive[i + 1] == 'e') {
            archive[i + 1] ^= 0xFF;
            break;
        }
    CHECK_MSG(!unzip_entry(archive, "a.txt", out),
              "a corrupted entry was returned as good");
}

TEST(zip_of_nothing_is_still_a_valid_archive) {
    const auto archive = zip({});
    CHECK(archive.size() == 22);
    CHECK(zip_names(archive).empty());
    std::vector<gcm::u8> out;
    CHECK(!unzip_entry(archive, "anything", out));
}

TEST(zip_is_deterministic) {
    const std::vector<ZipEntry> e = {{"a", {'x'}}, {"b", {'y'}}};
    CHECK(zip(e) == zip(e));
}

TEST(dwf_package_has_the_parts_a_reader_needs) {
    DwfWriter w;
    w.begin_page("A-101", 0.841, 0.594);
    w.add(line(0.1, 0.1, 0.5, 0.4));
    w.end_page();

    const auto pkg = w.build();
    const auto names = zip_names(pkg);
    for (const char* required : {"[Content_Types].xml", "_rels/.rels",
                                 "FixedDocSeq.fdseq",
                                 "Documents/1/FixedDoc.fdoc",
                                 "Documents/1/Pages/1.fpage"}) {
        bool found = false;
        for (const std::string& n : names) if (n == required) found = true;
        CHECK_MSG(found, "%s is missing from the package", required);
    }
}

TEST(dwf_geometry_reaches_the_page_markup) {
    DwfWriter w;
    w.begin_page("Sheet", 0.297, 0.210);
    w.add(line(0.010, 0.020, 0.100, 0.020));
    w.end_page();

    std::vector<gcm::u8> page;
    CHECK(unzip_entry(w.build(), "Documents/1/Pages/1.fpage", page));
    const std::string x = text_of(page);
    CHECK_MSG(x.find("<FixedPage") != std::string::npos, "not a fixed page");
    CHECK_MSG(x.find("<Path ") != std::string::npos, "the line never arrived");
    CHECK(x.find("Data=\"M ") != std::string::npos);
    CHECK(w.path_count() == 1);
    CHECK(w.page_count() == 1);
}

TEST(dwf_y_runs_down_the_page) {
    // XPS y runs down from the top; paper y runs up from the corner.
    DwfWriter w;
    w.begin_page("Sheet", 0.200, 0.100);
    w.add(line(0.0, 0.0, 0.0, 0.100));
    w.end_page();

    std::vector<gcm::u8> page;
    CHECK(unzip_entry(w.build(), "Documents/1/Pages/1.fpage", page));
    const std::string x = text_of(page);

    const std::size_t at = x.find("Data=\"M ");
    CHECK(at != std::string::npos);
    const std::string data = x.substr(at + 8, 60);
    // 0.100 m of page is 377.95 DIPs.
    CHECK_MSG(data.rfind("0.0,377.9", 0) == 0,
              "the first point is at '%s', not the bottom of the page",
              data.substr(0, 14).c_str());
    CHECK_MSG(data.find(" L 0.0,0.0") != std::string::npos,
              "the second point is not at the top");
}

TEST(dwf_page_size_is_in_dips_not_metres) {
    DwfWriter w;
    w.begin_page("A1", 0.841, 0.594);
    w.add(line(0, 0, 0.1, 0.1));
    w.end_page();

    std::vector<gcm::u8> page;
    CHECK(unzip_entry(w.build(), "Documents/1/Pages/1.fpage", page));
    const std::string x = text_of(page);
    // 0.841 m is 3177.9 DIPs at 96 per inch.
    CHECK_MSG(x.find("Width=\"3178.5") != std::string::npos, "the page is the "
              "wrong size: %s", x.substr(0, 200).c_str());
    CHECK(x.find("Height=\"2245.0") != std::string::npos);
}

TEST(dwf_layers_become_switchable_groups) {
    DwfWriter w;
    w.declare_layer("WALLS", true);
    w.declare_layer("GRID", false);
    w.begin_page("Sheet", 0.297, 0.210);
    w.add(line(0, 0, 0.1, 0, "WALLS"));
    w.add(line(0, 0.05, 0.1, 0.05, "GRID"));
    w.end_page();

    std::vector<gcm::u8> page;
    CHECK(unzip_entry(w.build(), "Documents/1/Pages/1.fpage", page));
    const std::string x = text_of(page);
    CHECK_MSG(x.find("Name=\"WALLS\"") != std::string::npos,
              "the WALLS layer is not a named group");
    CHECK(x.find("Name=\"GRID\"") != std::string::npos);
    const std::size_t grid = x.find("Name=\"GRID\"");
    CHECK_MSG(x.substr(grid, 40).find("Opacity=\"0\"") != std::string::npos,
              "a hidden layer was published visible");
}

TEST(dwf_lineweights_plot_at_their_paper_width) {
    DwfWriter w;
    w.begin_page("Sheet", 0.297, 0.210);
    PdfPath heavy = line(0, 0, 0.1, 0);
    heavy.width_mm = 0.70;
    w.add(heavy);
    w.end_page();

    std::vector<gcm::u8> page;
    CHECK(unzip_entry(w.build(), "Documents/1/Pages/1.fpage", page));
    const std::string x = text_of(page);
    // 0.70 mm is 2.646 DIPs.
    CHECK_MSG(x.find("StrokeThickness=\"2.6") != std::string::npos,
              "the lineweight is wrong: %s", x.substr(0, 400).c_str());
}

TEST(dwf_dashes_are_multiples_of_the_stroke_width) {
    DwfWriter w;
    w.begin_page("Sheet", 0.297, 0.210);
    PdfPath dashed = line(0, 0, 0.1, 0);
    dashed.width_mm = 0.5;
    dashed.dashes_mm = {5.0, 2.5};
    w.add(dashed);
    w.end_page();

    std::vector<gcm::u8> page;
    CHECK(unzip_entry(w.build(), "Documents/1/Pages/1.fpage", page));
    const std::string x = text_of(page);
    CHECK_MSG(x.find("StrokeDashArray=\"10.0 5.0\"") != std::string::npos,
              "the dash array is wrong: %s",
              x.substr(x.find("StrokeDash"), 60).c_str());
}

TEST(dwf_text_stays_searchable) {
    DwfWriter w;
    w.begin_page("Sheet", 0.297, 0.210);
    PdfText t;
    t.x = 0.05;
    t.y = 0.10;
    t.text = "SECTION A-A";
    t.height_mm = 3.5;
    w.add(t);
    w.end_page();

    std::vector<gcm::u8> page;
    CHECK(unzip_entry(w.build(), "Documents/1/Pages/1.fpage", page));
    const std::string x = text_of(page);
    CHECK_MSG(x.find("UnicodeString=\"SECTION A-A\"") != std::string::npos,
              "the text was not published as text");
}

TEST(dwf_escapes_xml_reserved_characters) {
    DwfWriter w;
    w.declare_layer("A&B<C>", true);
    w.begin_page("Sheet \"1\"", 0.297, 0.210);
    w.add(line(0, 0, 0.1, 0, "A&B<C>"));
    PdfText t;
    t.text = "5 < 6 & \"quoted\"";
    t.layer = "A&B<C>";
    w.add(t);
    w.end_page();

    const auto pkg = w.build();
    std::vector<gcm::u8> page, sheets;
    CHECK(unzip_entry(pkg, "Documents/1/Pages/1.fpage", page));
    CHECK(unzip_entry(pkg, "Documents/1/Sheets.xml", sheets));

    const std::string x = text_of(page) + text_of(sheets);
    CHECK(x.find("A&amp;B&lt;C&gt;") != std::string::npos);
    CHECK(x.find("&quot;") != std::string::npos);
    CHECK_MSG(x.find("\"A&B") == std::string::npos, "a raw ampersand survived");
}

TEST(dwf_sheet_names_are_the_drawings_own) {
    DwfWriter w;
    w.begin_page("A-101 GROUND FLOOR", 0.841, 0.594);
    w.add(line(0, 0, 0.1, 0));
    w.end_page();
    w.begin_page("A-102 FIRST FLOOR", 0.841, 0.594);
    w.add(line(0, 0, 0.1, 0));
    w.end_page();

    std::vector<gcm::u8> sheets;
    CHECK(unzip_entry(w.build(), "Documents/1/Sheets.xml", sheets));
    const std::string x = text_of(sheets);
    CHECK(x.find("A-101 GROUND FLOOR") != std::string::npos);
    CHECK(x.find("A-102 FIRST FLOOR") != std::string::npos);
    CHECK(w.page_count() == 2);
}

TEST(dwf_an_unnamed_page_still_gets_a_name) {
    DwfWriter w;
    w.begin_page("", 0.297, 0.210);
    w.add(line(0, 0, 0.1, 0));
    w.end_page();
    std::vector<gcm::u8> sheets;
    CHECK(unzip_entry(w.build(), "Documents/1/Sheets.xml", sheets));
    CHECK(text_of(sheets).find("Sheet 1") != std::string::npos);
}

TEST(dwf_geometry_outside_a_page_is_ignored_not_crashed) {
    DwfWriter w;
    w.add(line(0, 0, 1, 1));
    CHECK(w.path_count() == 0);
    w.begin_page("Sheet", 0.297, 0.210);
    PdfPath degenerate;
    degenerate.points = {{0.0, 0.0}};
    w.add(degenerate);
    PdfText empty;
    w.add(empty);
    w.end_page();
    CHECK(w.path_count() == 0);
    CHECK(w.build().size() > 0);
}

TEST(dwf_package_is_deterministic) {
    auto make = [] {
        DwfWriter w;
        w.set_title("Job 1234");
        w.declare_layer("WALLS");
        w.begin_page("A-101", 0.841, 0.594);
        w.add(line(0.1, 0.1, 0.5, 0.4, "WALLS"));
        w.end_page();
        return w.build();
    };
    CHECK_MSG(make() == make(),
              "two publishes of the same drawing produced different bytes");
}

TEST(dwf_writes_to_disk) {
    DwfWriter w;
    w.begin_page("A-101", 0.297, 0.210);
    w.add(line(0, 0, 0.1, 0.1));
    w.end_page();

    const std::string path = "/tmp/glcad_test.dwfx";
    std::remove(path.c_str());
    CHECK_MSG(w.write_file(path), "the package was not written");

    std::FILE* f = std::fopen(path.c_str(), "rb");
    CHECK(f != nullptr);
    if (f) {
        char head[4] = {};
        CHECK(std::fread(head, 1, 4, f) == 4);
        std::fclose(f);
        CHECK_MSG(head[0] == 'P' && head[1] == 'K',
                  "the file on disk is not an archive");
    }
    std::remove(path.c_str());
}

TEST(dwf_publishes_a_whole_sheet_set) {
    const auto v = vol();
    SheetSet set;
    for (const char* name : {"A-101", "A-102"}) {
        Sheet sh;
        sh.name = name;
        sh.setup.paper = *find_paper("A3");
        sh.setup.set_scale("1:100");
        set.add(sh);
    }

    DwfJob job;
    job.sheets = &set;
    job.volume = v;
    PlotJob::SpaceContent c;
    c.space = 0;
    c.shapes = {seg(v, 0, 0, 10, 0), seg(v, 10, 0, 10, 5)};
    c.shape_layers = {"0", "0"};
    job.content = {c};

    DwfWriter w;
    const PlotResult r = publish(job, w);
    CHECK_MSG(r.ok, "%s", r.message.c_str());
    CHECK_MSG(w.page_count() == 2, "%zu pages", w.page_count());
    CHECK_MSG(r.paths == 4, "%zu paths across two sheets", r.paths);

    const auto names = zip_names(w.build());
    bool page2 = false;
    for (const std::string& n : names)
        if (n == "Documents/1/Pages/2.fpage") page2 = true;
    CHECK(page2);
}

TEST(dwf_publishing_with_no_sheets_reports_cleanly) {
    SheetSet empty;
    DwfJob job;
    job.sheets = &empty;
    DwfWriter w;
    const PlotResult r = publish(job, w);
    CHECK(!r.ok);
    CHECK(!r.message.empty());

    DwfJob none;
    CHECK(!publish(none, w).ok);
}

TEST(dwf_and_pdf_do_not_disagree_about_the_drawing) {
    const auto v = vol();
    SheetSet set;
    Sheet sh;
    sh.name = "A-101";
    sh.setup.paper = *find_paper("A2");
    sh.setup.set_scale("1:50");
    set.add(sh);

    PlotJob::SpaceContent c;
    c.space = 0;
    c.shapes = {seg(v, 0, 0, 10, 0), seg(v, 10, 0, 10, 5), seg(v, 10, 5, 0, 5)};
    c.shape_layers = {"0", "0", "0"};

    DwfJob dwf_job;
    dwf_job.sheets = &set;
    dwf_job.volume = v;
    dwf_job.content = {c};
    DwfWriter dw;
    const PlotResult dwf_result = publish(dwf_job, dw);

    PlotJob pdf_job;
    pdf_job.sheet = &set.all()[0];
    pdf_job.volume = v;
    pdf_job.content = {c};
    PdfWriter pw;
    const PlotResult pdf_result = plot(pdf_job, pw);

    CHECK(dwf_result.ok && pdf_result.ok);
    CHECK_MSG(dwf_result.paths == pdf_result.paths,
              "DWF drew %zu paths and PDF drew %zu", dwf_result.paths,
              pdf_result.paths);
    CHECK(dwf_result.scale_name == pdf_result.scale_name);
}
