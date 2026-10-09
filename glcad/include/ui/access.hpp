#pragma once

#include <functional>
#include <string>
#include <vector>

#include "ui/context.hpp"

namespace gui {

struct AccessNode {
    Id            id{0};
    Id            parent{0};        // 0 for a root
    Context::Role role{Context::Role::None};
    std::string   label;
    std::string   value;
    Rect          bounds{};
    int           depth{0};
    bool          focused{false};
    bool          selected{false};
    bool          disabled{false};

    std::string describe() const;
};

enum class AccessEventKind : gcm::u8 {
    Added,
    Removed,
    LabelChanged,
    ValueChanged,
    StateChanged,
    BoundsChanged,
    FocusChanged,
    Announcement,
};

const char* access_event_name(AccessEventKind k) noexcept;

struct AccessEvent {
    AccessEventKind kind{AccessEventKind::Added};
    Id              node{0};
    std::string     text;
};

struct AccessSnapshot {
    std::vector<AccessNode> nodes;
    Id                      focus{0};

    const AccessNode* find(Id id) const;
    std::vector<Id>   children_of(Id parent) const;
    std::size_t       size() const noexcept { return nodes.size(); }
};

AccessSnapshot snapshot(const Context& c);

class AccessBridge {
public:
    std::vector<AccessEvent> update(const AccessSnapshot& now);

    void announce(std::string text);

    const AccessSnapshot& current() const noexcept { return current_; }
    Id                    focus() const noexcept { return current_.focus; }

    std::string focus_description() const;

    void reset();

private:
    AccessSnapshot           current_;
    bool                     started_{false};
    std::vector<std::string> pending_;
};

}
