#include "test.hpp"

#include <array>
#include <cmath>

#include "render/registry.hpp"
#include "render/world.hpp"

using namespace gtext;

namespace {

std::string utf(std::initializer_list<u32> cps) {
    std::string s;
    for (u32 c : cps) {
        if (c < 0x80) {
            s += static_cast<char>(c);
        } else if (c < 0x800) {
            s += static_cast<char>(0xC0 | (c >> 6));
            s += static_cast<char>(0x80 | (c & 0x3F));
        } else {
            s += static_cast<char>(0xE0 | (c >> 12));
            s += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            s += static_cast<char>(0x80 | (c & 0x3F));
        }
    }
    return s;
}

const u32 kAlef = 0x0627, kBeh = 0x0628, kTeh = 0x062A, kLam = 0x0644;
const u32 kMeem = 0x0645, kNoon = 0x0646, kReh = 0x0631, kDal = 0x062F;
const u32 kFatha = 0x064E;
const u32 kHebrewAlef = 0x05D0, kHebrewBet = 0x05D1;

f64 dot3(const f64 a[3], const f64 b[3]) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

}

TEST(world_text_frame_axes_are_orthonormal_and_right_handed) {
    for (auto n : {std::array<f64, 3>{{0, 0, 1}}, {{1, 0, 0}}, {{0, -1, 0}},
                   {{0.3, -0.5, 0.8}}}) {
        const TextFrame f = TextFrame::on_plane(1, 2, 3, n[0], n[1], n[2]);
        const f64 u[3] = {f.ux, f.uy, f.uz};
        const f64 v[3] = {f.vx, f.vy, f.vz};
        const f64 w[3] = {f.nx, f.ny, f.nz};

        CHECK(std::fabs(dot3(u, u) - 1.0) < 1e-9);
        CHECK(std::fabs(dot3(v, v) - 1.0) < 1e-9);
        CHECK(std::fabs(dot3(w, w) - 1.0) < 1e-9);
        CHECK_MSG(std::fabs(dot3(u, v)) < 1e-9, "the text axes are not square");
        CHECK(std::fabs(dot3(u, w)) < 1e-9);
        CHECK(std::fabs(dot3(v, w)) < 1e-9);

        const f64 cx = u[1] * v[2] - u[2] * v[1];
        const f64 cy = u[2] * v[0] - u[0] * v[2];
        const f64 cz = u[0] * v[1] - u[1] * v[0];
        CHECK_MSG(std::fabs(cx - w[0]) < 1e-9 && std::fabs(cy - w[1]) < 1e-9 &&
                      std::fabs(cz - w[2]) < 1e-9,
                  "the text frame is left-handed");
    }
}

TEST(world_text_frame_depends_only_on_the_normal) {
    const TextFrame a = TextFrame::on_plane(0, 0, 0, 0.3, -0.5, 0.8);
    const TextFrame b = TextFrame::on_plane(0, 0, 0, 3.0, -5.0, 8.0);
    CHECK(std::fabs(a.ux - b.ux) < 1e-9);
    CHECK(std::fabs(a.vy - b.vy) < 1e-9);
    CHECK(std::fabs(a.nz - b.nz) < 1e-9);
}

TEST(world_text_can_follow_a_chosen_direction) {
    const TextFrame f = TextFrame::along(0, 0, 0, 0, 0, 1, 1, 1, 0);
    const f64 inv = 1.0 / std::sqrt(2.0);
    CHECK_MSG(std::fabs(f.ux - inv) < 1e-9 && std::fabs(f.uy - inv) < 1e-9,
              "the baseline does not follow the requested direction");
    CHECK(std::fabs(f.uz) < 1e-9);
}

TEST(world_text_direction_is_projected_into_the_plane) {
    const TextFrame f = TextFrame::along(0, 0, 0, 0, 0, 1, 1, 0, 5.0);
    CHECK_MSG(std::fabs(f.uz) < 1e-9, "the baseline left the plane (uz=%g)",
              f.uz);
    CHECK(std::fabs(f.ux - 1.0) < 1e-9);

    const TextFrame degenerate = TextFrame::along(0, 0, 0, 0, 0, 1, 0, 0, 1);
    const f64 u[3] = {degenerate.ux, degenerate.uy, degenerate.uz};
    CHECK_MSG(std::fabs(dot3(u, u) - 1.0) < 1e-9,
              "a degenerate direction produced a zero baseline");
}

TEST(world_text_frame_maps_the_origin_to_itself) {
    const TextFrame f = TextFrame::on_plane(3, -4, 5, 0, 1, 0);
    f64 x, y, z;
    f.to_world(0, 0, x, y, z);
    CHECK(std::fabs(x - 3) < 1e-12 && std::fabs(y + 4) < 1e-12 &&
          std::fabs(z - 5) < 1e-12);

    f.to_world(2.5, -1.5, x, y, z);
    const f64 d = (x - 3) * f.nx + (y + 4) * f.ny + (z - 5) * f.nz;
    CHECK_MSG(std::fabs(d) < 1e-12, "a placed point is %g off the plane", d);
}

TEST(bidi_classifies_the_scripts_a_drawing_contains) {
    CHECK(bidi_class('A') == BidiClass::LeftToRight);
    CHECK(bidi_class('z') == BidiClass::LeftToRight);
    CHECK(bidi_class('5') == BidiClass::EuropeanNumber);
    CHECK(bidi_class(' ') == BidiClass::Neutral);
    CHECK(bidi_class('-') == BidiClass::Neutral);
    CHECK(bidi_class(kHebrewAlef) == BidiClass::RightToLeft);
    CHECK(bidi_class(kBeh) == BidiClass::ArabicLetter);
    CHECK(bidi_class(0x4E00) == BidiClass::LeftToRight);
}

TEST(bidi_arabic_indic_digits_are_numbers_not_letters) {
    CHECK(bidi_class(0x0660) == BidiClass::ArabicNumber);
    CHECK(bidi_class(0x0669) == BidiClass::ArabicNumber);
    CHECK(bidi_class(0x06F5) == BidiClass::ArabicNumber);
}

TEST(bidi_paragraph_direction_comes_from_the_first_strong_character) {
    CHECK(paragraph_direction("PART 12") == Direction::LeftToRight);
    CHECK(paragraph_direction(utf({kBeh, kTeh})) == Direction::RightToLeft);
    CHECK(paragraph_direction("  123 " + utf({kBeh})) == Direction::RightToLeft);
    CHECK(paragraph_direction("  123 A") == Direction::LeftToRight);
    CHECK(paragraph_direction("") == Direction::LeftToRight);
    CHECK(paragraph_direction("123") == Direction::LeftToRight);
}

TEST(bidi_a_pure_ltr_string_is_one_run) {
    const auto runs = bidi_runs("SECTION A-A");
    CHECK_MSG(runs.size() == 1, "%zu runs", runs.size());
    CHECK(runs[0].direction == Direction::LeftToRight);
    CHECK(runs[0].begin == 0);
    CHECK(runs[0].end == 11);
}

TEST(bidi_a_pure_rtl_string_is_one_reversed_run) {
    const std::string s = utf({kBeh, kTeh, kNoon});
    const auto runs = bidi_runs(s, Direction::RightToLeft);
    CHECK_MSG(runs.size() == 1, "%zu runs", runs.size());
    CHECK(runs[0].direction == Direction::RightToLeft);
    CHECK(runs[0].begin == 0 && runs[0].end == s.size());
}

TEST(bidi_puts_a_latin_phrase_the_right_way_round_inside_arabic) {
    const std::string arabic = utf({kBeh, kTeh});
    const std::string s = arabic + " ISO " + arabic;
    const auto runs = bidi_runs(s, Direction::RightToLeft);

    CHECK_MSG(runs.size() >= 3, "%zu runs for a mixed line", runs.size());
    CHECK(runs.front().direction == Direction::RightToLeft);
    CHECK(runs.front().begin > runs.back().begin);

    bool found_ltr = false;
    for (const BidiRun& r : runs)
        if (r.direction == Direction::LeftToRight) {
            found_ltr = true;
            CHECK(s.substr(r.begin, r.end - r.begin).find("ISO") !=
                  std::string::npos);
        }
    CHECK_MSG(found_ltr, "the Latin phrase lost its direction");
}

TEST(bidi_a_number_inside_rtl_text_keeps_its_own_order) {
    const std::string arabic = utf({kBeh, kTeh});
    const std::string s = arabic + " 1250 " + arabic;
    const auto runs = bidi_runs(s, Direction::RightToLeft);

    bool found = false;
    for (const BidiRun& r : runs) {
        const std::string piece = s.substr(r.begin, r.end - r.begin);
        if (piece.find("1250") == std::string::npos) continue;
        found = true;
        CHECK_MSG(r.direction == Direction::LeftToRight,
                  "the number run was marked right to left");
        CHECK_MSG(r.level % 2 == 0, "the number is at an odd level");
    }
    CHECK(found);
}

TEST(bidi_runs_cover_the_whole_string_exactly_once) {
    const std::string arabic = utf({kBeh, kTeh, kNoon});
    for (const std::string& s : {std::string("PART 12"),
                                 arabic,
                                 arabic + " ISO 9001 " + arabic,
                                 std::string("A ") + arabic + " B",
                                 utf({kHebrewAlef, kHebrewBet}) + " 5"}) {
        for (Direction d : {Direction::LeftToRight, Direction::RightToLeft}) {
            const auto runs = bidi_runs(s, d);
            std::size_t total = 0;
            std::vector<char> seen(s.size(), 0);
            for (const BidiRun& r : runs) {
                CHECK(r.end > r.begin);
                CHECK(r.end <= s.size());
                for (std::size_t i = r.begin; i < r.end; ++i) {
                    CHECK_MSG(!seen[i], "byte %zu is in two runs", i);
                    seen[i] = 1;
                }
                total += r.end - r.begin;
            }
            CHECK_MSG(total == s.size(), "%zu of %zu bytes covered", total,
                      s.size());
        }
    }
}

TEST(bidi_an_empty_string_has_no_runs) {
    CHECK(bidi_runs("").empty());
}

TEST(arabic_joining_properties) {
    CHECK(joins_left(kBeh));
    CHECK(joins_right(kBeh));
    CHECK(!joins_left(kAlef));
    CHECK(joins_right(kAlef));
    CHECK(!joins_left('A'));
    CHECK(!joins_right('A'));
    CHECK(joins_left(kLam));
    CHECK(!joins_left(kReh));
    CHECK(joins_right(kReh));
}

TEST(arabic_shaping_picks_the_form_from_both_neighbours) {
    const auto s = shape_arabic(utf({kBeh, kTeh, kNoon}));
    CHECK_MSG(s.size() == 3, "%zu glyphs", s.size());
    CHECK_MSG(s[0].form == JoiningForm::Initial, "the first letter is %d",
              static_cast<int>(s[0].form));
    CHECK_MSG(s[1].form == JoiningForm::Medial, "the middle letter is %d",
              static_cast<int>(s[1].form));
    CHECK_MSG(s[2].form == JoiningForm::Final, "the last letter is %d",
              static_cast<int>(s[2].form));

    CHECK(s[0].codepoint == 0xFE8F + 2);   // beh initial
    CHECK(s[1].codepoint == 0xFE95 + 3);   // teh medial
    CHECK(s[2].codepoint == 0xFEE5 + 1);   // noon final
}

TEST(arabic_a_right_joining_letter_breaks_the_run) {
    const auto s = shape_arabic(utf({kBeh, kAlef, kBeh}));
    CHECK(s.size() == 3);
    CHECK(s[0].form == JoiningForm::Initial);
    CHECK_MSG(s[1].form == JoiningForm::Final, "alef came out as %d",
              static_cast<int>(s[1].form));
    CHECK_MSG(s[2].form == JoiningForm::Isolated,
              "the letter after alef joined backwards anyway");
}

TEST(arabic_a_single_letter_is_isolated) {
    const auto s = shape_arabic(utf({kMeem}));
    CHECK(s.size() == 1);
    CHECK(s[0].form == JoiningForm::Isolated);
    CHECK(s[0].codepoint == 0xFEE1);
}

TEST(arabic_marks_join_through) {
    const auto s = shape_arabic(utf({kBeh, kFatha, kTeh}));
    CHECK(s.size() == 3);
    CHECK_MSG(s[0].form == JoiningForm::Initial, "the mark broke the join");
    CHECK_MSG(s[2].form == JoiningForm::Final,
              "the letter after the mark did not join back");
    CHECK(s[1].codepoint == kFatha);
}

TEST(arabic_shaping_leaves_non_arabic_alone) {
    const auto s = shape_arabic("AB" + utf({kBeh}) + "12");
    CHECK(s.size() == 5);
    CHECK(s[0].codepoint == 'A');
    CHECK(s[1].codepoint == 'B');
    CHECK(s[3].codepoint == '1');
    CHECK(s[2].form == JoiningForm::Isolated);
}

TEST(arabic_shaping_of_an_empty_string_is_empty) {
    CHECK(shape_arabic("").empty());
}

TEST(arabic_word_shapes_a_real_word) {
    const auto s = shape_arabic(utf({kLam, kMeem, kDal}));
    CHECK(s.size() == 3);
    CHECK(s[0].form == JoiningForm::Initial);
    CHECK(s[1].form == JoiningForm::Medial);
    CHECK_MSG(s[2].form == JoiningForm::Final,
              "dal is right-joining and should take its final form");
    CHECK(s[2].codepoint == 0xFEA9 + 1);
}

namespace {

bool load_a_font(Font& f) {
    FontRegistry r;
    r.scan_system();
    for (const char* preferred : {"Helvetica", "Arial", "HelveticaNeue",
                                  "DejaVuSans", "Times New Roman", "Verdana",
                                  "SFNS", "Geneva"}) {
        if (const FontEntry* e = r.find(preferred)) {
            std::string err;
            if (f.load_file(e->path, 32, 4, &err) && f.covers_ascii()) return true;
        }
    }
    for (const auto& e : r.all()) {
        if (e.kind != FontKind::TrueType && e.kind != FontKind::OpenType) continue;
        std::string err;
        if (f.load_file(e.path, 32, 4, &err) && f.covers_ascii()) return true;
    }
    return false;
}

}

TEST(world_text_lies_on_its_plane) {
    Font f;
    if (!load_a_font(f)) { CHECK_MSG(false, "no loadable font"); return; }

    const TextFrame frame = TextFrame::on_plane(1, 2, 3, 0.3, -0.5, 0.8);
    WorldTextStyle style;
    style.height = 0.01;
    const f64 eye[3] = {100, -200, 300};

    const WorldText w = place_in_world(f, "PART 1", frame, style, eye);
    CHECK_MSG(!w.glyphs.empty(), "nothing was placed");
    CHECK(w.visible);

    for (const WorldGlyph& g : w.glyphs)
        for (int i = 0; i < 4; ++i) {
            const f64 d = (g.x[i] - frame.ox) * frame.nx +
                          (g.y[i] - frame.oy) * frame.ny +
                          (g.z[i] - frame.oz) * frame.nz;
            CHECK_MSG(std::fabs(d) < 1e-9, "a glyph corner is %g off the plane",
                      d);
        }
}

TEST(world_text_height_is_in_world_units) {
    Font f;
    if (!load_a_font(f)) { CHECK_MSG(false, "no loadable font"); return; }

    const TextFrame frame = TextFrame::on_plane(0, 0, 0, 0, 0, 1);
    const f64 eye[3] = {0, 0, 10};

    WorldTextStyle small, large;
    small.height = 0.005;
    large.height = 0.010;
    const WorldText a = place_in_world(f, "ABC", frame, small, eye);
    const WorldText b = place_in_world(f, "ABC", frame, large, eye);
    CHECK(a.width > 0.0 && b.width > 0.0);
    CHECK_MSG(std::fabs(b.width / a.width - 2.0) < 1e-9,
              "doubling the height gave %g times the width", b.width / a.width);
}

TEST(world_text_seen_from_behind_is_flipped_not_mirrored) {
    Font f;
    if (!load_a_font(f)) { CHECK_MSG(false, "no loadable font"); return; }

    const TextFrame frame = TextFrame::on_plane(0, 0, 0, 0, 0, 1);
    WorldTextStyle style;
    style.height = 0.01;
    style.flip_when_behind = true;

    const f64 front[3] = {0, 0, 10};
    const f64 behind[3] = {0, 0, -10};
    const WorldText a = place_in_world(f, "ABC", frame, style, front);
    const WorldText b = place_in_world(f, "ABC", frame, style, behind);

    CHECK(!a.flipped);
    CHECK_MSG(b.flipped, "text seen from behind was left mirrored");
    CHECK(b.visible);
    for (const WorldGlyph& g : b.glyphs)
        for (int i = 0; i < 4; ++i)
            CHECK_MSG(std::fabs(g.z[i]) < 1e-9,
                      "flipping moved the text off its plane");
    CHECK_MSG(b.glyphs.front().x[0] > b.glyphs.back().x[0],
              "the flip did not reverse the baseline");
}

TEST(world_text_can_be_hidden_from_behind_instead) {
    Font f;
    if (!load_a_font(f)) { CHECK_MSG(false, "no loadable font"); return; }

    const TextFrame frame = TextFrame::on_plane(0, 0, 0, 0, 0, 1);
    WorldTextStyle style;
    style.height = 0.01;
    style.hide_when_behind = true;

    const f64 behind[3] = {0, 0, -10};
    const WorldText w = place_in_world(f, "ABC", frame, style, behind);
    CHECK_MSG(!w.visible, "hidden text was placed anyway");
    CHECK(w.glyphs.empty());
}

TEST(world_text_billboards_squarely_toward_the_eye) {
    Font f;
    if (!load_a_font(f)) { CHECK_MSG(false, "no loadable font"); return; }

    const TextFrame frame = TextFrame::on_plane(0, 0, 0, 0, 0, 1);
    WorldTextStyle style;
    style.height = 0.01;
    style.facing = FacingMode::Billboard;

    for (auto eye : {std::array<f64, 3>{{10, 0, 0}}, {{0, 10, 0}}, {{5, 5, 5}}}) {
        const f64 e[3] = {eye[0], eye[1], eye[2]};
        const WorldText w = place_in_world(f, "A", frame, style, e);
        CHECK(!w.glyphs.empty());

        const WorldGlyph& g = w.glyphs.front();
        const f64 ax = g.x[1] - g.x[0], ay = g.y[1] - g.y[0], az = g.z[1] - g.z[0];
        const f64 bx = g.x[3] - g.x[0], by = g.y[3] - g.y[0], bz = g.z[3] - g.z[0];
        f64 n[3] = {ay * bz - az * by, az * bx - ax * bz, ax * by - ay * bx};
        const f64 len = std::sqrt(dot3(n, n));
        CHECK(len > 0.0);
        for (int i = 0; i < 3; ++i) n[i] /= len;

        f64 to_eye[3] = {e[0], e[1], e[2]};
        const f64 d = std::sqrt(dot3(to_eye, to_eye));
        for (int i = 0; i < 3; ++i) to_eye[i] /= d;
        CHECK_MSG(std::fabs(dot3(n, to_eye) - 1.0) < 1e-6,
                  "a billboard faces %g away from the eye", dot3(n, to_eye));
    }
}

TEST(world_text_billboard_keeps_its_world_position) {
    Font f;
    if (!load_a_font(f)) { CHECK_MSG(false, "no loadable font"); return; }

    const TextFrame frame = TextFrame::on_plane(7, -3, 2, 0, 0, 1);
    WorldTextStyle style;
    style.height = 0.01;
    style.facing = FacingMode::Billboard;

    const f64 a[3] = {100, 0, 0};
    const f64 b[3] = {0, 100, 0};
    const WorldText wa = place_in_world(f, "A", frame, style, a);
    const WorldText wb = place_in_world(f, "A", frame, style, b);
    CHECK(!wa.glyphs.empty() && !wb.glyphs.empty());

    auto near_origin = [&](const WorldText& w) {
        f64 best = 1e30;
        for (const WorldGlyph& g : w.glyphs)
            for (int i = 0; i < 4; ++i)
                best = std::min(best, std::hypot(std::hypot(g.x[i] - 7.0,
                                                            g.y[i] + 3.0),
                                                 g.z[i] - 2.0));
        return best;
    };
    CHECK_MSG(near_origin(wa) < 0.05, "the label left its anchor");
    CHECK_MSG(near_origin(wb) < 0.05, "the label left its anchor");
}

TEST(world_text_billboard_does_not_roll_past_the_pole) {
    Font f;
    if (!load_a_font(f)) { CHECK_MSG(false, "no loadable font"); return; }

    TextFrame frame = TextFrame::on_plane(0, 0, 0, 0, 0, 1);
    WorldTextStyle style;
    style.height = 0.01;
    style.facing = FacingMode::Billboard;

    const f64 straight_up[3] = {frame.vx * 10.0, frame.vy * 10.0, frame.vz * 10.0};
    const WorldText w = place_in_world(f, "A", frame, style, straight_up);
    CHECK(!w.glyphs.empty());
    for (const WorldGlyph& g : w.glyphs)
        for (int i = 0; i < 4; ++i)
            CHECK_MSG(std::isfinite(g.x[i]) && std::isfinite(g.y[i]) &&
                          std::isfinite(g.z[i]),
                      "a degenerate billboard produced a NaN corner");
}

TEST(world_text_lays_out_arabic_through_the_bidi_path) {
    Font f;
    if (!load_a_font(f)) { CHECK_MSG(false, "no loadable font"); return; }

    const TextFrame frame = TextFrame::on_plane(0, 0, 0, 0, 0, 1);
    WorldTextStyle style;
    style.height = 0.01;
    const f64 eye[3] = {0, 0, 10};

    const WorldText w =
        place_in_world(f, utf({kBeh, kTeh}) + " ISO", frame, style, eye);
    CHECK(w.visible);
    for (const WorldGlyph& g : w.glyphs)
        for (int i = 0; i < 4; ++i)
            CHECK(std::isfinite(g.x[i]) && std::isfinite(g.y[i]));
}

TEST(bidi_layout_matches_plain_layout_for_pure_latin) {
    Font f;
    if (!load_a_font(f)) { CHECK_MSG(false, "no loadable font"); return; }
    TextStyle s;
    const Layout a = layout_text(f, "SECTION A-A", s);
    const Layout b = layout_bidi(f, "SECTION A-A", s);
    CHECK(a.glyphs.size() == b.glyphs.size());
    CHECK(std::fabs(a.width - b.width) < 1e-9);
    for (std::size_t i = 0; i < a.glyphs.size(); ++i)
        CHECK(std::fabs(a.glyphs[i].x - b.glyphs[i].x) < 1e-9);
}
