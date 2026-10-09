#include <algorithm>
#include <cmath>
#include "test.hpp"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "exchange/dwg.hpp"

using namespace gdwg;

namespace {

gcm::WorkingVolume vol() {
    gcm::WorkingVolume v;
    v.extent = 500.0;
    v.resolution_bits = 32;
    return v;
}

std::vector<u8> fixture() {
    static const char* candidates[] = {
        "glcad/src/exchange/tests/data/r2000.dwg",
        "../src/exchange/tests/data/r2000.dwg",
        "../../src/exchange/tests/data/r2000.dwg",
        GLCAD_DWG_FIXTURE,
    };
    for (const char* path : candidates) {
        std::FILE* f = std::fopen(path, "rb");
        if (!f) continue;
        std::fseek(f, 0, SEEK_END);
        const long n = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        std::vector<u8> out(static_cast<std::size_t>(n));
        const std::size_t got = std::fread(out.data(), 1, out.size(), f);
        std::fclose(f);
        out.resize(got);
        return out;
    }
    return {};
}

std::vector<u8> synthetic_header(const char* tag, u32 sections) {
    std::vector<u8> d(0x200, 0);
    std::memcpy(d.data(), tag, 6);
    d[0x15] = static_cast<u8>(sections & 0xFF);
    d[0x16] = static_cast<u8>((sections >> 8) & 0xFF);

    std::size_t at = 0x19;
    for (u32 i = 0; i < sections && at + 9 <= d.size(); ++i) {
        d[at] = static_cast<u8>(i);
        d[at + 1] = 0x40;
        d[at + 5] = 0x10;
        at += 9;
    }
    at += 2;
    static const u8 sentinel[16] = {0x95, 0xA0, 0x4E, 0x28, 0x99, 0x82, 0x1A,
                                    0xE5, 0x5E, 0x41, 0xE0, 0x5F, 0x9D, 0x3A,
                                    0x4D, 0x00};
    if (at + 16 <= d.size()) std::memcpy(d.data() + at, sentinel, 16);
    return d;
}

}

TEST(dwg_version_tags_are_recognised) {
    CHECK(version_of("AC1015") == Version::R2000);
    CHECK(version_of("AC1018") == Version::R2004);
    CHECK(version_of("AC1032") == Version::R2018);
    CHECK(version_of("AC1014") == Version::R14);
    CHECK(version_of("nonsense") == Version::Unknown);
    for (Version v : {Version::R13, Version::R14, Version::R2000, Version::R2004,
                      Version::R2007, Version::R2010, Version::R2013,
                      Version::R2018, Version::Unknown})
        CHECK(version_name(v)[0] != '\0');
}

TEST(dwg_a_newer_version_is_named_not_called_corrupt) {
    const auto d = synthetic_header("AC1014", 6);
    FileHeader h;
    std::string error;
    CHECK(!read_header(d.data(), d.size(), h, error));
    CHECK_MSG(error.find("AutoCAD R14") != std::string::npos,
              "the reason was '%s'", error.c_str());
    CHECK_MSG(error.find("corrupt") == std::string::npos,
              "a readable file was called corrupt");
}

TEST(dwg_something_that_is_not_a_dwg_is_refused) {
    std::vector<u8> junk(0x200, 0x41);
    FileHeader h;
    std::string error;
    CHECK(!read_header(junk.data(), junk.size(), h, error));
    CHECK(!error.empty());

    const u8 tiny[] = {'A', 'C', '1', '0', '1', '5'};
    CHECK(!read_header(tiny, sizeof tiny, h, error));
    CHECK(!read_header(nullptr, 0, h, error));
}

TEST(dwg_reads_the_header_of_a_real_file) {
    const auto file = fixture();
    if (file.empty()) {
        CHECK_MSG(false, "the DWG fixture is missing");
        return;
    }

    FileHeader h;
    std::string error;
    CHECK_MSG(read_header(file.data(), file.size(), h, error), "%s",
              error.c_str());
    CHECK(h.version == Version::R2000);
    CHECK(h.tag == "AC1015");
    CHECK_MSG(h.sections.size() == 6, "%zu section records", h.sections.size());
    CHECK_MSG(h.codepage == 30, "codepage %u", h.codepage);
}

TEST(dwg_every_section_lies_inside_the_file) {
    const auto file = fixture();
    if (file.empty()) return;

    FileHeader h;
    std::string error;
    CHECK(read_header(file.data(), file.size(), h, error));
    for (const SectionLocator& s : h.sections) {
        CHECK_MSG(s.seeker + s.size <= file.size(),
                  "section %u runs from %u for %u, past the %zu-byte file",
                  s.number, s.seeker, s.size, file.size());
        if (s.size == 0) CHECK(s.seeker == 0);
    }
    CHECK(h.section(2) != nullptr);
    CHECK(h.section(0) != nullptr);
    CHECK(h.section(99) == nullptr);
}

TEST(dwg_a_section_pointing_outside_the_file_is_refused) {
    auto d = synthetic_header("AC1015", 1);
    d[0x19 + 1] = 0xFF;
    d[0x19 + 2] = 0xFF;
    d[0x19 + 3] = 0xFF;
    d[0x19 + 4] = 0x7F;
    FileHeader h;
    std::string error;
    CHECK(!read_header(d.data(), d.size(), h, error));
    CHECK_MSG(error.find("outside") != std::string::npos, "%s", error.c_str());
}

TEST(dwg_an_absurd_section_count_is_refused) {
    auto d = synthetic_header("AC1015", 6);
    d[0x15] = 0xFF;
    d[0x16] = 0xFF;
    FileHeader h;
    std::string error;
    CHECK(!read_header(d.data(), d.size(), h, error));
    CHECK(error.find("no DWG has") != std::string::npos);
}

TEST(dwg_a_missing_sentinel_is_caught) {
    auto d = synthetic_header("AC1015", 6);
    FileHeader h;
    std::string error;
    CHECK(read_header(d.data(), d.size(), h, error));

    const std::size_t sentinel_at = 0x19 + 6 * 9 + 2;
    d[sentinel_at + 3] ^= 0xFF;
    CHECK_MSG(!read_header(d.data(), d.size(), h, error),
              "a damaged sentinel was accepted");
    CHECK(error.find("sentinel") != std::string::npos);
}

TEST(dwg_reads_the_object_map_of_a_real_file) {
    const auto file = fixture();
    if (file.empty()) return;

    FileHeader h;
    std::string error;
    CHECK(read_header(file.data(), file.size(), h, error));

    std::vector<ObjectRef> objects;
    CHECK_MSG(read_object_map(file.data(), file.size(), h, objects, error), "%s",
              error.c_str());
    CHECK_MSG(!objects.empty(), "the object map came back empty");

    std::vector<u64> handles;
    for (const ObjectRef& o : objects) {
        CHECK_MSG(o.offset < file.size(),
                  "handle %llu points to %u, past the %zu-byte file",
                  static_cast<unsigned long long>(o.handle), o.offset,
                  file.size());
        handles.push_back(o.handle);
    }
    std::sort(handles.begin(), handles.end());
    CHECK_MSG(std::adjacent_find(handles.begin(), handles.end()) == handles.end(),
              "a handle appeared twice: the deltas were not accumulated");
}

TEST(dwg_object_map_handles_climb) {
    const auto file = fixture();
    if (file.empty()) return;

    FileHeader h;
    std::string error;
    read_header(file.data(), file.size(), h, error);
    std::vector<ObjectRef> objects;
    CHECK(read_object_map(file.data(), file.size(), h, objects, error));
    if (objects.size() < 3) return;

    std::size_t ascending = 0;
    for (std::size_t i = 1; i < objects.size(); ++i)
        if (objects[i].handle > objects[i - 1].handle) ++ascending;
    CHECK_MSG(ascending == objects.size() - 1,
              "%zu of %zu handles climbed", ascending, objects.size() - 1);

    u32 lo = objects[0].offset, hi = objects[0].offset;
    for (const ObjectRef& o : objects) {
        lo = std::min(lo, o.offset);
        hi = std::max(hi, o.offset);
    }
    CHECK_MSG(hi - lo > 64, "every object is within %u bytes of the others",
              hi - lo);
}

TEST(dwg_a_corrupt_object_map_is_caught_by_its_crc) {
    auto file = fixture();
    if (file.empty()) return;

    FileHeader h;
    std::string error;
    CHECK(read_header(file.data(), file.size(), h, error));
    const SectionLocator* map = h.section(2);
    CHECK(map != nullptr);
    if (!map) return;

    file[map->seeker + 4] ^= 0xFF;
    std::vector<ObjectRef> objects;
    CHECK_MSG(!read_object_map(file.data(), file.size(), h, objects, error),
              "a corrupted object map was accepted");
    CHECK_MSG(error.find("CRC") != std::string::npos, "%s", error.c_str());
}

TEST(dwg_reads_a_real_file_end_to_end) {
    const auto file = fixture();
    if (file.empty()) return;

    const ReadResult r = read(file.data(), file.size(), vol());
    CHECK_MSG(r.ok, "%s", r.error.c_str());
    CHECK(r.header.version == Version::R2000);
    CHECK(!r.objects.empty());
}

TEST(dwg_reading_a_file_that_is_not_there_says_so) {
    const ReadResult r = read_file("/nonexistent/drawing.dwg", vol());
    CHECK(!r.ok);
    CHECK(r.error.find("could not open") != std::string::npos);
}

TEST(dwg_reads_from_a_path) {
    static const char* candidates[] = {
        "glcad/src/exchange/tests/data/r2000.dwg", GLCAD_DWG_FIXTURE};
    for (const char* path : candidates) {
        const ReadResult r = read_file(path, vol());
        if (r.ok) {
            CHECK(r.header.tag == "AC1015");
            return;
        }
    }
    CHECK_MSG(false, "the fixture could not be read from any known path");
}

namespace {

double wx(const gcm::WorkingVolume& v, const gcm::Lattice2& p) {
    double x, y;
    gcm::to_world(v, p, x, y);
    return x;
}
double wy(const gcm::WorkingVolume& v, const gcm::Lattice2& p) {
    double x, y;
    gcm::to_world(v, p, x, y);
    return y;
}

const Entity* find(const ReadResult& r, const char* kind, std::size_t nth = 0) {
    std::size_t seen = 0;
    for (const auto& e : r.entities) {
        if (e.kind == kind && seen++ == nth) return &e;
    }
    return nullptr;
}

}

#define CHECK_NEAR(a, b, tol)                                                  \
    CHECK_MSG(std::fabs((a) - (b)) <= (tol), "%.9f != %.9f (tol %g)",          \
              double(a), double(b), double(tol))

TEST(dwg_decodes_every_entity_in_the_fixture) {
    const auto file = fixture();
    if (file.empty()) return;

    const ReadResult r = read(file.data(), file.size(), vol());
    CHECK_MSG(r.ok, "%s", r.error.c_str());
    CHECK_MSG(r.entities.size() == 5, "expected 5 entities, got %zu",
              r.entities.size());
}

TEST(dwg_decodes_line_endpoints) {
    const auto file = fixture();
    if (file.empty()) return;
    const auto v = vol();
    const ReadResult r = read(file.data(), file.size(), v);

    const Entity* a = find(r, "LINE", 0);
    const Entity* b = find(r, "LINE", 1);
    CHECK(a != nullptr && b != nullptr);
    if (!a || !b) return;

    CHECK(a->shape.kind == gsnap::ShapeKind::Segment);
    CHECK_NEAR(wx(v, a->shape.a), 0.0, 1e-6);
    CHECK_NEAR(wy(v, a->shape.a), 0.0, 1e-6);
    CHECK_NEAR(wx(v, a->shape.b), 100.0, 1e-6);
    CHECK_NEAR(wy(v, a->shape.b), 50.0, 1e-6);

    CHECK_NEAR(wx(v, b->shape.a), 100.0, 1e-6);
    CHECK_NEAR(wy(v, b->shape.a), 50.0, 1e-6);
    CHECK_NEAR(wx(v, b->shape.b), 100.0, 1e-6);
    CHECK_NEAR(wy(v, b->shape.b), 0.0, 1e-6);
}

TEST(dwg_decodes_circle_centre_and_radius) {
    const auto file = fixture();
    if (file.empty()) return;
    const auto v = vol();
    const ReadResult r = read(file.data(), file.size(), v);

    const Entity* c = find(r, "CIRCLE");
    CHECK(c != nullptr);
    if (!c) return;
    CHECK(c->shape.kind == gsnap::ShapeKind::Circle);
    CHECK_NEAR(wx(v, c->shape.a), 25.0, 1e-6);
    CHECK_NEAR(wy(v, c->shape.a), 25.0, 1e-6);
    CHECK_NEAR(double(c->shape.radius) / v.scale(), 10.0, 1e-6);
}

TEST(dwg_decodes_arc_angles_in_radians) {
    const auto file = fixture();
    if (file.empty()) return;
    const auto v = vol();
    const ReadResult r = read(file.data(), file.size(), v);

    const Entity* a = find(r, "ARC");
    CHECK(a != nullptr);
    if (!a) return;
    CHECK(a->shape.kind == gsnap::ShapeKind::Arc);
    CHECK_NEAR(wx(v, a->shape.a), 60.0, 1e-6);
    CHECK_NEAR(wy(v, a->shape.a), 10.0, 1e-6);
    CHECK_NEAR(double(a->shape.radius) / v.scale(), 5.0, 1e-6);
    CHECK_NEAR(a->shape.start_angle, 0.0, 1e-9);
    CHECK_NEAR(a->shape.end_angle, 1.5707963267948966, 1e-9);
}

TEST(dwg_decodes_point_position) {
    const auto file = fixture();
    if (file.empty()) return;
    const auto v = vol();
    const ReadResult r = read(file.data(), file.size(), v);

    const Entity* p = find(r, "POINT");
    CHECK(p != nullptr);
    if (!p) return;
    CHECK(p->shape.kind == gsnap::ShapeKind::Point);
    CHECK_NEAR(wx(v, p->shape.a), 7.0, 1e-6);
    CHECK_NEAR(wy(v, p->shape.a), 8.0, 1e-6);
}

TEST(dwg_object_data_ends_inside_the_object) {
    const auto file = fixture();
    if (file.empty()) return;
    const ReadResult r = read(file.data(), file.size(), vol());

    CHECK(!r.raw.empty());
    for (const auto& o : r.raw) {
        CHECK_MSG(o.data_bits > 0, "object %llu declared no data bits",
                  (unsigned long long)o.handle);
        CHECK_MSG(data_end_bits(o) + 16 <= (o.body_at + std::size_t(o.size) * 8),
                  "object %llu ends at bit %zu, past its %u byte body",
                  (unsigned long long)o.handle, data_end_bits(o), o.size);
        CHECK_MSG(data_end_bits(o) > o.data_at,
                  "object %llu ends at bit %zu, before its data begins at %zu",
                  (unsigned long long)o.handle, data_end_bits(o), o.data_at);
    }
}

TEST(dwg_resolves_the_layer_an_entity_points_at) {
    const auto file = fixture();
    if (file.empty()) return;
    const ReadResult r = read(file.data(), file.size(), vol());

    const RawObject* layer_object = nullptr;
    for (const auto& o : r.raw) {
        if (static_cast<ObjectType>(o.type) == ObjectType::Layer) {
            layer_object = &o;
            break;
        }
    }
    CHECK(layer_object != nullptr);
    CHECK(!r.entities.empty());
    if (!layer_object || r.entities.empty()) return;

    for (const auto& e : r.entities) {
        CHECK_MSG(e.layer_handle == layer_object->handle,
                  "%s points at layer handle %llu, not %llu", e.kind.c_str(),
                  (unsigned long long)e.layer_handle,
                  (unsigned long long)layer_object->handle);
        CHECK(e.layer == "0");
    }
}

TEST(dwg_reads_layer_names_from_the_table) {
    const ReadResult r = read_file(GLCAD_DWG_LAYERS_FIXTURE, vol());
    if (!r.ok) return;

    CHECK_MSG(r.layers.find("WALLS") != nullptr, "no layer WALLS");
    CHECK_MSG(r.layers.find("DIMS") != nullptr, "no layer DIMS");
    CHECK(r.layers.find("0") != nullptr);
    CHECK_MSG(r.layers.size() == 3, "expected 3 layers, got %zu",
              r.layers.size());
}

TEST(dwg_layer_names_carry_no_trailing_nul) {
    const ReadResult r = read_file(GLCAD_DWG_LAYERS_FIXTURE, vol());
    if (!r.ok) return;
    for (const auto& l : r.layers.all()) {
        CHECK_MSG(l.name.find('\0') == std::string::npos,
                  "layer name '%s' contains a NUL", l.name.c_str());
        CHECK(!l.name.empty());
    }
}

TEST(dwg_2004_decompression_literals_and_copies) {
    // Eight literals, a three-byte copy from one back, then the terminator.
    const gdwg::u8 stream[] = {0x05, 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H',
                               0x21, 0x00, 0x00,     // 0x21: 3 bytes; offset (0 >> 2 | 0 << 6) + 1
                               0x11};
    std::string err;
    std::size_t produced = 0;
    const std::vector<gdwg::u8> out = gdwg::decompress_r2004(stream, sizeof stream, 11, &err, &produced);
    CHECK_MSG(err.empty(), "%s", err.c_str());
    CHECK_MSG(produced == 11, "%zu bytes", produced);
    CHECK(std::string(out.begin(), out.end()) == "ABCDEFGHHHH");

    // 0x40+: length in the high nibble (0x4x -> 3), offset in bits 2-3 plus a byte.
    const gdwg::u8 stream2[] = {0x03, 'x', 'y', 'z', 'w', 'q', 'r',   // six literals
                                0x40, 0x00,                            // copy 3 from 1 back: rrr
                                0x11};
    const std::vector<gdwg::u8> out2 = gdwg::decompress_r2004(stream2, sizeof stream2, 9, &err, &produced);
    CHECK_MSG(err.empty(), "%s", err.c_str());
    CHECK(std::string(out2.begin(), out2.end()) == "xyzwqrrrr");
}

TEST(dwg_object_map_offsets_restart_in_each_section) {
    // Two map sections: BE size, (UMC handle delta, MC offset delta) pairs, CRC.
    const auto section = [](std::vector<gdwg::u8> body) {
        std::vector<gdwg::u8> out;
        const gdwg::u16 size = static_cast<gdwg::u16>(body.size() + 2);
        out.push_back(static_cast<gdwg::u8>(size >> 8));
        out.push_back(static_cast<gdwg::u8>(size & 0xFF));
        out.insert(out.end(), body.begin(), body.end());
        const gdwg::u16 crc = gdwg::crc8(0xC0C1, out.data(), out.size());
        out.push_back(static_cast<gdwg::u8>(crc >> 8));
        out.push_back(static_cast<gdwg::u8>(crc & 0xFF));
        return out;
    };
    std::vector<gdwg::u8> map;
    // Section 1: handle 1 at 4, handle 2 at 4+10, handle 3 at 14+6.
    auto s1 = section({0x01, 0x04, 0x01, 0x0A, 0x01, 0x06});
    // Section 2: handle 4 at 100 (absolute; two MC bytes), handle 5 at 100+8.
    auto s2 = section({0x01, 0xE4, 0x00, 0x01, 0x08});
    map.insert(map.end(), s1.begin(), s1.end());
    map.insert(map.end(), s2.begin(), s2.end());
    map.push_back(0x00); map.push_back(0x02);   // the empty terminating section
    std::vector<gdwg::ObjectRef> refs;
    std::string err;
    CHECK_MSG(gdwg::parse_object_map(map.data(), map.size(), refs, err), "%s", err.c_str());
    CHECK_MSG(refs.size() == 5, "%zu refs", refs.size());
    if (refs.size() == 5) {
        CHECK(refs[0].handle == 1 && refs[0].offset == 4);
        CHECK(refs[2].handle == 3 && refs[2].offset == 20);
        CHECK_MSG(refs[3].handle == 4 && refs[3].offset == 100, "second section: handle %llu at %u",
                  (unsigned long long)refs[3].handle, refs[3].offset);
        CHECK(refs[4].offset == 108);
    }
}

TEST(dwg_reads_every_dimension_kind_and_leaders) {
    const auto v = vol();
    const ReadResult r = read_file(GLCAD_DWG_DIMS_FIXTURE, v);
    CHECK_MSG(r.ok, "%s", r.error.c_str());
    CHECK_MSG(r.dimensions.size() == 8, "%zu dimensions", r.dimensions.size());
    std::vector<const gdim::Dimension*> simple;for(const auto& dimension:r.dimensions)if(!dimension.dim.leader_text_absolute)simple.push_back(&dimension.dim);
    const auto at = [&](std::size_t i) -> const gdim::Dimension* {
        return i < simple.size() ? simple[i] : nullptr;
    };
    const auto is = [&](const gcm::Lattice2& p, double x, double y) {
        return std::fabs(wx(v, p) - x) < 1e-3 && std::fabs(wy(v, p) - y) < 1e-3;
    };
    if (const auto* d = at(0)) {   // rotated linear: 0,0 -> 10,0 at y = 2
        CHECK(d->kind == gdim::Kind::Horizontal);
        CHECK(is(d->p1, 0, 0) && is(d->p2, 10, 0) && is(d->p3, 0, 2));
    }
    if (const auto* d = at(1)) {   // aligned
        CHECK(d->kind == gdim::Kind::Aligned);
        CHECK(is(d->p1, 0, 0) && is(d->p2, 7.07, 7.07));
    }
    if (const auto* d = at(2)) {   // radius: centre 3,0, on the arc at 0,0
        CHECK(d->kind == gdim::Kind::Radial);
        CHECK(is(d->centre, 3, 0) && is(d->p1, 0, 0));
    }
    if (const auto* d = at(3)) {   // diameter with a text override
        CHECK(d->kind == gdim::Kind::Diameter);
        // first_arc_pt (DXF 15) is +3; def_pt (DXF 10) is -3, per LibreDWG.
        CHECK(is(d->p1, 3, 0) && is(d->p2, -3, 0) && is(d->centre, 0, 0));
        CHECK(std::fabs(gdim::measure(*d,v)-6)<1e-3);
        CHECK(d->text_override == "D=6");
    }
    if (const auto* d = at(4)) {   // three-point angular about the origin
        CHECK(d->kind == gdim::Kind::Angular);
        CHECK(is(d->centre, 0, 0) && is(d->p1, 4, 0) && is(d->p2, 0, 4));
    }
    if (const auto* d = at(5)) {   // ordinate
        CHECK(d->kind == gdim::Kind::Ordinate);
        CHECK(is(d->p1, 6, 0) && is(d->p2, 6, -2));
        CHECK(std::fabs(gdim::measure(*d,v)-6)<1e-2);
    }
    CHECK(!find(r,"LEADER",0));
    if (const auto* d=at(6)) {
        CHECK(d->kind==gdim::Kind::Leader);
        CHECK(is(d->p1,1,1)&&is(d->p2,3,4)&&is(d->p3,6,4));
    }

}

TEST(dwg_reads_the_class_table_and_multileaders) {
    const auto v = vol();
    const ReadResult r = read_file(GLCAD_DWG_DIMS_FIXTURE, v);
    CHECK_MSG(r.ok, "%s", r.error.c_str());
    u16 ml = 0;
    for (const auto& [num, name] : r.classes) if (name == "MULTILEADER") ml = num;
    CHECK_MSG(ml == 532, "MULTILEADER class number %u", unsigned(ml));
    const auto is = [&](const gcm::Lattice2& p, double x, double y) {
        return std::fabs(wx(v, p) - x) < 1e-2 && std::fabs(wy(v, p) - y) < 1e-2;
    };
    CHECK(!find(r,"MULTILEADER",0));
    const gdim::Dimension* leader=nullptr;
    for(const auto& dimension:r.dimensions)if(dimension.dim.text_override=="xx xx")leader=&dimension.dim;
    CHECK(leader);if(!leader)return;
    CHECK(leader->kind==gdim::Kind::Leader);CHECK(leader->leader_paths.size()==1);
    if(leader->leader_paths.size()==1) {
        const auto& points=leader->leader_paths.front().vertices;CHECK(points.size()==3);
        if(points.size()==3){CHECK(is(points[0],-2577.70,142.429));CHECK(is(points[1],-2239.49,607.218));CHECK(is(points[2],-2239.13,607.218));}
    }
    CHECK(leader->layer=="Tavolo 3");CHECK(is(leader->p3,-2237.13,609.218));
    CHECK(std::fabs(leader->leader_text_height-4)<1e-9);
    CHECK(std::none_of(r.texts.begin(),r.texts.end(),[](const auto& text){return text.text=="xx xx";}));

}

TEST(dwg_relative_handle_references_resolve_against_the_referrer) {
    // code 6: own + 1 (no bytes); code 8: own - 1; 0xA: own + value; 0xC: own - value.
    const u8 bytes[] = {0x60,                  // 6, count 0
                        0x80,                  // 8, count 0
                        0xA1, 0x05,            // A, one byte: +5
                        0xC1, 0x03,            // C, one byte: -3
                        0x51, 0x2A};           // 5, absolute 0x2A
    BitReader r(bytes, sizeof(bytes));
    CHECK(r.handle(100).value == 101);
    CHECK(r.handle(100).value == 99);
    CHECK(r.handle(100).value == 105);
    CHECK(r.handle(100).value == 97);
    CHECK(r.handle(100).value == 0x2A);
}

TEST(dwg_reads_polylines_and_hatches) {
    const auto v = vol();
    const ReadResult r = read_file(GLCAD_DWG_DIMS_FIXTURE, v);
    CHECK_MSG(r.ok, "%s", r.error.c_str());
    // The closed square at (20,0)..(24,4): four segments.
    std::size_t poly_segs = 0;
    for (const auto& e : r.entities)
        if (e.kind == "POLYLINE_3D") {
            ++poly_segs;
            CHECK(wx(v, e.shape.a) > 19.0 && wx(v, e.shape.a) < 25.0);
            CHECK(wy(v, e.shape.a) > -1.0 && wy(v, e.shape.a) < 5.0);
        }
    CHECK_MSG(poly_segs == 4, "%zu polyline segments", poly_segs);
    CHECK_MSG(r.hatches.size() == 1, "%zu hatches", r.hatches.size());
    if (r.hatches.size() == 1) {
        const auto& h = r.hatches[0];
        CHECK(h.solid);
        CHECK_MSG(h.boundary.size() == 4, "%zu boundary edges", h.boundary.size());
        CHECK_MSG(std::fabs(wx(v, h.seed) - 32.0) < 1e-3, "seed at (%g, %g)", wx(v, h.seed), wy(v, h.seed));
    }
}

TEST(dwg_modern_common_appearance_retains_alpha_and_color_handle_alignment) {
    for(const auto version:{Version::R2004,Version::R2007,Version::R2010,Version::R2013,Version::R2018})
        for(const bool color_handle:{false,true})
            for(const u32 alpha:{0u,0x01000000u,0x03000000u,0x03000080u,0x030000ffu}) {
                BitWriter stream;
                stream.handle({0,0x40});stream.bs(0);stream.b(false);
                stream.bb(2);stream.bl(0);stream.b(true);
                if(version>=Version::R2013)stream.b(false);
                stream.bs(color_handle?0xe007:0xa007);stream.bl(alpha);
                if(!color_handle)stream.bl(0xc2123456);
                stream.bd(2.5);stream.bb(3);stream.bb(0);
                if(version>=Version::R2007){stream.bb(0);stream.rc(0);}
                if(version>=Version::R2010){stream.b(false);stream.b(false);stream.b(false);}
                stream.bs(1);stream.rc(11);
                RawObject raw;raw.handle=0x40;raw.data_bits=u32(stream.bit_position());
                if(color_handle)stream.handle({5,0x22});
                stream.handle({5,0x10});stream.handle({5,0x30});
                raw.size=u32(stream.bytes().size());
                EntityAppearance appearance;
                CHECK(read_entity_appearance(stream.bytes().data(),stream.bytes().size(),raw,version,appearance));
                CHECK(appearance.layer_handle==0x10);CHECK(appearance.linetype_handle==0x30);
                CHECK(appearance.color_handle==(color_handle?0x22:0));
                CHECK(!appearance.attrs.visible);CHECK(appearance.attrs.lineweight==gdoc::Lineweight::W050);
                CHECK(appearance.attrs.linetype_scale==2.5);
                if(!color_handle)CHECK(appearance.attrs.color==gdoc::Color::rgb(0x12,0x34,0x56));
                const int expected=alpha==0?-1:alpha==0x01000000u?-2:
                    int(std::lround((255-(alpha&255))*100.0/255));
                CHECK(appearance.attrs.transparency==expected);
                CHECK(!read_entity_appearance(stream.bytes().data(),stream.bytes().size()/2,raw,version,appearance));
            }
}


TEST(dwg_layer_transparency_resolves_appids_after_layer_records) {
    const auto path = std::filesystem::path(GLCAD_DWG_FIXTURE).parent_path() /
        "transparency/layer-alpha.dwg";
    const auto result = read_file(path.string(),vol());
    CHECK_MSG(result.ok,"%s",result.error.c_str());
    for (const auto& [name, expected] : std::vector<std::pair<std::string,int>>{
        {"0",25},{"HALF",50},{"INVISIBLE",100},{"UNRELATED",0},
        {"WRONG_TYPE",0},{"BYBLOCK",0},{"INVALID_BITS",0}}) {
        const auto* layer = result.layers.find(name);
        CHECK_MSG(layer != nullptr,"missing layer %s",name.c_str());
        if (layer) CHECK_MSG(layer->transparency == expected,"%s: %d",name.c_str(),layer->transparency);
    }
    CHECK(!result.entities.empty());
    if (!result.entities.empty()) CHECK(result.entities.front().layer == "0");
    const auto layer = std::find_if(result.raw.begin(),result.raw.end(),[](const auto& o){return o.handle==0x100;});
    const auto app = std::find_if(result.raw.begin(),result.raw.end(),[](const auto& o){return o.handle==0x200;});
    CHECK(layer != result.raw.end() && app != result.raw.end() && layer < app);
}

TEST(dwg_layer_eed_is_bounded_and_does_not_shift_appearance) {
    for (const auto version : {Version::R2000,Version::R2004,Version::R2007,
                              Version::R2010,Version::R2013,Version::R2018}) {
        BitWriter stream;
        stream.handle({0,0x40});
        stream.bs(5);stream.handle({5,0x80});stream.rc(71);stream.rl(0x02000080);
        stream.bs(6);stream.handle({5,0x81});
        for (u8 byte : {u8(4),u8(4),u8(71),u8(0),u8(0),u8(2)}) stream.rc(byte);
        stream.bs(0);stream.bl(0);
        if (version>=Version::R2004) stream.b(true);
        if (version>=Version::R2013) stream.b(false);
        if (version<Version::R2007) stream.tv("Faded");
        if (version<=Version::R2004) {stream.b(false);stream.bs(0);stream.b(false);}
        else stream.bs(0);
        stream.bs(8 | 16);stream.bs(3);
        if (version>=Version::R2004) {stream.bl(0);stream.rc(0);}
        if (version>=Version::R2007) {
            const auto start=stream.bit_position();
            stream.bs(5);for(char c:std::string("Faded"))stream.rs(u16(c));
            const auto length=stream.bit_position()-start;
            stream.rs(u16(length));stream.b(true);
        }
        RawObject raw;raw.handle=0x40;raw.data_bits=u32(stream.bit_position());
        raw.size=u32(stream.bytes().size());
        LayerAppearance appearance;
        CHECK(read_layer_appearance(stream.bytes().data(),stream.bytes().size(),raw,version,appearance));
        CHECK(appearance.layer.name=="Faded");
        CHECK(appearance.layer.locked && appearance.layer.plottable);
        CHECK(appearance.layer.color==gdoc::Color::aci(3));
        CHECK(appearance.extended_longs.size()==1);
        if (!appearance.extended_longs.empty()) {
            CHECK(appearance.extended_longs.front().first==0x80);
            CHECK(appearance.extended_longs.front().second==0x02000080);
        }
        const auto before=appearance.layer;
        auto truncated=raw;truncated.data_bits=32;
        CHECK(!read_layer_appearance(stream.bytes().data(),stream.bytes().size(),truncated,version,appearance));
        CHECK(appearance.layer==before);
        CHECK(!read_layer_appearance(stream.bytes().data(),2,raw,version,appearance));
    }
    BitWriter unterminated;
    unterminated.handle({0,0x40});
    for (int i=0;i<64;++i) {unterminated.bs(1);unterminated.handle({5,0x80});unterminated.rc(0);}
    RawObject raw;raw.data_bits=u32(unterminated.bit_position());raw.size=u32(unterminated.bytes().size());
    LayerAppearance appearance;
    CHECK(!read_layer_appearance(unterminated.bytes().data(),unterminated.bytes().size(),raw,Version::R2000,appearance));
}

TEST(dwg_text_styles_decode_all_supported_versions_and_distinct_shape_flags) {
    for(const auto version : {Version::R2000,Version::R2004,Version::R2007,
                             Version::R2010,Version::R2013,Version::R2018})
        for(bool vertical : {false,true})
            for(bool shape_file : {false,true}) {
                BitWriter stream;
                stream.handle({0,0x40});stream.bs(0);stream.bl(0);
                if(version>=Version::R2004)stream.b(true);
                if(version>=Version::R2013)stream.b(false);
                if(version<Version::R2007)stream.tv("Notes");
                if(version<=Version::R2004){stream.b(true);stream.bs(0);stream.b(false);}else stream.bs(0);
                // DWG STYLE stores shape-resource status first, vertical text second.
                stream.b(shape_file);stream.b(vertical);
                stream.bd(2.5);stream.bd(.75);stream.bd(.2);stream.rc(6);stream.bd(3.5);
                if(version<Version::R2007){stream.tv("simplex.shx");stream.tv("bigfont.shx");}
                else {
                    const auto start=stream.bit_position();
                    for(const auto text : {"Notes","simplex.shx","bigfont.shx"}) {
                        stream.bs(u16(std::strlen(text)));for(const char* c=text;*c;++c)stream.rs(u16(*c));
                    }
                    const auto length=stream.bit_position()-start;stream.rs(u16(length));stream.b(true);
                }
                RawObject raw;raw.handle=0x40;raw.type=53;
                raw.data_bits=u32(stream.bit_position());raw.size=u32(stream.bytes().size());
                gtext::TextStyleDef style;
                const bool ok=read_text_style(stream.bytes().data(),stream.bytes().size(),raw,version,style);
                CHECK_MSG(ok==!shape_file,"version %d, vertical %d, shape %d",int(version),vertical,shape_file);
                if(shape_file)continue;
                CHECK(style.name=="Notes");CHECK(style.vertical==vertical);
                CHECK(style.font=="simplex.shx" && style.big_font=="bigfont.shx");
                CHECK(style.height==2.5f && style.width_factor==.75f);
                CHECK(std::abs(style.oblique-.2*180/3.141592653589793)<1e-5);
                CHECK(style.backwards && style.upside_down);
                const auto before=style;
                CHECK(!read_text_style(stream.bytes().data(),stream.bytes().size()/2,raw,version,style));
                CHECK(style==before);
            }
}

TEST(dwg_dimension_styles_read_versioned_fields_and_linear_precision) {
    for(const auto version:{Version::R2000,Version::R2004,Version::R2007,
                            Version::R2010,Version::R2013,Version::R2018})
        for(bool ticks:{false,true}) {
            BitWriter b;std::vector<std::string> strings;
            const auto text=[&](std::string value) {if(version<Version::R2007)b.tv(value);else strings.push_back(value);};
            const auto colour=[&](u8 flags) {
                b.bs(3);
                if(version>=Version::R2004) {
                    b.bl(0xc2123456);b.rc(flags);
                    if(flags&1)text("Colour name");if(flags&2)text("Colour book");
                }
            };
            b.handle({0,0x60});b.bs(0);b.bl(0);
            if(version>=Version::R2004)b.b(true);
            if(version>=Version::R2013)b.b(false);
            text("Dimensions");
            if(version<=Version::R2004){b.b(true);b.bs(0);b.b(false);}else b.bs(0);
            text("pre<>post");text("alternate");
            for(double value:{2.,3.,4.,9.,5.,.1,.2,.3,.4})b.bd(value);
            if(version>=Version::R2007){b.bd(7);b.bd(.8);b.bs(1);colour(3);}
            b.b(true);b.b(false);b.b(true);b.b(false);b.b(true);b.b(false);
            b.bs(4);b.bs(12);b.bs(0);
            if(version>=Version::R2007)b.bs(2);
            for(double value:{6.,7.,ticks?2.:0.,25.4,1.25,.4,.5,.75,.125})b.bd(value);
            b.b(false);b.bs(2);b.b(true);b.b(false);b.b(true);b.b(false);
            colour(1);colour(2);colour(0);
            for(u16 value:{u16(1),u16(5),u16(2),u16(2),u16(2),u16(0),u16(0),u16(4),u16('.'),u16(1),u16(2)})b.bs(value);
            b.b(true);b.b(false);b.bs(1);b.bs(2);b.bs(3);b.bs(4);b.b(false);b.bs(3);
            if(version>=Version::R2007)b.b(true);
            if(version>=Version::R2010){b.b(true);b.bd(1.1);text("alt suffix");b.bd(1.2);text("suffix");}
            b.bs(65534);b.bs(65534);b.b(false);
            if(version>=Version::R2007) {
                const auto start=b.bit_position();
                for(const auto& value:strings){b.bs(u16(value.size()));for(char c:value)b.rs(u16(c));}
                const auto length=b.bit_position()-start;b.rs(u16(length));b.b(true);
            }
            RawObject raw;raw.handle=0x60;raw.type=69;raw.data_bits=u32(b.bit_position());raw.size=u32(b.bytes().size());
            gdim::Style style;
            CHECK_MSG(read_dimension_style(b.bytes().data(),b.bytes().size(),raw,version,style),"version %d",int(version));
            CHECK(style.name=="Dimensions");CHECK(style.prefix=="pre" && style.suffix=="post");
            CHECK(style.dimscale==2 && style.dimtxt==6 && style.dimexo==4 && style.dimexe==5);
            CHECK(style.dimasz==(ticks?2:3));CHECK(style.arrow==(ticks?gdim::ArrowType::Tick:gdim::ArrowType::ClosedFilled));
            CHECK(style.dimtol && style.dimtp==.3 && style.dimtm==.4);
            CHECK(style.dimtih && !style.dimtoh && style.dimse1 && !style.dimse2);
            CHECK(style.text_pos==gdim::TextPos::Below && style.dimsd1 && !style.dimsd2);
            CHECK(style.dimdec==5 && style.format==gunit::Style::Architectural);
            CHECK(style.dimzin_leading && style.dimzin_trailing);
            CHECK(style.dimlfac==1.25 && style.dimgap==.75 && style.dimtvp==.4);
            const auto before=style;
            CHECK(!read_dimension_style(b.bytes().data(),b.bytes().size()/2,raw,version,style));
            CHECK(style.name==before.name && style.dimtxt==before.dimtxt);
        }
}

TEST(dwg_dimension_associations_match_independent_records_in_six_versions) {
    const auto dir=std::filesystem::path(GLCAD_DWG_FIXTURE).parent_path()/"dimassoc";
    std::ifstream input(dir/"records.txt");CHECK(input.good());
    unsigned version;RawObject raw;std::string hex;std::size_t count=0;
    while(input>>version>>raw.handle>>raw.body_at>>raw.data_at>>raw.data_bits>>raw.size>>hex) {
        std::vector<u8> bytes;
        for(std::size_t i=0;i+1<hex.size();i+=2)bytes.push_back(u8(std::stoul(hex.substr(i,2),nullptr,16)));
        DimensionAssociation a;CHECK(read_dimension_association(bytes.data(),bytes.size(),raw,Version(version),a));
        CHECK(a.handle==raw.handle);CHECK(!a.trans_space);CHECK(a.rotated_type==0);
        const auto p=[&](unsigned i)->const DimensionAssociation::PointRef& {return a.points.at(i);};
        if(a.handle==0x3b4) {
            CHECK(a.dimension==0x399);CHECK(a.flags==7);CHECK(a.points.size()==3);
            CHECK(p(0).osnap==13);CHECK(p(1).osnap==1);CHECK(p(2).osnap==3);
            CHECK(p(0).objects==std::vector<u64>{0x398});CHECK(p(0).marker==0);
            CHECK(std::abs(p(0).parameter-4.131293495034893)<1e-12);
            CHECK(std::abs(p(1).parameter-7.448450234144667)<1e-12);
            CHECK(p(0).z==2e50);CHECK(p(2).z==0);
        } else if(a.handle==0x42f) {
            CHECK(a.dimension==0x420);CHECK(a.flags==2);CHECK(a.points.size()==1);CHECK(p(0).index==1);
            CHECK((p(0).objects==std::vector<u64>{0x41a,0x41e}));CHECK(p(0).parameter==1);
        } else if(a.handle==0x452) {
            CHECK(a.dimension==0x43b);CHECK(a.flags==15);CHECK(a.points.size()==4);
            CHECK(p(0).marker==5);CHECK(p(2).marker==4);
            CHECK(std::abs(p(0).x-490.6216519543077)<1e-10);
            CHECK(std::abs(p(1).y-4701.847034571434)<1e-10);
        } else if(a.handle==0x466) {
            CHECK(a.dimension==0x453);CHECK(a.flags==2);CHECK(a.points.size()==1);
            CHECK(p(0).index==1);CHECK(p(0).marker==1);CHECK(p(0).osnap==13);
        } else {
            CHECK(a.flags==3);CHECK(a.points.size()==2);
            const u32 first=a.handle==0x47e?2:a.handle==0x496?3:a.handle==0x4ae?1:4;
            const u32 second=a.handle==0x47e?3:a.handle==0x496?1:4;
            CHECK(p(0).marker==first);CHECK(p(1).marker==second);
            CHECK(p(0).objects==std::vector<u64>{0x419});CHECK(p(1).objects==p(0).objects);
            CHECK(p(0).osnap==13);CHECK(p(1).osnap==(a.handle==0x4c6?1:13));
        }
        for(const auto& ref:a.points) {
            CHECK(ref.subentity==2);CHECK(ref.paths.empty());CHECK(!ref.last_point);
            CHECK(ref.intersection_objects.empty());
        }
        for(std::size_t size=0;size<bytes.size();++size) {
            DimensionAssociation untouched;untouched.dimension=123;
            CHECK(!read_dimension_association(bytes.data(),size,raw,Version(version),untouched));
            CHECK(untouched.dimension==123);
        }
        ++count;
    }
    CHECK(count==48);
}

TEST(dwg_polyline_dimension_associations_resolve_to_source_edges) {
    const auto path=std::filesystem::path(GLCAD_DWG_FIXTURE).parent_path()/"dimassoc/example_2018.dwg";
    auto r=read_file(path.string(),vol());CHECK(r.ok);CHECK(r.dimension_associations.size()==8);
    std::size_t linked=0;
    for(const auto& rec:r.dimensions)if(rec.dim.associative()) {
        ++linked;
        CHECK(rec.handle==0x467 || rec.handle==0x47f || rec.handle==0x497 || rec.handle==0x4af);
        for(const auto& ref:{rec.dim.o1,rec.dim.o2}) {
            const auto shape=std::find_if(r.entities.begin(),r.entities.end(),[&](const auto& e){return e.shape.key==ref.shape;});
            CHECK(shape!=r.entities.end());if(shape==r.entities.end())continue;
            CHECK(shape->handle==0x419);CHECK(shape->subentity>=1 && shape->subentity<=4);
        }
    }
    CHECK(linked==4);
    CHECK(std::any_of(r.skipped.begin(),r.skipped.end(),[](const auto& s){return s.find("unsupported geometry associations")!=std::string::npos;}));
}
