#include "test.hpp"

#include <cmath>
#include <random>
#include <vector>

#include "exchange/bitcode.hpp"

using namespace gdwg;

TEST(dwg_bits_are_packed_most_significant_first) {
    const u8 data[] = {0b10110001, 0b01000000};
    BitReader r(data, sizeof data);
    CHECK(r.b());
    CHECK(!r.b());
    CHECK(r.b());
    CHECK(r.b());
    CHECK(!r.b());
    CHECK(!r.b());
    CHECK(!r.b());
    CHECK(r.b());
    CHECK(!r.b());
    CHECK(r.b());
}

TEST(dwg_raw_integers_are_little_endian_within_the_bit_stream) {
    BitWriter w;
    w.rs(0x1234);
    w.rl(0xDEADBEEF);
    BitReader r(w.bytes().data(), w.bytes().size());
    CHECK_MSG(r.rs() == 0x1234, "the short came back wrong");
    CHECK_MSG(r.rl() == 0xDEADBEEF, "the long came back wrong");
}

TEST(dwg_bitshort_uses_all_four_codes) {
    BitWriter w;
    for (u16 v : {u16(0), u16(1), u16(255), u16(256), u16(257), u16(65535)})
        w.bs(v);
    BitReader r(w.bytes().data(), w.bytes().size());
    CHECK(r.bs() == 0);
    CHECK(r.bs() == 1);
    CHECK(r.bs() == 255);
    CHECK_MSG(r.bs() == 256, "the 256 code was misread");
    CHECK(r.bs() == 257);
    CHECK(r.bs() == 65535);
}

TEST(dwg_bitshort_256_is_written_as_its_own_code) {
    BitWriter w;
    w.bs(256);
    CHECK_MSG(w.bit_position() == 2, "256 took %zu bits", w.bit_position());
}

TEST(dwg_bitlong_and_bitdouble_round_trip) {
    BitWriter w;
    for (u32 v : {0u, 1u, 255u, 256u, 70000u, 0xFFFFFFFFu}) w.bl(v);
    for (f64 v : {0.0, 1.0, -1.0, 3.14159, 1e12, -1e-9}) w.bd(v);

    BitReader r(w.bytes().data(), w.bytes().size());
    for (u32 v : {0u, 1u, 255u, 256u, 70000u, 0xFFFFFFFFu})
        CHECK_MSG(r.bl() == v, "bitlong %u", v);
    for (f64 v : {0.0, 1.0, -1.0, 3.14159, 1e12, -1e-9})
        CHECK_MSG(r.bd() == v, "bitdouble %g", v);
}

TEST(dwg_bitdouble_shortcuts_zero_and_one) {
    BitWriter zero;
    zero.bd(0.0);
    CHECK(zero.bit_position() == 2);
    BitWriter one;
    one.bd(1.0);
    CHECK(one.bit_position() == 2);
    BitWriter other;
    other.bd(2.5);
    CHECK(other.bit_position() == 66);
}

TEST(dwg_modular_char_carries_its_sign_in_the_last_group) {
    BitWriter w;
    const std::vector<gcm::i32> values = {0, 1, -1, 63, -63, 64, -64, 127, -127,
                                          128, -128, 4097, -4097, 1000000,
                                          -1000000};
    for (gcm::i32 v : values) w.mc(v);

    BitReader r(w.bytes().data(), w.bytes().size());
    for (gcm::i32 v : values) {
        const gcm::i32 got = r.mc();
        CHECK_MSG(got == v, "modular char %d came back as %d", v, got);
    }
}

TEST(dwg_modular_char_boundary_at_bit_six) {
    for (gcm::i32 v : {-64, -65, -8191, -8192, -8193}) {
        BitWriter w;
        w.mc(v);
        BitReader r(w.bytes().data(), w.bytes().size());
        CHECK_MSG(r.mc() == v, "%d did not survive", v);
    }
}

TEST(dwg_modular_short_and_unsigned_char_round_trip) {
    BitWriter w;
    const std::vector<u32> values = {0, 1, 127, 128, 32767, 32768, 100000,
                                     4000000};
    for (u32 v : values) w.ms(v);
    for (u32 v : values) w.umc(v);

    BitReader r(w.bytes().data(), w.bytes().size());
    for (u32 v : values) CHECK_MSG(r.ms() == v, "modular short %u", v);
    for (u32 v : values) CHECK_MSG(r.umc() == v, "unsigned modular char %u", v);
}

TEST(dwg_text_variables_round_trip) {
    BitWriter w;
    for (const char* s : {"", "0", "A-WALL", "a layer with spaces"}) w.tv(s);
    BitReader r(w.bytes().data(), w.bytes().size());
    CHECK(r.tv() == "");
    CHECK(r.tv() == "0");
    CHECK(r.tv() == "A-WALL");
    CHECK(r.tv() == "a layer with spaces");
}

TEST(dwg_handles_carry_a_code_and_a_length) {
    BitWriter w;
    w.handle({2, 0x1F});
    w.handle({5, 0x1234});
    w.handle({3, 0x00});

    BitReader r(w.bytes().data(), w.bytes().size());
    const Handle a = r.handle();
    CHECK_MSG(a.code == 2, "code %u", a.code);
    CHECK_MSG(a.value == 0x1F, "value %llu", (unsigned long long)a.value);
    const Handle b = r.handle();
    CHECK(b.code == 5 && b.value == 0x1234);
    const Handle c = r.handle();
    CHECK(c.code == 3 && c.value == 0);
}

TEST(dwg_relative_handles_resolve_against_the_referring_object) {
    BitWriter next;
    next.handle({6, 0});
    BitReader rn(next.bytes().data(), next.bytes().size());
    CHECK(rn.handle(100).value == 101);

    BitWriter prev;
    prev.handle({8, 0});
    BitReader rp(prev.bytes().data(), prev.bytes().size());
    CHECK(rp.handle(100).value == 99);

    BitWriter fwd;
    fwd.handle({0x0A, 7});
    BitReader rf(fwd.bytes().data(), fwd.bytes().size());
    CHECK_MSG(rf.handle(100).value == 107, "a forward offset did not add");

    BitWriter back;
    back.handle({0x0C, 7});
    BitReader rb(back.bytes().data(), back.bytes().size());
    CHECK_MSG(rb.handle(100).value == 93, "a backward offset did not subtract");

    BitWriter abs;
    abs.handle({2, 42});
    BitReader ra(abs.bytes().data(), abs.bytes().size());
    CHECK(ra.handle(100).value == 42);
}

TEST(dwg_bit_extrusion_shortcuts_the_common_normal) {
    BitWriter w;
    w.b(true);
    w.b(false);
    w.bd3(0.0, 0.6, 0.8);

    BitReader r(w.bytes().data(), w.bytes().size());
    f64 x = 9, y = 9, z = 9;
    r.be(x, y, z);
    CHECK(x == 0.0 && y == 0.0 && z == 1.0);
    r.be(x, y, z);
    CHECK(x == 0.0 && std::fabs(y - 0.6) < 1e-12 && std::fabs(z - 0.8) < 1e-12);
}

TEST(dwg_bit_thickness_shortcuts_zero) {
    BitWriter w;
    w.b(true);
    w.b(false);
    w.bd(2.5);
    BitReader r(w.bytes().data(), w.bytes().size());
    CHECK(r.bt() == 0.0);
    CHECK(r.bt() == 2.5);
}

TEST(dwg_a_double_with_a_default_keeps_what_it_was_not_given) {
    const f64 fallback = 1234.5;
    BitWriter w;
    w.bb(0);
    w.bb(3);
    w.rd(-9.75);

    BitReader r(w.bytes().data(), w.bytes().size());
    CHECK(r.dd(fallback) == fallback);
    CHECK(r.dd(fallback) == -9.75);
}

TEST(dwg_reading_past_the_end_reports_it_and_stops) {
    const u8 data[] = {0xFF, 0xFF};
    BitReader r(data, sizeof data);
    CHECK(r.ok());
    r.rl();
    CHECK_MSG(!r.ok(), "an over-read was not reported");
    CHECK(r.exhausted());
    CHECK(r.rc() == 0);
}

TEST(dwg_alignment_moves_to_the_next_byte) {
    BitWriter w;
    w.b(true);
    w.align_byte();
    w.rc(0xAB);
    BitReader r(w.bytes().data(), w.bytes().size());
    CHECK(r.b());
    r.align_byte();
    CHECK(r.rc() == 0xAB);
    CHECK(r.bit_position() == 16);
}

TEST(dwg_a_mixed_stream_survives_a_round_trip) {
    BitWriter w;
    w.b(true);
    w.bs(300);
    w.bd(2.5);
    w.mc(-77);
    w.rc(0x5A);
    w.bl(70000);
    w.tv("A-WALL");
    w.b(false);
    w.bd3(1.0, 0.0, -3.5);
    w.handle({5, 0x99});
    w.rs(0xBEEF);

    BitReader r(w.bytes().data(), w.bytes().size());
    CHECK(r.b());
    CHECK(r.bs() == 300);
    CHECK(r.bd() == 2.5);
    CHECK(r.mc() == -77);
    CHECK(r.rc() == 0x5A);
    CHECK(r.bl() == 70000);
    CHECK(r.tv() == "A-WALL");
    CHECK(!r.b());
    f64 x, y, z;
    r.bd3(x, y, z);
    CHECK(x == 1.0 && y == 0.0 && z == -3.5);
    const Handle h = r.handle();
    CHECK(h.code == 5 && h.value == 0x99);
    CHECK_MSG(r.rs() == 0xBEEF, "the stream drifted before the last field");
    CHECK(r.ok());
}

TEST(dwg_a_random_stream_survives_a_round_trip) {
    std::mt19937 rng(20260829u);
    std::uniform_int_distribution<int> what(0, 5);
    std::uniform_int_distribution<gcm::i32> small(-100000, 100000);
    std::uniform_int_distribution<u32> big(0, 0xFFFFFFFFu);

    struct Field { int kind; gcm::i32 i; u32 u; f64 d; };
    std::vector<Field> fields;
    BitWriter w;
    for (int n = 0; n < 800; ++n) {
        Field f{what(rng), small(rng), big(rng),
                static_cast<f64>(small(rng)) * 0.125};
        switch (f.kind) {
            case 0: w.b(f.i & 1); break;
            case 1: w.bs(static_cast<u16>(f.u & 0xFFFF)); break;
            case 2: w.bl(f.u); break;
            case 3: w.bd(f.d); break;
            case 4: w.mc(f.i); break;
            default: w.rc(static_cast<u8>(f.u & 0xFF)); break;
        }
        fields.push_back(f);
    }

    BitReader r(w.bytes().data(), w.bytes().size());
    for (std::size_t n = 0; n < fields.size(); ++n) {
        const Field& f = fields[n];
        switch (f.kind) {
            case 0: CHECK_MSG(r.b() == bool(f.i & 1), "field %zu", n); break;
            case 1: CHECK_MSG(r.bs() == u16(f.u & 0xFFFF), "field %zu", n); break;
            case 2: CHECK_MSG(r.bl() == f.u, "field %zu", n); break;
            case 3: CHECK_MSG(r.bd() == f.d, "field %zu", n); break;
            case 4: CHECK_MSG(r.mc() == f.i, "field %zu", n); break;
            default: CHECK_MSG(r.rc() == u8(f.u & 0xFF), "field %zu", n); break;
        }
    }
    CHECK(r.ok());
}

TEST(dwg_crc_is_not_zips) {
    const u8 data[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    const u16 got = crc8(0, data, sizeof data);
    // CRC-16/ARC check value for "123456789".
    CHECK_MSG(got == 0xBB3D, "got %04X", got);
}

TEST(dwg_crc_detects_a_flipped_bit) {
    std::vector<u8> data(64);
    for (std::size_t i = 0; i < data.size(); ++i)
        data[i] = static_cast<u8>(i * 7 + 3);
    const u16 good = crc8(0xC0C1, data.data(), data.size());
    for (std::size_t i = 0; i < data.size(); ++i) {
        std::vector<u8> bad = data;
        bad[i] ^= 0x01;
        CHECK_MSG(crc8(0xC0C1, bad.data(), bad.size()) != good,
                  "a flipped bit in byte %zu was not caught", i);
    }
}

TEST(dwg_crc_seed_matters) {
    const u8 data[] = {1, 2, 3};
    CHECK(crc8(0, data, sizeof data) != crc8(0xC0C1, data, sizeof data));
}
