#include "render/views.hpp"

#include <algorithm>
#include <cmath>

namespace gview {

namespace {

f64 wrap_angle(f64 a) {
    constexpr f64 kTwoPi = 6.28318530717958647692;
    while (a <= -kTwoPi) a += kTwoPi;
    while (a >= kTwoPi) a -= kTwoPi;
    return a;
}

}

ViewState ViewState::from(const Camera& c) {
    ViewState s;
    s.target = c.target();
    s.distance = c.distance();
    s.azimuth = c.azimuth();
    s.elevation = c.elevation();
    s.projection = c.projection();
    s.fov = c.fov();
    return s;
}

void ViewState::apply(Camera& c) const {
    c.set_target(target);
    c.set_distance(distance);
    c.set_orbit(azimuth, elevation);
    c.set_projection(projection);
    c.set_fov(fov);
}

bool ViewState::same_as(const ViewState& o, f64 tol) const noexcept {
    return std::fabs(target.x.to_f64() - o.target.x.to_f64()) < tol &&
           std::fabs(target.y.to_f64() - o.target.y.to_f64()) < tol &&
           std::fabs(target.z.to_f64() - o.target.z.to_f64()) < tol &&
           std::fabs(distance - o.distance) < tol &&
           std::fabs(azimuth - o.azimuth) < tol &&
           std::fabs(elevation - o.elevation) < tol &&
           projection == o.projection && std::fabs(fov - o.fov) < tol;
}

ViewTable::ViewTable() {
    struct Standard { const char* name; StandardView v; };
    const Standard standards[] = {
        {"Top", StandardView::Top},       {"Bottom", StandardView::Bottom},
        {"Front", StandardView::Front},   {"Back", StandardView::Back},
        {"Left", StandardView::Left},     {"Right", StandardView::Right},
        {"SW Isometric", StandardView::IsoSW},
        {"NE Isometric", StandardView::IsoNE},
    };

    for (const auto& s : standards) {
        Camera c;
        c.look(s.v);
        NamedView v;
        v.name = s.name;
        v.state = ViewState::from(c);
        v.locked = true;
        views_.push_back(std::move(v));
    }
}

NamedView& ViewTable::add(NamedView v) {
    for (auto& e : views_)
        if (e.name == v.name) {
            e = std::move(v);
            return e;
        }
    views_.push_back(std::move(v));
    return views_.back();
}

const NamedView* ViewTable::find(std::string_view name) const {
    for (const auto& v : views_)
        if (v.name == name) return &v;
    return nullptr;
}

bool ViewTable::remove(std::string_view name) {
    const auto it = std::find_if(views_.begin(), views_.end(),
                                 [&](const NamedView& v) { return v.name == name; });
    if (it == views_.end()) return false;
    if (it->locked) return false;
    views_.erase(it);
    return true;
}

bool ViewTable::rename(std::string_view from, std::string_view to) {
    if (to.empty() || find(to)) return false;
    for (auto& v : views_)
        if (v.name == from) {
            v.name = std::string(to);
            return true;
        }
    return false;
}

NamedView& ViewTable::capture(const Camera& c, std::string_view name,
                              std::string_view description) {
    NamedView v;
    v.name = std::string(name);
    v.description = std::string(description);
    v.state = ViewState::from(c);
    return add(std::move(v));
}

bool ViewTable::restore(Camera& c, std::string_view name) const {
    const NamedView* v = find(name);
    if (!v) return false;
    v->state.apply(c);
    return true;
}

void ViewHistory::record(const Camera& c) { record(ViewState::from(c)); }

void ViewHistory::record(const ViewState& s) {
    // Repeats collapse, which also absorbs the record right after back().
    if (!states_.empty() && states_[index_].same_as(s, 1e-9)) return;

    if (!states_.empty() && index_ + 1 < states_.size())
        states_.erase(states_.begin() + static_cast<long>(index_) + 1, states_.end());

    states_.push_back(s);
    if (states_.size() > limit_) states_.erase(states_.begin());
    index_ = states_.size() - 1;
}

bool ViewHistory::back(Camera& c) {
    if (!can_go_back()) return false;
    --index_;
    states_[index_].apply(c);
    return true;
}

bool ViewHistory::forward(Camera& c) {
    if (!can_go_forward()) return false;
    ++index_;
    states_[index_].apply(c);
    return true;
}

void ViewHistory::clear() {
    states_.clear();
    index_ = 0;
}

Point3 eye_of(const Camera& c) noexcept {
    const f64 ce = std::cos(c.elevation());
    const f64 d = c.distance();
    return Point3::from(c.target().x.to_f64() + std::cos(c.azimuth()) * ce * d,
                        c.target().y.to_f64() + std::sin(c.azimuth()) * ce * d,
                        c.target().z.to_f64() + std::sin(c.elevation()) * d);
}

void set_eye(Camera& c, Point3 eye) noexcept {
    const f64 ce = std::cos(c.elevation());
    const f64 d = c.distance();
    c.set_target(Point3::from(eye.x.to_f64() - std::cos(c.azimuth()) * ce * d,
                              eye.y.to_f64() - std::sin(c.azimuth()) * ce * d,
                              eye.z.to_f64() - std::sin(c.elevation()) * d));
}

void Walker::begin(const Camera& c) {
    active_ = true;
    (void)c;
}

Point3 Walker::eye(const Camera& c) const { return eye_of(c); }

void Walker::step(Camera& c, const WalkInput& in) {
    if (!active_) return;

    Point3 e = eye_of(c);

    const f64 sens = settings.look_sensitivity * (settings.invert_look ? -1.0 : 1.0);
    constexpr f64 kLimit = 1.5533430342749532;    // 89 degrees
    c.set_orbit(wrap_angle(c.azimuth() - in.look_dx * sens),
                std::clamp(c.elevation() + in.look_dy * sens, -kLimit, kLimit));

    const f64 ce = std::cos(c.elevation());
    f64 fx = -std::cos(c.azimuth()) * ce;
    f64 fy = -std::sin(c.azimuth()) * ce;
    f64 fz = -std::sin(c.elevation());
    if (settings.mode == WalkMode::Walk) {
        fz = 0.0;
        const f64 len = std::hypot(fx, fy);
        if (len > 1e-12) {
            fx /= len;
            fy /= len;
        }
    }
    const f64 rx = -fy, ry = fx;

    const f64 speed = settings.speed * (in.fast ? 3.0 : 1.0) * in.dt;
    f64 mx = 0.0, my = 0.0, mz = 0.0;
    if (in.forward) { mx += fx * speed; my += fy * speed; mz += fz * speed; }
    if (in.back)    { mx -= fx * speed; my -= fy * speed; mz -= fz * speed; }
    if (in.right)   { mx += rx * speed; my += ry * speed; }
    if (in.left)    { mx -= rx * speed; my -= ry * speed; }
    if (settings.mode == WalkMode::Fly) {
        if (in.up) mz += speed;
        if (in.down) mz -= speed;
    }

    e = Point3::from(e.x.to_f64() + mx, e.y.to_f64() + my, e.z.to_f64() + mz);

    if (settings.mode == WalkMode::Walk && (mx != 0.0 || my != 0.0 || mz != 0.0))
        e = Point3::from(e.x.to_f64(), e.y.to_f64(),
                         settings.ground_z + settings.eye_height);

    set_eye(c, e);
}

}
