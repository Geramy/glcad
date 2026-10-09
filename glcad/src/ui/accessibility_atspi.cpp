#include <cstdlib>
#include <mutex>
#include <string>
#include <vector>

#include "ui/access.hpp"

namespace gcapp {

const char* atspi_role(gui::Context::Role r) noexcept {
    switch (r) {
        case gui::Context::Role::Button:   return "push button";
        case gui::Context::Role::Toggle:   return "toggle button";
        case gui::Context::Role::Checkbox: return "check box";
        case gui::Context::Role::Field:    return "entry";
        case gui::Context::Role::Label:    return "label";
        case gui::Context::Role::Panel:    return "panel";
        case gui::Context::Role::Tab:      return "page tab";
        case gui::Context::Role::Row:      return "table row";
        case gui::Context::Role::TreeItem: return "tree item";
        case gui::Context::Role::Slider:   return "slider";
        case gui::Context::Role::Canvas:   return "canvas";
        case gui::Context::Role::None:
        default:                           return "unknown";
    }
}

const char* atspi_signal(gui::AccessEventKind k) noexcept {
    switch (k) {
        case gui::AccessEventKind::Added:         return "object:children-changed:add";
        case gui::AccessEventKind::Removed:       return "object:children-changed:remove";
        case gui::AccessEventKind::LabelChanged:  return "object:property-change:accessible-name";
        case gui::AccessEventKind::ValueChanged:  return "object:property-change:accessible-value";
        case gui::AccessEventKind::StateChanged:  return "object:state-changed:selected";
        case gui::AccessEventKind::BoundsChanged: return "object:bounds-changed";
        case gui::AccessEventKind::FocusChanged:  return "object:state-changed:focused";
        case gui::AccessEventKind::Announcement:  return "object:announcement";
    }
    return "";
}

struct AtspiSignal {
    std::string signal;
    gcm::u64    node{0};
    std::string text;
    std::string role;
    double      x{0}, y{0}, w{0}, h{0};
};

class AtspiBackend {
public:
    using Emitter = void (*)(void* context, const AtspiSignal& signal);

    void set_emitter(Emitter e, void* context) noexcept {
        emit_ = e;
        context_ = context;
    }

    static bool reader_running() noexcept {
        return std::getenv("AT_SPI_BUS") != nullptr ||
               std::getenv("GTK_MODULES") != nullptr;
    }

    void apply(const std::vector<gui::AccessEvent>& events,
               const gui::AccessSnapshot& snapshot) {
        if (events.empty()) return;

        {
            std::lock_guard<std::mutex> lock(mutex_);
            published_ = snapshot;
        }
        if (!emit_) return;

        for (const gui::AccessEvent& e : events) {
            AtspiSignal s;
            s.signal = atspi_signal(e.kind);
            s.node = e.node;
            s.text = e.text;

            if (const gui::AccessNode* n = snapshot.find(e.node)) {
                s.role = atspi_role(n->role);
                s.x = n->bounds.x;
                s.y = n->bounds.y;
                s.w = n->bounds.w;
                s.h = n->bounds.h;
                if (s.text.empty()) s.text = n->describe();
            }
            emit_(context_, s);
        }
    }

    gui::AccessSnapshot snapshot() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return published_;
    }

private:
    mutable std::mutex  mutex_;
    gui::AccessSnapshot published_;
    Emitter             emit_{nullptr};
    void*               context_{nullptr};
};

}
