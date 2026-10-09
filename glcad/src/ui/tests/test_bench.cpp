#include "test.hpp"
#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include "ui/shell.hpp"
#include "ui/ai_tools.hpp"
#include "exchange/dxf.hpp"
#include "exchange/dwg.hpp"
#include "geometry/boolean.hpp"
using namespace gcapp;
static const char* bench_file(const char* var) { const char* p = std::getenv(var); return p ? p : ""; }
static double now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
TEST(bench_door_drag_with_remote_rooms_and_backgrounds) {
    Shell shell;shell.set_dpr(1);shell.set_viewport(1600,1000);auto& c=shell.canvas();
    c.set_display_unit(gunit::Length::Metre);
    const auto p=[&](double x,double y){return gcm::to_lattice(c.volume(),x,y);};
    CHECK(c.add_wall(p(0,0),p(8,0),.2));
    gai::ToolCall call;call.function.name="draw_door";
    call.function.arguments=R"({"hinge":"1,0","latch":"2,0","framing":"none"})";
    CHECK(!gai::Json::parse(ai_tools::execute(shell,call)).contains("error"));
    c.modify_shapes([&](auto& shapes) {
        const auto edge=[&](double ax,double ay,double bx,double by,bool wall=false) {
            gsnap::Shape s;s.a=p(ax,ay);s.b=p(bx,by);
            if(wall){s.layer="A-WALL";s.attrs.user["sketch_object"]="wall";}
            shapes.push_back(s);
        };
        for(int room=0;room<12;++room) {
            const double x=20+(room%4)*6,y=(room/4)*6;
            for(int n=0;n<24;++n) {
                const double a=n/6.,b=(n+1)/6.;
                edge(x+a,y,x+b,y);edge(x+4,y+a,x+4,y+b);
                edge(x+4-a,y+4,x+4-b,y+4);edge(x,y+4-a,x,y+4-b);
            }
            edge(x,y-1,x+4,y-1,true);edge(x+4,y-1,x+4,y-.8,true);
            edge(x+4,y-.8,x,y-.8,true);edge(x,y-.8,x,y-1,true);
            for(int n=0;n<16;++n)edge(x+.5,y+.25+n*.2,x+1,y+.25+n*.2);
        }
    });
    for(int room=0;room<12;++room)CHECK(c.background_at_world(p(22+(room%4)*6,2+(room/4)*6),gdoc::Color::rgb(90,140,190),.01,35));
    std::vector<std::size_t> selected;
    for(std::size_t i=0;i<c.shapes().size();++i)if(c.shapes()[i].attrs.user.contains("door"))selected.push_back(i);
    c.set_selection(selected);c.set_tool(Tool::Select);c.view().set_mapping(200,500,80);
    double x,y;c.view().screen_of(1,.6,x,y);CHECK(c.begin_selection_drag(x,y));
    const auto work=c.drafting_work();const auto start=now();double worst=0;
    for(int frame=0;frame<16;++frame) {
        const auto t=now();c.update_selection_drag(x+80+frame*12,y);worst=std::max(worst,(now()-t)*1000);
    }
    std::printf("BENCH door drag: shapes=%zu backgrounds=%zu update=%.2fms worst=%.2fms\n",c.shapes().size(),c.hatches().size(),(now()-start)*1000/16,worst);
    CHECK(c.drafting_work().background_traces==work.background_traces);
    CHECK(c.drafting_work().hatch_updates==work.hatch_updates);
    CHECK_MSG(c.drafting_work().opening_edges-work.opening_edges<=16*12,"opening edges=%llu",static_cast<unsigned long long>(c.drafting_work().opening_edges-work.opening_edges));
    CHECK(c.end_selection_drag());for(const auto& h:c.hatches())CHECK(h.valid&&!h.orphaned);
}

TEST(bench_import_gateway) {
    const char* path = bench_file("GLCAD_BENCH_PLAN_DXF");
    std::ifstream f(path, std::ios::binary); std::ostringstream ss; ss << f.rdbuf(); const std::string text = ss.str();
    if (text.empty()) return;
    gcm::WorkingVolume v; v.extent = 500.0; v.resolution_bits = 32;
    double t0 = now();
    const auto tape = gdxf::lex(text);
    double t1 = now();
    const auto r = gdxf::read(text, v);
    double t2 = now();
    std::printf("BENCH lex %.3fs  read(total) %.3fs  entities=%zu texts=%zu hatches=%zu\n", t1 - t0, t2 - t1, r.document.entities.size(), r.document.texts.size(), r.document.hatches.size());
    {
        Shell c; c.set_dpr(1.0); c.set_viewport(1600, 1000);
        std::vector<gsnap::Shape> shapes;
        for (const auto& e : r.document.entities) shapes.push_back(e.shape);
        double a = now(); c.canvas().load_shapes(shapes); double b = now();
        std::printf("BENCH load_shapes %.3fs\n", b - a);
        a = now(); for (const auto& t : r.document.texts) c.canvas().add_text(t.pos, t.text, t.height, t.angle); b = now();
        std::printf("BENCH add_text x%zu %.3fs\n", r.document.texts.size(), b - a);
        a = now(); c.canvas().add_dimensions(r.document.dimensions); c.canvas().add_splines(r.document.splines); b = now();
        std::printf("BENCH dims+splines %.3fs\n", b - a);
        a = now(); std::size_t h = 0; for (const auto& hh : r.document.hatches) if (c.canvas().add_hatch(hh.boundary, hh.seed, hh.pattern, hh.scale, hh.angle)) ++h; b = now();
        std::printf("BENCH hatches placed %zu of %zu %.3fs\n", h, r.document.hatches.size(), b - a);
        a = now(); for (const auto& l : r.document.layers.all()) if (!c.layers().find(l.name)) c.layers().add(l); b = now();
        std::printf("BENCH layers x%zu %.3fs\n", r.document.layers.all().size(), b - a);
        a = now(); for (const auto& def : r.document.blocks.all()) if (!c.canvas().blocks().find(def.name)) c.canvas().blocks().define(def); b = now();
        std::printf("BENCH block defs x%zu %.3fs\n", r.document.blocks.all().size(), b - a);
        a = now(); for (const auto& ref : r.document.blocks.references()) c.canvas().blocks().insert(ref); b = now();
        std::printf("BENCH block refs x%zu %.3fs\n", r.document.blocks.references().size(), b - a);
        a = now(); c.build_frame(); b = now();
        std::printf("BENCH frame after %.3fs\n", b - a);
    }
    Shell s; s.set_dpr(1.0); s.set_viewport(1600, 1000);
    double t3 = now();
    s.import_file(path);
    double t4 = now();
    std::printf("BENCH shell import %.3fs\n", t4 - t3);
    double t5 = now(); s.build_frame(); double t6 = now();
    std::printf("BENCH first frame %.3fs\n", t6 - t5);
    {
        const int n = std::getenv("GLCAD_BENCH_FRAMES") ? std::atoi(std::getenv("GLCAD_BENCH_FRAMES")) : 20;
        const double a = now();
        for (int i = 0; i < n; ++i) { s.scroll(0, i % 2 ? 1.0 : -1.0, false); s.build_frame(); }
        const double b = now();
        std::printf("BENCH zoom frames %d: %.1f ms each\n", n, (b - a) / n * 1000.0);
    }
    {
        Shell w; w.set_dpr(1.0); w.set_viewport(1600, 1000);
        const double a = now();
        w.import_file(bench_file("GLCAD_BENCH_PLAN"));
        const double b = now();
        std::printf("BENCH dwg import %.3fs\n", b - a);
        for (const auto& l : w.log()) if (l.find("imported") != std::string::npos) std::printf("  log: %s\n", l.c_str());
        int coloured = 0; for (const auto& l : w.layers().all()) if (l.color.r != l.color.g || l.color.g != l.color.b) ++coloured;
        std::printf("BENCH dwg layers %zu, %d coloured\n", w.layers().all().size(), coloured);
        w.build_frame();
        const int n = std::getenv("GLCAD_BENCH_FRAMES") ? std::atoi(std::getenv("GLCAD_BENCH_FRAMES")) : 20;
        const double c = now();
        for (int i = 0; i < n; ++i) { w.scroll(0, i % 2 ? 1.0 : -1.0, false); w.build_frame(); }
        const double d = now();
        std::printf("BENCH dwg zoom frames %d: %.1f ms each  (lines=%zu text=%zu ui=%zu)\n", n, (d - c) / n * 1000.0,
                    w.static_lines().size(), w.text().size(), w.ui().size());
        const double e = now();
        double worst = 0.0; int rebuilt = 0; gcm::u64 last_key = w.static_lines_version();
        for (int i = 0; i < n; ++i) {
            const double f0 = now();
            w.scroll(0, 0.1, false); w.build_frame();
            const double f1 = now();
            worst = std::max(worst, f1 - f0);
            if (w.static_lines_version() != last_key) { ++rebuilt; last_key = w.static_lines_version(); }
        }
        const double f = now();
        std::printf("BENCH dwg fine zoom frames %d: %.1f ms each, worst %.1f ms, %d rebuilds\n", n, (f - e) / n * 1000.0, worst * 1000.0, rebuilt);
        {
            const double h0 = now();
            for (int i = 0; i < 100; ++i) { w.pointer_move(400.0 + i, 500.0); w.build_frame(); }
            const double h1 = now();
            std::printf("BENCH dwg hover+frame: %.3f ms\n", (h1 - h0) / 100.0 * 1000.0);
            const double h2 = now();
            for (int i = 0; i < 1000; ++i) w.pointer_move(400.0 + (i % 100), 500.0);
            const double h3 = now();
            std::printf("BENCH dwg move flood x1000 (no frame): %.3f ms total\n", (h3 - h2) * 1000.0);
            const double c0 = now();
            for (int i = 0; i < 100; ++i) (void)w.cursor();
            const double c1 = now();
            for (int i = 0; i < 100; ++i) (void)w.canvas().pick(400.0 + i, 500.0);
            const double c2 = now();
            for (int i = 0; i < 100; ++i) (void)w.canvas().pick_text(400.0 + i, 500.0);
            const double c3 = now();
            for (int i = 0; i < 100; ++i) (void)w.canvas().pick_reference(400.0 + i, 500.0);
            const double c4 = now();
            std::printf("BENCH dwg cursor %.3f pick %.3f pick_text %.3f pick_ref %.3f ms each\n",
                        (c1 - c0) * 10.0, (c2 - c1) * 10.0, (c3 - c2) * 10.0, (c4 - c3) * 10.0);
            const double d0 = now();
            for (int i = 0; i < 100; ++i) w.canvas().hover(400.0 + i, 500.0);
            const double d1 = now();
            for (int i = 0; i < 100; ++i) { w.pointer_move(400.0 + i, 500.0); (void)w.cursor(); }
            const double d2 = now();
            std::printf("BENCH dwg canvas.hover %.3f, move(uncoalesced follow-up) %.3f ms each\n",
                        (d1 - d0) * 10.0, (d2 - d1) * 10.0);
        }
        const double g = now();
        for (int i = 0; i < n; ++i) w.build_frame();
        const double h = now();
        std::printf("BENCH dwg idle frames %d: %.1f ms each\n", n, (h - g) / n * 1000.0);
    }
    for (const auto& l : s.log()) std::printf("  log: %s\n", l.c_str());
}

TEST(bench_dwg_zoom_with_font) {
    Shell w; w.set_dpr(1.0); w.set_viewport(1600, 1000);
    std::string error;
    if (!w.load_font(&error)) { std::printf("BENCH font load failed: %s\n", error.c_str()); return; }
    if (!w.import_file(bench_file("GLCAD_BENCH_PLAN"))) return;
    w.build_frame();
    const int n = std::getenv("GLCAD_BENCH_FRAMES") ? std::atoi(std::getenv("GLCAD_BENCH_FRAMES")) : 30;
    const double a = now();
    double worst = 0.0;
    for (int i = 0; i < n; ++i) {
        const double f0 = now();
        w.scroll(0, i % 2 ? 1.0 : -1.0, false);
        w.build_frame();
        worst = std::max(worst, now() - f0);
    }
    const double b = now();
    std::printf("BENCH dwg zoom+font frames %d: %.1f ms each, worst %.1f ms (text verts=%zu labels=%zu)\n",
        n, (b - a) / n * 1000.0, worst * 1000.0, w.text().size(), w.canvas().labels().size());
    const double i0 = now();
    for (int i = 0; i < n; ++i) w.build_frame();
    const double i1 = now();
    std::printf("BENCH dwg idle+font frames %d: %.1f ms each (text verts=%zu)\n",
        n, (i1 - i0) / n * 1000.0, w.text().size());
}

TEST(bench_gateway_text_audit) {
    const auto in_atlas = [](unsigned cp) {
        if (cp >= 32 && cp <= 126) return true;
        switch (cp) {
            case 0xB0: case 0xB1: case 0xD8: case 0xF8: case 0xB5:
            case 0x2205: case 0x2300:
            case 0x2013: case 0x2014:
            case 0x2190: case 0x2191: case 0x2192: case 0x2193:
            case 0xD7: case 0xF7: case 0xB2: case 0xB3:
            case 0x221E: return true;
        }
        return false;
    };
    const auto audit = [&](Shell& s, const char* tag) {
        std::printf("== AUDIT %s ==\n", tag);
        for (const auto& l : s.log())
            if (l.find("imported") != std::string::npos || l.find("dimension style") != std::string::npos)
                std::printf("  log: %s\n", l.c_str());
        s.build_frame();
        const auto& labels = s.canvas().labels();
        std::printf("  labels=%zu texts=%zu dims=%zu\n", labels.size(),
                    s.canvas().texts().size(), s.canvas().dimensions().size());
        unsigned missing_chars = 0;
        for (const auto& lab : labels) {
            std::size_t i = 0;
            while (i < lab.text.size()) {
                const unsigned char c = static_cast<unsigned char>(lab.text[i]);
                unsigned cp; std::size_t n = 1;
                if (c < 0x80) cp = c;
                else if ((c >> 5) == 6 && i + 1 < lab.text.size()) { cp = ((c & 31) << 6) | (lab.text[i + 1] & 63); n = 2; }
                else if ((c >> 4) == 14 && i + 2 < lab.text.size()) { cp = ((c & 15) << 12) | ((lab.text[i + 1] & 63) << 6) | (lab.text[i + 2] & 63); n = 3; }
                else if ((c >> 3) == 30 && i + 3 < lab.text.size()) { cp = ((c & 7) << 18) | ((lab.text[i + 1] & 63) << 12) | ((lab.text[i + 2] & 63) << 6) | (lab.text[i + 3] & 63); n = 4; }
                else cp = 0xFFFD;
                if (!in_atlas(cp)) {
                    ++missing_chars;
                    std::printf("  MISSING U+%04X in label %zu%s: \"%s\"\n", cp,
                                &lab - labels.data(),
                                lab.stroke_font ? " [stroke-font]" : "",
                                lab.text.c_str());
                    if (missing_chars > 40) { std::printf("  ...truncated\n"); break; }
                }
                i += n;
            }
        }
        if (missing_chars == 0) std::printf("  all label characters are in the atlas\n");
        std::map<std::string, int> counts;
        for (const auto& lab : labels) if (!lab.text.empty()) ++counts[lab.text];
        for (const auto& [s, n] : counts) std::printf("  L %3d \"%s\"\n", n, s.c_str());
    };
    {
        gcm::WorkingVolume v; v.extent = 500.0; v.resolution_bits = 32;
        const auto r = gdwg::read_file(bench_file("GLCAD_BENCH_PLAN"), v);
        std::printf("  raw objects=%zu unknown=%zu texts=%zu skipped-warnings:\n", r.raw.size(), r.unknown_objects, r.texts.size());
        for (const auto& s : r.skipped) std::printf("    - %s\n", s.c_str());
        const auto name_of = [&](gcm::u16 t) -> std::string {
            for (const auto& [n, nm] : r.classes) if (n == t) return nm;
            const char* fixed = gdwg::object_type_name(t);
            return fixed[0] ? std::string(fixed) : std::string("<none>");
        };
        std::map<std::string, std::size_t> tally;
        for (const auto& o : r.raw) ++tally[name_of(o.type)];
        for (const auto& [nm, n] : tally) std::printf("    type %-24s %zu\n", nm.c_str(), n);
        std::printf("  texts in file: %zu\n", r.texts.size());
    }
    {
        Shell s; s.set_dpr(1.0); s.set_viewport(1600, 1000);
        const bool ok = s.import_file(bench_file("GLCAD_BENCH_PLAN_DXF"));
        std::printf("  dxf-disguised import ok=%d\n", ok ? 1 : 0);
        if (ok) audit(s, "dxf-disguised");
    }
    {
        Shell s; s.set_dpr(1.0); s.set_viewport(1600, 1000);
        const bool ok = s.import_file(bench_file("GLCAD_BENCH_PLAN"));
        std::printf("  dwg import ok=%d\n", ok ? 1 : 0);
        if (ok) audit(s, "dwg");
    }
}

TEST(bench_quiet_frame_on_a_real_drawing) {
    const char* path = bench_file("GLCAD_BENCH_FLOOR");
    { std::ifstream probe(path, std::ios::binary); if (!probe) return; }

    Shell s; s.set_dpr(1.0); s.set_viewport(1600, 1000);
    if (!s.open_file(path)) return;
    s.command("ZOOMEXTENTS");
    for (int i = 0; i < 10; ++i) s.build_frame();

    const auto encodes = s.metadata_encodes();
    const double a = now();
    constexpr int kFrames = 200;
    for (int i = 0; i < kFrames; ++i) s.build_frame();
    const double b = now();
    std::printf("BENCH quiet frame %.3f ms  document encodes over %d frames: %llu\n",
                (b - a) * 1000.0 / kFrames, kFrames,
                (unsigned long long)(s.metadata_encodes() - encodes));

    const auto& doc = s.document();
    std::printf("BENCH document: %zu entities, %zu spaces  "
                "(lines=%zu arcs=%zu circles=%zu text=%zu dims=%zu sketches=%zu bodies=%zu)\n",
                doc.entity_count(), doc.space_count(),
                doc.of_kind(gdoc::Kind::Line).size(), doc.of_kind(gdoc::Kind::Arc).size(),
                doc.of_kind(gdoc::Kind::Circle).size(), doc.of_kind(gdoc::Kind::Text).size(),
                doc.of_kind(gdoc::Kind::Dimension).size(),
                doc.of_kind(gdoc::Kind::Sketch).size(), doc.of_kind(gdoc::Kind::Body).size());
    {
        const double m0 = now();
        for (int i = 0; i < 20; ++i) s.sync_document();
        const double m1 = now();
        std::printf("BENCH document sync (nothing changed): %.4f ms each\n",
                    (m1 - m0) * 1000.0 / 20);
        const double r0 = now();
        for (int i = 0; i < 20; ++i) { s.force_document_resync(); s.sync_document(); }
        const double r1 = now();
        std::printf("BENCH document sync (full reconcile): %.3f ms each\n",
                    (r1 - r0) * 1000.0 / 20);
    }
}

TEST(bench_extrude_regions_in_a_real_plan) {
    const char* path = bench_file("GLCAD_BENCH_FLOOR");
    { std::ifstream probe(path, std::ios::binary); if (!probe) return; }

    Shell s; s.set_dpr(1.0); s.set_viewport(1600, 1000);
    if (!s.open_file(path)) return;
    s.command("ZOOMEXTENTS");
    for (int i = 0; i < 6; ++i) s.build_frame();
    s.activate_tool(ToolId::Extrude);
    s.build_frame();

    int hits = 0, tried = 0;
    for (int gy = 1; gy < 16; ++gy)
        for (int gx = 1; gx < 24; ++gx) {
            const double px = 1600.0 * gx / 24.0, py = 1000.0 * gy / 16.0;
            s.pointer_move(px, py);
            s.build_frame();
            ++tried;
            if (s.modeller().hovered().valid) ++hits;
        }
    std::printf("BENCH extrude regions: %d of %d probes found a loop  (segments=%zu)\n",
                hits, tried, s.sketch_segment_count());
}

TEST(bench_document_round_trip_on_a_real_plan) {
    const char* path = bench_file("GLCAD_BENCH_FLOOR");
    { std::ifstream probe(path, std::ios::binary); if (!probe) return; }

    Shell s; s.set_dpr(1.0); s.set_viewport(1600, 1000);
    if (!s.open_file(path)) return;
    s.build_frame();

    const auto space = s.active_space_id();
    const auto& doc = s.document();
    std::printf("BENCH round-trip subject: %zu entities "
                "(shapes=%zu inserts=%zu notes=%zu)\n",
                doc.entity_count(), s.canvas().shapes().size(),
                s.canvas().blocks().references().size(), s.canvas().texts().size());

    const auto& shapes_in = s.canvas().shapes();
    const auto shapes_out = s.shapes_from_document(space);
    CHECK_MSG(shapes_out.size() == shapes_in.size(),
              "%zu shapes in, %zu out", shapes_in.size(), shapes_out.size());
    std::size_t bad = 0;
    if (shapes_out.size() == shapes_in.size())
        for (std::size_t i = 0; i < shapes_in.size(); ++i) {
            const auto& x = shapes_in[i];
            const auto& y = shapes_out[i];
            if (x.key != y.key || x.kind != y.kind || x.layer != y.layer ||
                x.a.x != y.a.x || x.a.y != y.a.y || x.b.x != y.b.x || x.b.y != y.b.y ||
                x.radius != y.radius || x.start_angle != y.start_angle ||
                x.end_angle != y.end_angle || x.poly != y.poly ||
                x.poly_index != y.poly_index || x.attrs.color != y.attrs.color ||
                x.attrs.linetype != y.attrs.linetype)
                ++bad;
        }
    CHECK_MSG(bad == 0, "%zu of %zu shapes came back different", bad, shapes_in.size());

    const auto& ins_in = s.canvas().blocks().references();
    const auto ins_out = s.inserts_from_document(space);
    CHECK_MSG(ins_out.size() == ins_in.size(),
              "%zu insertions in, %zu out", ins_in.size(), ins_out.size());
    std::size_t bad_ins = 0;
    if (ins_out.size() == ins_in.size())
        for (std::size_t i = 0; i < ins_in.size(); ++i) {
            const auto& x = ins_in[i];
            const auto& y = ins_out[i];
            if (x.name != y.name || x.layer != y.layer ||
                x.insertion.x != y.insertion.x || x.insertion.y != y.insertion.y ||
                x.scale_x != y.scale_x || x.scale_y != y.scale_y ||
                x.rotation != y.rotation || x.selection_id != y.selection_id ||
                x.attributes.size() != y.attributes.size() ||
                x.notes.size() != y.notes.size())
                ++bad_ins;
        }
    CHECK_MSG(bad_ins == 0, "%zu of %zu insertions came back different",
              bad_ins, ins_in.size());

    const auto& notes_in = s.canvas().texts();
    const auto notes_out = s.notes_from_document(space);
    CHECK_MSG(notes_out.size() == notes_in.size(),
              "%zu notes in, %zu out", notes_in.size(), notes_out.size());
}

TEST(bench_zoom_with_extrude_armed) {
    const char* path = bench_file("GLCAD_BENCH_FLOOR");
    { std::ifstream probe(path, std::ios::binary); if (!probe) return; }

    Shell s; s.set_dpr(1.0); s.set_viewport(1600, 1000);
    if (!s.open_file(path)) return;
    s.command("ZOOMEXTENTS");
    for (int i = 0; i < 8; ++i) s.build_frame();

    const auto time_scrolls = [&](const char* what) {
        const auto encodes = s.metadata_encodes();
        const double a = now();
        constexpr int kSteps = 60;
        for (int i = 0; i < kSteps; ++i) {
            s.scroll(0, i % 2 ? 1 : -1, false);
            s.build_frame();
        }
        const double b = now();
        std::printf("BENCH zoom %-18s %.3f ms per wheel step, %llu document encodes\n",
                    what, (b - a) * 1000.0 / kSteps,
                    (unsigned long long)(s.metadata_encodes() - encodes));
    };

    time_scrolls("(plain)");
    s.activate_tool(ToolId::Extrude);
    s.build_frame();
    time_scrolls("(extrude armed)");
}

TEST(bench_wall_solid_zoom) {
    Shell s; s.set_dpr(1.0); s.set_viewport(1600,1000);
    s.canvas().set_display_unit(gunit::Length::Metre);
    s.canvas().set_tool(Tool::Line);
    for(int i=0;i<240;++i) {
        const std::string x=std::to_string((i%20)*5), y=std::to_string((i/20)*5);
        s.canvas().type_point(x+","+y);
        s.canvas().type_point(std::to_string((i%20)*5+4)+","+y);
        s.canvas().cancel();
    }
    s.canvas().set_tool(Tool::Select);
    s.command("WALLS 0.2 2.7");
    s.build_frame();
    const auto& model=static_cast<const Shell&>(s).modeller();
    CHECK(model.bodies().size()==240);
    const auto version=s.mesh_version();
    const auto first=now();
    for(int i=0;i<30;++i) { s.scroll(0,i%2?1.0:-1.0,false);s.build_frame(); }
    std::printf("BENCH wall solid zoom: %.3f ms/frame (%zu bodies, %zu mesh vertices)\n",
        (now()-first)*1000/30,model.bodies().size(),s.mesh().size());
    CHECK(s.mesh_version()==version);
    double sum=0;
    const auto uncached=now();
    for(int frame=0;frame<10;++frame)
        for(const auto& body:model.bodies()) sum+=gtopo::volume(body,s.canvas().volume());
    const auto uncached_ms=(now()-uncached)*100;
    for(std::size_t i=0;i<model.bodies().size();++i) (void)model.describe(i);
    const auto cached=now();
    for(int frame=0;frame<10;++frame)
        for(std::size_t i=0;i<model.bodies().size();++i) (void)model.describe(i);
    std::printf("BENCH wall volume labels: uncached %.3f ms/frame, cached %.3f ms/frame\n",
        uncached_ms,(now()-cached)*100);
    CHECK(sum>0);
    s.command("UNDO"); s.build_frame();
    CHECK(s.mesh_version()!=version);
    CHECK(s.mesh().empty());
}

TEST(frame_mesh_cache_tracks_highlights_and_plot_preview) {
    Shell s; s.set_dpr(1.0); s.set_viewport(1600,1000);
    s.canvas().set_display_unit(gunit::Length::Metre);
    s.canvas().set_tool(Tool::Line);
    s.canvas().type_point("0,0");s.canvas().type_point("4,0");s.canvas().cancel();
    s.canvas().set_tool(Tool::Select); s.command("WALLS 1.0 2.7");
    s.camera().look(gview::StandardView::Top);
    s.camera().set_target(gview::Point3::from(2,0,0));s.fit_width(8);s.build_frame();
    const auto initial=s.mesh_version(); const auto count=s.mesh().size();
    CHECK(count>0);
    s.build_frame(); CHECK(s.mesh_version()==initial);
    double x,y; CHECK(s.camera().project(gview::Point3::from(2,0,2.7),x,y));
    s.pointer_move(x,y);s.build_frame();
    const auto hovered=s.mesh_version(); CHECK(hovered>initial);
    CHECK(s.mesh().size()>count);
    s.build_frame();CHECK(s.mesh_version()==hovered);
    s.pointer_move(0,0);s.build_frame();CHECK(s.mesh_version()>hovered);
    const auto normal=s.mesh_version();
    CHECK(s.open_plot_preview());s.build_frame();CHECK(s.mesh().empty());
    CHECK(s.mesh_version()>normal);
    s.command("PREVIEW CLOSE");s.build_frame();CHECK(s.mesh().size()>=count);
}

TEST(frame_wireframe_background_has_only_valid_overlay_ranges) {
    Shell s; s.set_dpr(1.0);s.set_viewport(1200,800);
    auto& c=s.canvas();c.set_display_unit(gunit::Length::Metre);
    c.set_tool(Tool::Line);c.type_point("0,0");c.type_point("4,0");c.cancel();
    s.command("WALLS 0.2 2.7");s.set_mode(Mode::Sketch);
    c.clear();c.set_tool(Tool::Line);
    for(const char* p:{"0,0","4,0","4,3","0,3","0,0"}) CHECK(c.type_point(p));
    c.cancel();c.set_tool(Tool::Select);
    CHECK(c.background_at_world(gcm::to_lattice(c.volume(),2,1),gdoc::Color::rgb(80,120,160),0,60));
    s.command("VSCURRENT wireframe");s.build_frame();
    CHECK(!s.mesh().empty());
    for(const auto& item:s.scene().items()) {
        CHECK(item.kind==grender::DrawKind::Overlay);
        CHECK(item.first+item.count<=s.mesh().size());
    }
}

TEST(mechanical_drawing_check_omits_residential_requirements) {
    Shell s;s.command("DISCIPLINE MECHANICAL");
    const auto findings=s.plan_check();
    CHECK(findings.size()==1);CHECK(findings[0].rule=="drawing.clean");
    s.command("DISCIPLINE ARCHITECTURAL");
    CHECK(s.plan_check()[0].rule=="plan.empty");
}

TEST(walls_conversion_clears_in_progress_line_preview) {
    Shell s;s.set_dpr(1);s.set_viewport(1200,800);
    auto& c=s.canvas();c.set_display_unit(gunit::Length::Metre);
    c.set_tool(Tool::Line);CHECK(c.type_point("0,0"));CHECK(c.type_point("4,0"));
    s.command("WALLS 200mm");
    CHECK(c.tool()==Tool::Select);
    grender::d2::DrawList dynamic;c.build_dynamic_only(dynamic);
    CHECK(dynamic.lines().empty());
}

TEST(command_history_is_transparent_until_hover_and_scrolls_without_zoom) {
    Shell s;s.set_dpr(1);s.set_viewport(1200,800);
    for(int i=0;i<40;++i) s.log_line("History entry "+std::to_string(i));
    s.build_frame();CHECK(s.log().size()>=40);
    gui::Rect first{},last{};
    for(const auto& item:s.accessible_ui()) if(item.label.find("History entry ")==0) {
        if(first.w==0) first=item.bounds;last=item.bounds;
    }
    CHECK(first.w>0);
    const auto card_visible=[&] {
        for(const auto& item:s.ui())
            if(std::abs(item.rect[0]-(first.x-10))<.01 && std::abs(item.rect[1]-(first.y-6))<.01 &&
               std::abs(item.rect[2]-(first.w+20))<.01 && item.params[2]==0 && item.color[3]>.9f) return true;
        return false;
    };
    CHECK(!card_visible());
    s.pointer_move(first.x+20,first.y+5);s.build_frame();CHECK(card_visible());
    const auto distance=s.camera().distance();s.scroll(0,4,false);s.build_frame();
    CHECK(s.camera().distance()==distance);
    bool latest_visible=false;
    for(const auto& item:s.accessible_ui()) latest_visible|=item.label=="History entry 39";
    CHECK(!latest_visible);
    s.pointer_move(0,0);s.build_frame();CHECK(!card_visible());
}

TEST(bench_draw_inside_populated_closed_rooms) {
    for (bool backgrounds : {false,true}) {
        Shell shell; shell.set_dpr(1); shell.set_viewport(1200,800);
        auto& canvas=shell.canvas(); canvas.set_display_unit(gunit::Length::Metre);
        std::vector<gsnap::Shape> shapes;
        const auto add=[&](double ax,double ay,double bx,double by) {
            gsnap::Shape shape; shape.a=gcm::to_lattice(canvas.volume(),ax,ay);
            shape.b=gcm::to_lattice(canvas.volume(),bx,by); shapes.push_back(shape);
        };
        for(int room=0;room<12;++room) {
            const double x=(room%4)*6.,y=(room/4)*6.;
            for(int i=0;i<24;++i) {
                const double a=i/6.,b=(i+1)/6.;
                add(x+a,y,x+b,y); add(x+4,y+a,x+4,y+b);
                add(x+4-a,y+4,x+4-b,y+4); add(x,y+4-a,x,y+4-b);
            }
            for(int i=0;i<16;++i) add(x+.5,y+.25+i*.2,x+1,y+.25+i*.2);
        }
        canvas.load_shapes(std::move(shapes));
        const double setup_start=now();
        if(backgrounds) for(int room=0;room<12;++room)
            CHECK(canvas.background_at_world(gcm::to_lattice(canvas.volume(),(room%4)*6.+2,(room/4)*6.+2),
                gdoc::Color::rgb(90,140,190),.01,35));
        const double setup_ms=(now()-setup_start)*1000;
        shell.camera().set_target(gview::Point3::from(2,2,0));shell.fit_width(12);shell.build_frame();
        double drawing=0,frame=0,refresh=0,unchanged=0;
        const auto before=canvas.shapes().size();
        for(int i=0;i<6;++i) {
            double start=now(); canvas.set_tool(Tool::Line);
            canvas.type_point("2,"+std::to_string(1.+i*.25));
            canvas.type_point("3,"+std::to_string(1.+i*.25)); canvas.cancel();
            drawing+=(now()-start)*1000;
            start=now();shell.build_frame();frame+=(now()-start)*1000;
            start=now();canvas.refresh_hatches();refresh+=(now()-start)*1000;
            start=now();canvas.refresh_hatches();unchanged+=(now()-start)*1000;
        }
        double pointer_move=0,pointer_click=0,pointer_frame=0;
        for(int i=0;i<6;++i) {
            canvas.set_tool(Tool::Line);
            for(double x:{2.,3.}) {
                double px=0,py=0;CHECK(shell.camera().project(gview::Point3::from(x,.8+i*.15,0),px,py));
                double start=now();shell.pointer_move(px,py);pointer_move+=(now()-start)*1000;
                start=now();shell.build_frame();pointer_frame+=(now()-start)*1000;
                start=now();shell.pointer_down(px,py);shell.pointer_up(px,py);pointer_click+=(now()-start)*1000;
                start=now();shell.build_frame();pointer_frame+=(now()-start)*1000;
            }
            canvas.cancel();
        }
        std::printf("BENCH closed rooms backgrounds=%d pointer-move=%.2fms pointer-down-up=%.2fms pointer-frame=%.2fms per endpoint\n",
            int(backgrounds),pointer_move/12,pointer_click/12,pointer_frame/24);
        CHECK(canvas.shapes().size()==before+12);
        CHECK(canvas.hatches().size()==(backgrounds?12u:0u));
        for(const auto& hatch:canvas.hatches())CHECK(hatch.valid && !hatch.orphaned);
        std::printf("BENCH closed rooms backgrounds=%d shapes=%zu setup=%.2fms draw=%.2fms frame=%.2fms refresh=%.2fms unchanged-refresh=%.2fms per edit\n",
            int(backgrounds),canvas.shapes().size(),setup_ms,drawing/6,frame/6,refresh/6,unchanged/6);
    }
}

TEST(bench_pointer_draw_inside_document_regions) {
    const char* path=std::getenv("GLCAD_BENCH_DRAWING");
    if(!path) return;
    Shell shell; shell.set_dpr(1); shell.set_viewport(1600,1000);
    std::string font_error; CHECK(shell.load_font(&font_error));
    CHECK(shell.open_file(path));
    shell.set_mode(Mode::Sketch);
    shell.camera().look(gview::StandardView::Top);
    shell.camera().set_target(gview::Point3::from(3,3,0));shell.fit_width(16);
    shell.build_frame();
    double move=0,click=0,frame=0,worst=0;
    shell.activate_tool(ToolId::Line);
    const int steps=std::getenv("GLCAD_BENCH_DRAW_STEPS") ? std::max(12,std::atoi(std::getenv("GLCAD_BENCH_DRAW_STEPS"))) : 12;
    for(int i=0;i<steps;++i) {
        const double x=1.5+(i%2)*.5,y=1.5+((i/2)%6)*.15;
        double px,py; CHECK(shell.camera().project(gview::Point3::from(x,y,0),px,py));
        double start=now();shell.pointer_move(px,py);move+=(now()-start)*1000;
        start=now();shell.build_frame();frame+=(now()-start)*1000;
        start=now();shell.pointer_down(px,py);shell.pointer_up(px,py);
        const double edit=(now()-start)*1000;click+=edit;worst=std::max(worst,edit);
        start=now();shell.build_frame();const double draw=(now()-start)*1000;frame+=draw;worst=std::max(worst,draw);
    }
    std::printf("BENCH document pointer draw shapes=%zu hatches=%zu move=%.2fms click=%.2fms frame=%.2fms worst=%.2fms\n",
        shell.canvas().shapes().size(),shell.canvas().hatches().size(),move/steps,click/steps,frame/(2*steps),worst);
}

TEST(bench_door_drag_full_frames_in_document) {
    const char* path=std::getenv("GLCAD_BENCH_DRAWING");
    if(!path)return;
    Shell shell;shell.set_dpr(1);shell.set_viewport(1600,1000);
    std::string error;CHECK(shell.load_font(&error));CHECK(shell.open_file(path));
    shell.set_mode(Mode::Sketch);shell.camera().look(gview::StandardView::Top);
    auto& c=shell.canvas();c.set_tool(Tool::Select);
    const auto found=std::find_if(c.shapes().begin(),c.shapes().end(),[](const auto& s) {
        return s.kind==gsnap::ShapeKind::Arc&&s.attrs.user.contains("opening_auto");
    });
    CHECK(found!=c.shapes().end());if(found==c.shapes().end())return;
    const auto door=*found;
    double x,y,rx,ry;gcm::to_world(c.volume(),door.a,x,y);
    gcm::to_world(c.volume(),gcm::Lattice2{door.radius,0},rx,ry);
    shell.camera().set_target(gview::Point3::from(x,y,0));shell.fit_width(std::max(8.,rx*12));
    shell.build_frame();
    std::vector<std::size_t> selection;
    for(std::size_t i=0;i<c.shapes().size();++i)if(c.shapes()[i].poly==door.poly)selection.push_back(i);
    c.set_selection(selection);shell.build_frame();
    const double angle=(door.start_angle+door.end_angle)*.5;
    double px,py;CHECK(shell.camera().project(gview::Point3::from(x+rx*std::cos(angle),y+rx*std::sin(angle),0),px,py));
    double sx,sy;c.view().screen_of(x+rx*std::cos(angle),y+rx*std::sin(angle),sx,sy);
    CHECK(c.begin_selection_drag(sx,sy));
    const int steps=std::getenv("GLCAD_BENCH_DOOR_STEPS")?std::max(12,std::atoi(std::getenv("GLCAD_BENCH_DOOR_STEPS"))):24;
    double move=0,frame=0,worst=0;
    const auto work=c.drafting_work();
    for(int i=0;i<steps;++i) {
        const double start=now();shell.pointer_move(px+12+(i%24)*2,py);
        const double moved=now();shell.build_frame();const double end=now();
        move+=(moved-start)*1000;frame+=(end-moved)*1000;worst=std::max(worst,(end-start)*1000);
    }
    std::printf("BENCH full door drag: shapes=%zu notes=%zu hatches=%zu move=%.2fms frame=%.2fms worst=%.2fms\n",
        c.shapes().size(),c.texts().size(),c.hatches().size(),move/steps,frame/steps,worst);
    std::printf("BENCH door work/update: edges=%.1f doors=%.1f traces=%.1f segments=%.1f hatches=%.1f\n",
        double(c.drafting_work().opening_edges-work.opening_edges)/steps,
        double(c.drafting_work().opening_doors-work.opening_doors)/steps,
        double(c.drafting_work().background_traces-work.background_traces)/steps,
        double(c.drafting_work().background_segments-work.background_segments)/steps,
        double(c.drafting_work().hatch_updates-work.hatch_updates)/steps);
    shell.key_special(1);shell.build_frame();CHECK(!c.selection_dragging());
}

TEST(bench_drawing_with_loaded_outline_notes) {
    Shell shell;shell.set_dpr(1);shell.set_viewport(1600,1000);
    std::string error;CHECK(shell.load_font(&error));auto& canvas=shell.canvas();
    std::vector<Canvas::TextItem> notes;
    for(int i=0;i<240;++i) {
        Canvas::TextItem note;note.pos=gcm::to_lattice(canvas.volume(),(i%20)*.7,(i/20)*.4);
        note.text="{\\f"+shell.font().family()+"|b1|i0;BUILDING 012345}";note.height=.1;
        notes.push_back(std::move(note));
    }
    canvas.load_texts(std::move(notes));shell.fit_width(18);
    const double start=now();shell.build_frame();const double first=(now()-start)*1000;
    double total=0,worst=0,canvas_total=0,shell_total=0;
    const bool stages=std::getenv("GLCAD_BENCH_NOTE_STAGES")!=nullptr;
    const int edits=std::getenv("GLCAD_BENCH_NOTE_EDITS") ? std::max(12,std::atoi(std::getenv("GLCAD_BENCH_NOTE_EDITS"))) : 12;
    for(int i=0;i<edits;++i) {
        canvas.set_tool(Tool::Line);canvas.type_point("1m,"+std::to_string(1.+i*.1)+"m");
        canvas.type_point("2m,"+std::to_string(1.+i*.1)+"m");canvas.cancel();
        const double frame_start=now();
        if(stages) {grender::d2::DrawList dynamic;canvas.build_dynamic_only(dynamic);}
        const double shell_start=now();shell.build_frame();const double end=now();
        const double duration=(end-frame_start)*1000;
        canvas_total+=(shell_start-frame_start)*1000;shell_total+=(end-shell_start)*1000;
        total+=duration;worst=std::max(worst,duration);
    }
    CHECK(canvas.shapes().size()==std::size_t(edits));CHECK(canvas.texts().size()==240);
    CHECK(!shell.font().outline('B').triangles.empty());
    std::printf("BENCH outline notes: 240 notes with loaded font, first %.2fms, edit frame average %.2fms worst %.2fms\n",first,total/edits,worst);
    if(stages)std::printf("BENCH note stages: Canvas %.2fms Shell %.2fms\n",canvas_total/edits,shell_total/edits);
}

TEST(bench_partitioned_building_wall_tool) {
    Shell shell;shell.set_dpr(1);shell.set_viewport(1600,1000);
    shell.canvas().set_display_unit(gunit::Length::Foot);
    shell.fit_width(22);shell.build_frame();
    struct Wall {double ax,ay,bx,by;};
    std::vector<Wall> walls{{0,0,40,0},{40,0,40,60},{40,60,0,60},{0,60,0,0}};
    walls.push_back({18,0,18,60});walls.push_back({22,0,22,60});
    for(int y=10;y<60;y+=10) {walls.push_back({0,double(y),18,double(y)});walls.push_back({22,double(y),40,double(y)});}
    for(int y=0;y<60;y+=20) {
        walls.push_back({4,double(y),4,double(y+4)});
        walls.push_back({4,double(y+4),8,double(y+4)});
        walls.push_back({8,double(y+4),8,double(y)});
    }
    double total=0,worst=0;
    for(std::size_t i=0;i<walls.size();++i) {
        const auto& w=walls[i];
        const auto point=[](double x,double y){return std::to_string(x)+"ft,"+std::to_string(y)+"ft";};
        gai::ToolCall call;call.function.name="draw_wall";
        call.function.arguments=gai::Json{{"from",point(w.ax,w.ay)},{"to",point(w.bx,w.by)},
            {"type",i<4?"exterior":"interior"}}.dump();
        const auto start=now();
        const auto result=gai::Json::parse(ai_tools::execute(shell,call,ai_tools::Origin::Silent));
        const auto drawn=now();shell.sync_document();const auto synced=now();shell.build_frame();const auto end=now();
        CHECK_MSG(!result.contains("error"),"wall %zu: %s",i,result.dump().c_str());
        CHECK(!result.contains("note"));
        const auto elapsed=(end-start)*1000;total+=elapsed;worst=std::max(worst,elapsed);
        std::printf("BENCH building wall %zu/%zu: tool %.2fms sync %.2fms frame %.2fms shapes %zu hatches %zu\n",
            i+1,walls.size(),(drawn-start)*1000,(synced-drawn)*1000,(end-synced)*1000,
            shell.canvas().shapes().size(),shell.canvas().hatches().size());std::fflush(stdout);
    }
    std::printf("BENCH partitioned 40x60ft building: %zu walls average %.2fms worst %.2fms\n",walls.size(),total/walls.size(),worst);
    CHECK(static_cast<const Shell&>(shell).modeller().bodies().empty());
    CHECK(shell.canvas().hatches().size()==1);
    CHECK_MSG(worst<1000,"one wall edit stalled for %.2fms",worst);
}
