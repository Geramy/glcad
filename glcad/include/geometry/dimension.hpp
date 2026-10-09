#pragma once

#include <string>
#include <cmath>
#include <vector>

#include "geometry/snap.hpp"
#include "engine/units.hpp"

namespace gdim {

using gcm::f64;
using gcm::i64;
using gcm::u8;
using gcm::u32;
using Point = gcm::Lattice2;
using Volume = gcm::WorkingVolume;

enum class Kind : u8 {
    Linear,
    Horizontal,
    Vertical,
    Aligned,
    Angular,
    Radial,
    Diameter,
    Ordinate,
    ArcLength,
    Leader,
};

const char* kind_name(Kind k) noexcept;

enum class ArrowType : u8 { ClosedFilled, Open, Tick, Dot, None };
enum class TextPos : u8 { Above, Centred, Below };

struct Style {
    std::string name{"Standard"};

    f64       dimexe{1.25};      // extension line beyond the dimension line
    f64       dimexo{0.625};     // extension line offset from the origin
    f64       dimgap{0.625};     // gap around the text
    f64       dimasz{2.5};       // arrow size
    ArrowType arrow{ArrowType::ClosedFilled};
    bool      dimse1{false}, dimse2{false};   // suppress extension line 1 / 2
    bool      dimsd1{false}, dimsd2{false};   // suppress dimension line 1 / 2

    f64     dimtxt{2.5};         // text height
    TextPos text_pos{TextPos::Above};
    bool    dimtih{false};       // text inside horizontal
    bool    dimtoh{false};       // text outside horizontal
    f64     dimtvp{0.0};         // vertical position adjustment

    gunit::Length unit{gunit::Length::Millimetre};
    gunit::Style  format{gunit::Style::Decimal};
    int           dimdec{2};     // decimal places
    f64           dimlfac{1.0};  // linear measurement scale factor
    f64           dimscale{1.0}; // overall scale applied to every size above
    bool          dimzin_leading{false};   // suppress leading zeros
    bool          dimzin_trailing{true};   // suppress trailing zeros

    std::string prefix, suffix;

    bool dimtol{false};
    f64  dimtp{0.0}, dimtm{0.0};

    f64 scaled(f64 v) const noexcept { return v * dimscale; }
};

class StyleTable {
public:
    StyleTable();
    const Style* find(std::string_view name) const;
    Style*       find_mut(std::string_view name) {
        return const_cast<Style*>(static_cast<const StyleTable*>(this)->find(name));
    }
    Style&       add(Style s);
    void         set_current(std::string_view name);
    const std::string& current() const noexcept { return current_; }
    std::size_t  size() const noexcept { return styles_.size(); }
    const std::vector<Style>& all() const noexcept { return styles_; }

private:
    std::vector<Style> styles_;
    std::string        current_{"Standard"};
};

struct Dimension {
    Kind        kind{Kind::Linear};
    std::string style{"Standard"};
    std::string layer;

    Point p1{}, p2{}, p3{};
    Point extension_offset{};
    Point centre{};
    i64   radius{0};

    enum class Anchor : u8 { None = 0, Start = 1, End = 2, Centre = 3, Edge = 4 };
    struct Ref {
        u32    shape{0};        // gsnap::Shape::key; 0 means not associated
        Anchor at{Anchor::None};
        bool   bound() const noexcept { return shape != 0 && at >= Anchor::Start && at <= Anchor::Edge; }
    };
    Ref o1, o2;
    static bool anchor_point(const gsnap::Shape& shape, Anchor anchor, Point& out);
    std::pair<Point,Point> radial_points() const;

    bool orphaned{false};

    bool associative() const noexcept { return o1.bound() || o2.bound(); }

    std::string text_override;
    gcm::u64 selection_id{0}; // Session identity, retained by undo; never serialized.
    struct LeaderPath {
        std::vector<Point> vertices;
        bool arrow{true};
    };
    std::vector<LeaderPath> leader_paths;
    bool leader_text_absolute{false};
    double leader_text_height{0},leader_text_angle{0},leader_arrow_size{0};
    int leader_text_halign{-1},leader_text_valign{-1}; // -1 uses the drafting placement.
    std::vector<LeaderPath> paths() const;
    std::vector<Point> leader_grips() const;
    bool move_leader_grip(std::size_t index,Point point);
    void move_leader_point(Point from,Point to);
    template<class F> void transform_points(F&& transform) {
        if(kind==Kind::Leader) {
            constexpr double length=1048576;
            Point origin{},direction{i64(std::llround(std::cos(leader_text_angle)*length)),i64(std::llround(std::sin(leader_text_angle)*length))};
            Point vertical{-direction.y,direction.x};
            const double original_angle=std::atan2(double(direction.y),double(direction.x));
            const double original_length=std::hypot(double(direction.x),double(direction.y));
            transform(origin);transform(direction);transform(vertical);
            const double dx=double(direction.x)-origin.x,dy=double(direction.y)-origin.y;
            if(dx||dy)leader_text_angle+=std::remainder(std::atan2(dy,dx)-original_angle,2*std::acos(-1.));
            const double scale=std::hypot(double(vertical.x)-origin.x,double(vertical.y)-origin.y)/original_length;
            leader_text_height*=scale;leader_arrow_size*=scale;
        }
        if(extension_offset.x || extension_offset.y) {
            Point zero{},offset=extension_offset;
            transform(zero);transform(offset);
            extension_offset={offset.x-zero.x,offset.y-zero.y};
        }
        transform(p1);transform(p2);transform(p3);transform(centre);
        for(auto& path:leader_paths)for(auto& point:path.vertices)transform(point);
    }
    bool        has_override() const noexcept { return !text_override.empty(); }
};

// Metres, or radians for angular.
bool linear_is_horizontal(const Dimension& d, const Volume& v);
f64 measure(const Dimension& d, const Volume& v);

std::string format(const Dimension& d, const Style& s, const Volume& v);

struct Geometry {
    std::vector<gsnap::Shape> lines;
    std::vector<gsnap::Shape> arrows;
    Point                     text_at{};
    f64                       text_angle{0.0};
    f64                       text_height{0.0};
    int                       halign{1}, valign{2};
    std::string               text;
    bool                      ok{false};
    const char*               reason{""};
};

Geometry build(const Dimension& d, const Style& s, const Volume& v);

}
