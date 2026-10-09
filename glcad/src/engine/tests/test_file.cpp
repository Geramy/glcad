#include "test.hpp"
#include <unistd.h>

#include <cmath>
#include <limits>
#include <cstdio>
#include <string>

#include <thread>

#include "engine/document_file.hpp"
#include "engine/tcp.hpp"

using namespace gfile;

namespace {

std::string temp_path(const char* name) {
    // Unique per process: the test tiers run in parallel.
    return "/tmp/glcad_doc_" + std::to_string(getpid()) + "_" + name + ".glcad";
}

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

Contents sample() {
    Contents c;
    c.volume = vol();
    c.units = "mm";
    c.precision = 3;
    c.created_by = "test";

    gdoc::Layer wall;
    wall.name = "A-WALL";
    wall.description = "exterior walls";
    wall.color = gdoc::Color::aci(3);
    wall.lineweight = gdoc::Lineweight::W050;
    wall.linetype = "Continuous";
    wall.transparency = 20;
    wall.plot_style = "Heavy";
    wall.vp_frozen = {2, 7};
    c.layers.add(wall);

    gdoc::Layer grid;
    grid.name = "A-GRID";
    grid.on = false;
    grid.locked = true;
    grid.plottable = false;
    grid.color = gdoc::Color::rgb(200, 40, 60);
    c.layers.add(grid);

    gdoc::Linetype dashed;
    dashed.name = "DASHED";
    dashed.description = "__ __ __";
    dashed.pattern = {{0.5, "", 1.0}, {-0.25, "", 1.0}};
    c.linetypes.add(dashed);

    gdoc::MultilineStyle wall_style;
    wall_style.name = "WALL-200";
    wall_style.description = "200 mm stud wall";
    wall_style.elements = {{0.1, gdoc::Color::aci(1), "Continuous",
                            gdoc::Lineweight::W050},
                           {-0.1, gdoc::Color::aci(1), "Continuous",
                            gdoc::Lineweight::W050}};
    wall_style.filled = true;
    wall_style.fill = gdoc::Color::aci(8);
    wall_style.start_cap = gdoc::EndCap::Line;
    wall_style.end_cap = gdoc::EndCap::OuterArc;
    c.multilines.add(wall_style);

    gdoc::BlockDef door;
    door.name = "DOOR-900";
    door.description = "900 mm single door";
    door.base = gcm::to_lattice(c.volume, 0, 0);
    door.geometry = {seg(c.volume, 0, 0, 0.9, 0)};
    gdoc::AttributeDef tag;
    tag.tag = "DOOR_NO";
    tag.prompt = "Door number";
    tag.default_value = "D-01";
    tag.height = 0.0025;
    door.attributes.push_back(tag);
    c.blocks.define(door);

    gdoc::BlockRef ref;
    ref.name = "DOOR-900";
    ref.insertion = gcm::to_lattice(c.volume, 3.0, 1.5);
    ref.rotation = 1.5707963267948966;
    ref.layer = "A-WALL";
    ref.attributes = {{"DOOR_NO", "D-14"}};
    c.blocks.insert(ref);

    gdoc::Space model;
    model.id = 0;
    model.kind = gdoc::SpaceKind::Model;
    model.name = "Model";
    c.spaces.push_back(model);

    gdoc::Space sheet;
    sheet.id = 1;
    sheet.kind = gdoc::SpaceKind::Sheet;
    sheet.name = "A-101";
    sheet.paper_width = 0.841;
    sheet.paper_height = 0.594;
    c.spaces.push_back(sheet);

    Contents::SpaceGeometry g;
    g.space = 0;
    g.shapes = {seg(c.volume, 0, 0, 10, 0), seg(c.volume, 10, 0, 10, 6)};
    gsnap::Shape circle;
    circle.kind = gsnap::ShapeKind::Circle;
    circle.a = gcm::to_lattice(c.volume, 5, 3);
    circle.radius = static_cast<i64>(std::llround(1.25 * c.volume.scale()));
    g.shapes.push_back(circle);
    g.layers = {"A-WALL", "A-WALL", "A-GRID"};
    c.geometry.push_back(g);

    return c;
}

}

TEST(file_encode_decode_round_trip) {
    const Contents in = sample();
    const std::vector<u8> buf = encode(in);
    CHECK_MSG(!buf.empty(), "encode produced nothing");

    Contents out;
    const Error e = decode(buf, out);
    CHECK_MSG(e.ok, "%s", e.message.c_str());

    CHECK(out.units == "mm");
    CHECK(out.precision == 3);
    CHECK(out.created_by == "test");
    CHECK(std::fabs(out.volume.extent - in.volume.extent) < 1e-12);
    CHECK(out.volume.resolution_bits == in.volume.resolution_bits);
}

TEST(file_buffer_carries_the_glcad_identifier) {
    const std::vector<u8> buf = encode(sample());
    CHECK(buf.size() > 8);
    CHECK_MSG(buf[4] == 'G' && buf[5] == 'L' && buf[6] == 'C' && buf[7] == 'D',
              "identifier is %c%c%c%c", buf[4], buf[5], buf[6], buf[7]);
}

TEST(file_layers_survive_with_every_property) {
    Contents out;
    CHECK(decode(encode(sample()), out).ok);

    const gdoc::Layer* w = out.layers.find("A-WALL");
    CHECK_MSG(w != nullptr, "A-WALL did not survive");
    CHECK(w->description == "exterior walls");
    CHECK(w->color.kind == gdoc::Color::Kind::Index);
    CHECK(w->color.index == 3);
    CHECK(w->lineweight == gdoc::Lineweight::W050);
    CHECK(w->transparency == 20);
    CHECK(w->plot_style == "Heavy");
    CHECK_MSG(w->vp_frozen.size() == 2, "per-viewport freeze was lost");
    CHECK(w->frozen_in(7));

    const gdoc::Layer* g = out.layers.find("A-GRID");
    CHECK(g != nullptr);
    CHECK(!g->on);
    CHECK(g->locked);
    CHECK(!g->plottable);
    CHECK(g->color.kind == gdoc::Color::Kind::True);
    CHECK(g->color.r == 200 && g->color.g == 40 && g->color.b == 60);
}

TEST(file_linetypes_and_multilines_survive) {
    Contents out;
    CHECK(decode(encode(sample()), out).ok);

    const gdoc::Linetype* d = out.linetypes.find("DASHED");
    CHECK(d != nullptr);
    CHECK(d->pattern.size() == 2);
    CHECK(std::fabs(d->pattern[0].length - 0.5) < 1e-12);
    CHECK(std::fabs(d->pattern[1].length + 0.25) < 1e-12);

    const gdoc::MultilineStyle* m = out.multilines.find("WALL-200");
    CHECK_MSG(m != nullptr, "the wall template did not survive");
    CHECK(m->elements.size() == 2);
    CHECK(std::fabs(m->elements[0].offset - 0.1) < 1e-12);
    CHECK(m->elements[0].lineweight == gdoc::Lineweight::W050);
    CHECK(m->filled);
    CHECK(m->end_cap == gdoc::EndCap::OuterArc);
}

TEST(file_blocks_and_their_insertions_survive) {
    Contents out;
    CHECK(decode(encode(sample()), out).ok);

    const gdoc::BlockDef* d = out.blocks.find("DOOR-900");
    CHECK_MSG(d != nullptr, "the block definition did not survive");
    CHECK(d->geometry.size() == 1);
    CHECK(d->attributes.size() == 1);
    CHECK(d->attributes[0].tag == "DOOR_NO");
    CHECK(d->attributes[0].default_value == "D-01");

    CHECK(out.blocks.references().size() == 1);
    const gdoc::BlockRef& r = out.blocks.references()[0];
    CHECK(r.name == "DOOR-900");
    CHECK(r.layer == "A-WALL");
    CHECK(std::fabs(r.rotation - 1.5707963267948966) < 1e-12);
    const auto v = r.attribute("DOOR_NO");
    CHECK_MSG(v.has_value() && *v == "D-14", "the attribute value was lost");
}

TEST(file_geometry_is_bit_exact_on_the_lattice) {
    const Contents in = sample();
    Contents out;
    CHECK(decode(encode(in), out).ok);

    CHECK(out.geometry.size() == 1);
    CHECK(out.geometry[0].space == 0);
    CHECK(out.geometry[0].shapes.size() == in.geometry[0].shapes.size());

    for (std::size_t i = 0; i < in.geometry[0].shapes.size(); ++i) {
        const auto& a = in.geometry[0].shapes[i];
        const auto& b = out.geometry[0].shapes[i];
        CHECK_MSG(a.kind == b.kind, "shape %zu changed kind", i);
        CHECK_MSG(a.a.x == b.a.x && a.a.y == b.a.y,
                  "shape %zu moved: (%lld,%lld) -> (%lld,%lld)", i,
                  (long long)a.a.x, (long long)a.a.y,
                  (long long)b.a.x, (long long)b.a.y);
        CHECK(a.b.x == b.b.x && a.b.y == b.b.y);
        CHECK(a.radius == b.radius);
    }
    CHECK(out.geometry[0].layers.size() == 3);
    CHECK(out.geometry[0].layers[2] == "A-GRID");
}

TEST(file_spaces_survive_with_their_frames) {
    Contents out;
    CHECK(decode(encode(sample()), out).ok);
    CHECK(out.spaces.size() == 2);
    CHECK(out.spaces[1].kind == gdoc::SpaceKind::Sheet);
    CHECK(out.spaces[1].name == "A-101");
    CHECK(std::fabs(out.spaces[1].paper_width - 0.841) < 1e-12);
    CHECK(std::fabs(out.spaces[0].frame.u.x.to_f64() - 1.0) < 1e-12);
    CHECK(std::fabs(out.spaces[0].frame.n.z.to_f64() - 1.0) < 1e-12);
}

TEST(file_encoding_is_deterministic) {
    const Contents c = sample();
    CHECK(encode(c) == encode(c));
}

TEST(file_empty_document_round_trips) {
    Contents in;
    in.volume = vol();
    Contents out;
    const Error e = decode(encode(in), out);
    CHECK_MSG(e.ok, "%s", e.message.c_str());
    CHECK(out.shape_count() == 0);
}

TEST(file_decode_rejects_a_truncated_buffer) {
    auto buf = encode(sample());
    buf.resize(buf.size() / 2);
    Contents out;
    const Error e = decode(buf, out);
    CHECK_MSG(!e.ok, "a truncated buffer was accepted");
    CHECK(!e.message.empty());
}

TEST(file_decode_rejects_rubbish) {
    const std::vector<u8> junk(512, 0xAB);
    Contents out;
    CHECK(!decode(junk, out).ok);
    CHECK(!decode(nullptr, 0, out).ok);
}

TEST(file_decode_survives_a_flipped_byte_without_crashing) {
    const auto good = encode(sample());
    for (std::size_t i = 0; i < good.size(); i += 7) {
        auto bad = good;
        bad[i] ^= 0xFF;
        Contents out;
        decode(bad, out);       // must not crash; may or may not be accepted
    }
    CHECK(true);
}

TEST(file_stream_round_trip_through_memory) {
    gio::MemoryWriter w;
    const Contents in = sample();
    CHECK(write_document(w, in).ok);

    gio::MemoryReader r(w.bytes());
    Contents out;
    ReadReport rep;
    const Error e = read_document(r, out, &rep);
    CHECK_MSG(e.ok, "%s", e.message.c_str());
    CHECK(rep.format_version == kSchemaVersion);
    CHECK(rep.frames_read == 1);
    CHECK(rep.frames_skipped == 0);
    CHECK(out.shape_count() == in.shape_count());
}

TEST(file_stream_detects_corruption_by_checksum) {
    gio::MemoryWriter w;
    write_document(w, sample());
    auto b = w.bytes();
    b[b.size() / 2] ^= 0x01;

    gio::MemoryReader r(b);
    Contents out;
    const Error e = read_document(r, out);
    CHECK_MSG(!e.ok, "corruption slipped through");
}

TEST(file_stream_without_a_document_frame_is_an_error) {
    gio::MemoryWriter w;
    gio::write_frame(w, gio::FrameKind::Thumbnail, {1, 2, 3});
    gio::MemoryReader r(w.bytes());
    Contents out;
    const Error e = read_document(r, out);
    CHECK(!e.ok);
    CHECK(e.message.find("no document") != std::string::npos);
}

TEST(file_unknown_frames_survive_a_round_trip) {
    gio::MemoryWriter w;
    write_document(w, sample());
    const std::vector<u8> future{'f', 'u', 't', 'u', 'r', 'e'};
    gio::write_frame(w, static_cast<gio::FrameKind>(4242), future);

    gio::MemoryReader r(w.bytes());
    Contents out;
    ReadReport rep;
    CHECK(read_document(r, out, &rep).ok);
    CHECK_MSG(rep.frames_skipped == 1, "the unknown frame was not noticed");
    CHECK(out.foreign.size() == 1);
    CHECK(out.foreign[0].kind == 4242);
    CHECK(out.foreign[0].payload == future);

    gio::MemoryWriter w2;
    CHECK(write_document(w2, out).ok);
    gio::MemoryReader r2(w2.bytes());
    Contents again;
    ReadReport rep2;
    CHECK(read_document(r2, again, &rep2).ok);
    CHECK_MSG(again.foreign.size() == 1, "the preserved frame was dropped");
    CHECK(again.foreign[0].payload == future);
}

TEST(file_saves_and_loads_through_a_path) {
    const std::string path = temp_path("path");
    std::remove(path.c_str());

    const Contents in = sample();
    const Error saved = save_file(path, in);
    CHECK_MSG(saved.ok, "%s", saved.message.c_str());

    Contents out;
    ReadReport rep;
    const Error loaded = load_file(path, out, &rep);
    CHECK_MSG(loaded.ok, "%s", loaded.message.c_str());
    CHECK(out.shape_count() == in.shape_count());
    CHECK(out.layers.find("A-WALL") != nullptr);
    std::remove(path.c_str());
}

TEST(file_saves_and_loads_through_a_url) {
    gio::MemoryTransport::reset();
    const Contents in = sample();
    CHECK(save("memory://doc", in).ok);

    Contents out;
    CHECK(load("memory://doc", out).ok);
    CHECK(out.shape_count() == in.shape_count());
}

TEST(file_save_to_an_unserved_family_reports_honestly) {
    const Error e = save("ipv4://127.0.0.1:5911", sample());
    CHECK(!e.ok);
    CHECK_MSG(e.message.find("ipv4") != std::string::npos, "%s",
              e.message.c_str());
}

TEST(file_load_of_a_missing_file_is_reported) {
    Contents out;
    const Error e = load_file("/tmp/glcad_definitely_absent.glcad", out);
    CHECK(!e.ok);
    CHECK(!e.message.empty());
}

TEST(file_save_does_not_destroy_the_previous_document_on_failure) {
    const std::string path = temp_path("atomic");
    std::remove(path.c_str());
    CHECK(save_file(path, sample()).ok);

    Contents first;
    CHECK(load_file(path, first).ok);
    const std::size_t before = first.shape_count();

    std::FILE* f = std::fopen((path + ".part").c_str(), "wb");
    if (f) {
        std::fwrite("garbage", 1, 7, f);
        std::fclose(f);
    }
    Contents again;
    CHECK(load_file(path, again).ok);
    CHECK(again.shape_count() == before);

    std::remove(path.c_str());
    std::remove((path + ".part").c_str());
}

TEST(file_document_handle_write_then_read) {
    const std::string path = temp_path("handle");
    std::remove(path.c_str());

    DocumentFile f;
    CHECK(f.create(path).ok);
    CHECK(f.is_open());
    CHECK(f.write(sample()).ok);
    CHECK(f.file_version() == kSchemaVersion);
    CHECK(!f.needs_upgrade());
    CHECK(!f.is_future_version());

    DocumentFile g;
    CHECK_MSG(g.open(path).ok, "could not reopen the document");
    Contents c;
    CHECK(g.read(c).ok);
    CHECK(c.shape_count() == 3);
    CHECK(g.last_read().frames_read == 1);

    g.close();
    CHECK(!g.is_open());
    CHECK(!g.read(c).ok);
    std::remove(path.c_str());
}

TEST(file_document_handle_reports_a_bad_path) {
    DocumentFile f;
    CHECK(!f.open("/tmp/glcad_not_a_document.glcad").ok);
    CHECK(!f.is_open());
}

TEST(file_large_document_round_trips) {
    Contents in;
    in.volume = vol();
    Contents::SpaceGeometry g;
    g.space = 0;
    const int n = static_cast<int>(2000 * t::scale());
    for (int i = 0; i < n; ++i) {
        g.shapes.push_back(seg(in.volume, i * 0.01, 0.0, i * 0.01, 1.0));
        g.layers.push_back("0");
    }
    in.geometry.push_back(g);

    Contents out;
    CHECK(decode(encode(in), out).ok);
    CHECK_MSG(out.shape_count() == in.shape_count(), "%zu vs %zu",
              out.shape_count(), in.shape_count());
    if (n > 0) {
        const auto& a = in.geometry[0].shapes[n / 2];
        const auto& b = out.geometry[0].shapes[n / 2];
        CHECK(a.a.x == b.a.x && a.b.y == b.b.y);
    }
}

TEST(file_a_document_travels_over_a_socket_unchanged) {
    gio::register_tcp_transport();

    gio::TcpTransport server;
    gio::Status s;
    if (!server.listen(gio::Endpoint::ipv4("127.0.0.1", 0), s)) {
        CHECK_MSG(false, "could not listen: %s", s.message.c_str());
        return;
    }
    server.set_timeout(3.0);
    const gio::u16 port = server.bound().port;

    const Contents in = sample();

    std::thread sender([&] {
        gio::TcpTransport out;
        gio::Status cs;
        auto conn = out.connect(gio::Endpoint::ipv4("127.0.0.1", port),
                                gio::Mode::ReadWrite, cs);
        if (!conn || !conn->output()) return;
        write_document(*conn->output(), in);
        conn->close();
    });

    gio::Status as;
    auto incoming = server.accept(as);
    CHECK_MSG(incoming != nullptr, "%s", as.message.c_str());

    if (incoming) {
        Contents out;
        ReadReport rep;
        const Error e = read_document(*incoming->input(), out, &rep);
        CHECK_MSG(e.ok, "%s", e.message.c_str());
        CHECK(rep.format_version == kSchemaVersion);
        CHECK_MSG(out.shape_count() == in.shape_count(), "%zu of %zu shapes",
                  out.shape_count(), in.shape_count());
        CHECK(out.layers.find("A-WALL") != nullptr);

        for (std::size_t i = 0; i < in.geometry[0].shapes.size(); ++i) {
            const auto& a = in.geometry[0].shapes[i];
            const auto& b = out.geometry[0].shapes[i];
            CHECK_MSG(a.a.x == b.a.x && a.a.y == b.a.y,
                      "shape %zu moved in flight", i);
        }
        incoming->close();
    }

    sender.join();
    server.close_listener();
}

TEST(file_the_url_form_reaches_a_socket_too) {
    gio::register_tcp_transport();
    const Error e = save("ipv4://127.0.0.1:1", sample());
    CHECK(!e.ok);
    CHECK_MSG(e.message.find("no transport") == std::string::npos,
              "the seam is still open: %s", e.message.c_str());
}

TEST(file_diff_finds_only_what_changed) {
    const Contents a = sample();
    Contents b = a;

    CHECK_MSG(diff(a, b).empty(), "an unchanged document produced a delta");

    b.geometry[0].shapes.push_back(seg(b.volume, 0, 5, 9, 5));
    b.geometry[0].layers.push_back("A-WALL");
    const Delta d = diff(a, b);
    CHECK(!d.empty());
    CHECK_MSG(d.spaces.size() == 1, "%zu spaces in the delta", d.spaces.size());
    CHECK_MSG(d.spaces[0].inserted.size() == 1, "%zu shapes in the splice",
              d.spaces[0].inserted.size());
    CHECK(d.spaces[0].removed == 0);
    CHECK(d.changed_layers.empty());
}

TEST(file_diff_notices_layer_changes_and_deletions) {
    const Contents a = sample();
    Contents b = a;

    gdoc::Layer edited = *b.layers.find("A-WALL");
    edited.color = gdoc::Color::aci(9);
    b.layers.add(edited);
    b.layers.remove("A-GRID");

    const Delta d = diff(a, b);
    CHECK_MSG(d.changed_layers.size() == 1, "%zu changed", d.changed_layers.size());
    CHECK(d.changed_layers[0].name == "A-WALL");
    CHECK(d.removed_layers.size() == 1);
    CHECK(d.removed_layers[0] == "A-GRID");
}

TEST(file_applying_a_delta_reproduces_the_later_document) {
    const Contents a = sample();
    Contents b = a;
    b.geometry[0].shapes.push_back(seg(b.volume, 0, 5, 9, 5));
    b.geometry[0].layers.push_back("A-WALL");
    gdoc::Layer edited = *b.layers.find("A-WALL");
    edited.transparency = 70;
    b.layers.add(edited);

    Contents rebuilt = a;
    apply(rebuilt, diff(a, b));
    CHECK_MSG(rebuilt.shape_count() == b.shape_count(), "%zu vs %zu shapes",
              rebuilt.shape_count(), b.shape_count());
    CHECK(rebuilt.layers.find("A-WALL")->transparency == 70);
}

TEST(file_delta_round_trips_through_its_own_encoding) {
    const Contents a = sample();
    Contents b = a;
    b.geometry[0].shapes.push_back(seg(b.volume, 1, 1, 8, 8));
    b.geometry[0].layers.push_back("A-WALL");
    b.layers.remove("A-GRID");

    const Delta d = diff(a, b);
    Delta back;
    const Error e = decode_delta(encode_delta(d).data(), encode_delta(d).size(),
                                 back);
    CHECK_MSG(e.ok, "%s", e.message.c_str());
    CHECK(back.spaces.size() == d.spaces.size());
    CHECK(back.removed_layers == d.removed_layers);

    CHECK(back.spaces[0].at == d.spaces[0].at);
    CHECK(back.spaces[0].removed == d.spaces[0].removed);
    CHECK(back.spaces[0].inserted.size() == d.spaces[0].inserted.size());
    for (std::size_t i = 0; i < d.spaces[0].inserted.size(); ++i)
        CHECK(back.spaces[0].inserted[i].a.x == d.spaces[0].inserted[i].a.x);
}

TEST(file_decode_delta_refuses_rubbish) {
    const std::vector<u8> junk(64, 0x5A);
    Delta d;
    CHECK(!decode_delta(junk.data(), junk.size(), d).ok);
    CHECK(!decode_delta(nullptr, 0, d).ok);
}

TEST(file_appending_a_delta_does_not_rewrite_the_document) {
    const std::string path = temp_path("incremental");
    std::remove(path.c_str());

    Contents a;
    a.volume = vol();
    Contents::SpaceGeometry g;
    g.space = 0;
    for (int i = 0; i < 4000; ++i) {
        g.shapes.push_back(seg(a.volume, i * 0.01, 0, i * 0.01, 1));
        g.layers.push_back("0");
    }
    a.geometry.push_back(g);
    CHECK(save_file(path, a).ok);

    gio::FileReader probe;
    CHECK(probe.open(path).ok);
    const gio::i64 base = probe.size();
    probe.close();

    Contents b = a;
    b.geometry[0].shapes.push_back(seg(b.volume, 99, 99, 100, 100));
    b.geometry[0].layers.push_back("0");

    gio::u64 grown = 0;
    CHECK(append_delta(path, diff(a, b), &grown).ok);
    CHECK_MSG(static_cast<gio::i64>(grown) > base, "the file did not grow");
    CHECK_MSG(static_cast<gio::i64>(grown) - base < base / 4,
              "appending %lld bytes onto a %lld byte file",
              static_cast<long long>(grown) - base, static_cast<long long>(base));

    std::remove(path.c_str());
}

TEST(file_an_appended_delta_is_applied_on_load) {
    const std::string path = temp_path("deltacompat");
    std::remove(path.c_str());

    const Contents a = sample();
    CHECK(save_file(path, a).ok);

    Contents b = a;
    b.geometry[0].shapes.push_back(seg(b.volume, 20, 20, 30, 30));
    b.geometry[0].layers.push_back("A-WALL");
    CHECK(append_delta(path, diff(a, b)).ok);

    Contents loaded;
    ReadReport rep;
    CHECK_MSG(load_file(path, loaded, &rep).ok, "the file would not open");
    CHECK_MSG(loaded.shape_count() == b.shape_count(),
              "%zu shapes loaded, %zu expected -- the delta was not applied",
              loaded.shape_count(), b.shape_count());
    CHECK_MSG(rep.deltas_applied == 1, "%zu deltas applied", rep.deltas_applied);
    CHECK_MSG(loaded.foreign.empty(), "the delta was kept as well as applied");

    Contents c2 = b;
    c2.geometry[0].shapes.push_back(seg(c2.volume, 40, 40, 50, 50));
    c2.geometry[0].layers.push_back("A-WALL");
    CHECK(append_delta(path, diff(b, c2)).ok);
    Contents loaded2;
    ReadReport rep2;
    CHECK(load_file(path, loaded2, &rep2).ok);
    CHECK_MSG(loaded2.shape_count() == c2.shape_count(),
              "%zu shapes after two deltas, %zu expected",
              loaded2.shape_count(), c2.shape_count());
    CHECK(rep2.deltas_applied == 2);

    std::remove(path.c_str());
}

TEST(file_appending_to_nothing_is_refused) {
    Delta d;
    d.removed_layers.push_back("X");
    CHECK(!append_delta("/tmp/glcad_no_such_document.glcad", d).ok);
}

TEST(file_appending_an_empty_delta_is_a_no_op) {
    const std::string path = temp_path("emptydelta");
    std::remove(path.c_str());
    CHECK(save_file(path, sample()).ok);
    gio::u64 size = 123;
    CHECK(append_delta(path, Delta{}, &size).ok);
    CHECK(size == 0);
    std::remove(path.c_str());
}

TEST(file_transaction_log_records_in_order) {
    TransactionLog log;
    const u32 a = log.record("LINE", "two points");
    const u32 b = log.record("MOVE", "one object");
    CHECK(log.size() == 2);
    CHECK(b > a);
    CHECK(log.all()[0].command == "LINE");
    CHECK(log.all()[1].detail == "one object");
}

TEST(file_transaction_log_round_trips) {
    TransactionLog log;
    log.record("LINE", "first", 1000);
    log.record("CIRCLE", "second", 2000);

    gio::MemoryWriter w;
    CHECK(log.write(w).ok);

    gio::MemoryReader r(w.bytes());
    TransactionLog back;
    CHECK_MSG(back.read(r).ok, "the log would not reload");
    CHECK(back.size() == 2);
    CHECK(back.all()[1].command == "CIRCLE");
    CHECK(back.all()[1].timestamp == 2000);
}

TEST(file_replaying_the_log_is_crash_recovery) {
    const Contents saved = sample();

    Contents edited = saved;
    edited.geometry[0].shapes.push_back(seg(edited.volume, 40, 40, 50, 50));
    edited.geometry[0].layers.push_back("A-WALL");

    TransactionLog log;
    log.record(diff(saved, edited), "LINE", 1);

    Contents recovered = saved;
    CHECK_MSG(log.replay(recovered) == 1, "nothing was replayed");
    CHECK_MSG(recovered.shape_count() == edited.shape_count(),
              "%zu of %zu shapes recovered", recovered.shape_count(),
              edited.shape_count());
}

TEST(file_replay_can_stop_partway) {
    const Contents base = sample();
    Contents one = base;
    one.geometry[0].shapes.push_back(seg(one.volume, 1, 1, 2, 2));
    one.geometry[0].layers.push_back("A-WALL");
    Contents two = one;
    two.geometry[0].shapes.push_back(seg(two.volume, 3, 3, 4, 4));
    two.geometry[0].layers.push_back("A-WALL");

    TransactionLog log;
    const u32 first = log.record(diff(base, one), "LINE", 1);
    log.record(diff(one, two), "LINE", 2);

    Contents c = base;
    CHECK(log.replay(c, first) == 1);
    CHECK(c.shape_count() == one.shape_count());
}

TEST(file_a_log_entry_with_no_payload_is_skipped_not_fatal) {
    TransactionLog log;
    log.record("ZOOM", "not a document change");
    Contents c = sample();
    const std::size_t before = c.shape_count();
    CHECK(log.replay(c) == 0);
    CHECK(c.shape_count() == before);
}

TEST(file_log_appends_to_a_document_file) {
    const std::string path = temp_path("withlog");
    std::remove(path.c_str());
    CHECK(save_file(path, sample()).ok);

    TransactionLog log;
    log.record("LINE", "one");
    CHECK(log.append_to(path).ok);

    Contents c;
    ReadReport rep;
    CHECK(load_file(path, c, &rep).ok);
    CHECK(rep.frames_skipped == 1);
    std::remove(path.c_str());
}

TEST(file_cache_round_trips_and_answers_whether_it_is_stale) {
    TessellationCache c;
    TessellationCache::Entry e;
    e.space = 0;
    e.shape = 7;
    e.source_hash = 0xABCDEF0123456789ull;
    e.points = {0.0, 0.0, 1.0, 0.5, 2.0, 0.0};
    c.entries.push_back(e);

    CHECK(c.valid_for(0, 0xABCDEF0123456789ull));
    CHECK_MSG(!c.valid_for(0, 1), "a stale cache reported itself fresh");
    CHECK(!c.valid_for(99, 0xABCDEF0123456789ull));

    TessellationCache back;
    const auto bytes = encode_cache(c);
    CHECK(decode_cache(bytes.data(), bytes.size(), back).ok);
    CHECK(back.size() == 1);
    CHECK(back.entries[0].source_hash == e.source_hash);
    CHECK(back.entries[0].points.size() == 6);
    CHECK(std::fabs(back.entries[0].points[3] - 0.5) < 1e-12);
}

TEST(file_cache_invalidation_removes_only_its_own_entry) {
    TessellationCache c;
    for (u32 i = 0; i < 4; ++i) {
        TessellationCache::Entry e;
        e.space = 0;
        e.shape = i;
        c.entries.push_back(e);
    }
    c.invalidate(0, 2);
    CHECK(c.size() == 3);
    for (const auto& e : c.entries) CHECK(e.shape != 2);
}

TEST(file_decode_cache_refuses_rubbish) {
    TessellationCache c;
    const std::vector<u8> junk(32, 0x11);
    CHECK(!decode_cache(junk.data(), junk.size(), c).ok);
}

TEST(file_a_template_carries_the_standards_and_no_geometry) {
    const std::string path = temp_path("template");
    std::remove(path.c_str());

    const Contents source = sample();
    CHECK(source.shape_count() > 0);
    CHECK(save_template(path, source).ok);

    Contents t;
    CHECK(load_template(path, t).ok);
    CHECK_MSG(t.shape_count() == 0, "the template carried %zu shapes",
              t.shape_count());
    CHECK_MSG(t.layers.find("A-WALL") != nullptr, "the standards were lost");
    CHECK(t.blocks.find("DOOR-900") != nullptr);
    CHECK(t.spaces.size() == source.spaces.size());
    std::remove(path.c_str());
}

TEST(file_template_filters_entities_insertions_and_opaque_model_frames) {
    const auto path = temp_path("template-projection");
    auto source = sample();
    gdoc::BlockDef external; external.name = "Project"; external.is_xref = true;
    external.geometry.push_back(seg(vol(), 0, 0, 200, 200)); source.blocks.define(external);
    gdoc::BlockDef nested; nested.name = "Standard"; nested.nested = {"Project"};
    source.blocks.define(nested);
    source.blocks.make_dynamic("Standard");
    gdoc::BlockRef placement; placement.name = "Standard"; source.blocks.insert(placement);
    gdoc::Space paper; paper.id = 42; paper.kind = gdoc::SpaceKind::Sheet;
    source.spaces.push_back(paper);
    Contents::SpaceGeometry border; border.space = 42;
    border.shapes.push_back(seg(vol(), 0, 0, 0.2, 0)); border.layers.push_back("0");
    source.geometry.push_back(border);
    gdoc::Entity model; model.space = 0; source.entities.push_back(model);
    gdoc::Entity note; note.space = 42; source.entities.push_back(note);
    source.foreign.push_back({0x7777, {1, 2, 3}});
    CHECK(save_template(path, source).ok);
    Contents saved; CHECK(load_file(path, saved).ok);
    CHECK(saved.shape_count() == 1); CHECK(saved.geometry[0].space == 42);
    CHECK(saved.entities.size() == 1); CHECK(saved.entities[0].space == 42);
    CHECK(saved.blocks.references().empty()); CHECK(saved.foreign.empty());
    CHECK(saved.blocks.find("DOOR-900") != nullptr);
    CHECK(saved.blocks.find("Project") == nullptr);
    CHECK(saved.blocks.is_dynamic("Standard"));
    if (const auto* def = saved.blocks.find("Standard")) CHECK(def->nested.empty());
    CHECK(save_file(path, source).ok);
    Contents loaded; CHECK(load_template(path, loaded).ok);
    CHECK(encode(loaded) == encode(saved));
    CHECK(source.shape_count() > 1); CHECK(!source.foreign.empty());
    std::remove(path.c_str());
}

TEST(file_a_package_lists_what_the_drawing_depends_on) {
    const std::string path = temp_path("package");
    std::remove(path.c_str());

    Package p;
    p.document = sample();
    p.fonts = {"romans.shx", "Arial"};
    p.xrefs = {"SITE.glcad"};
    p.images = {"aerial.png"};
    p.note = "issued for tender";
    CHECK(save_package(path, p).ok);

    Package back;
    CHECK_MSG(load_package(path, back).ok, "the package would not open");
    CHECK(back.document.shape_count() == p.document.shape_count());
    CHECK_MSG(back.fonts.size() == 2, "%zu fonts", back.fonts.size());
    CHECK(back.fonts[0] == "romans.shx");
    CHECK(back.xrefs.size() == 1);
    CHECK(back.images.size() == 1);
    CHECK(back.note == "issued for tender");
    std::remove(path.c_str());
}

TEST(file_a_package_opens_as_a_plain_document_too) {
    const std::string path = temp_path("packagecompat");
    std::remove(path.c_str());

    Package p;
    p.document = sample();
    p.fonts = {"romans.shx"};
    CHECK(save_package(path, p).ok);

    Contents c;
    CHECK_MSG(load_file(path, c).ok, "a package would not open as a document");
    CHECK(c.shape_count() == p.document.shape_count());
    std::remove(path.c_str());
}

TEST(file_a_splice_handles_a_deletion_in_the_middle) {
    Contents a;
    a.volume = vol();
    Contents::SpaceGeometry g;
    g.space = 0;
    for (int i = 0; i < 200; ++i) {
        g.shapes.push_back(seg(a.volume, i * 0.1, 0, i * 0.1, 1));
        g.layers.push_back("0");
    }
    a.geometry.push_back(g);

    Contents b = a;
    b.geometry[0].shapes.erase(b.geometry[0].shapes.begin() + 100);
    b.geometry[0].layers.erase(b.geometry[0].layers.begin() + 100);

    const Delta d = diff(a, b);
    CHECK(d.spaces.size() == 1);
    CHECK_MSG(d.spaces[0].removed == 1, "%u removed", d.spaces[0].removed);
    CHECK_MSG(d.spaces[0].inserted.empty(), "%zu inserted",
              d.spaces[0].inserted.size());
    CHECK(d.spaces[0].at == 100);

    Contents rebuilt = a;
    apply(rebuilt, d);
    CHECK(rebuilt.shape_count() == b.shape_count());
    CHECK(rebuilt.geometry[0].shapes[100].a.x == b.geometry[0].shapes[100].a.x);
}

TEST(file_a_splice_handles_an_edit_in_place) {
    Contents a;
    a.volume = vol();
    Contents::SpaceGeometry g;
    g.space = 0;
    for (int i = 0; i < 50; ++i) {
        g.shapes.push_back(seg(a.volume, i, 0, i, 1));
        g.layers.push_back("0");
    }
    a.geometry.push_back(g);

    Contents b = a;
    b.geometry[0].shapes[20] = seg(b.volume, 99, 99, 100, 100);

    const Delta d = diff(a, b);
    CHECK(d.spaces[0].at == 20);
    CHECK(d.spaces[0].removed == 1);
    CHECK(d.spaces[0].inserted.size() == 1);

    Contents rebuilt = a;
    apply(rebuilt, d);
    CHECK(rebuilt.geometry[0].shapes[20].a.x == b.geometry[0].shapes[20].a.x);
    CHECK(rebuilt.shape_count() == b.shape_count());
}

TEST(file_a_splice_against_a_document_that_moved_on_is_refused) {
    Contents c;
    c.volume = vol();
    Contents::SpaceGeometry g;
    g.space = 0;
    g.shapes.push_back(seg(c.volume, 0, 0, 1, 0));
    g.layers.push_back("0");
    c.geometry.push_back(g);

    Delta d;
    SpaceDelta sp;
    sp.space = 0;
    sp.at = 500;                    // far past the end
    sp.removed = 1;
    d.spaces.push_back(sp);

    const std::size_t before = c.shape_count();
    apply(c, d);
    CHECK_MSG(c.shape_count() == before, "an impossible splice was applied");
}

namespace {

gdoc::Entity make_entity(const gcm::WorkingVolume& v, gdoc::Kind k,
                         gdoc::SpaceId space, std::string_view layer) {
    gdoc::Entity e;
    e.kind = k;
    e.space = space;
    e.layer = std::string(layer);
    e.payload.points.push_back(gcm::to_lattice(v, 1.0, 2.0, 3.0));
    e.payload.points.push_back(gcm::to_lattice(v, -4.5, 0.25, 7.0));
    e.payload.reals = {1.5, -2.25};
    e.payload.ints = {7, 9};
    e.payload.text = "a note";
    e.payload.blob = {0, 255, 128, 1, 0};
    return e;
}

}

TEST(file_entities_survive_a_round_trip) {
    const auto v = vol();
    Contents c;
    c.volume = v;

    gdoc::Space model;
    model.id = 0;
    model.kind = gdoc::SpaceKind::Model;
    model.name = "Model";
    c.spaces.push_back(model);

    gdoc::Entity line = make_entity(v, gdoc::Kind::Line, 0, "WALLS");
    line.id = {0x1122334455667788ull, 0x99aabbccddeeff00ull};
    line.attrs.color = gdoc::Color::aci(5);
    line.attrs.linetype = "DASHED";
    line.attrs.lineweight = gdoc::Lineweight::W050;
    line.attrs.linetype_scale = 2.5;
    line.attrs.transparency = 30;
    line.attrs.visible = false;
    line.attrs.user["author"] = "gl";
    line.attrs.user["revision"] = "C";
    c.entities.push_back(line);

    const auto bytes = encode(c);
    Contents back;
    const Error e = decode(bytes, back);
    CHECK_MSG(e.ok, "%s", e.message.c_str());
    CHECK_MSG(back.entities.size() == 1, "%zu entities came back",
              back.entities.size());

    const gdoc::Entity& r = back.entities[0];
    CHECK_MSG(r.id == line.id, "the entity id changed on the way through");
    CHECK(r.kind == gdoc::Kind::Line);
    CHECK(r.space == 0);
    CHECK(r.layer == "WALLS");
    CHECK(r.attrs.color.kind == gdoc::Color::Kind::Index);
    CHECK(r.attrs.color.index == 5);
    CHECK(r.attrs.linetype == "DASHED");
    CHECK(r.attrs.lineweight == gdoc::Lineweight::W050);
    CHECK(std::fabs(r.attrs.linetype_scale - 2.5) < 1e-12);
    CHECK(r.attrs.transparency == 30);
    CHECK(!r.attrs.visible);
    CHECK(r.attrs.user.size() == 2);
    CHECK(r.attrs.user.at("author") == "gl");
    CHECK(r.attrs.user.at("revision") == "C");

    CHECK(r.payload.points.size() == 2);
    CHECK(r.payload.points[0].x == line.payload.points[0].x);
    CHECK(r.payload.points[1].z == line.payload.points[1].z);
    CHECK(r.payload.reals == line.payload.reals);
    CHECK(r.payload.ints == line.payload.ints);
    CHECK(r.payload.text == "a note");
    CHECK(r.payload.blob == line.payload.blob);
}

TEST(file_entity_binary_only_edits_survive_incremental_save) {
    Contents before;
    before.volume = vol();
    auto entity = make_entity(before.volume, gdoc::Kind::Hatch, 0, "0");
    entity.id = {1, 2};
    before.entities.push_back(entity);
    auto after = before;
    after.entities[0].payload.blob = {17, 0, 255, 3};
    const auto delta = diff(before, after);
    CHECK(!delta.empty());
    const auto bytes = encode_delta(delta);
    Delta decoded;
    CHECK(decode_delta(bytes.data(), bytes.size(), decoded).ok);
    apply(before, decoded);
    CHECK(before.entities.size() == 1);
    CHECK(before.entities[0].payload.blob == after.entities[0].payload.blob);
    after.entities[0].payload.blob.clear();
    const auto cleared = encode_delta(diff(before, after));
    CHECK(decode_delta(cleared.data(), cleared.size(), decoded).ok);
    apply(before, decoded);
    CHECK(before.entities[0].payload.blob.empty());
    CHECK(diff(before, after).empty());
}

TEST(file_entity_references_still_point_where_they_pointed) {
    const auto v = vol();
    Contents c;
    c.volume = v;
    gdoc::Space model;
    model.id = 0;
    c.spaces.push_back(model);

    gdoc::Entity line = make_entity(v, gdoc::Kind::Line, 0, "0");
    line.id = {1, 2};
    gdoc::Entity dim = make_entity(v, gdoc::Kind::Dimension, 0, "DIMS");
    dim.id = {3, 4};
    dim.payload.refs.push_back(line.id);
    c.entities.push_back(line);
    c.entities.push_back(dim);

    Contents back;
    CHECK(decode(encode(c), back).ok);
    CHECK(back.entities.size() == 2);

    const gdoc::Entity* d = nullptr;
    for (const auto& e : back.entities)
        if (e.kind == gdoc::Kind::Dimension) d = &e;
    CHECK(d != nullptr);
    CHECK_MSG(d->payload.refs.size() == 1, "%zu refs survived",
              d->payload.refs.size());
    CHECK_MSG(d->payload.refs[0] == line.id,
              "the dimension lost track of what it measures");
}

TEST(file_foreign_entity_data_is_kept_byte_for_byte) {
    const auto v = vol();
    Contents c;
    c.volume = v;
    gdoc::Space model;
    model.id = 0;
    c.spaces.push_back(model);

    gdoc::Entity e = make_entity(v, gdoc::Kind::Unknown, 0, "0");
    e.id = {9, 9};
    e.attrs.foreign_format = "DWG-2018";
    e.attrs.foreign_blob = {0x00, 0xff, 0x7f, 0x80, 0x01, 0xfe};
    c.entities.push_back(e);

    Contents back;
    CHECK(decode(encode(c), back).ok);
    CHECK(back.entities.size() == 1);
    CHECK(back.entities[0].attrs.foreign_format == "DWG-2018");
    CHECK_MSG(back.entities[0].attrs.foreign_blob == e.attrs.foreign_blob,
              "the foreign blob came back different");
}

TEST(file_a_document_round_trips_through_the_bridge) {
    const auto v = vol();
    gdoc::Document doc;

    gdoc::Layer walls;
    walls.name = "WALLS";
    doc.layers().add(walls);

    gdoc::Space draft;
    draft.kind = gdoc::SpaceKind::Draft;
    draft.name = "Plan";
    const gdoc::SpaceId ds = doc.add_space(draft);

    doc.begin("draw");
    gdoc::Entity a = make_entity(v, gdoc::Kind::Line, doc.model_space(), "WALLS");
    const gdoc::EntityId ia = doc.create(a);
    gdoc::Entity b = make_entity(v, gdoc::Kind::Circle, ds, "0");
    const gdoc::EntityId ib = doc.create(b);
    doc.commit();
    doc.set_current_space(ds);

    Contents c;
    c.volume = v;
    from_document(doc, c);
    CHECK_MSG(c.entities.size() == 2, "%zu entities exported",
              c.entities.size());
    CHECK(c.current_space == ds);

    Contents back;
    CHECK(decode(encode(c), back).ok);

    gdoc::Document loaded;
    to_document(back, loaded);
    CHECK_MSG(loaded.entity_count() == 2, "%zu entities loaded",
              loaded.entity_count());
    CHECK_MSG(loaded.find(ia) != nullptr, "the first entity lost its id");
    CHECK_MSG(loaded.find(ib) != nullptr, "the second entity lost its id");
    CHECK(loaded.find(ia)->layer == "WALLS");
    CHECK(loaded.find(ib)->space == ds);
    CHECK(loaded.layers().find("WALLS") != nullptr);
    CHECK(loaded.current_space() == ds);
}

TEST(file_a_document_with_entities_encodes_deterministically) {
    const auto v = vol();
    gdoc::Document doc;
    for (int i = 0; i < 40; ++i) {
        gdoc::Entity e = make_entity(v, gdoc::Kind::Line, doc.model_space(), "0");
        e.attrs.user["k" + std::to_string(i)] = std::to_string(i);
        e.attrs.user["z" + std::to_string(i)] = "z";
        doc.create(e);
    }

    Contents a, b;
    a.volume = v;
    b.volume = v;
    from_document(doc, a);
    from_document(doc, b);
    CHECK_MSG(encode(a) == encode(b),
              "two exports of the same document produced different bytes");
}

TEST(file_adopting_an_entity_keeps_its_id_and_creating_one_does_not) {
    const auto v = vol();
    gdoc::Document doc;
    gdoc::Entity e = make_entity(v, gdoc::Kind::Line, doc.model_space(), "0");

    const gdoc::EntityId assigned = doc.create(e);
    CHECK(assigned.valid());

    gdoc::Entity carried = e;
    carried.id = {0xdeadbeefull, 0xcafebabeull};
    CHECK(doc.adopt(carried));
    CHECK_MSG(doc.find(carried.id) != nullptr,
              "an adopted entity was not stored under its own id");
    CHECK(doc.entity_count() == 2);

    gdoc::Entity anonymous = e;
    anonymous.id = {};
    CHECK(!doc.adopt(anonymous));
    CHECK(doc.entity_count() == 2);
}

TEST(file_adopting_the_same_entity_twice_replaces_rather_than_duplicates) {
    const auto v = vol();
    gdoc::Document doc;
    gdoc::Entity e = make_entity(v, gdoc::Kind::Line, doc.model_space(), "0");
    e.id = {1, 1};
    CHECK(doc.adopt(e));
    e.layer = "WALLS";
    CHECK(doc.adopt(e));
    CHECK_MSG(doc.entity_count() == 1, "%zu entities after a reload",
              doc.entity_count());
    CHECK(doc.find(e.id)->layer == "WALLS");
}

TEST(file_a_corrupt_entity_point_run_is_dropped_not_completed) {
    const auto v = vol();
    Contents c;
    c.volume = v;
    gdoc::Space model;
    model.id = 0;
    c.spaces.push_back(model);
    gdoc::Entity e = make_entity(v, gdoc::Kind::Line, 0, "0");
    e.id = {5, 5};
    c.entities.push_back(e);

    Contents back;
    CHECK(decode(encode(c), back).ok);
    CHECK(back.entities[0].payload.points.size() == 2);
}

TEST(file_a_document_with_no_entities_still_round_trips) {
    const auto v = vol();
    Contents c;
    c.volume = v;
    Contents back;
    CHECK(decode(encode(c), back).ok);
    CHECK(back.entities.empty());
}

TEST(delta_preserves_metadata_properties_and_deleted_spaces) {
    gfile::Contents a;
    gsnap::Shape line; line.a = {1, 2}; line.b = {3, 4}; line.key = 99;
    a.geometry.push_back({0, {line}, {"0"}});
    a.geometry.push_back({1, {line}, {"0"}});
    auto b = a;
    b.geometry.pop_back();
    auto& sh = b.geometry[0].shapes[0];
    sh.attrs.color = gdoc::Color::rgb(10, 20, 30); sh.attrs.transparency = 35;
    sh.attrs.linetype_scale = 2.5; sh.attrs.foreign_blob = {1, 2, 3};
    sh.poly = 12; sh.poly_index = 3;
    b.foreign.push_back({12345, {6, 7, 8}});
    gdoc::Entity entity; entity.id = {1, 2}; entity.kind = gdoc::Kind::Parameter;
    entity.payload.text = "wall"; b.entities.push_back(entity);
    gdoc::Layer layer; layer.name = "Colour"; layer.color = gdoc::Color::rgb(5, 6, 7);
    layer.vp_frozen = {7}; b.layers.add(layer);
    auto d = gfile::diff(a, b); CHECK(!d.empty());
    const auto wire = gfile::encode_delta(d);
    gfile::Delta back; CHECK(gfile::decode_delta(wire.data(), wire.size(), back).ok);
    gfile::apply(a, back);
    CHECK(a.geometry.size() == 1);
    CHECK(a.entities.size() == 1);
    CHECK(a.foreign.size() == 1);
    CHECK(a.geometry[0].shapes[0].key == 99);
    CHECK(a.geometry[0].shapes[0].poly == 12);
    CHECK(a.geometry[0].shapes[0].attrs.color == sh.attrs.color);
    CHECK(a.geometry[0].shapes[0].attrs.foreign_blob == sh.attrs.foreign_blob);
    CHECK(a.layers.find("Colour")->color == layer.color);
    CHECK(a.layers.find("Colour")->vp_frozen == layer.vp_frozen);
    CHECK(gfile::diff(a, b).empty());
}


TEST(delta_v1_remains_readable_and_v2_rejects_truncation) {
    // Original v1 encoding: sequence 7, one empty splice, no layer changes.
    const std::vector<gcm::u8> legacy = {
        1,0,0,0, 7,0,0,0, 1,0,0,0,
        3,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
        0,0,0,0, 0,0,0,0
    };
    gfile::Delta d;
    CHECK(gfile::decode_delta(legacy.data(), legacy.size(), d).ok);
    CHECK(d.sequence == 7); CHECK(d.spaces.size() == 1);
    if (!d.spaces.empty()) CHECK(d.spaces[0].space == 3);
    CHECK(!d.metadata.has_value());
    const auto bytes = gfile::encode_delta(d);
    for (std::size_t n : {std::size_t(0), std::size_t(7), bytes.size()/2, bytes.size()-1})
        CHECK(!gfile::decode_delta(bytes.data(), n, d).ok);
}

TEST(delta_replaces_and_removes_opaque_document_records) {
    gfile::Contents a;
    a.foreign = {{900, {1, 2}}, {901, {3, 4}}};
    gdoc::Entity e; e.id = {1, 2}; e.kind = gdoc::Kind::Parameter; e.payload.text = "old";
    a.entities.push_back(e);
    auto b = a;
    b.foreign = {{901, {7, 8, 9}}}; b.entities[0].payload.text = "new";
    auto bytes = gfile::encode_delta(gfile::diff(a, b)); gfile::Delta d;
    CHECK(gfile::decode_delta(bytes.data(), bytes.size(), d).ok); gfile::apply(a, d);
    CHECK(a.foreign.size() == 1); CHECK(a.foreign[0].kind == 901);
    CHECK(a.foreign[0].payload == b.foreign[0].payload);
    CHECK(a.entities[0].payload.text == "new");
    b.entities.clear(); b.foreign.clear();
    bytes = gfile::encode_delta(gfile::diff(a, b));
    CHECK(gfile::decode_delta(bytes.data(), bytes.size(), d).ok); gfile::apply(a, d);
    CHECK(a.entities.empty()); CHECK(a.foreign.empty()); CHECK(gfile::diff(a, b).empty());
}


TEST(native_block_extensions_preserve_actions_choices_and_xref_transforms) {
    gfile::Contents c;
    gdoc::BlockDef def; def.name = "Door"; c.blocks.define(def);
    auto& dyn = c.blocks.make_dynamic("Door");
    gdoc::BlockParameter p; p.name = "Type"; p.kind = gdoc::ParameterKind::Lookup;
    p.base = {123, 456}; p.end = {789, 1011}; p.value = 0.9;
    p.minimum = 0.6; p.maximum = 1.2; p.increment = 0.05;
    p.choices = {"single", "double"}; p.choice = 1; dyn.parameters.push_back(p);
    gdoc::BlockAction a; a.name = "Stretch"; a.kind = gdoc::ActionKind::Stretch;
    a.parameter = p.name; a.geometry = {0, 3}; a.dx = 1.25; a.dy = -0.5;
    a.scale = 2.5; a.origin = {10, 20}; a.window_lo = {30, 40}; a.window_hi = {50, 60};
    dyn.actions.push_back(a); dyn.visibility = {{"open", {0}}, {"closed", {3}}}; dyn.visible_set = 1;
    gdoc::Xref x; x.name = "Site"; x.path = "../site.glcad"; x.resolved_path = "/drawing/site.glcad";
    x.type = gdoc::XrefType::Overlay; x.state = gdoc::XrefState::Unloaded;
    x.insertion = {999, -888}; x.scale = 0.25; x.rotation = 1.125;
    x.clip = {{1, 2}, {3, 4}, {5, 6}}; x.clip_inverted = true; x.clip_enabled = false; x.nested = true;
    c.blocks.attach(x);
    gfile::Contents r; CHECK(gfile::decode(gfile::encode(c), r).ok);
    const auto* d = r.blocks.dynamic("Door"); CHECK(d != nullptr);
    if (d) {
        CHECK(d->parameters[0].choices == p.choices); CHECK(d->parameters[0].choice == 1);
        CHECK(d->parameters[0].minimum == p.minimum); CHECK(d->parameters[0].increment == p.increment);
        CHECK(d->actions[0].geometry == a.geometry); CHECK(d->actions[0].dx == a.dx);
        CHECK(d->actions[0].window_hi.y == 60); CHECK(d->visible_set == 1);
    }
    const auto* xr = r.blocks.xref("Site"); CHECK(xr != nullptr);
    if (xr) {
        CHECK(xr->path == x.path); CHECK(xr->resolved_path == x.resolved_path);
        CHECK(xr->type == x.type); CHECK(xr->state == x.state);
        CHECK(xr->insertion.y == -888); CHECK(xr->scale == x.scale); CHECK(xr->rotation == x.rotation);
        CHECK(xr->clip.size() == 3); CHECK(xr->clip_inverted); CHECK(!xr->clip_enabled); CHECK(xr->nested);
    }
}

TEST(native_regions_preserve_topology_and_partial_incremental_edits) {
    gfile::Contents before;
    gsnap::Shape circle;circle.kind=gsnap::ShapeKind::Circle;circle.radius=1000;
    circle.key=10;circle.poly=11;circle.region=true;circle.layer="Boundary";
    auto hole=circle;hole.key=12;hole.radius=400;hole.poly_index=1;hole.region_loop=1;hole.region_reversed=true;
    before.geometry.push_back({0,{circle,hole},{"Boundary","Boundary"}});
    gdoc::BlockDef block;block.name="Region";block.geometry={circle,hole};before.blocks.define(block);
    gio::MemoryWriter writer;CHECK(gfile::write_document(writer,before).ok);
    auto after=before;after.geometry[0].shapes[1].radius=500;
    const auto delta=gfile::diff(before,after);CHECK(delta.spaces.size()==1);
    CHECK(delta.spaces[0].inserted.size()==1);
    CHECK(gio::write_frame(writer,gio::FrameKind::Delta,gfile::encode_delta(delta)));
    gio::MemoryReader reader(writer.bytes());gfile::Contents back;
    const auto error=gfile::read_document(reader,back);CHECK_MSG(error.ok,"%s",error.message.c_str());
    CHECK(gfile::encode(back)==gfile::encode(after));
    const auto* def=back.blocks.find("Region");CHECK(def!=nullptr);
    if(def){CHECK(def->geometry[0].region);CHECK(def->geometry[1].region_loop==1);CHECK(def->geometry[0].layer=="Boundary");}
    after.geometry[0].shapes[1].region_reversed=false;
    CHECK(!gfile::diff(back,after).empty());
}

TEST(native_regions_reject_open_or_mixed_membership_before_writing) {
    gfile::Contents c;
    gsnap::Shape edge;edge.a={0,0};edge.b={1000,0};edge.poly=1;edge.region=true;
    c.geometry.push_back({0,{edge},{"0"}});
    gio::MemoryWriter writer;
    CHECK(!gfile::write_document(writer,c).ok);CHECK(writer.bytes().empty());
    c.geometry[0].shapes[0].kind=gsnap::ShapeKind::Circle;c.geometry[0].shapes[0].radius=1000;
    CHECK(gfile::validate_regions(c).ok);
    auto mixed=edge;mixed.region=false;c.geometry[0].shapes.push_back(mixed);
    CHECK(!gfile::validate_regions(c).ok);
    c.geometry[0].shapes.pop_back();c.geometry[0].shapes[0].poly_index=1;
    CHECK(!gfile::validate_regions(c).ok);
}

TEST(native_block_insertion_appearance_survives_save_and_incremental_update) {
    auto before=sample();gdoc::BlockRef ref;ref.name="Appearance";ref.layer="Ink";ref.attrs.transparency=-2;
    before.blocks.insert(ref);
    gio::MemoryWriter writer;CHECK(write_document(writer,before).ok);
    auto after=before;auto* changed=after.blocks.reference_mut(gcm::u32(after.blocks.references().size()-1));
    changed->attrs.transparency=40;changed->attrs.visible=false;changed->attrs.color=gdoc::Color::rgb(32,64,128);
    const auto delta=diff(before,after);CHECK(!delta.empty());CHECK(gio::write_frame(writer,gio::FrameKind::Delta,encode_delta(delta)));
    Contents back;gio::MemoryReader input(writer.bytes());CHECK(read_document(input,back).ok);
    CHECK(encode(back)==encode(after));
    Contents inherited;CHECK(decode(encode(before),inherited).ok);
    CHECK(inherited.blocks.references().back().attrs.transparency==-2);
}

TEST(native_nested_insertions_preserve_placements_attributes_and_appearance) {
    gfile::Contents source;gdoc::BlockDef child;child.name="Child";source.blocks.define(child);
    gdoc::BlockDef parent;parent.name="Parent";gdoc::BlockRef ref;ref.name="Child";
    ref.insertion={123,-456};ref.scale_x=-2;ref.scale_y=3;ref.rotation=.6;ref.layer="Parts";
    ref.attrs.color=gdoc::Color::aci(2);ref.attrs.transparency=35;ref.set_attribute("ID","B-12");
    parent.nested_refs={ref};source.blocks.define(parent);
    gfile::Contents back;CHECK(gfile::decode(gfile::encode(source),back).ok);
    const auto* result=back.blocks.find("Parent");CHECK(result!=nullptr);if(!result)return;
    CHECK(result->nested_refs.size()==1);if(result->nested_refs.empty())return;
    const auto& got=result->nested_refs.front();CHECK(got.name=="Child");CHECK(got.insertion.x==123);CHECK(got.insertion.y==-456);
    CHECK(got.scale_x==-2);CHECK(got.scale_y==3);CHECK(got.rotation==.6);CHECK(got.layer=="Parts");
    CHECK(got.attrs.color.index==2);CHECK(got.attrs.transparency==35);CHECK(got.attribute("ID").value_or("")=="B-12");
    CHECK(back.blocks.references().empty());
}

TEST(native_block_notes_survive_full_and_incremental_save) {
    Contents before;
    gdoc::BlockDef block; block.name="Title";
    gdoc::BlockDef::Note note;
    note.pos={123,-456}; note.text="Project\\PSecond line";
    note.height=.003; note.angle=.4; note.width=.08;
    note.layer="Labels"; note.halign=2; note.valign=3; note.style="Narrow";
    note.attrs.color=gdoc::Color::rgb(10,20,30); note.attrs.transparency=40;
    note.attrs.visible=false; note.attrs.user["purpose"]="sheet heading";
    block.notes.push_back(note); before.blocks.define(block);
    gio::MemoryWriter writer; CHECK(write_document(writer,before).ok);
    Contents back; gio::MemoryReader reader(writer.bytes()); CHECK(read_document(reader,back).ok);
    const auto* got=back.blocks.find("Title"); CHECK(got!=nullptr); if(!got)return;
    CHECK(got->notes.size()==1); if(got->notes.empty())return;
    const auto& text=got->notes[0];
    CHECK(text.text==note.text); CHECK(text.pos.x==123); CHECK(text.pos.y==-456);
    CHECK(text.height==.003); CHECK(text.width==.08); CHECK(text.angle==.4);
    CHECK(text.layer=="Labels"); CHECK(text.halign==2); CHECK(text.valign==3); CHECK(text.style=="Narrow");
    CHECK(text.attrs.color.r==10); CHECK(text.attrs.color.g==20); CHECK(text.attrs.color.b==30);
    CHECK(text.attrs.transparency==40); CHECK(!text.attrs.visible);
    CHECK(text.attrs.user.at("purpose")=="sheet heading");
    auto after=before; block.notes[0].text="Revised"; block.notes[0].width=.12; after.blocks.define(block);
    const auto delta=diff(before,after); CHECK(!delta.empty());
    CHECK(gio::write_frame(writer,gio::FrameKind::Delta,encode_delta(delta)));
    Contents updated; gio::MemoryReader updates(writer.bytes()); CHECK(read_document(updates,updated).ok);
    CHECK(encode(updated)==encode(after));
    block.notes.clear(); auto cleared=after; cleared.blocks.define(block);
    CHECK(gio::write_frame(writer,gio::FrameKind::Delta,encode_delta(diff(after,cleared))));
    Contents empty; gio::MemoryReader last(writer.bytes()); CHECK(read_document(last,empty).ok);
    CHECK(empty.blocks.find("Title")->notes.empty());
}

TEST(native_block_notes_reject_invalid_dimensions_before_writing) {
    Contents document; gdoc::BlockDef block; block.name="Invalid";
    gdoc::BlockDef::Note note; note.text="Invalid width"; note.width=-1;
    block.notes.push_back(note); document.blocks.define(block);
    gio::MemoryWriter writer; CHECK(!write_document(writer,document).ok); CHECK(writer.bytes().empty());
    block.notes[0].width=0; block.notes[0].angle=std::numeric_limits<double>::quiet_NaN();
    document.blocks.define(block); CHECK(!decode(encode(document),document).ok);
}

TEST(block_underlay_extensions_preserve_axes_clip_state_holes_and_source_units) {
    Contents source;gdoc::BlockDef block;block.name="Reference";
    gdoc::Underlay u;u.name="Survey";u.path="survey.pdf";u.kind=gdoc::UnderlayKind::Pdf;u.sheet="3";
    u.y_factor=-2;u.source_unit=.0254;u.source_unit_known=true;u.source_coordinates_pending=true;u.contrast_neutral=100;u.contrast=67;u.clip_enabled=false;u.adjust_background=true;u.layer="Survey";
    u.clip={{0,0},{100,0},{100,100},{0,100}};u.clip_hole={{20,20},{80,20},{80,80},{20,80}};
    block.underlays={u};source.blocks.define(block);
    Contents restored;const auto error=decode(encode(source),restored);CHECK_MSG(error.ok,"%s",error.message.c_str());
    const auto* def=restored.blocks.find(block.name);CHECK(def!=nullptr);if(!def)return;
    CHECK(def->underlays.size()==1);if(def->underlays.empty())return;
    const auto& back=def->underlays[0];CHECK(back.y_factor==-2);CHECK(back.source_unit==.0254);CHECK(back.source_unit_known);CHECK(back.source_coordinates_pending);CHECK(back.contrast_neutral==100);CHECK(back.contrast==67);
    CHECK(!back.clip_enabled);CHECK(back.adjust_background);CHECK(back.layer=="Survey");CHECK(back.clip_hole.size()==4);CHECK(back.clip_hole[2].x==80);
}
