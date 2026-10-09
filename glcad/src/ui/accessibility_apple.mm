#import <Foundation/Foundation.h>

#if TARGET_OS_IPHONE
#import <UIKit/UIKit.h>
#else
#import <AppKit/AppKit.h>
#endif

#include <string>
#include <unordered_map>
#include <vector>

#include "ui/access.hpp"
#include "ui/native_accessibility.hpp"

namespace {

NSString* ns(const std::string& s) {
    return [NSString stringWithUTF8String:s.c_str()] ?: @"";
}

#if !TARGET_OS_IPHONE
NSString* role_for(gui::Context::Role r) {
    switch (r) {
        case gui::Context::Role::Button:   return NSAccessibilityButtonRole;
        case gui::Context::Role::Toggle:
        case gui::Context::Role::Checkbox: return NSAccessibilityCheckBoxRole;
        case gui::Context::Role::Field:    return NSAccessibilityTextFieldRole;
        case gui::Context::Role::Label:    return NSAccessibilityStaticTextRole;
        case gui::Context::Role::Panel:    return NSAccessibilityGroupRole;
        case gui::Context::Role::Tab:      return NSAccessibilityRadioButtonRole;
        case gui::Context::Role::Row:      return NSAccessibilityRowRole;
        case gui::Context::Role::TreeItem: return NSAccessibilityRowRole;
        case gui::Context::Role::Slider:   return NSAccessibilitySliderRole;
        case gui::Context::Role::Canvas:   return NSAccessibilityGroupRole;
        case gui::Context::Role::None:
        default:                           return NSAccessibilityUnknownRole;
    }
}
#else
UIAccessibilityTraits traits_for(gui::Context::Role r) {
    switch (r) {
        case gui::Context::Role::Button:   return UIAccessibilityTraitButton;
        case gui::Context::Role::Toggle:
        case gui::Context::Role::Checkbox: return UIAccessibilityTraitButton;
        case gui::Context::Role::Field:    return UIAccessibilityTraitNone;
        case gui::Context::Role::Label:    return UIAccessibilityTraitStaticText;
        case gui::Context::Role::Tab:      return UIAccessibilityTraitButton;
        case gui::Context::Role::Slider:   return UIAccessibilityTraitAdjustable;
        default:                           return UIAccessibilityTraitNone;
    }
}
#endif

}

#if TARGET_OS_IPHONE
@interface GlcadAccessibleElement : UIAccessibilityElement
#else
@interface GlcadAccessibleElement : NSAccessibilityElement
#endif
@property(nonatomic, assign) unsigned long long nodeId;
- (void)setAction:(gcapp::NativeAccessAction)action;
@end

@implementation GlcadAccessibleElement {
    gcapp::NativeAccessAction _action;
}
- (void)setAction:(gcapp::NativeAccessAction)action { _action = std::move(action); }
#if TARGET_OS_IPHONE
- (BOOL)accessibilityActivate { return _action && _action(self.nodeId, 0); }
- (void)accessibilityIncrement { if (_action) _action(self.nodeId, 1); }
- (void)accessibilityDecrement { if (_action) _action(self.nodeId, -1); }
#else
- (BOOL)accessibilityPerformPress { return _action && _action(self.nodeId, 0); }
#endif
@end

@interface GlcadAccessibilityBridge : NSObject
- (instancetype)initWithView:(id)view action:(gcapp::NativeAccessAction)action;
- (void)apply:(const std::vector<gui::AccessEvent>&)events
     snapshot:(const gui::AccessSnapshot&)snapshot
        scale:(double)scale
       height:(double)height;
@end

@implementation GlcadAccessibilityBridge {
    __weak id _view;
    std::unordered_map<unsigned long long, GlcadAccessibleElement*> _elements;
    unsigned long long _focus;
    gcapp::NativeAccessAction _action;
    std::vector<unsigned long long> _order;
}

- (instancetype)initWithView:(id)view action:(gcapp::NativeAccessAction)action {
    self = [super init];
    if (!self) return nil;
    _view = view;
    _focus = 0;
    _action = std::move(action);
    return self;
}

- (GlcadAccessibleElement*)elementFor:(unsigned long long)nodeId {
    const auto it = _elements.find(nodeId);
    if (it != _elements.end()) return it->second;

#if TARGET_OS_IPHONE
    GlcadAccessibleElement* e =
        [[GlcadAccessibleElement alloc] initWithAccessibilityContainer:_view];
#else
    GlcadAccessibleElement* e = [[GlcadAccessibleElement alloc] init];
    [e setAccessibilityParent:_view];
#endif
    e.nodeId = nodeId;
    [e setAction:_action];
    _elements[nodeId] = e;
    return e;
}

- (void)rebuildChildren {
#if TARGET_OS_IPHONE
    NSMutableArray* kids = [NSMutableArray array];
    for (const auto id : _order) {
        const auto found = _elements.find(id);
        if (found != _elements.end()) [kids addObject:found->second];
    }
    if ([_view respondsToSelector:@selector(setAccessibilityElements:)])
        [_view setAccessibilityElements:kids];
#else
    NSMutableArray* kids = [NSMutableArray array];
    for (const auto id : _order) {
        const auto found = _elements.find(id);
        if (found != _elements.end()) [kids addObject:found->second];
    }
    if ([_view respondsToSelector:@selector(setAccessibilityChildren:)])
        [_view setAccessibilityChildren:kids];
#endif
}

- (void)apply:(const std::vector<gui::AccessEvent>&)events
     snapshot:(const gui::AccessSnapshot&)snapshot
        scale:(double)scale
       height:(double)height {
    bool structure_changed = false;
    std::vector<unsigned long long> order;
    for (const auto& node : snapshot.nodes) order.push_back(node.id);
    if (_order != order) { _order = std::move(order); structure_changed = true; }
    if (events.empty() && !structure_changed) return;

    for (const gui::AccessEvent& e : events) {
        switch (e.kind) {
            case gui::AccessEventKind::Announcement: {
#if TARGET_OS_IPHONE
                UIAccessibilityPostNotification(
                    UIAccessibilityAnnouncementNotification, ns(e.text));
#else
                if (_view) {
                    NSAccessibilityPostNotificationWithUserInfo(
                        _view, NSAccessibilityAnnouncementRequestedNotification,
                        @{NSAccessibilityAnnouncementKey : ns(e.text),
                          NSAccessibilityPriorityKey :
                              @(NSAccessibilityPriorityHigh)});
                }
#endif
                continue;
            }

            case gui::AccessEventKind::Removed: {
                const auto it = _elements.find(e.node);
                if (it != _elements.end()) {
                    _elements.erase(it);
                    structure_changed = true;
                }
                continue;
            }

            case gui::AccessEventKind::Added:
                [self elementFor:e.node];
                structure_changed = true;
                break;

            default:
                break;
        }

        const gui::AccessNode* node = snapshot.find(e.node);
        if (!node) continue;
        GlcadAccessibleElement* element = [self elementFor:e.node];

        // Toolkit is pixels with y down; AppKit wants points with y up.
        const double s = scale > 0.0 ? scale : 1.0;
        const double x = node->bounds.x / s;
        const double w = node->bounds.w / s;
        const double h = node->bounds.h / s;
#if TARGET_OS_IPHONE
        const double y = node->bounds.y / s;
        element.accessibilityFrame = UIAccessibilityConvertFrameToScreenCoordinates(
            CGRectMake(x, y, w, h), (UIView*)_view);
        element.accessibilityLabel = ns(node->label);
        element.accessibilityValue = ns(node->value);
        element.accessibilityTraits = traits_for(node->role) |
            (node->selected ? UIAccessibilityTraitSelected : UIAccessibilityTraitNone) |
            (node->disabled ? UIAccessibilityTraitNotEnabled
                            : UIAccessibilityTraitNone);
#else
        const double y = (height / s) - (node->bounds.y / s) - h;
        [element setAccessibilityFrame:NSMakeRect(x, y, w, h)];
        [element setAccessibilityLabel:ns(node->label)];
        [element setAccessibilityValue:ns(node->value)];
        [element setAccessibilityRole:role_for(node->role)];
        [element setAccessibilityEnabled:!node->disabled];
        [element setAccessibilitySelected:node->selected];
#endif

        switch (e.kind) {
            case gui::AccessEventKind::ValueChanged:
            case gui::AccessEventKind::LabelChanged:
            case gui::AccessEventKind::StateChanged:
#if TARGET_OS_IPHONE
                UIAccessibilityPostNotification(
                    UIAccessibilityLayoutChangedNotification, element);
#else
                NSAccessibilityPostNotification(
                    element, NSAccessibilityValueChangedNotification);
#endif
                break;

            case gui::AccessEventKind::FocusChanged:
                _focus = e.node;
#if TARGET_OS_IPHONE
                UIAccessibilityPostNotification(
                    UIAccessibilityScreenChangedNotification, element);
#else
                NSAccessibilityPostNotification(
                    element, NSAccessibilityFocusedUIElementChangedNotification);
#endif
                break;

            default:
                break;
        }
    }

    // Rebuilding unconditionally makes VoiceOver re-read the window every frame.
    if (structure_changed) [self rebuildChildren];
}

@end

namespace gcapp {

class VoiceOverBackend {
public:
    explicit VoiceOverBackend(void* view, NativeAccessAction action)
        : bridge_((__bridge_retained void*)[[GlcadAccessibilityBridge alloc]
              initWithView:(__bridge id)view action:std::move(action)]) {}

    ~VoiceOverBackend() {
        if (bridge_) CFRelease(bridge_);
    }

    VoiceOverBackend(const VoiceOverBackend&) = delete;
    VoiceOverBackend& operator=(const VoiceOverBackend&) = delete;

    void apply(const std::vector<gui::AccessEvent>& events,
               const gui::AccessSnapshot& snapshot, double scale,
               double height) {
        if (!bridge_) return;
        GlcadAccessibilityBridge* b = (__bridge GlcadAccessibilityBridge*)bridge_;
        [b apply:events snapshot:snapshot scale:scale height:height];
    }

    static bool reader_running() {
#if TARGET_OS_IPHONE
        return UIAccessibilityIsVoiceOverRunning();
#else
        return [[NSWorkspace sharedWorkspace] isVoiceOverEnabled];
#endif
    }

private:
    void* bridge_{nullptr};
public:
    gui::AccessBridge changes;
};

void* create_voice_over(void* view, NativeAccessAction action) {
    return new VoiceOverBackend(view, std::move(action));
}

void destroy_voice_over(void* handle) { delete static_cast<VoiceOverBackend*>(handle); }

void update_voice_over(void* handle, const gui::AccessSnapshot& snapshot,
                       double scale, double height) {
    if (!handle) return;
    auto& backend = *static_cast<VoiceOverBackend*>(handle);
    backend.apply(backend.changes.update(snapshot), snapshot, scale, height);
}

}
