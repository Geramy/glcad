#include "geometry/table.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace gdim {

namespace {

struct W { f64 x, y; };
W world(const Volume& v, Point p) {
    f64 x, y;
    gcm::to_world(v, p, x, y);
    return {x, y};
}
Point lat(const Volume& v, f64 x, f64 y) { return gcm::to_lattice(v, x, y); }

gsnap::Shape seg(const Volume& v, W a, W b) {
    gsnap::Shape s;
    s.kind = gsnap::ShapeKind::Segment;
    s.a = lat(v, a.x, a.y);
    s.b = lat(v, b.x, b.y);
    return s;
}

f64 text_width(const std::string& s, f64 height) {
    std::size_t n = 0;
    for (unsigned char c : s)
        if ((c & 0xC0) != 0x80) ++n;
    return static_cast<f64>(n) * height * 0.62;
}

bool parse_ref(std::string_view s, int& row, int& col) {
    std::size_t i = 0;
    col = 0;
    while (i < s.size() && s[i] >= 'A' && s[i] <= 'Z') {
        col = col * 26 + (s[i] - 'A' + 1);
        ++i;
    }
    if (i == 0 || i >= s.size()) return false;
    int n = 0;
    for (; i < s.size(); ++i) {
        if (s[i] < '0' || s[i] > '9') return false;
        n = n * 10 + (s[i] - '0');
    }
    if (n <= 0) return false;
    col -= 1;
    row = n - 1;
    return true;
}

}

TableStyle TableStyle::standard() {
    TableStyle s;
    s.title.halign = HAlign::Centre;
    s.title.text_height = 0.0035;
    s.header.halign = HAlign::Centre;
    s.header.text_height = 0.0025;
    s.data.halign = HAlign::Left;
    s.data.text_height = 0.0025;
    return s;
}

TableStyleTable::TableStyleTable() { styles_.push_back(TableStyle::standard()); }

const TableStyle* TableStyleTable::find(std::string_view n) const {
    for (const auto& s : styles_)
        if (s.name == n) return &s;
    return nullptr;
}

TableStyle& TableStyleTable::add(TableStyle s) {
    for (auto& e : styles_)
        if (e.name == s.name) { e = std::move(s); return e; }
    styles_.push_back(std::move(s));
    return styles_.back();
}

void TableStyleTable::set_current(std::string_view n) {
    if (find(n)) current_ = std::string(n);
}

void Table::resize(int rows, int columns) {
    rows = std::max(0, rows);
    columns = std::max(0, columns);

    std::vector<Cell> next(static_cast<std::size_t>(rows) *
                           static_cast<std::size_t>(columns));
    for (int r = 0; r < std::min(rows, rows_); ++r)
        for (int c = 0; c < std::min(columns, cols_); ++c)
            next[static_cast<std::size_t>(r) * columns + c] =
                cells_[static_cast<std::size_t>(r) * cols_ + c];

    cells_ = std::move(next);
    rows_ = rows;
    cols_ = columns;
    col_w_.resize(static_cast<std::size_t>(columns), 0.0);
    row_h_.resize(static_cast<std::size_t>(rows), 0.0);
}

Cell* Table::at(int r, int c) {
    if (r < 0 || c < 0 || r >= rows_ || c >= cols_) return nullptr;
    return &cells_[static_cast<std::size_t>(r) * cols_ + c];
}
const Cell* Table::at(int r, int c) const {
    if (r < 0 || c < 0 || r >= rows_ || c >= cols_) return nullptr;
    return &cells_[static_cast<std::size_t>(r) * cols_ + c];
}

bool Table::set_text(int r, int c, std::string text) {
    Cell* cell = at(r, c);
    if (!cell) return false;
    cell->text = std::move(text);
    return true;
}

std::string Table::text(int r, int c) const {
    const Cell* cell = at(r, c);
    return cell ? cell->text : std::string();
}

bool Table::merge(int r, int c, int row_span, int col_span) {
    if (row_span < 1 || col_span < 1) return false;
    if (r < 0 || c < 0 || r + row_span > rows_ || c + col_span > cols_) return false;

    for (int i = r; i < r + row_span; ++i)
        for (int j = c; j < c + col_span; ++j) {
            const Cell* cell = at(i, j);
            if (cell->covered) return false;
            if (i == r && j == c) continue;
            if (cell->row_span > 1 || cell->col_span > 1) return false;
        }

    Cell* head = at(r, c);
    head->row_span = row_span;
    head->col_span = col_span;
    for (int i = r; i < r + row_span; ++i)
        for (int j = c; j < c + col_span; ++j) {
            if (i == r && j == c) continue;
            at(i, j)->covered = true;
        }
    return true;
}

bool Table::unmerge(int r, int c) {
    Cell* head = at(r, c);
    if (!head || (head->row_span == 1 && head->col_span == 1)) return false;
    for (int i = r; i < r + head->row_span; ++i)
        for (int j = c; j < c + head->col_span; ++j)
            if (Cell* cell = at(i, j)) cell->covered = false;
    head->row_span = 1;
    head->col_span = 1;
    return true;
}

bool Table::is_merged(int r, int c) const {
    const Cell* cell = at(r, c);
    return cell && (cell->row_span > 1 || cell->col_span > 1);
}

bool Table::insert_row(int before) {
    if (before < 0 || before > rows_) return false;
    std::vector<Cell> next(static_cast<std::size_t>(rows_ + 1) * cols_);
    for (int r = 0; r < rows_; ++r) {
        const int dst = r < before ? r : r + 1;
        for (int c = 0; c < cols_; ++c)
            next[static_cast<std::size_t>(dst) * cols_ + c] =
                cells_[static_cast<std::size_t>(r) * cols_ + c];
    }
    cells_ = std::move(next);
    ++rows_;
    row_h_.insert(row_h_.begin() + std::min<std::size_t>(before, row_h_.size()), 0.0);
    return true;
}

bool Table::insert_column(int before) {
    if (before < 0 || before > cols_) return false;
    std::vector<Cell> next(static_cast<std::size_t>(rows_) * (cols_ + 1));
    for (int r = 0; r < rows_; ++r)
        for (int c = 0; c < cols_; ++c) {
            const int dst = c < before ? c : c + 1;
            next[static_cast<std::size_t>(r) * (cols_ + 1) + dst] =
                cells_[static_cast<std::size_t>(r) * cols_ + c];
        }
    cells_ = std::move(next);
    ++cols_;
    col_w_.insert(col_w_.begin() + std::min<std::size_t>(before, col_w_.size()), 0.0);
    return true;
}

bool Table::remove_row(int r) {
    if (r < 0 || r >= rows_) return false;
    std::vector<Cell> next(static_cast<std::size_t>(rows_ - 1) * cols_);
    for (int i = 0, dst = 0; i < rows_; ++i) {
        if (i == r) continue;
        for (int c = 0; c < cols_; ++c)
            next[static_cast<std::size_t>(dst) * cols_ + c] =
                cells_[static_cast<std::size_t>(i) * cols_ + c];
        ++dst;
    }
    cells_ = std::move(next);
    --rows_;
    if (static_cast<std::size_t>(r) < row_h_.size())
        row_h_.erase(row_h_.begin() + r);
    return true;
}

bool Table::remove_column(int c) {
    if (c < 0 || c >= cols_) return false;
    std::vector<Cell> next(static_cast<std::size_t>(rows_) * (cols_ - 1));
    for (int r = 0; r < rows_; ++r)
        for (int i = 0, dst = 0; i < cols_; ++i) {
            if (i == c) continue;
            next[static_cast<std::size_t>(r) * (cols_ - 1) + dst] =
                cells_[static_cast<std::size_t>(r) * cols_ + i];
            ++dst;
        }
    cells_ = std::move(next);
    --cols_;
    if (static_cast<std::size_t>(c) < col_w_.size())
        col_w_.erase(col_w_.begin() + c);
    return true;
}

void Table::set_column_width(int c, f64 w) {
    if (c >= 0 && static_cast<std::size_t>(c) < col_w_.size()) col_w_[c] = w;
}
void Table::set_row_height(int r, f64 h) {
    if (r >= 0 && static_cast<std::size_t>(r) < row_h_.size()) row_h_[r] = h;
}
f64 Table::column_width(int c) const {
    return (c >= 0 && static_cast<std::size_t>(c) < col_w_.size()) ? col_w_[c] : 0.0;
}
f64 Table::row_height(int r) const {
    return (r >= 0 && static_cast<std::size_t>(r) < row_h_.size()) ? row_h_[r] : 0.0;
}

Band Table::band_of(int row) const noexcept {
    if (row < title_rows_) return Band::Title;
    if (row < title_rows_ + header_rows_) return Band::Header;
    return Band::Data;
}

std::size_t Table::evaluate() {
    std::size_t resolved = 0;

    auto value_at = [&](int r, int c, f64& out) {
        const Cell* cell = at(r, c);
        if (!cell || cell->text.empty()) return false;
        char* end = nullptr;
        const double v = std::strtod(cell->text.c_str(), &end);
        if (end == cell->text.c_str()) return false;
        out = v;
        return true;
    };

    for (int r = 0; r < rows_; ++r)
        for (int c = 0; c < cols_; ++c) {
            Cell* cell = at(r, c);
            if (cell->kind != CellKind::Formula) continue;
            const std::string& f = cell->text;
            if (f.size() < 2 || f[0] != '=') continue;

            const auto open = f.find('(');
            const auto close = f.rfind(')');
            const auto colon = f.find(':');
            if (open == std::string::npos || close == std::string::npos ||
                colon == std::string::npos || colon < open || colon > close)
                continue;

            const std::string fn = f.substr(1, open - 1);
            int r0, c0, r1, c1;
            if (!parse_ref(std::string_view(f).substr(open + 1, colon - open - 1),
                           r0, c0) ||
                !parse_ref(std::string_view(f).substr(colon + 1, close - colon - 1),
                           r1, c1))
                continue;

            if (r0 > r1) std::swap(r0, r1);
            if (c0 > c1) std::swap(c0, c1);

            f64 sum = 0.0, lo = 0.0, hi = 0.0;
            int n = 0;
            for (int i = r0; i <= r1; ++i)
                for (int j = c0; j <= c1; ++j) {
                    f64 v = 0.0;
                    if (!value_at(i, j, v)) continue;
                    if (n == 0) { lo = hi = v; }
                    lo = std::min(lo, v);
                    hi = std::max(hi, v);
                    sum += v;
                    ++n;
                }

            f64 result = 0.0;
            if (fn == "SUM") result = sum;
            else if (fn == "AVERAGE") result = n ? sum / n : 0.0;
            else if (fn == "COUNT") result = n;
            else if (fn == "MIN") result = lo;
            else if (fn == "MAX") result = hi;
            else continue;

            char buf[64];
            std::snprintf(buf, sizeof(buf), "%.10g", result);
            if (cell->block.empty()) cell->block = f;
            cell->text = buf;
            ++resolved;
        }

    return resolved;
}

TableGeometry build(const Table& t, const TableStyle& s, const Volume& v) {
    TableGeometry g;
    if (t.rows() <= 0 || t.columns() <= 0) {
        g.reason = "an empty table has no geometry";
        return g;
    }

    auto style_for = [&](int r, int c) -> const CellStyle& {
        const Cell* cell = t.at(r, c);
        if (cell && cell->has_style) return cell->style;
        switch (t.band_of(r)) {
            case Band::Title:  return s.title;
            case Band::Header: return s.header;
            default:           return s.data;
        }
    };

    std::vector<f64> width(static_cast<std::size_t>(t.columns()), 0.0);
    for (int c = 0; c < t.columns(); ++c) {
        f64 w = t.column_width(c);
        if (w <= 0.0) {
            for (int r = 0; r < t.rows(); ++r) {
                const Cell* cell = t.at(r, c);
                if (!cell || cell->covered || cell->col_span > 1) continue;
                const CellStyle& cs = style_for(r, c);
                w = std::max(w, text_width(cell->text, cs.text_height * s.scale) +
                                    cs.margin_h * 2.0 * s.scale);
            }
            if (w <= 0.0) w = 0.02;      // a visible minimum, 20 mm
        }
        width[static_cast<std::size_t>(c)] = w;
        g.width += w;
    }

    std::vector<f64> height(static_cast<std::size_t>(t.rows()), 0.0);
    for (int r = 0; r < t.rows(); ++r) {
        f64 h = t.row_height(r);
        if (h <= 0.0) {
            const CellStyle& cs = style_for(r, 0);
            h = cs.text_height * s.scale + cs.margin_v * 2.0 * s.scale;
        }
        height[static_cast<std::size_t>(r)] = h;
        g.height += h;
    }

    std::vector<f64> xs(static_cast<std::size_t>(t.columns()) + 1, 0.0);
    for (int c = 0; c < t.columns(); ++c)
        xs[c + 1] = xs[c] + width[static_cast<std::size_t>(c)];

    std::vector<f64> ys(static_cast<std::size_t>(t.rows()) + 1, 0.0);
    for (int r = 0; r < t.rows(); ++r)
        ys[r + 1] = ys[r] + height[static_cast<std::size_t>(r)];

    const W at = world(v, t.position);
    auto X = [&](int c) { return at.x + xs[static_cast<std::size_t>(c)]; };
    auto Y = [&](int r) { return at.y - ys[static_cast<std::size_t>(r)]; };

    for (int r = 0; r < t.rows(); ++r)
        for (int c = 0; c < t.columns(); ++c) {
            const Cell* cell = t.at(r, c);
            if (!cell || cell->covered) continue;

            const int r1 = std::min(r + cell->row_span, t.rows());
            const int c1 = std::min(c + cell->col_span, t.columns());
            const f64 x0 = X(c), x1 = X(c1);
            const f64 y0 = Y(r1), y1 = Y(r);
            const CellStyle& cs = style_for(r, c);

            if (cs.top.visible)    g.lines.push_back(seg(v, {x0, y1}, {x1, y1}));
            if (cs.bottom.visible) g.lines.push_back(seg(v, {x0, y0}, {x1, y0}));
            if (cs.left.visible)   g.lines.push_back(seg(v, {x0, y0}, {x0, y1}));
            if (cs.right.visible)  g.lines.push_back(seg(v, {x1, y0}, {x1, y1}));

            if (cell->text.empty()) continue;

            const f64 th = cs.text_height * s.scale;
            const f64 mh = cs.margin_h * s.scale;
            const f64 mv = cs.margin_v * s.scale;
            const f64 tw = text_width(cell->text, th);

            TableCellGeometry tc;
            tc.text = cell->text;
            tc.width = x1 - x0;
            tc.height = y1 - y0;
            tc.text_height = th;
            tc.align = cs.halign;

            switch (cs.halign) {
                case HAlign::Left:   tc.x = x0 + mh; break;
                case HAlign::Centre: tc.x = x0 + (tc.width - tw) * 0.5; break;
                case HAlign::Right:  tc.x = x1 - mh - tw; break;
            }
            switch (cs.valign) {
                case VAlign::Top:    tc.y = y1 - mv - th; break;
                case VAlign::Middle: tc.y = y0 + (tc.height - th) * 0.5; break;
                case VAlign::Bottom: tc.y = y0 + mv; break;
            }
            g.cells.push_back(tc);
        }

    g.ok = true;
    return g;
}

std::size_t load_csv(Table& t, std::string_view text) {
    std::vector<std::vector<std::string>> rows;
    std::vector<std::string> row;
    std::string field;
    bool quoted = false;

    for (std::size_t i = 0; i < text.size(); ++i) {
        const char ch = text[i];
        if (quoted) {
            if (ch == '"') {
                if (i + 1 < text.size() && text[i + 1] == '"') {
                    field.push_back('"');
                    ++i;
                } else {
                    quoted = false;
                }
            } else {
                field.push_back(ch);
            }
            continue;
        }
        if (ch == '"') { quoted = true; continue; }
        if (ch == ',') { row.push_back(field); field.clear(); continue; }
        if (ch == '\n') {
            row.push_back(field);
            field.clear();
            rows.push_back(row);
            row.clear();
            continue;
        }
        if (ch == '\r') continue;
        field.push_back(ch);
    }
    if (!field.empty() || !row.empty()) {
        row.push_back(field);
        rows.push_back(row);
    }
    if (rows.empty()) return 0;

    std::size_t cols = 0;
    for (const auto& r : rows) cols = std::max(cols, r.size());

    t.resize(static_cast<int>(rows.size()), static_cast<int>(cols));
    std::size_t filled = 0;
    for (std::size_t r = 0; r < rows.size(); ++r)
        for (std::size_t c = 0; c < rows[r].size(); ++c) {
            t.set_text(static_cast<int>(r), static_cast<int>(c), rows[r][c]);
            if (!rows[r][c].empty()) ++filled;
        }
    return filled;
}

std::string save_csv(const Table& t) {
    std::string out;
    for (int r = 0; r < t.rows(); ++r) {
        for (int c = 0; c < t.columns(); ++c) {
            if (c) out.push_back(',');
            const std::string s = t.text(r, c);
            const bool needs_quotes =
                s.find(',') != std::string::npos ||
                s.find('"') != std::string::npos ||
                s.find('\n') != std::string::npos;
            if (!needs_quotes) {
                out += s;
                continue;
            }
            out.push_back('"');
            for (char ch : s) {
                if (ch == '"') out.push_back('"');
                out.push_back(ch);
            }
            out.push_back('"');
        }
        out.push_back('\n');
    }
    return out;
}

}
