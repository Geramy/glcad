#include "test.hpp"

#include <cmath>
#include <string>

#include "geometry/table.hpp"

using namespace gdim;

namespace {
Volume vol() {
    Volume v;
    v.extent = 500.0;
    v.resolution_bits = 32;
    return v;
}

Table schedule() {
    Table t(5, 4);
    t.set_title_rows(1);
    t.set_header_rows(1);
    t.merge(0, 0, 1, 4);
    t.set_text(0, 0, "DOOR SCHEDULE");
    t.set_text(1, 0, "MARK");
    t.set_text(1, 1, "WIDTH");
    t.set_text(1, 2, "HEIGHT");
    t.set_text(1, 3, "TYPE");
    t.set_text(2, 0, "D-01");
    t.set_text(2, 1, "900");
    t.set_text(2, 2, "2100");
    t.set_text(2, 3, "SOLID CORE");
    t.set_text(3, 0, "D-02");
    t.set_text(3, 1, "800");
    t.set_text(3, 2, "2100");
    t.set_text(3, 3, "HOLLOW");
    t.set_text(4, 0, "D-03");
    t.set_text(4, 1, "1800");
    t.set_text(4, 2, "2100");
    t.set_text(4, 3, "DOUBLE");
    return t;
}
}

TEST(table_resize_keeps_what_overlaps) {
    Table t(2, 2);
    t.set_text(0, 0, "A");
    t.set_text(1, 1, "D");

    t.resize(4, 4);
    CHECK(t.rows() == 4 && t.columns() == 4);
    CHECK(t.text(0, 0) == "A");
    CHECK(t.text(1, 1) == "D");
    CHECK(t.text(3, 3).empty());

    t.resize(1, 1);
    CHECK(t.text(0, 0) == "A");
}

TEST(table_out_of_range_access_is_null_not_a_crash) {
    Table t(2, 2);
    CHECK(t.at(-1, 0) == nullptr);
    CHECK(t.at(0, 5) == nullptr);
    CHECK(t.at(2, 0) == nullptr);
    CHECK(!t.set_text(9, 9, "x"));
    CHECK(t.text(9, 9).empty());
}

TEST(table_merge_covers_and_unmerge_restores) {
    Table t(3, 3);
    t.set_text(0, 0, "TITLE");
    CHECK(t.merge(0, 0, 1, 3));
    CHECK(t.is_merged(0, 0));
    CHECK(t.at(0, 1)->covered);
    CHECK(t.at(0, 2)->covered);
    CHECK(!t.at(1, 0)->covered);

    CHECK(t.unmerge(0, 0));
    CHECK(!t.is_merged(0, 0));
    CHECK_MSG(!t.at(0, 1)->covered, "unmerge lost the cells it had covered");
}

TEST(table_merge_refuses_to_overlap_an_existing_merge) {
    Table t(3, 3);
    CHECK(t.merge(0, 0, 1, 2));
    CHECK_MSG(!t.merge(0, 1, 2, 2), "an overlapping merge was allowed");
    CHECK(t.merge(1, 0, 2, 1));
}

TEST(table_merge_rejects_a_span_off_the_edge) {
    Table t(2, 2);
    CHECK(!t.merge(0, 0, 3, 1));
    CHECK(!t.merge(0, 0, 1, 5));
    CHECK(!t.merge(0, 0, 0, 1));
    CHECK(!t.merge(5, 5, 1, 1));
}

TEST(table_insert_and_remove_rows_and_columns) {
    Table t = schedule();
    const int rows = t.rows();
    CHECK(t.insert_row(2));
    CHECK(t.rows() == rows + 1);
    CHECK_MSG(t.text(3, 0) == "D-01", "the rows below did not shift down");
    CHECK(t.text(2, 0).empty());

    CHECK(t.remove_row(2));
    CHECK(t.rows() == rows);
    CHECK(t.text(2, 0) == "D-01");

    const int cols = t.columns();
    CHECK(t.insert_column(1));
    CHECK(t.columns() == cols + 1);
    CHECK(t.text(2, 2) == "900");
    CHECK(t.remove_column(1));
    CHECK(t.text(2, 1) == "900");

    CHECK(!t.insert_row(-1));
    CHECK(!t.remove_row(99));
}

TEST(table_bands_follow_the_title_and_header_counts) {
    Table t = schedule();
    CHECK(t.band_of(0) == Band::Title);
    CHECK(t.band_of(1) == Band::Header);
    CHECK(t.band_of(2) == Band::Data);
    CHECK(t.band_of(4) == Band::Data);

    t.set_title_rows(0);
    CHECK(t.band_of(0) == Band::Header);
    t.set_header_rows(0);
    CHECK(t.band_of(0) == Band::Data);
}

TEST(table_sum_formula_totals_a_column) {
    Table t(5, 2);
    t.set_text(0, 0, "AREA");
    t.set_text(1, 0, "12.5");
    t.set_text(2, 0, "30");
    t.set_text(3, 0, "7.5");
    t.at(4, 0)->kind = CellKind::Formula;
    t.set_text(4, 0, "=SUM(A2:A4)");

    CHECK(t.evaluate() == 1);
    CHECK_MSG(t.text(4, 0) == "50", "totalled to '%s'", t.text(4, 0).c_str());
}

TEST(table_other_functions) {
    Table t(5, 1);
    t.set_text(0, 0, "10");
    t.set_text(1, 0, "20");
    t.set_text(2, 0, "60");
    t.at(3, 0)->kind = CellKind::Formula;
    t.set_text(3, 0, "=AVERAGE(A1:A3)");
    t.at(4, 0)->kind = CellKind::Formula;
    t.set_text(4, 0, "=MAX(A1:A3)");

    CHECK(t.evaluate() == 2);
    CHECK(t.text(3, 0) == "30");
    CHECK(t.text(4, 0) == "60");
}

TEST(table_count_ignores_text_cells) {
    Table t(4, 1);
    t.set_text(0, 0, "5");
    t.set_text(1, 0, "TBC");
    t.set_text(2, 0, "7");
    t.at(3, 0)->kind = CellKind::Formula;
    t.set_text(3, 0, "=COUNT(A1:A3)");
    CHECK(t.evaluate() == 1);
    CHECK_MSG(t.text(3, 0) == "2", "counted '%s'", t.text(3, 0).c_str());
}

TEST(table_formula_survives_being_evaluated) {
    Table t(3, 1);
    t.set_text(0, 0, "4");
    t.set_text(1, 0, "6");
    t.at(2, 0)->kind = CellKind::Formula;
    t.set_text(2, 0, "=SUM(A1:A2)");
    t.evaluate();
    CHECK(t.text(2, 0) == "10");
    CHECK_MSG(t.at(2, 0)->block == "=SUM(A1:A2)", "the formula was lost");
}

TEST(table_malformed_formulas_are_left_alone) {
    Table t(4, 1);
    t.at(0, 0)->kind = CellKind::Formula;
    t.set_text(0, 0, "=SUM(");
    t.at(1, 0)->kind = CellKind::Formula;
    t.set_text(1, 0, "=NONSENSE(A1:A2)");
    t.at(2, 0)->kind = CellKind::Formula;
    t.set_text(2, 0, "=SUM(ZZ:99)");
    CHECK(t.evaluate() == 0);
    CHECK(t.text(0, 0) == "=SUM(");
    CHECK(t.text(1, 0) == "=NONSENSE(A1:A2)");
}

TEST(table_builds_lines_and_positioned_text) {
    const Volume v = vol();
    Table t = schedule();
    const TableStyle s = TableStyle::standard();

    const TableGeometry g = build(t, s, v);
    CHECK_MSG(g.ok, "%s", g.reason);
    CHECK(!g.lines.empty());
    CHECK(g.width > 0.0 && g.height > 0.0);

    CHECK_MSG(g.cells.size() == 17, "%zu text runs", g.cells.size());
}

TEST(table_merged_cell_spans_its_neighbours_width) {
    const Volume v = vol();
    Table t = schedule();
    const TableGeometry g = build(t, TableStyle::standard(), v);
    CHECK(g.ok);

    f64 title_w = 0.0, mark_w = 0.0;
    for (const auto& c : g.cells) {
        if (c.text == "DOOR SCHEDULE") title_w = c.width;
        if (c.text == "MARK") mark_w = c.width;
    }
    CHECK(title_w > 0.0 && mark_w > 0.0);
    CHECK_MSG(title_w > mark_w * 2.0,
              "the merged title did not span (%.4f vs %.4f)", title_w, mark_w);
    CHECK(std::fabs(title_w - g.width) < 1e-9);
}

TEST(table_auto_fits_a_column_to_its_widest_entry) {
    const Volume v = vol();
    Table narrow(2, 1), wide(2, 1);
    narrow.set_text(0, 0, "A");
    wide.set_text(0, 0, "A MUCH LONGER HEADING INDEED");

    const TableStyle s = TableStyle::standard();
    CHECK_MSG(build(wide, s, v).width > build(narrow, s, v).width,
              "auto-fit ignored the content");
}

TEST(table_explicit_width_overrides_auto_fit) {
    const Volume v = vol();
    Table t(2, 1);
    t.set_text(0, 0, "A MUCH LONGER HEADING INDEED");
    t.set_column_width(0, 0.05);
    const TableGeometry g = build(t, TableStyle::standard(), v);
    CHECK(std::fabs(g.width - 0.05) < 1e-9);
}

TEST(table_hangs_down_from_its_position) {
    const Volume v = vol();
    Table t = schedule();
    t.position = gcm::to_lattice(v, 0.0, 0.0);
    const TableGeometry g = build(t, TableStyle::standard(), v);
    CHECK(g.ok);
    for (const auto& c : g.cells)
        CHECK_MSG(c.y <= 1e-9, "a cell sat above the insertion point (%.6f)", c.y);
}

TEST(table_alignment_moves_the_text_within_the_cell) {
    const Volume v = vol();
    Table t(1, 1);
    t.set_title_rows(0);
    t.set_header_rows(0);
    t.set_text(0, 0, "X");
    t.set_column_width(0, 0.1);

    TableStyle left = TableStyle::standard();
    left.data.halign = HAlign::Left;
    TableStyle right = TableStyle::standard();
    right.data.halign = HAlign::Right;

    const f64 lx = build(t, left, v).cells[0].x;
    const f64 rx = build(t, right, v).cells[0].x;
    CHECK_MSG(rx > lx, "right alignment did not move the text (%.6f vs %.6f)",
              lx, rx);
}

TEST(table_style_scale_grows_everything) {
    const Volume v = vol();
    Table t = schedule();
    TableStyle a = TableStyle::standard();
    TableStyle b = TableStyle::standard();
    b.scale = 4.0;
    CHECK(build(t, b, v).height > build(t, a, v).height * 3.5);
}

TEST(table_empty_table_has_no_geometry) {
    const Volume v = vol();
    Table t;
    const TableGeometry g = build(t, TableStyle::standard(), v);
    CHECK(!g.ok);
    CHECK(std::string(g.reason).size() > 0);
}

TEST(table_style_table) {
    TableStyleTable t;
    CHECK(t.find("Standard") != nullptr);
    TableStyle s = TableStyle::standard();
    s.name = "Schedule";
    s.scale = 2.0;
    t.add(s);
    CHECK(t.find("Schedule")->scale == 2.0);
    t.set_current("Schedule");
    CHECK(t.current() == "Schedule");
}

TEST(table_csv_round_trip) {
    Table t;
    const std::size_t n = load_csv(t, "MARK,WIDTH\nD-01,900\nD-02,800\n");
    CHECK(n == 6);
    CHECK(t.rows() == 3 && t.columns() == 2);
    CHECK(t.text(0, 0) == "MARK");
    CHECK(t.text(2, 1) == "800");

    CHECK(save_csv(t) == "MARK,WIDTH\nD-01,900\nD-02,800\n");
}

TEST(table_csv_handles_quoted_fields) {
    Table t;
    load_csv(t, "TYPE,NOTE\n\"DOUBLE, EQUAL\",\"24\"\" LEAF\"\n");
    CHECK(t.rows() == 2 && t.columns() == 2);
    CHECK_MSG(t.text(1, 0) == "DOUBLE, EQUAL", "got '%s'", t.text(1, 0).c_str());
    CHECK_MSG(t.text(1, 1) == "24\" LEAF", "got '%s'", t.text(1, 1).c_str());

    const std::string out = save_csv(t);
    CHECK(out.find("\"DOUBLE, EQUAL\"") != std::string::npos);
    CHECK(out.find("\"24\"\" LEAF\"") != std::string::npos);
}

TEST(table_csv_pads_ragged_rows) {
    Table t;
    load_csv(t, "A,B,C\n1\n2,3\n");
    CHECK(t.columns() == 3);
    CHECK(t.text(1, 0) == "1");
    CHECK(t.text(1, 2).empty());
}

TEST(table_csv_of_nothing_is_nothing) {
    Table t;
    CHECK(load_csv(t, "") == 0);
    CHECK(t.rows() == 0);
}
