#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace gdgn {
struct Point {double x{},y{},z{};};
struct Element {
    enum class Kind {Polyline,Arc,Text,ComplexHeader,CellHeader,Unsupported};
    Kind kind{Kind::Unsupported};
    int type{},id{},level{},group{},weight{},style{},properties{};
    std::uint64_t offset{},bytes{},group_end{};
    bool hole{};
    bool complex{},closed{};
    int color_index{},fill_index{-1};
    std::array<std::uint8_t,3> color{};
    bool filled{};
    std::array<std::uint8_t,3> fill_color{};
    std::vector<Point> points;
    Point origin{};
    double primary_axis{},secondary_axis{},rotation{},start_angle{},sweep_angle{};
    std::array<int,4> quaternion{};
    std::string text;
    int font{},justification{},children{};
    double text_width{},text_height{};
};
struct Drawing {
    bool ok{};
    int dimension{};
    std::string error,master_unit,subunit;
    double subunits_per_master{},uor_per_subunit{};
    Point origin{};
    // Coordinates and dimensions remain in the source master units.
    std::vector<Element> elements;
    std::vector<std::string> warnings;
};
Drawing read_file(const std::string& path);
}
