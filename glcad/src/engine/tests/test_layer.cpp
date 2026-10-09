#include "test.hpp"

#include <cmath>
#include <cstdlib>

#include "engine/linetype.hpp"

using namespace gdoc;

TEST(layer_table_always_has_layer_zero) {
    LayerTable t;
    CHECK(t.size() == 1);
    CHECK(t.find("0") != nullptr);
    CHECK(t.current() == "0");
    CHECK_MSG(!t.remove("0"), "layer 0 must be permanent");
}

TEST(layer_visibility_and_editability) {
    LayerTable t;
    Layer w;
    w.name = "A-WALL";
    t.add(w);

    Layer* l = t.find("A-WALL");
    CHECK(l->visible() && l->editable());

    l->locked = true;
    CHECK_MSG(l->visible(), "a locked layer is still visible and snappable");
    CHECK(!l->editable());

    l->locked = false;
    l->frozen = true;
    CHECK(!l->visible());

    l->frozen = false;
    l->on = false;
    CHECK(!l->visible());
}

TEST(layer_per_viewport_freeze) {
    Layer l;
    l.vp_frozen = {3, 7};
    CHECK(l.frozen_in(3));
    CHECK(l.frozen_in(7));
    CHECK(!l.frozen_in(4));
}

TEST(layer_name_filters_use_globs) {
    CHECK(glob_match("A-WALL-*", "A-WALL-FULL"));
    CHECK(glob_match("A-WALL-*", "A-WALL-"));
    CHECK(!glob_match("A-WALL-*", "A-DOOR-FULL"));
    CHECK(glob_match("*WALL*", "X-WALL-Y"));
    CHECK(glob_match("A-????", "A-WALL"));
    CHECK(!glob_match("A-????", "A-DOORS"));
    CHECK(glob_match("a-wall", "A-WALL"));      // case-insensitive

    LayerTable t;
    for (const char* n : {"A-WALL-FULL", "A-WALL-PART", "A-DOOR", "E-LITE"}) {
        Layer l; l.name = n; t.add(l);
    }
    CHECK(t.filter_by_name("A-WALL-*").size() == 2);
    CHECK(t.filter_by_name("A-*").size() == 3);
    CHECK(t.filter_by_name("*").size() == 5);   // includes layer 0
}

TEST(layer_isolate_and_restore) {
    LayerTable t;
    for (const char* n : {"A", "B", "C"}) { Layer l; l.name = n; t.add(l); }

    t.isolate("B");
    CHECK(!t.find("A")->on);
    CHECK(t.find("B")->on);
    CHECK(!t.find("0")->on);

    t.all_on();
    CHECK(t.find("A")->on);
}

TEST(layer_states_save_and_restore) {
    LayerTable t;
    Layer a; a.name = "A"; t.add(a);
    Layer b; b.name = "B"; t.add(b);

    t.save_state("all-visible");
    t.find("A")->on = false;
    t.find("B")->frozen = true;
    CHECK(!t.find("A")->visible());

    CHECK(t.restore_state("all-visible"));
    CHECK(t.find("A")->visible());
    CHECK(t.find("B")->visible());
    CHECK(!t.restore_state("nonexistent"));
    CHECK(t.state_names().size() == 1);
}

TEST(layer_rename_rules) {
    LayerTable t;
    Layer a; a.name = "OLD"; t.add(a);
    CHECK(t.rename("OLD", "NEW"));
    CHECK(t.find("NEW") != nullptr);
    CHECK(t.find("OLD") == nullptr);
    CHECK_MSG(!t.rename("0", "ZERO"), "layer 0 cannot be renamed");

    Layer b; b.name = "TAKEN"; t.add(b);
    CHECK_MSG(!t.rename("NEW", "TAKEN"), "renaming onto an existing name must fail");
}

TEST(layer_colour_model) {
    CHECK(Color::by_layer().kind == Color::Kind::ByLayer);
    CHECK(!Color::by_layer().resolved());
    CHECK(Color::aci(7).resolved());
    CHECK(Color::rgb(255, 128, 0).resolved());
    CHECK(Color::rgb(255, 128, 0).r == 255);
}

TEST(lineweight_values_are_paper_millimetres) {
    CHECK(lineweight_mm(Lineweight::W050) == 0.5);
    CHECK(lineweight_mm(Lineweight::W013) == 0.13);
    CHECK(lineweight_mm(Lineweight::W200) == 2.0);
}

TEST(linetype_standard_set_present) {
    LinetypeTable t;
    for (const char* n : {"Continuous", "Center", "Dashed", "Hidden", "Phantom"})
        CHECK_MSG(t.find(n) != nullptr, "%s missing", n);
    CHECK(t.find("CONTINUOUS") != nullptr);   // case-insensitive
    CHECK(t.find("Continuous")->solid());
    CHECK(!t.find("Dashed")->solid());
}

TEST(linetype_dash_pattern_alternates) {
    LinetypeTable t;
    const Linetype* d = t.find("Dashed");     // 0.5 on, 0.25 off
    CHECK(d->cycle_length() == 0.75);
    CHECK(d->draws_at(0.0));
    CHECK(d->draws_at(0.4));
    CHECK(!d->draws_at(0.6));
    CHECK(d->draws_at(0.8));                  // next cycle

    CHECK(t.find("Continuous")->draws_at(12345.6));
}

TEST(linetype_lin_import) {
    LinetypeTable t;
    const char* lin =
        ";; a comment\n"
        "*MYDASH,My dash __ __ __\n"
        "A,.75,-.25\n"
        "*MYDOT,My dots . . . .\n"
        "A,0,-.2\n";
    CHECK(t.load_lin(lin) == 2);
    CHECK(t.find("MYDASH") != nullptr);
    CHECK(t.find("MYDASH")->pattern.size() == 2);
    CHECK(t.find("MYDOT")->pattern[0].length == 0.0);
}

TEST(multiline_ships_architectural_templates) {
    MultilineTable t;
    for (const char* n : {"Standard", "Wall-100", "Wall-150", "Wall-200",
                          "Wall-Cavity-300", "Pony-Wall-900", "Window", "Door",
                          "Curtain-Wall", "Duct-Round"})
        CHECK_MSG(t.find(n) != nullptr, "template %s missing", n);
}

TEST(multiline_wall_widths_are_metric_and_correct) {
    MultilineTable t;
    CHECK(std::abs(t.find("Wall-100")->width() - 0.100) < 1e-9);
    CHECK(std::abs(t.find("Wall-150")->width() - 0.150) < 1e-9);
    CHECK(std::abs(t.find("Wall-200")->width() - 0.200) < 1e-9);
    CHECK(std::abs(t.find("Wall-Cavity-300")->width() - 0.300) < 1e-9);
}

TEST(multiline_cavity_wall_has_four_lines) {
    MultilineTable t;
    const MultilineStyle* c = t.find("Wall-Cavity-300");
    CHECK(c->elements.size() == 4);
    CHECK(c->elements[0].offset > c->elements[1].offset);
    CHECK(c->elements[2].offset > c->elements[3].offset);
    CHECK(c->filled);
}

TEST(multiline_openings_are_not_filled) {
    MultilineTable t;
    CHECK(!t.find("Window")->filled);
    CHECK(!t.find("Door")->filled);
    CHECK(t.find("Wall-200")->filled);
}

TEST(multiline_justification_shifts_the_offsets) {
    MultilineTable t;
    const MultilineStyle* w = t.find("Wall-200");   // +-0.1

    const auto zero = resolve_offsets(*w, Justify::Zero);
    CHECK(std::abs(zero[0] - 0.1) < 1e-9);
    CHECK(std::abs(zero[1] + 0.1) < 1e-9);

    const auto top = resolve_offsets(*w, Justify::Top);
    CHECK(std::abs(top[0]) < 1e-9);
    CHECK(std::abs(top[1] + 0.2) < 1e-9);

    const auto bot = resolve_offsets(*w, Justify::Bottom);
    CHECK(std::abs(bot[0] - 0.2) < 1e-9);
    CHECK(std::abs(bot[1]) < 1e-9);
}

TEST(multiline_custom_template) {
    MultilineTable t;
    MultilineStyle s;
    s.name = "Wall-Timber-90";
    s.description = "90 mm timber stud";
    s.elements = {{0.045}, {-0.045}};
    s.filled = true;
    t.add(s);

    CHECK(t.find("Wall-Timber-90") != nullptr);
    CHECK(std::abs(t.find("Wall-Timber-90")->width() - 0.090) < 1e-9);
}

TEST(aci_colours_resolve_to_the_bytes_every_dwg_states) {
    gcm::u8 r = 0, g = 0, b = 0;
    gdoc::aci_rgb(1, r, g, b);
    CHECK(r == 255 && g == 0 && b == 0);
    gdoc::aci_rgb(3, r, g, b);
    CHECK(r == 0 && g == 255 && b == 0);
    gdoc::aci_rgb(5, r, g, b);
    CHECK(r == 0 && g == 0 && b == 255);
    gdoc::aci_rgb(8, r, g, b);
    CHECK(r == 128 && g == 128 && b == 128);      // dark grey
    gdoc::aci_rgb(252, r, g, b);
    CHECK(r == 132 && g == 132 && b == 132);      // the grey ramp

    // Hue wheel: index 10 is pure red; ten indices is a 15-degree turn.
    gdoc::aci_rgb(10, r, g, b);
    CHECK_MSG(r == 255 && g == 0 && b == 0, "aci 10 = %u,%u,%u", r, g, b);
    gdoc::aci_rgb(50, r, g, b);
    CHECK_MSG(r == 255 && g == 255 && b == 0, "aci 50 = %u,%u,%u", r, g, b);
    for (gcm::i32 i = 10; i <= 249; ++i) {
        gdoc::aci_rgb(i, r, g, b);
        CHECK_MSG(r || g || b, "aci %d came back black", int(i));
    }
}
