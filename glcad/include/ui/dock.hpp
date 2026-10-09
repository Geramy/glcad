#pragma once

#include <string>
#include <vector>

#include "ui/context.hpp"

namespace gui {

enum class Side : u8 { Left, Right, Top, Bottom, Floating };

struct PanelState {
    std::string id;
    std::string title;
    Side        side{Side::Right};
    f32         extent{260.0f};     // width when Left/Right, height otherwise
    bool        open{true};
    bool        collapsed{false};
    int         order{0};
    Rect        floating{100, 100, 280, 320};
    f32         min_extent{160.0f};
    f32         max_extent{620.0f};
    f32         share{1.0f};
    f32         preferred{0.0f};
    f32         content_h{0.0f};
};

struct PanelSlot {
    Rect content{};
    Rect frame{};
    Rect title{};
    bool visible{false};
    bool collapsed{false};
};

class Dock {
public:
    void add(PanelState p);
    PanelState*       find(std::string_view id);
    const PanelState* find(std::string_view id) const;

    void toggle(std::string_view id);
    void collapse(std::string_view id, bool collapsed);
    void move_to(std::string_view id, Side s);
    void set_content_height(std::string_view id, f32 h) {
        if (PanelState* p = find(id)) p->content_h = h;
    }

    Rect layout(Context& c, Rect area);

    bool take_changed() noexcept { const bool c = changed_; changed_ = false; return c; }

    std::string save(f32 vw, f32 vh) const;
    void        restore(std::string_view text, f32 vw, f32 vh);

    PanelSlot slot(std::string_view id) const;

    const std::vector<PanelState>& panels() const noexcept { return panels_; }
    std::size_t size() const noexcept { return panels_.size(); }

    void begin_content(Context& c, std::string_view id) const;
    void end_content(Context& c, std::string_view id) const;

    f32 rail_thickness() const noexcept { return rail_; }

    struct DragState {
        bool        active{false};
        std::string id;
        Vec2        grab{};
        Side        target{Side::Floating};
        bool        target_valid{false};
    };

    const DragState& drag() const noexcept { return drag_; }
    bool dragging() const noexcept { return drag_.active; }

    Side drop_target(const Rect& area, Vec2 at, bool& valid) const;

    Rect drop_preview(const Rect& area, Side s, f32 extent) const;

private:
    DragState drag_{};
    bool      changed_{false};

    Rect layout_side(Context& c, Rect& area, Side s);
    void draw_frame(Context& c, PanelState& p, const Rect& frame);

    std::vector<PanelState> panels_;
    std::vector<std::pair<std::string, PanelSlot>> slots_;
public:
    void set_scroll(std::string_view id, f32 offset) {
        for (auto& [k, v] : scroll_)
            if (k == id) { v = offset; return; }
        scroll_.emplace_back(std::string(id), offset);
    }

private:
    std::vector<std::pair<std::string, f32>> scroll_;
    mutable std::vector<std::pair<std::string, Rect>> clips_;
    f32 rail_{30.0f};
};

}
