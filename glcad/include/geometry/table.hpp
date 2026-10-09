#pragma once

#include <string>
#include <vector>

#include "geometry/annotation.hpp"

namespace gdim {

enum class CellKind : u8 { Text, Block, Formula };
enum class HAlign : u8 { Left, Centre, Right };
enum class VAlign : u8 { Top, Middle, Bottom };

enum class Band : u8 { Title, Header, Data };

enum class Direction : u8 { Down, Up };

struct Border {
    bool visible{true};
    f64  weight{0.25};        // millimetres on paper
};

struct CellStyle {
    f64    text_height{0.0025};    // metres
    HAlign halign{HAlign::Left};
    VAlign valign{VAlign::Middle};
    f64    margin_h{0.0015};
    f64    margin_v{0.0010};
    Border top{}, bottom{}, left{}, right{};
    bool   fill{false};
    u8     fill_r{255}, fill_g{255}, fill_b{255};
};

struct Cell {
    std::string text;
    CellKind    kind{CellKind::Text};
    std::string block;

    int row_span{1}, col_span{1};
    bool covered{false};

    bool      has_style{false};
    CellStyle style{};

    bool empty() const noexcept { return text.empty() && block.empty(); }
};

struct TableStyle {
    std::string name{"Standard"};
    Direction   direction{Direction::Down};

    CellStyle title{};
    CellStyle header{};
    CellStyle data{};

    f64  scale{1.0};
    bool annotative{false};

    static TableStyle standard();
};

class TableStyleTable {
public:
    TableStyleTable();
    const TableStyle* find(std::string_view name) const;
    TableStyle&       add(TableStyle s);
    std::size_t       size() const noexcept { return styles_.size(); }
    const std::vector<TableStyle>& all() const noexcept { return styles_; }
    void set_current(std::string_view name);
    const std::string& current() const noexcept { return current_; }

private:
    std::vector<TableStyle> styles_;
    std::string             current_{"Standard"};
};

class Table {
public:
    Table() = default;
    Table(int rows, int columns) { resize(rows, columns); }

    void resize(int rows, int columns);
    int  rows() const noexcept { return rows_; }
    int  columns() const noexcept { return cols_; }

    Cell*       at(int r, int c);
    const Cell* at(int r, int c) const;
    bool        set_text(int r, int c, std::string text);
    std::string text(int r, int c) const;

    bool merge(int r, int c, int row_span, int col_span);
    bool unmerge(int r, int c);
    bool is_merged(int r, int c) const;

    bool insert_row(int before);
    bool insert_column(int before);
    bool remove_row(int r);
    bool remove_column(int c);

    void set_column_width(int c, f64 w);
    void set_row_height(int r, f64 h);
    f64  column_width(int c) const;
    f64  row_height(int r) const;

    Band band_of(int row) const noexcept;
    void set_title_rows(int n) noexcept { title_rows_ = n < 0 ? 0 : n; }
    void set_header_rows(int n) noexcept { header_rows_ = n < 0 ? 0 : n; }
    int  title_rows() const noexcept { return title_rows_; }
    int  header_rows() const noexcept { return header_rows_; }

    Point position{};
    std::string style{"Standard"};
    Annotative  annotative{};

    std::size_t evaluate();

private:
    int rows_{0}, cols_{0};
    std::vector<Cell> cells_;
    std::vector<f64>  col_w_, row_h_;
    int title_rows_{1}, header_rows_{1};
};

struct TableCellGeometry {
    std::string text;
    f64 x{0.0}, y{0.0};
    f64 width{0.0}, height{0.0};
    f64 text_height{0.0};
    HAlign align{HAlign::Left};
};

struct TableGeometry {
    std::vector<gsnap::Shape>      lines;
    std::vector<TableCellGeometry> cells;
    f64  width{0.0}, height{0.0};
    bool ok{false};
    const char* reason{""};
};

TableGeometry build(const Table& t, const TableStyle& s, const Volume& v);

std::size_t load_csv(Table& t, std::string_view text);
std::string save_csv(const Table& t);

}
