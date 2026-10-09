#include "ui/access.hpp"

#include <algorithm>
#include <cmath>

namespace gui {

const char* access_event_name(AccessEventKind k) noexcept {
    switch (k) {
        case AccessEventKind::Added:         return "added";
        case AccessEventKind::Removed:       return "removed";
        case AccessEventKind::LabelChanged:  return "label changed";
        case AccessEventKind::ValueChanged:  return "value changed";
        case AccessEventKind::StateChanged:  return "state changed";
        case AccessEventKind::BoundsChanged: return "bounds changed";
        case AccessEventKind::FocusChanged:  return "focus changed";
        case AccessEventKind::Announcement:  return "announcement";
    }
    return "";
}

std::string AccessNode::describe() const {
    std::string out = label;
    const char* r = Context::role_name(role);
    if (r && r[0]) {
        if (!out.empty()) out += ", ";
        out += r;
    }
    if (!value.empty()) {
        out += ", ";
        out += value;
    }
    if (selected) out += ", selected";
    if (disabled) out += ", unavailable";
    return out;
}

const AccessNode* AccessSnapshot::find(Id id) const {
    for (const AccessNode& n : nodes)
        if (n.id == id) return &n;
    return nullptr;
}

std::vector<Id> AccessSnapshot::children_of(Id parent) const {
    std::vector<Id> out;
    for (const AccessNode& n : nodes)
        if (n.parent == parent) out.push_back(n.id);
    return out;
}

AccessSnapshot snapshot(const Context& c) {
    AccessSnapshot out;
    out.focus = c.focused();

    std::vector<Id> ancestry;
    for (const Context::Accessible& a : c.accessibility_tree()) {
        AccessNode n;
        n.id = a.id;
        n.role = a.role;
        n.label = a.label;
        n.value = a.value;
        n.bounds = a.bounds;
        n.depth = a.depth;
        n.focused = a.focused;
        n.selected = a.selected;
        n.disabled = a.disabled;

        const std::size_t d = static_cast<std::size_t>(std::max(0, a.depth));
        if (d < ancestry.size()) ancestry.resize(d);
        n.parent = ancestry.empty() ? 0 : ancestry.back();
        ancestry.resize(d + 1, 0);
        ancestry[d] = n.id;

        out.nodes.push_back(std::move(n));
    }
    if (!out.find(out.focus)) out.focus = 0;
    return out;
}

void AccessBridge::announce(std::string text) {
    if (text.empty()) return;
    pending_.push_back(std::move(text));
}

void AccessBridge::reset() {
    current_ = AccessSnapshot{};
    started_ = false;
    pending_.clear();
}

std::string AccessBridge::focus_description() const {
    if (const AccessNode* n = current_.find(current_.focus)) return n->describe();
    return {};
}

std::vector<AccessEvent> AccessBridge::update(const AccessSnapshot& now) {
    std::vector<AccessEvent> out;

    auto bounds_differ = [](const Rect& a, const Rect& b) {
        constexpr f32 kSlack = 0.5f;
        return std::fabs(a.x - b.x) > kSlack || std::fabs(a.y - b.y) > kSlack ||
               std::fabs(a.w - b.w) > kSlack || std::fabs(a.h - b.h) > kSlack;
    };

    if (!started_) {
        for (const AccessNode& n : now.nodes)
            out.push_back({AccessEventKind::Added, n.id, n.describe()});
        if (now.focus)
            out.push_back({AccessEventKind::FocusChanged, now.focus,
                           now.find(now.focus) ? now.find(now.focus)->describe()
                                               : std::string()});
        started_ = true;
    } else {
        for (const AccessNode& n : now.nodes) {
            const AccessNode* was = current_.find(n.id);
            if (!was) {
                out.push_back({AccessEventKind::Added, n.id, n.describe()});
                continue;
            }
            if (was->label != n.label)
                out.push_back({AccessEventKind::LabelChanged, n.id, n.label});
            if (was->value != n.value)
                out.push_back({AccessEventKind::ValueChanged, n.id, n.value});
            if (was->selected != n.selected || was->disabled != n.disabled)
                out.push_back({AccessEventKind::StateChanged, n.id, n.describe()});
            if (bounds_differ(was->bounds, n.bounds))
                out.push_back({AccessEventKind::BoundsChanged, n.id, {}});
        }
        for (const AccessNode& n : current_.nodes)
            if (!now.find(n.id))
                out.push_back({AccessEventKind::Removed, n.id, {}});

        if (now.focus != current_.focus) {
            const AccessNode* n = now.find(now.focus);
            out.push_back({AccessEventKind::FocusChanged, now.focus,
                           n ? n->describe() : std::string()});
        }
    }

    for (std::string& text : pending_)
        out.push_back({AccessEventKind::Announcement, 0, std::move(text)});
    pending_.clear();

    current_ = now;
    return out;
}

}
