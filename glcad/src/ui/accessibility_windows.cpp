#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef UNICODE
#define UNICODE
#endif
#include <windows.h>
#include <oleacc.h>
#include <uiautomation.h>

#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "ui/access.hpp"

namespace gcapp {
namespace {

std::wstring to_utf16(const std::string& text) {
    if (text.empty()) return {};
    const int needed = MultiByteToWideChar(CP_UTF8, 0, text.c_str(),
                                           static_cast<int>(text.size()), nullptr, 0);
    if (needed <= 0) return {};
    std::wstring out(static_cast<std::size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                        out.data(), needed);
    return out;
}

long control_type(gui::Context::Role r) {
    switch (r) {
        case gui::Context::Role::Button:   return UIA_ButtonControlTypeId;
        case gui::Context::Role::Toggle:
        case gui::Context::Role::Checkbox: return UIA_CheckBoxControlTypeId;
        case gui::Context::Role::Field:    return UIA_EditControlTypeId;
        case gui::Context::Role::Label:    return UIA_TextControlTypeId;
        case gui::Context::Role::Panel:    return UIA_GroupControlTypeId;
        case gui::Context::Role::Tab:      return UIA_TabItemControlTypeId;
        case gui::Context::Role::Row:      return UIA_DataItemControlTypeId;
        case gui::Context::Role::TreeItem: return UIA_TreeItemControlTypeId;
        case gui::Context::Role::Slider:   return UIA_SliderControlTypeId;
        case gui::Context::Role::Canvas:   return UIA_GroupControlTypeId;
        case gui::Context::Role::None:
        default:                           return UIA_CustomControlTypeId;
    }
}

}

class UiaBackend {
public:
    explicit UiaBackend(HWND window) : window_(window) {}

    void apply(const std::vector<gui::AccessEvent>& events,
               const gui::AccessSnapshot& snapshot) {
        if (events.empty()) return;

        {
            std::lock_guard<std::mutex> lock(mutex_);
            published_ = snapshot;
        }

        if (!UiaClientsAreListening()) return;

        for (const gui::AccessEvent& e : events) {
            switch (e.kind) {
                case gui::AccessEventKind::Announcement: {
                    const std::wstring text = to_utf16(e.text);
                    VARIANT v;
                    VariantInit(&v);
                    v.vt = VT_BSTR;
                    v.bstrVal = SysAllocString(text.c_str());
                    UiaRaiseNotificationEvent(
                        nullptr, NotificationKind_Other,
                        NotificationProcessing_ImportantAll, v.bstrVal, nullptr);
                    VariantClear(&v);
                    break;
                }

                case gui::AccessEventKind::FocusChanged:
                    focus_ = e.node;
                    NotifyWinEvent(EVENT_OBJECT_FOCUS, window_, OBJID_CLIENT,
                                   static_cast<LONG>(e.node & 0x7FFFFFFF));
                    break;

                case gui::AccessEventKind::ValueChanged:
                case gui::AccessEventKind::LabelChanged:
                    NotifyWinEvent(EVENT_OBJECT_VALUECHANGE, window_, OBJID_CLIENT,
                                   static_cast<LONG>(e.node & 0x7FFFFFFF));
                    break;

                case gui::AccessEventKind::StateChanged:
                    NotifyWinEvent(EVENT_OBJECT_STATECHANGE, window_, OBJID_CLIENT,
                                   static_cast<LONG>(e.node & 0x7FFFFFFF));
                    break;

                case gui::AccessEventKind::Added:
                case gui::AccessEventKind::Removed:
                    if (!structure_raised_) {
                        NotifyWinEvent(EVENT_OBJECT_REORDER, window_,
                                       OBJID_CLIENT, CHILDID_SELF);
                        structure_raised_ = true;
                    }
                    break;

                case gui::AccessEventKind::BoundsChanged:
                    break;
            }
        }
        structure_raised_ = false;
    }

    // Returns a copy: the render thread may be mid-update.
    gui::AccessSnapshot snapshot() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return published_;
    }

    std::wstring describe(gcm::u64 node) const {
        std::lock_guard<std::mutex> lock(mutex_);
        if (const gui::AccessNode* n = published_.find(node))
            return to_utf16(n->describe());
        return {};
    }

    long control_type_of(gcm::u64 node) const {
        std::lock_guard<std::mutex> lock(mutex_);
        if (const gui::AccessNode* n = published_.find(node))
            return control_type(n->role);
        return UIA_CustomControlTypeId;
    }

    static void runtime_id(gcm::u64 node, int out[2]) {
        out[0] = UiaAppendRuntimeId;
        out[1] = static_cast<int>(node & 0x7FFFFFFF);
    }

    static bool reader_running() { return UiaClientsAreListening() != FALSE; }

private:
    HWND                        window_{nullptr};
    mutable std::mutex          mutex_;
    gui::AccessSnapshot         published_;
    gcm::u64                    focus_{0};
    bool                        structure_raised_{false};
};

}
