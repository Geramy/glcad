#pragma once

#include <string>
#include <utility>
#include <string_view>
#include <vector>

#include "engine/layer.hpp"

namespace gdoc {

struct Dash {
    f64         length{0.0};
    std::string text;
    f64         text_scale{1.0};
};

struct Linetype {
    std::string       name{"Continuous"};
    std::string       description;
    std::vector<Dash> pattern;

    bool solid() const noexcept { return pattern.empty(); }
    f64  cycle_length() const noexcept;

    bool draws_at(f64 d, f64 scale = 1.0) const noexcept;

    std::vector<std::pair<f64, f64>> spans(f64 length, f64 scale = 1.0,
                                           f64 dot = 0.0) const;
};

class LinetypeTable {
public:
    LinetypeTable();

    const Linetype* find(std::string_view name) const;
    Linetype&       add(Linetype lt);
    std::size_t     size() const noexcept { return types_.size(); }
    const std::vector<Linetype>& all() const noexcept { return types_; }

    std::size_t load_lin(std::string_view text);

private:
    std::vector<Linetype> types_;
};

struct MultilineElement {
    f64         offset{0.0};
    Color       color{Color::by_layer()};
    std::string linetype{"ByLayer"};
    Lineweight  lineweight{Lineweight::ByLayer};
};

enum class EndCap : u8 { None, Line, OuterArc, InnerArc };

struct MultilineStyle {
    std::string                   name;
    std::string                   description;
    std::vector<MultilineElement> elements;

    bool   filled{false};
    Color  fill{Color::aci(8)};

    EndCap start_cap{EndCap::Line};
    EndCap end_cap{EndCap::Line};
    bool   show_joints{false};

    f64 width() const noexcept;
    f64 max_offset() const noexcept;
    f64 min_offset() const noexcept;
};

enum class Justify : u8 { Top, Zero, Bottom };

class MultilineTable {
public:
    MultilineTable();

    const MultilineStyle* find(std::string_view name) const;
    MultilineStyle&       add(MultilineStyle s);
    std::size_t           size() const noexcept { return styles_.size(); }
    const std::vector<MultilineStyle>& all() const noexcept { return styles_; }

private:
    std::vector<MultilineStyle> styles_;
};

std::vector<f64> resolve_offsets(const MultilineStyle& s, Justify j);

}
