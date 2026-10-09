#pragma once

#include <string>

#include "ui/context.hpp"

namespace gui {

template <class D>
class Element {
public:
    explicit Element(Context& ctx) : ctx_(ctx) {}

    template <typename... Args>
    auto run(const Rect& r, Args&&... args) {
        rect_ = r;
        return static_cast<D*>(this)->paint(std::forward<Args>(args)...);
    }

    const Rect& rect() const noexcept { return rect_; }
    Context&    ctx() const noexcept { return ctx_; }

protected:
    void contained_text(const Rect& r, std::string_view text, f32 size,
                        const Color& color, TextAlign align) const {
        Rect inside = r.intersect(rect_);
        if (inside.empty()) return;
        ctx_.draw().text(inside, std::string(text), size, color, align);
    }

    Context::Interaction interact(std::string_view id, const Rect& r) const {
        return ctx_.interact(ctx_.id(id), r);
    }

    const Style& style() const noexcept { return ctx_.style(); }
    const gtheme::Theme& theme() const noexcept { return ctx_.theme(); }

    Rect     rect_{};
    Context& ctx_;
};

}
