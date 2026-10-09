#include "test.hpp"
#include <unistd.h>

#include <cstdio>
#include <cerrno>
#include <cstring>
#include <string>
#include <filesystem>

#include "engine/frame.hpp"
#include "engine/transport.hpp"

using namespace gio;

namespace {
std::vector<u8> bytes(const char* s) {
    return std::vector<u8>(reinterpret_cast<const u8*>(s),
                           reinterpret_cast<const u8*>(s) + std::strlen(s));
}
std::string temp_path(const char* name) {
    // Unique per process: the parallel test variants must not share files.
    return "/tmp/glcad_io_" + std::to_string(getpid()) + "_" + name;
}
}

TEST(stream_memory_round_trip) {
    MemoryWriter w;
    CHECK(w.write_u32(0xDEADBEEFu));
    CHECK(w.write_u16(0x1234));
    CHECK(w.write_u64(0x0123456789ABCDEFull));
    CHECK(w.write_u8(0x5A));
    CHECK(w.position() == 15);

    MemoryReader r(w.bytes());
    u32 a = 0; u16 b = 0; u64 c = 0; u8 d = 0;
    CHECK(r.read_u32(a) && a == 0xDEADBEEFu);
    CHECK(r.read_u16(b) && b == 0x1234);
    CHECK(r.read_u64(c) && c == 0x0123456789ABCDEFull);
    CHECK(r.read_u8(d) && d == 0x5A);
    CHECK(r.eof());
}

TEST(stream_integers_are_little_endian_on_the_wire) {
    MemoryWriter w;
    w.write_u32(0x01020304u);
    const auto& b = w.bytes();
    CHECK(b.size() == 4);
    CHECK_MSG(b[0] == 0x04 && b[1] == 0x03 && b[2] == 0x02 && b[3] == 0x01,
              "byte order is %02x %02x %02x %02x", b[0], b[1], b[2], b[3]);
}

TEST(stream_read_exact_fails_on_a_short_stream) {
    MemoryReader r(bytes("abc"));
    u8 buf[8];
    CHECK_MSG(!r.read_exact(buf, 8), "read_exact accepted a short read");
}

TEST(stream_read_returns_what_is_there) {
    MemoryReader r(bytes("abcdef"));
    u8 buf[100];
    const std::size_t n = r.read(buf, sizeof(buf));
    CHECK(n == 6);
    CHECK(r.eof());
    CHECK(r.read(buf, sizeof(buf)) == 0);
}

TEST(stream_memory_reader_seeks) {
    MemoryReader r(bytes("0123456789"));
    CHECK(r.seekable());
    CHECK(r.seek(4));
    u8 c = 0;
    CHECK(r.read_u8(c) && c == '4');
    CHECK(!r.seek(999));
    CHECK(r.size() == 10);
}

TEST(stream_null_writer_counts_without_keeping) {
    NullWriter n;
    for (int i = 0; i < 100; ++i) n.write_u32(i);
    CHECK(n.position() == 400);
}

TEST(stream_file_round_trip) {
    const std::string path = temp_path("round.bin");
    std::remove(path.c_str());

    {
        FileWriter w;
        CHECK_MSG(w.open(path).ok, "could not open for writing");
        const auto payload = bytes("GLCAD stream test");
        CHECK(w.write_all(payload));
        CHECK(w.close().ok);
    }

    FileReader r;
    CHECK(r.open(path).ok);
    CHECK(r.size() == 17);
    std::vector<u8> back;
    CHECK(r.read_all(back));
    CHECK(back == bytes("GLCAD stream test"));
    r.close();
    std::remove(path.c_str());
}

TEST(stream_file_write_is_atomic) {
    const std::string path = temp_path("atomic.bin");
    std::remove(path.c_str());

    {
        FileWriter first;
        CHECK(first.open(path).ok);
        first.write_all(bytes("original"));
        CHECK(first.close().ok);
    }

    {
        FileWriter aborted;
        CHECK(aborted.open(path).ok);
        aborted.write_all(bytes("this save never finishes"));
        std::FILE* f = std::fopen(path.c_str(), "rb");
        CHECK_MSG(f != nullptr, "the original vanished during the save");
        char buf[16] = {};
        std::fread(buf, 1, 8, f);
        std::fclose(f);
        CHECK_MSG(std::string(buf) == "original",
                  "the original was clobbered before the commit: '%s'", buf);
    }
    std::remove(path.c_str());
    std::remove((path + ".part").c_str());
}

TEST(stream_file_open_failure_is_reported) {
    FileReader r;
    const Status s = r.open("/definitely/not/here/at/all.glcad");
    CHECK(!s.ok);
    CHECK(!s.message.empty());
}

TEST(frame_crc32_matches_the_known_vector) {
    // "123456789" -> 0xCBF43926 is the standard CRC-32 check value.
    const auto v = bytes("123456789");
    CHECK_MSG(crc32(v) == 0xCBF43926u, "got %08x", crc32(v));
    CHECK(crc32(nullptr, 0) == 0u);
}

TEST(frame_round_trip) {
    MemoryWriter w;
    const auto payload = bytes("a whole document would go here");
    CHECK(write_frame(w, FrameKind::Document, payload));
    CHECK(w.bytes().size() == kFrameHeaderSize + payload.size());

    MemoryReader r(w.bytes());
    std::vector<u8> back;
    const auto res = read_frame(r, back);
    CHECK_MSG(res.ok, "%s", res.error.c_str());
    CHECK(res.header.kind == FrameKind::Document);
    CHECK(res.header.length == payload.size());
    CHECK(back == payload);
}

TEST(frame_several_in_one_stream) {
    MemoryWriter w;
    write_frame(w, FrameKind::Document, bytes("one"));
    write_frame(w, FrameKind::Thumbnail, bytes("two"));
    write_frame(w, FrameKind::Extension, bytes("three"));

    MemoryReader r(w.bytes());
    std::vector<u8> p;
    int seen = 0;
    for (;;) {
        const auto res = read_frame(r, p);
        if (res.end_of_stream) break;
        CHECK_MSG(res.ok, "%s", res.error.c_str());
        ++seen;
    }
    CHECK_MSG(seen == 3, "read %d frames", seen);
}

TEST(frame_empty_payload_is_legal) {
    MemoryWriter w;
    CHECK(write_frame(w, FrameKind::Ack, nullptr, 0));
    MemoryReader r(w.bytes());
    std::vector<u8> p;
    const auto res = read_frame(r, p);
    CHECK(res.ok);
    CHECK(p.empty());
}

TEST(frame_clean_end_is_not_an_error) {
    MemoryReader r(std::vector<u8>{});
    std::vector<u8> p;
    const auto res = read_frame(r, p);
    CHECK(!res.ok);
    CHECK_MSG(res.end_of_stream, "an empty stream reported as corruption");
    CHECK(res.error.empty());
}

TEST(frame_truncated_header_is_corruption_not_an_end) {
    MemoryWriter w;
    write_frame(w, FrameKind::Document, bytes("payload"));
    auto b = w.bytes();
    b.resize(10);                       // cut inside the header

    MemoryReader r(b);
    std::vector<u8> p;
    const auto res = read_frame(r, p);
    CHECK(!res.ok);
    CHECK(!res.end_of_stream);
    CHECK(!res.error.empty());
}

TEST(frame_truncated_payload_is_detected) {
    MemoryWriter w;
    write_frame(w, FrameKind::Document, bytes("a reasonably long payload here"));
    auto b = w.bytes();
    b.resize(b.size() - 5);

    MemoryReader r(b);
    std::vector<u8> p;
    const auto res = read_frame(r, p);
    CHECK(!res.ok);
    CHECK_MSG(p.empty(), "a truncated payload was handed back anyway");
}

TEST(frame_corrupt_payload_fails_the_checksum) {
    MemoryWriter w;
    write_frame(w, FrameKind::Document, bytes("the quick brown fox"));
    auto b = w.bytes();
    b[kFrameHeaderSize + 4] ^= 0x20;

    MemoryReader r(b);
    std::vector<u8> p;
    const auto res = read_frame(r, p);
    CHECK(!res.ok);
    CHECK_MSG(res.error.find("checksum") != std::string::npos, "%s",
              res.error.c_str());
}

TEST(frame_foreign_data_is_rejected) {
    MemoryReader r(bytes("this is not a GLCAD file at all, not even close"));
    std::vector<u8> p;
    const auto res = read_frame(r, p);
    CHECK(!res.ok);
    CHECK(!res.end_of_stream);
}

TEST(frame_implausible_length_is_refused_before_allocating) {
    MemoryWriter w;
    w.write_u32(kFrameMagic);
    w.write_u16(kFrameVersion);
    w.write_u16(1);
    w.write_u64(0xFFFFFFFFFFull);
    w.write_u32(0);
    w.write_u32(0);

    MemoryReader r(w.bytes());
    std::vector<u8> p;
    const auto res = read_frame(r, p);
    CHECK(!res.ok);
    CHECK(res.error.find("implausible") != std::string::npos);
}

TEST(frame_unknown_kind_can_be_skipped) {
    MemoryWriter w;
    write_frame(w, static_cast<FrameKind>(9999), bytes("from the future"));
    write_frame(w, FrameKind::Document, bytes("something we do understand"));

    MemoryReader r(w.bytes());
    std::vector<u8> p;
    auto res = read_frame(r, p);
    CHECK(res.ok);
    CHECK(res.header.kind != FrameKind::Document);

    res = read_frame(r, p);
    CHECK(res.ok);
    CHECK(res.header.kind == FrameKind::Document);
    CHECK(p == bytes("something we do understand"));
}

TEST(endpoint_parses_every_family) {
    Endpoint e;
    CHECK(Endpoint::parse("file:///drawings/plan.glcad", e));
    CHECK(e.family == Family::File);
    CHECK(e.host == "/drawings/plan.glcad");

    CHECK(Endpoint::parse("ipv4://192.168.1.20:5911", e));
    CHECK(e.family == Family::IPv4);
    CHECK(e.host == "192.168.1.20");
    CHECK(e.port == 5911);

    CHECK(Endpoint::parse("memory://scratch", e));
    CHECK(e.family == Family::Memory);

    CHECK(Endpoint::parse("tcp://10.0.0.5:1234", e));
    CHECK(e.family == Family::IPv4);
    CHECK(e.port == 1234);
}

TEST(endpoint_rejects_nonsense) {
    Endpoint e;
    CHECK(!Endpoint::parse("just a path", e));
    CHECK(!Endpoint::parse("smtp://mail:25", e));
    CHECK(!Endpoint::parse("ipv4://10.0.0.1", e));         // no port
    CHECK(!Endpoint::parse("ipv4://10.0.0.1:0", e));       // port 0
    CHECK(!Endpoint::parse("ipv4://10.0.0.1:99999", e));   // out of range
    CHECK(!Endpoint::parse("ipv4://10.0.0.1:80x", e));     // not a number
}

TEST(endpoint_round_trips_through_its_url) {
    const Endpoint a = Endpoint::ipv4("127.0.0.1", 5911);
    CHECK(a.to_string() == "ipv4://127.0.0.1:5911");

    Endpoint b;
    CHECK(Endpoint::parse(a.to_string(), b));
    CHECK(b.family == a.family && b.host == a.host && b.port == a.port);

    const Endpoint f = Endpoint::file("/tmp/x.glcad");
    CHECK(f.to_string() == "file:///tmp/x.glcad");
}

TEST(endpoint_validity_requires_a_port_for_network_families) {
    CHECK(Endpoint::file("/tmp/a").valid());
    CHECK(Endpoint::memory("s").valid());
    CHECK(Endpoint::ipv4("10.0.0.1", 80).valid());
    CHECK(!Endpoint::ipv4("10.0.0.1", 0).valid());
    CHECK(!Endpoint::file("").valid());
}

TEST(transport_file_writes_and_reads_back) {
    const std::string path = temp_path("transport.glcad");
    std::remove(path.c_str());
    auto& reg = TransportRegistry::instance();

    Status s;
    auto out = reg.open("file://" + path, Mode::Write, s);
    CHECK_MSG(out != nullptr, "%s", s.message.c_str());
    CHECK(out->output() != nullptr);
    CHECK_MSG(out->input() == nullptr, "a write connection offered a reader");
    write_frame(*out->output(), FrameKind::Document, bytes("through a transport"));
    CHECK(out->close().ok);

    auto in = reg.open("file://" + path, Mode::Read, s);
    CHECK_MSG(in != nullptr, "%s", s.message.c_str());
    CHECK(in->input() != nullptr);
    std::vector<u8> p;
    const auto res = read_frame(*in->input(), p);
    CHECK(res.ok);
    CHECK(p == bytes("through a transport"));
    in->close();
    std::remove(path.c_str());
}

TEST(transport_memory_is_byte_identical_to_the_file_transport) {
    MemoryTransport::reset();
    const std::string path = temp_path("compare.glcad");
    std::remove(path.c_str());
    auto& reg = TransportRegistry::instance();
    const auto payload = bytes("identical either way");

    Status s;
    auto a = reg.open("memory://compare", Mode::Write, s);
    CHECK(a != nullptr);
    write_frame(*a->output(), FrameKind::Document, payload);
    a->close();

    auto b = reg.open("file://" + path, Mode::Write, s);
    CHECK(b != nullptr);
    write_frame(*b->output(), FrameKind::Document, payload);
    b->close();

    FileReader fr;
    CHECK(fr.open(path).ok);
    std::vector<u8> from_file;
    fr.read_all(from_file);
    fr.close();

    CHECK_MSG(MemoryTransport::buffer("compare") == from_file,
              "the two transports produced different bytes");
    std::remove(path.c_str());
}

TEST(transport_ipv4_reports_honestly_that_it_is_not_here_yet) {
    auto& reg = TransportRegistry::instance();
    Status s;
    auto c = reg.open("ipv4://127.0.0.1:5911", Mode::ReadWrite, s);
    CHECK(c == nullptr);
    CHECK(!s.ok);
    CHECK_MSG(s.message.find("ipv4") != std::string::npos,
              "unhelpful message: %s", s.message.c_str());
}

TEST(transport_unparseable_url_is_reported) {
    auto& reg = TransportRegistry::instance();
    Status s;
    auto c = reg.open("nonsense", Mode::Read, s);
    CHECK(c == nullptr);
    CHECK(!s.ok);
}

TEST(transport_registry_lists_what_ships) {
    const auto names = TransportRegistry::instance().registered();
    bool file = false, memory = false;
    for (const auto& n : names) {
        if (n == "file") file = true;
        if (n == "memory") memory = true;
    }
    CHECK(file);
    CHECK(memory);
    CHECK(TransportRegistry::instance().find(Family::File) != nullptr);
}

TEST(transport_a_new_family_needs_no_change_above_this_layer) {
    class FakeUnix final : public Transport {
    public:
        const char* name() const noexcept override { return "fake-unix"; }
        bool supports(Family f) const noexcept override { return f == Family::Unix; }
        std::unique_ptr<Connection> connect(const Endpoint& e, Mode,
                                            Status& s) override {
            class C final : public Connection {
            public:
                explicit C(Endpoint e) : e_(std::move(e)) {}
                Reader* input() override { return nullptr; }
                Writer* output() override { return &w_; }
                bool connected() const override { return true; }
                Status close() override { return Status::good(); }
                Endpoint peer() const override { return e_; }
            private:
                Endpoint e_;
                MemoryWriter w_;
            };
            s = Status::good();
            return std::make_unique<C>(e);
        }
    };

    auto& reg = TransportRegistry::instance();
    CHECK(reg.find(Family::Unix) == nullptr);
    reg.add(std::make_unique<FakeUnix>());

    Status s;
    auto c = reg.open("unix:///tmp/glcad.sock", Mode::Write, s);
    CHECK_MSG(c != nullptr, "%s", s.message.c_str());
    CHECK(c->connected());
    CHECK(c->peer().family == Family::Unix);
    CHECK(write_frame(*c->output(), FrameKind::Hello, bytes("hello")));
}

TEST(stream_the_same_bytes_reach_a_file_and_a_socket_shaped_endpoint) {
    MemoryTransport::reset();
    const std::string path = temp_path("parity.bin");
    std::remove(path.c_str());
    auto& reg = TransportRegistry::instance();
    Status s;

    auto emit = [&](Writer& w) {
        write_frame(w, FrameKind::Hello, bytes("glcad/1"));
        write_frame(w, FrameKind::Document, bytes("the document itself"));
        write_frame(w, FrameKind::Ack, nullptr, 0);
    };

    auto to_file = reg.open("file://" + path, Mode::Write, s);
    CHECK(to_file != nullptr);
    emit(*to_file->output());
    to_file->close();

    auto to_mem = reg.open("memory://parity", Mode::Write, s);
    CHECK(to_mem != nullptr);
    emit(*to_mem->output());
    to_mem->close();

    FileReader fr;
    CHECK(fr.open(path).ok);
    std::vector<u8> from_file;
    fr.read_all(from_file);
    fr.close();
    std::remove(path.c_str());

    CHECK(MemoryTransport::buffer("parity") == from_file);

    MemoryReader r(from_file);
    std::vector<u8> p;
    auto a = read_frame(r, p);
    CHECK(a.ok && a.header.kind == FrameKind::Hello);
    auto b = read_frame(r, p);
    CHECK(b.ok && b.header.kind == FrameKind::Document);
    CHECK(p == bytes("the document itself"));
    auto c = read_frame(r, p);
    CHECK(c.ok && c.header.kind == FrameKind::Ack && p.empty());
    CHECK(read_frame(r, p).end_of_stream);
}

#ifndef _WIN32
TEST(stream_failed_commit_preserves_the_previous_destination) {
    const auto path = temp_path("failed-commit.bin");
    { FileWriter original; CHECK(original.open(path).ok); CHECK(original.write_all(bytes("issued"))); CHECK(original.close().ok); }
    FileWriter replacement;
    CHECK(replacement.open(path).ok);
    CHECK(replacement.write_all(bytes("replacement")));
    CHECK(std::remove((path + ".part").c_str()) == 0);
    CHECK(!replacement.close().ok);
    FileReader reader; CHECK(reader.open(path).ok);
    std::vector<u8> content; CHECK(reader.read_all(content)); CHECK(content == bytes("issued"));
    reader.close(); std::remove(path.c_str());
}
#endif

TEST(stream_unicode_paths_roundtrip_and_replace) {
    const auto path = temp_path("drawing-\xc3\xa9-\xe5\x9b\xbe.bin");
    for (const char* text : {"first", "revised"}) {
        FileWriter writer; const auto opened = writer.open(path);
        CHECK_MSG(opened.ok, "%s (errno %d)", opened.message.c_str(), errno);
        CHECK(writer.write_all(bytes(text))); CHECK(writer.close().ok);
        FileReader reader; const auto read_opened = reader.open(path);
        CHECK_MSG(read_opened.ok, "%s (errno %d)", read_opened.message.c_str(), errno);
        std::vector<u8> content; CHECK(reader.read_all(content)); CHECK(content == bytes(text));
    }
    std::error_code ec; std::filesystem::remove(std::filesystem::u8path(path), ec);
    CHECK(!ec);
}
