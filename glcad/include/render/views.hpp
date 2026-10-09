#pragma once

#include <string>
#include <vector>

#include "render/camera.hpp"

namespace gview {

struct ViewState {
    Point3     target{};
    f64        distance{10.0};
    f64        azimuth{0.0};
    f64        elevation{0.0};
    Projection projection{Projection::Perspective};
    f64        fov{0.7853981633974483};

    static ViewState from(const Camera& c);
    void apply(Camera& c) const;
    bool same_as(const ViewState& o, f64 tol = 1e-9) const noexcept;
};

struct NamedView {
    std::string name;
    ViewState   state{};
    std::string description;
    bool        locked{false};
};

class ViewTable {
public:
    ViewTable();

    NamedView&       add(NamedView v);
    const NamedView* find(std::string_view name) const;
    bool             remove(std::string_view name);
    bool             rename(std::string_view from, std::string_view to);

    NamedView& capture(const Camera& c, std::string_view name,
                       std::string_view description = "");
    bool restore(Camera& c, std::string_view name) const;

    std::size_t size() const noexcept { return views_.size(); }
    const std::vector<NamedView>& all() const noexcept { return views_; }

private:
    std::vector<NamedView> views_;
};

class ViewHistory {
public:
    explicit ViewHistory(std::size_t limit = 64) : limit_(limit) {}

    void record(const Camera& c);
    void record(const ViewState& s);

    bool can_go_back() const noexcept { return index_ > 0; }
    bool can_go_forward() const noexcept {
        return !states_.empty() && index_ + 1 < states_.size();
    }
    bool back(Camera& c);
    bool forward(Camera& c);

    void clear();
    std::size_t size() const noexcept { return states_.size(); }
    std::size_t position() const noexcept { return index_; }

private:
    std::vector<ViewState> states_;
    std::size_t            index_{0};
    std::size_t            limit_;
    bool                   navigating_{false};
};

enum class WalkMode : u8 {
    Walk,
    Fly,
};

struct WalkSettings {
    WalkMode mode{WalkMode::Walk};
    f64 speed{1.5};              // metres per second
    f64 eye_height{1.7};
    f64 look_sensitivity{0.004}; // radians per pixel
    f64 ground_z{0.0};
    bool invert_look{false};
};

struct WalkInput {
    bool forward{false}, back{false}, left{false}, right{false};
    bool up{false}, down{false};
    bool fast{false};
    f64  look_dx{0.0}, look_dy{0.0};
    f64  dt{0.016};
};

class Walker {
public:
    WalkSettings settings{};

    void begin(const Camera& c);
    void step(Camera& c, const WalkInput& in);
    bool active() const noexcept { return active_; }
    void end() noexcept { active_ = false; }

    Point3 eye(const Camera& c) const;

private:
    bool active_{false};
};

Point3 eye_of(const Camera& c) noexcept;

void set_eye(Camera& c, Point3 eye) noexcept;

}
