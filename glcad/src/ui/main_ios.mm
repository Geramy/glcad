#import <QuartzCore/CAMetalLayer.h>
#import <UIKit/UIKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "ui/shell.hpp"
#include "ui/frame_demand.hpp"
#include "ui/touch.hpp"
#include "ui/renderer.hpp"
#include "ui/native_accessibility.hpp"
#include "ui/product.hpp"
#include "ui/scenario.hpp"
#ifdef GLCAD_WITH_PGLITE
#include "rag/library.hpp"
#include "engine/tls.hpp"
#include "ai/client.hpp"
#endif

using namespace gcapp;

namespace {
Shell*     g_shell = nullptr;
Renderer*  g_render = nullptr;
TouchState g_touch;
gui::Id g_requested_field = 0;

void request_field_at(double x, double y) {
    if (!g_shell) return;
    for (auto it = g_shell->accessible_ui().rbegin(); it != g_shell->accessible_ui().rend(); ++it) {
        if (it->disabled || it->role != gui::Context::Role::Field) continue;
        if (x >= it->bounds.x && x <= it->bounds.right() &&
            y >= it->bounds.y && y <= it->bounds.bottom()) {
            g_requested_field = it->id;
            return;
        }
    }
}

std::unordered_map<const void*, gcm::u64> g_ids;
gcm::u64 g_next_id = 1;

gcm::u64 id_for(UITouch* t) {
    const void* key = (__bridge const void*)t;
    const auto it = g_ids.find(key);
    if (it != g_ids.end()) return it->second;
    const gcm::u64 id = g_next_id++;
    g_ids[key] = id;
    return id;
}

bool is_pointer(UITouch* t) {
    if (@available(iOS 13.4, *)) return t.type == UITouchTypeIndirectPointer;
    return false;
}

TouchKind kind_of(UITouch* t) {
    if (@available(iOS 9.1, *)) {
        if (t.type == UITouchTypePencil) return TouchKind::Pencil;
    }
    return TouchKind::Finger;
}
}

@interface CanvasView : UIView
@end

static gcapp::FrameDemand g_frame_demand;
static CADisplayLink* g_link = nil;
static bool g_rate_high = false;
static void want_frames() {
    g_frame_demand.request();
    if (!g_rate_high && g_link) {
        g_rate_high = true;
        if (@available(iOS 15.0, *))
            g_link.preferredFrameRateRange = CAFrameRateRangeMake(60, 120, 120);
    }
}
static void rest_rate() {
    if (g_rate_high && g_link) {
        g_rate_high = false;
        if (@available(iOS 15.0, *))
            g_link.preferredFrameRateRange = CAFrameRateRangeMake(10, 120, 30);
    }
}

static const double kPadUiScale = 1.15;
static const double kPadTextBoost = 1.65;

@implementation CanvasView

+ (Class)layerClass { return [CAMetalLayer class]; }

- (instancetype)initWithFrame:(CGRect)frame {
    self = [super initWithFrame:frame];
    if (!self) return nil;
    self.multipleTouchEnabled = YES;
    self.contentScaleFactor = UIScreen.mainScreen.nativeScale;
    UIHoverGestureRecognizer* hover =
        [[UIHoverGestureRecognizer alloc] initWithTarget:self
                                                  action:@selector(hovered:)];
    [self addGestureRecognizer:hover];
    if (@available(iOS 13.4, *)) {
        UIPanGestureRecognizer* trackpad =
            [[UIPanGestureRecognizer alloc] initWithTarget:self action:@selector(trackpadScrolled:)];
        trackpad.allowedScrollTypesMask = UIScrollTypeMaskContinuous;
        trackpad.allowedTouchTypes = @[];
        trackpad.cancelsTouchesInView = NO;
        [self addGestureRecognizer:trackpad];
        UIPanGestureRecognizer* wheel =
            [[UIPanGestureRecognizer alloc] initWithTarget:self action:@selector(wheelScrolled:)];
        wheel.allowedScrollTypesMask = UIScrollTypeMaskDiscrete;
        wheel.allowedTouchTypes = @[];
        wheel.cancelsTouchesInView = NO;
        [self addGestureRecognizer:wheel];
        UIPinchGestureRecognizer* pinch =
            [[UIPinchGestureRecognizer alloc] initWithTarget:self action:@selector(trackpadPinched:)];
        pinch.allowedTouchTypes = @[ @(UITouchTypeIndirectPointer) ];
        pinch.cancelsTouchesInView = NO;
        [self addGestureRecognizer:pinch];
    }
    CAMetalLayer* l = (CAMetalLayer*)self.layer;
    l.pixelFormat = MTLPixelFormatBGRA8Unorm;
    l.framebufferOnly = YES;
    return self;
}

- (void)scrollFrom:(UIPanGestureRecognizer*)g precise:(bool)precise {
    want_frames();
    if (!g_shell) return;
    const CGFloat s = self.contentScaleFactor;
    const CGPoint at = [g locationInView:self];
    const CGPoint d = [g translationInView:self];
    [g setTranslation:CGPointZero inView:self];
    g_shell->pointer_move(at.x * s, at.y * s);
    if (precise || d.y != 0.0) g_shell->scroll(d.x * s, d.y * s, precise);
}
- (void)trackpadScrolled:(UIPanGestureRecognizer*)g { [self scrollFrom:g precise:true]; }
- (void)wheelScrolled:(UIPanGestureRecognizer*)g { [self scrollFrom:g precise:false]; }

- (void)trackpadPinched:(UIPinchGestureRecognizer*)g {
    want_frames();
    if (!g_shell) return;
    if (g.state != UIGestureRecognizerStateBegan && g.state != UIGestureRecognizerStateChanged) return;
    const CGFloat s = self.contentScaleFactor;
    const CGPoint at = [g locationInView:self];
    const double mag = g.scale - 1.0;
    g.scale = 1.0;
    g_shell->pinch(at.x * s, at.y * s, mag);
}

- (void)hovered:(UIHoverGestureRecognizer*)g {
    want_frames();
    if (!g_shell) return;
    if (g.state == UIGestureRecognizerStateEnded ||
        g.state == UIGestureRecognizerStateCancelled)
        return;
    const CGPoint p = [g locationInView:self];
    const CGFloat s = self.contentScaleFactor;
    g_shell->pointer_move(p.x * s, p.y * s);
}

- (void)layoutSubviews {
    [super layoutSubviews];
    want_frames();
    CAMetalLayer* l = (CAMetalLayer*)self.layer;
    const CGFloat s = self.contentScaleFactor;
    const CGSize sz = CGSizeMake(self.bounds.size.width * s,
                                 self.bounds.size.height * s);
    l.contentsScale = s;
    l.drawableSize = sz;
    if (g_render) g_render->resize((uint32_t)sz.width, (uint32_t)sz.height);
    if (g_shell) {
        g_shell->set_dpr(s * kPadUiScale);
        g_shell->set_viewport(sz.width, sz.height);
    }
}

- (void)deliver:(NSSet<UITouch*>*)changed withEvent:(UIEvent*)event {
    want_frames();
    std::vector<TouchPoint> points;
    const CGFloat s = self.contentScaleFactor;

    bool direct = false;
    for (UITouch* t in changed) {
        if (!is_pointer(t) || !g_shell) continue;
        const CGPoint p = [t locationInView:self];
        bool secondary = false;
        if (@available(iOS 13.4, *)) {
            const UIKeyModifierFlags f = event.modifierFlags;
            g_shell->set_modifiers(f & UIKeyModifierShift, f & UIKeyModifierControl,
                                   f & UIKeyModifierAlternate, f & UIKeyModifierCommand);
            secondary = (event.buttonMask & UIEventButtonMaskSecondary) != 0;
        }
        switch (t.phase) {
            case UITouchPhaseBegan:
                if (!secondary) request_field_at(p.x * s, p.y * s);
                g_shell->pointer_input(Shell::PointerPhase::Down, p.x * s, p.y * s, secondary);
                break;
            case UITouchPhaseMoved:
                g_shell->pointer_input(Shell::PointerPhase::Move, p.x * s, p.y * s, secondary);
                break;
            case UITouchPhaseEnded:
            case UITouchPhaseCancelled:
                g_shell->pointer_input(Shell::PointerPhase::Up, p.x * s, p.y * s, secondary);
                break;
            default:
                break;
        }
    }

    for (UITouch* t in [event touchesForView:self]) {
        if (is_pointer(t)) continue;
        direct = true;
        if (t.phase == UITouchPhaseEnded || t.phase == UITouchPhaseCancelled)
            continue;
        const CGPoint p = [t locationInView:self];
        TouchPoint tp;
        tp.id = id_for(t);
        tp.x = p.x * s;
        tp.y = p.y * s;
        tp.kind = kind_of(t);
        tp.pressure = t.maximumPossibleForce > 0.0
                          ? (double)(t.force / t.maximumPossibleForce) : 0.0;
        tp.down = true;
        points.push_back(tp);
    }

    if (!direct && g_touch.active() == 0) return;
    const double now = CACurrentMediaTime();
    g_touch.settings.scale = s;
    const TouchResult r = g_touch.update(points, now);
    if (!g_shell) return;

    if (r.gesture == Gesture::Tap)
        request_field_at(r.from_pencil ? r.x : r.raw_x, r.from_pencil ? r.y : r.raw_y);
    g_shell->apply_gesture(r);

    if (r.ended) {
        if (g_touch.active() == 0) g_ids.clear();
    }
}

- (void)touchesBegan:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
    [self deliver:touches withEvent:event];
}
- (void)touchesMoved:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
    [self deliver:touches withEvent:event];
}
- (void)touchesEnded:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
    [self deliver:touches withEvent:event];
}
- (void)touchesCancelled:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
    (void)touches;
    (void)event;
    g_touch.reset();
    g_ids.clear();
    if (g_shell) g_shell->key_special(1);   // escape
}

@end

@interface CanvasController : UIViewController <UITextViewDelegate, UIDocumentPickerDelegate>
@property(nonatomic, strong) CADisplayLink* link;
@property(nonatomic, strong) CanvasView* canvas;
@property(nonatomic, strong) UIView* editorPanel;
@property(nonatomic, strong) UITextView* editor;
@property(nonatomic, strong) UILabel* editorLabel;
@property(nonatomic, strong) NSTimer* recoveryTimer;
- (void)askFile:(Shell::FileAsk)kind completion:(std::function<void(std::string)>)completion;
- (void)openIncomingURL:(NSURL*)url;
@end

@implementation CanvasController {
    gui::Id _editingId;
    std::string _submittedText;
    CGFloat _keyboardTop;
    void* _voiceOver;
    std::function<void(std::string)> _fileCompletion;
    Shell::FileAsk _fileKind;
    std::string _fileDocument;
    NSURL* _exportURL;
}

- (void)loadView {
    self.view = [[UIView alloc] initWithFrame:UIScreen.mainScreen.bounds];
    self.view.backgroundColor = UIColor.systemBackgroundColor;
    self.canvas = [[CanvasView alloc] initWithFrame:self.view.bounds];
    [self.view addSubview:self.canvas];
    self.editorPanel = [UIView new];
    self.editorPanel.backgroundColor = UIColor.secondarySystemBackgroundColor;
    self.editorPanel.hidden = YES;
    [self.view addSubview:self.editorPanel];
    self.editorLabel = [UILabel new];
    self.editorLabel.font = [UIFont preferredFontForTextStyle:UIFontTextStyleCaption1];
    [self.editorPanel addSubview:self.editorLabel];
    self.editor = [UITextView new];
    self.editor.delegate = self;
    self.editor.font = [UIFont preferredFontForTextStyle:UIFontTextStyleBody];
    self.editor.autocorrectionType = UITextAutocorrectionTypeNo;
    self.editor.autocapitalizationType = UITextAutocapitalizationTypeNone;
    self.editor.smartQuotesType = UITextSmartQuotesTypeNo;
    self.editor.smartDashesType = UITextSmartDashesTypeNo;
    self.editor.returnKeyType = UIReturnKeyDone;
    self.editor.accessibilityIdentifier = @"cad-native-editor";
    [self.editorPanel addSubview:self.editor];
    UIToolbar* toolbar = [[UIToolbar alloc] initWithFrame:CGRectMake(0, 0, 500, 44)];
    toolbar.items = @[
        [[UIBarButtonItem alloc] initWithTitle:@"Previous" style:UIBarButtonItemStylePlain target:self action:@selector(previousField:)],
        [[UIBarButtonItem alloc] initWithTitle:@"Next" style:UIBarButtonItemStylePlain target:self action:@selector(nextField:)],
        [[UIBarButtonItem alloc] initWithBarButtonSystemItem:UIBarButtonSystemItemFlexibleSpace target:nil action:nil],
        [[UIBarButtonItem alloc] initWithTitle:@"Hide Keyboard" style:UIBarButtonItemStylePlain target:self action:@selector(hideKeyboard:)],
        [[UIBarButtonItem alloc] initWithBarButtonSystemItem:UIBarButtonSystemItemDone target:self action:@selector(commitEditor:)]];
    self.editor.inputAccessoryView = toolbar;
}

- (void)viewDidLayoutSubviews {
    [super viewDidLayoutSubviews];
    const UIEdgeInsets safe = self.view.safeAreaInsets;
    const CGFloat bottom = _keyboardTop > 0 ? std::min(_keyboardTop, self.view.bounds.size.height - safe.bottom)
                                          : self.view.bounds.size.height - safe.bottom;
    const CGFloat editorHeight = self.editorPanel.hidden ? 0 : 92;
    const bool raised = _keyboardTop > 0 || editorHeight > 0;
    self.canvas.frame = CGRectMake(0, 0, self.view.bounds.size.width,
        raised ? std::max<CGFloat>(safe.top + 80, bottom - editorHeight) : self.view.bounds.size.height);
    if (g_shell) {
        const CGFloat s = self.canvas.contentScaleFactor;
        g_shell->set_safe_insets(safe.left * s, safe.top * s, safe.right * s, raised ? 0 : safe.bottom * s);
        want_frames();
    }
    self.editorPanel.frame = CGRectMake(safe.left, bottom - editorHeight,
        self.view.bounds.size.width - safe.left - safe.right, editorHeight);
    self.editorLabel.frame = CGRectMake(12, 4, self.editorPanel.bounds.size.width - 24, 20);
    self.editor.frame = CGRectMake(8, 26, self.editorPanel.bounds.size.width - 16, 62);
}

- (void)keyboardChanged:(NSNotification*)notification {
    CGRect screen = [notification.userInfo[UIKeyboardFrameEndUserInfoKey] CGRectValue];
    CGRect frame = [self.view convertRect:screen fromView:nil];
    const bool docked = CGRectGetMaxY(frame) >= self.view.bounds.size.height - 1 &&
                        frame.size.width >= self.view.bounds.size.width * .8;
    _keyboardTop = docked && CGRectGetMinY(frame) < self.view.bounds.size.height ? CGRectGetMinY(frame) : 0;
    [self.view setNeedsLayout];
    want_frames();
}

- (void)beginEditing:(const gui::Context::Accessible&)field {
    if (!g_shell || field.disabled || !g_shell->focus_accessible(field.id)) return;
    _editingId = field.id;
    _submittedText = field.value;
    self.editor.text = [NSString stringWithUTF8String:field.value.c_str()] ?: @"";
    self.editorLabel.text = [NSString stringWithUTF8String:field.label.c_str()];
    self.editor.accessibilityLabel = self.editorLabel.text;
    self.editorPanel.hidden = NO;
    [self.view setNeedsLayout];
    [self.editor becomeFirstResponder];
    [self.editor selectAll:nil];
    want_frames();
}

- (void)syncEditor {
    if (!g_shell) return;
    const auto& fields = g_shell->accessible_ui();
    if (g_requested_field) {
        const gui::Id wanted = g_requested_field;
        g_requested_field = 0;
        for (const auto& field : fields) if (field.id == wanted && field.role == gui::Context::Role::Field) {
            [self beginEditing:field];
            break;
        }
    }
    if (!_editingId) return;
    const gui::Context::Accessible* focused = nullptr;
    for (const auto& field : fields)
        if (field.focused && !field.disabled && field.role == gui::Context::Role::Field) focused = &field;
    if (!focused) { [self hideKeyboard:nil]; return; }
    if (focused->id != _editingId) { [self beginEditing:*focused]; return; }
    // Never replace an active IME composition on a frame.
    if (!self.editor.markedTextRange && focused->value != _submittedText) {
        _submittedText = focused->value;
        self.editor.text = [NSString stringWithUTF8String:focused->value.c_str()] ?: @"";
    }
}

- (void)textViewDidChange:(UITextView*)textView {
    if (!g_shell || !_editingId || textView.markedTextRange) return;
    std::string value = textView.text.UTF8String ?: "";
    if (g_shell->replace_focused_text(_editingId, value)) {
        _submittedText = std::move(value);
        // Text events may repeat before a refresh; consume each before comparing.
        g_shell->build_frame();
        want_frames();
    }
}

- (BOOL)textView:(UITextView*)textView shouldChangeTextInRange:(NSRange)range replacementText:(NSString*)text {
    (void)range;
    if ([text isEqualToString:@"\n"] && !textView.markedTextRange) { [self commitEditor:nil]; return NO; }
    return YES;
}

- (void)hideKeyboard:(id)sender {
    (void)sender;
    [self.editor unmarkText];
    [self textViewDidChange:self.editor];
    _editingId = 0;
    self.editorPanel.hidden = YES;
    [self.editor resignFirstResponder];
    [self becomeFirstResponder];
    [self.view setNeedsLayout];
    want_frames();
}

- (void)commitEditor:(id)sender {
    (void)sender;
    [self.editor unmarkText];
    [self textViewDidChange:self.editor];
    const bool editing = _editingId != 0;
    _editingId = 0;
    if (g_shell && editing) { g_shell->key_special(0); g_shell->build_frame(); }
    [self hideKeyboard:nil];
}

- (void)stepField:(BOOL)backwards {
    if (!g_shell) return;
    [self.editor unmarkText];
    [self textViewDidChange:self.editor];
    g_shell->set_modifiers(backwards, false, false, false);
    g_shell->key_special(3);
    g_shell->set_modifiers(false, false, false, false);
    g_shell->build_frame();
    [self syncEditor];
    want_frames();
}
- (void)previousField:(id)sender { (void)sender; [self stepField:YES]; }
- (void)nextField:(id)sender { (void)sender; [self stepField:NO]; }

- (void)showFileError:(NSString*)message {
    if (g_shell) g_shell->log_line(message.UTF8String ?: "File operation failed");
    UIAlertController* alert = [UIAlertController alertControllerWithTitle:@"File operation failed"
        message:message preferredStyle:UIAlertControllerStyleAlert];
    [alert addAction:[UIAlertAction actionWithTitle:@"OK" style:UIAlertActionStyleDefault handler:nil]];
    [self presentNativeController:alert];
    want_frames();
}

- (void)presentNativeController:(UIViewController*)controller {
    // UIKit may still be dismissing the controller; sequence presentations.
    if (self.presentedViewController) {
        [self dismissViewControllerAnimated:YES completion:^{
            [self presentViewController:controller animated:YES completion:nil];
        }];
    } else [self presentViewController:controller animated:YES completion:nil];
}

- (NSURL*)copyIncomingFile:(NSURL*)source {
    return [self copyIncomingFile:source folder:@"Imported"];
}

- (NSURL*)copyIncomingFile:(NSURL*)source folder:(NSString*)folder {
    const BOOL scoped = [source startAccessingSecurityScopedResource];
    NSFileManager* fm = NSFileManager.defaultManager;
    NSURL* documents = [fm URLsForDirectory:NSDocumentDirectory inDomains:NSUserDomainMask].firstObject;
    NSURL* directory = [[documents URLByAppendingPathComponent:folder isDirectory:YES]
        URLByAppendingPathComponent:NSUUID.UUID.UUIDString isDirectory:YES];
    NSError* error = nil;
    if (![fm createDirectoryAtURL:directory withIntermediateDirectories:YES attributes:nil error:&error]) {
        if (scoped) [source stopAccessingSecurityScopedResource];
        [self showFileError:error.localizedDescription];
        return nil;
    }
    NSURL* target = [directory URLByAppendingPathComponent:source.lastPathComponent];
    __block NSError* copyError = nil;
    NSFileCoordinator* coordinator = [[NSFileCoordinator alloc] initWithFilePresenter:nil];
    [coordinator coordinateReadingItemAtURL:source options:0 error:&error byAccessor:^(NSURL* coordinated) {
        [fm copyItemAtURL:coordinated toURL:target error:&copyError];
    }];
    if (scoped) [source stopAccessingSecurityScopedResource];
    if (error || copyError) {
        [fm removeItemAtURL:directory error:nil];
        [self showFileError:(error ?: copyError).localizedDescription];
        return nil;
    }
    return target;
}

- (void)openIncomingURL:(NSURL*)url {
    if (!url.isFileURL || !g_shell) return;
    NSURL* local = [self copyIncomingFile:url];
    if (!local) return;
    g_shell->new_tab();
    g_shell->open_file(local.fileSystemRepresentation);
    want_frames();
}

- (void)askFile:(Shell::FileAsk)kind completion:(std::function<void(std::string)>)completion {
    if (self.presentedViewController || _fileCompletion) {
        completion({});
        return;
    }
    _fileKind = kind;
    _fileDocument = g_shell ? g_shell->document_identity() : "";
    _fileCompletion = std::move(completion);
    [self hideKeyboard:nil];
    const bool reading = kind == Shell::FileAsk::Open || kind == Shell::FileAsk::Import || kind == Shell::FileAsk::Attach || kind == Shell::FileAsk::LoadLibrary;
    if (reading) {
        UIDocumentPickerViewController* picker = [[UIDocumentPickerViewController alloc]
            initForOpeningContentTypes:@[UTTypeData] asCopy:YES];
        picker.delegate = self;
        picker.allowsMultipleSelection = NO;
        picker.shouldShowFileExtensions = YES;
        picker.title = kind == Shell::FileAsk::Open ? @"Open a Copy" : @"Import a Copy";
        [self presentNativeController:picker];
        return;
    }
    if (kind == Shell::FileAsk::Save) { [self askFilename:@"glcad"]; return; }
    if (kind == Shell::FileAsk::ExportPdf) { [self askFilename:@"pdf"]; return; }
    if (kind == Shell::FileAsk::SaveLibrary) { [self askFilename:@"gllib"]; return; }
    UIAlertController* formats = [UIAlertController alertControllerWithTitle:@"Export drawing"
        message:@"Choose a format." preferredStyle:UIAlertControllerStyleActionSheet];
    for (NSString* extension in @[@"glcad", @"step", @"x_t", @"stl", @"3mf", @"obj", @"dxf", @"dwg", @"pdf", @"dwfx"]) {
        [formats addAction:[UIAlertAction actionWithTitle:extension.uppercaseString style:UIAlertActionStyleDefault
            handler:^(UIAlertAction*) { [self askFilename:extension]; }]];
    }
    [formats addAction:[UIAlertAction actionWithTitle:@"Cancel" style:UIAlertActionStyleCancel handler:^(UIAlertAction*) {
        auto done = std::move(self->_fileCompletion); self->_fileCompletion = {};
        if (done) done({});
    }]];
    formats.popoverPresentationController.sourceView = self.view;
    formats.popoverPresentationController.sourceRect = CGRectMake(24, 24, 1, 1);
    [self presentNativeController:formats];
}

- (void)askFilename:(NSString*)extension {
    UIAlertController* name = [UIAlertController alertControllerWithTitle:@"Save drawing"
        message:@"Keep an editable copy on this iPad, then save or share a copy." preferredStyle:UIAlertControllerStyleAlert];
    [name addTextFieldWithConfigurationHandler:^(UITextField* field) {
        NSString* title = g_shell ? [NSString stringWithUTF8String:g_shell->document_path().c_str()] : nil;
        title = title.lastPathComponent.stringByDeletingPathExtension;
        field.text = [(title.length ? title : @"Drawing") stringByAppendingPathExtension:extension];
        field.autocapitalizationType = UITextAutocapitalizationTypeNone;
        field.autocorrectionType = UITextAutocorrectionTypeNo;
        field.accessibilityLabel = @"File name";
    }];
    [name addAction:[UIAlertAction actionWithTitle:@"Cancel" style:UIAlertActionStyleCancel handler:^(UIAlertAction*) {
        auto done = std::move(self->_fileCompletion); self->_fileCompletion = {};
        if (done) done({});
    }]];
    [name addAction:[UIAlertAction actionWithTitle:@"Save" style:UIAlertActionStyleDefault handler:^(UIAlertAction*) {
        NSString* filename = [name.textFields.firstObject.text stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet];
        if (!filename.length || ![filename isEqualToString:filename.lastPathComponent] ||
            [filename isEqualToString:@"."] || [filename isEqualToString:@".."]) {
            auto done = std::move(self->_fileCompletion); self->_fileCompletion = {};
            if (done) done({});
            [self showFileError:@"Use a file name without folder separators."];
            return;
        }
        if (![filename.pathExtension.lowercaseString isEqualToString:extension])
            filename = [filename stringByAppendingPathExtension:extension];
        NSFileManager* fm = NSFileManager.defaultManager;
        NSURL* documents = [fm URLsForDirectory:NSDocumentDirectory inDomains:NSUserDomainMask].firstObject;
        NSURL* directory = [[documents URLByAppendingPathComponent:@"Drawings" isDirectory:YES]
            URLByAppendingPathComponent:NSUUID.UUID.UUIDString isDirectory:YES];
        NSError* error = nil;
        if (![fm createDirectoryAtURL:directory withIntermediateDirectories:YES attributes:nil error:&error]) {
            auto done = std::move(self->_fileCompletion); self->_fileCompletion = {};
            if (done) done({});
            [self showFileError:error.localizedDescription];
            return;
        }
        NSURL* url = [directory URLByAppendingPathComponent:filename];
        if (!g_shell || g_shell->document_identity() != self->_fileDocument) {
            auto done = std::move(self->_fileCompletion); self->_fileCompletion = {};
            if (done) done({});
            [fm removeItemAtURL:directory error:nil];
            [self showFileError:@"The active drawing changed. Choose Save or Export again for the current drawing."];
            return;
        }
        auto done = std::move(self->_fileCompletion); self->_fileCompletion = {};
        if (done) done(url.fileSystemRepresentation);
        if (![fm fileExistsAtPath:url.path]) {
            [fm removeItemAtURL:directory error:nil];
            [self showFileError:@"The drawing could not be written. Check the drawing's error message."];
            return;
        }
        self->_exportURL = url;
        [self offerExport:url];
        want_frames();
    }]];
    [self presentNativeController:name];
}

- (void)offerExport:(NSURL*)url {
    UIAlertController* destination = [UIAlertController alertControllerWithTitle:@"Drawing saved on this iPad"
        message:url.lastPathComponent preferredStyle:UIAlertControllerStyleActionSheet];
    [destination addAction:[UIAlertAction actionWithTitle:@"Save a Copy to Files" style:UIAlertActionStyleDefault handler:^(UIAlertAction*) {
        UIDocumentPickerViewController* picker = [[UIDocumentPickerViewController alloc] initForExportingURLs:@[url] asCopy:YES];
        picker.delegate = self;
        [self presentNativeController:picker];
    }]];
    [destination addAction:[UIAlertAction actionWithTitle:@"Share…" style:UIAlertActionStyleDefault handler:^(UIAlertAction*) {
        UIActivityViewController* share = [[UIActivityViewController alloc] initWithActivityItems:@[url] applicationActivities:nil];
        share.popoverPresentationController.sourceView = self.view;
        share.popoverPresentationController.sourceRect = CGRectMake(24, 24, 1, 1);
        [self presentNativeController:share];
    }]];
    [destination addAction:[UIAlertAction actionWithTitle:@"Done" style:UIAlertActionStyleCancel handler:nil]];
    destination.popoverPresentationController.sourceView = self.view;
    destination.popoverPresentationController.sourceRect = CGRectMake(24, 24, 1, 1);
    [self presentNativeController:destination];
}

- (void)documentPicker:(UIDocumentPickerViewController*)controller didPickDocumentsAtURLs:(NSArray<NSURL*>*)urls {
    (void)controller;
    auto done = std::move(_fileCompletion); _fileCompletion = {};
    if (!done) { _exportURL = nil; want_frames(); return; }
    NSURL* local = urls.count ? [self copyIncomingFile:urls.firstObject] : nil;
    if (local && g_shell && _fileKind == Shell::FileAsk::Open) g_shell->new_tab();
    done(local ? local.fileSystemRepresentation : "");
    want_frames();
}

- (void)documentPickerWasCancelled:(UIDocumentPickerViewController*)controller {
    (void)controller;
    auto done = std::move(_fileCompletion); _fileCompletion = {};
    if (done) done({});
    _exportURL = nil;
    want_frames();
}

#ifndef NDEBUG
#include "tests/ios_native_smoke.inc"
#endif

- (BOOL)prefersStatusBarHidden { return YES; }
- (BOOL)prefersHomeIndicatorAutoHidden { return YES; }

- (void)viewDidLoad {
    [super viewDidLoad];
    self.link = [CADisplayLink displayLinkWithTarget:self
                                            selector:@selector(tick:)];
    [self.link addToRunLoop:NSRunLoop.mainRunLoop forMode:NSRunLoopCommonModes];
    g_link = self.link;
    if (@available(iOS 15.0, *))
        self.link.preferredFrameRateRange = CAFrameRateRangeMake(60, 120, 120);
    g_rate_high = true;
    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(keyboardChanged:)
        name:UIKeyboardWillChangeFrameNotification object:nil];
    self.recoveryTimer = [NSTimer scheduledTimerWithTimeInterval:5 repeats:YES block:^(NSTimer*) {
        if (g_shell && UIApplication.sharedApplication.applicationState == UIApplicationStateActive)
            g_shell->autosave_tick();
    }];
    __weak CanvasController* weakSelf = self;
    _voiceOver = create_voice_over((__bridge void*)self.canvas, [weakSelf](gui::Id id, int direction) {
        CanvasController* controller = weakSelf;
        if (!controller || !g_shell) return false;
        for (const auto& field : g_shell->accessible_ui()) {
            if (field.id != id || field.disabled) continue;
            if (field.role == gui::Context::Role::Field) {
                g_requested_field = id;
                want_frames();
                return true;
            }
            if (direction && field.role == gui::Context::Role::Slider) {
                if (!g_shell->focus_accessible(id)) return false;
                g_shell->key_special(direction > 0 ? 6 : 5);
                want_frames();
                return true;
            }
            if (field.role == gui::Context::Role::Label || field.role == gui::Context::Role::Panel ||
                field.role == gui::Context::Role::Canvas || field.role == gui::Context::Role::None) return false;
            const double x = field.bounds.x + field.bounds.w * .5;
            const double y = field.bounds.y + field.bounds.h * .5;
            g_shell->pointer_move(x, y);
            g_shell->pointer_down(x, y);
            g_shell->pointer_up(x, y);
            want_frames();
            return true;
        }
        return false;
    });
    // iPadOS forbids background GPU work: pause the link or lose the VkDevice.
    [[NSNotificationCenter defaultCenter]
        addObserverForName:UIApplicationWillResignActiveNotification
                    object:nil queue:NSOperationQueue.mainQueue
                usingBlock:^(NSNotification*) {
                    CanvasController* controller = weakSelf;
                    [controller.editor unmarkText];
                    [controller textViewDidChange:controller.editor];
                    if (g_shell) g_shell->prepare_suspend();
                    controller.link.paused = YES;
                }];
    [[NSNotificationCenter defaultCenter]
        addObserverForName:UIApplicationDidBecomeActiveNotification
                    object:nil queue:NSOperationQueue.mainQueue
                usingBlock:^(NSNotification*) {
                    weakSelf.link.paused = NO;
                    gc_product_refresh();
                    want_frames();
                }];
}

- (void)dealloc {
    [[NSNotificationCenter defaultCenter] removeObserver:self];
    [self.recoveryTimer invalidate];
    [self.link invalidate];
    gc_product_stop();
    destroy_voice_over(_voiceOver);
}

- (void)tick:(CADisplayLink*)sender {
    (void)sender;
    if (!g_shell || !g_render) return;
    if (!g_frame_demand.consume(g_shell->wants_frame())) { rest_rate(); return; }
    g_shell->build_frame();
    [self syncEditor];
    gui::AccessSnapshot accessible;
    for (const auto& field : g_shell->accessible_ui()) {
        gui::AccessNode node;
        node.id = field.id; node.role = field.role; node.label = field.label;
        node.value = field.value; node.bounds = field.bounds; node.depth = field.depth;
        node.focused = field.focused; node.selected = field.selected; node.disabled = field.disabled;
        if (node.focused) accessible.focus = node.id;
        accessible.nodes.push_back(std::move(node));
    }
    update_voice_over(_voiceOver, accessible, self.canvas.contentScaleFactor, self.canvas.bounds.size.height * self.canvas.contentScaleFactor);

    FrameData fd;
    fd.static_lines   = &g_shell->static_lines();
    fd.static_arcs    = &g_shell->static_arcs();
    fd.canvas_arcs    = &g_shell->canvas_arcs();
    fd.static_version = g_shell->static_lines_version();
    fd.canvas_lines  = &g_shell->canvas_lines();
    fd.canvas_points = &g_shell->canvas_points();
    fd.sketch_lines  = g_shell->sketch_line_count();
    fd.ui            = &g_shell->ui();
    fd.text          = &g_shell->text();
    fd.tris          = &g_shell->tris();
    fd.images = &g_shell->images();
    fd.layers        = &g_shell->ui_layers();
    fd.mesh          = &g_shell->mesh();
            fd.mesh_version  = g_shell->mesh_version();
    fd.mesh_items    = &g_shell->scene().items();
    const auto& preview_modeller = static_cast<const Shell&>(*g_shell).modeller();
    const auto gpu_preview = preview_modeller.gpu_preview();
    const auto preview_range = preview_modeller.preview_range();
    fd.preview_first = preview_range.first;
    fd.preview_count = preview_range.second;
    fd.preview_cut = preview_modeller.preview().cut;
    fd.preview_gpu = gpu_preview.tool.empty() ? nullptr : &gpu_preview;
    fd.uniforms = g_shell->frame_uniforms();

    g_render->set_msaa(g_shell->graphics_msaa());
    g_render->draw(fd);
}

- (BOOL)canBecomeFirstResponder { return YES; }

- (void)pressesBegan:(NSSet<UIPress*>*)presses withEvent:(UIPressesEvent*)event {
    want_frames();
    bool handled = false;
    for (UIPress* p in presses) {
        UIKey* key = nil;
        if (@available(iOS 13.4, *)) key = p.key;
        if (!key) continue;
        handled = true;
        if (@available(iOS 13.4, *)) {
            const UIKeyModifierFlags m = key.modifierFlags;
            g_shell->set_modifiers((m & UIKeyModifierShift) != 0,
                                   (m & UIKeyModifierControl) != 0,
                                   (m & UIKeyModifierAlternate) != 0,
                                   (m & UIKeyModifierCommand) != 0);
        }
        // By key code: iPadOS spells escape as the string "UIKeyInputEscape".
        bool special = true;
        if (@available(iOS 13.4, *)) {
            switch (key.keyCode) {
                case UIKeyboardHIDUsageKeyboardReturnOrEnter:
                case UIKeyboardHIDUsageKeypadEnter:          g_shell->key_special(0); break;
                case UIKeyboardHIDUsageKeyboardEscape:       g_shell->key_special(1); break;
                case UIKeyboardHIDUsageKeyboardDeleteOrBackspace: g_shell->key_special(2); break;
                case UIKeyboardHIDUsageKeyboardTab:          g_shell->key_special(3); break;
                default: special = false; break;
            }
        } else {
            special = false;
        }
        if (!special) {
            const std::string chars = key.charactersIgnoringModifiers.UTF8String
                                          ? key.charactersIgnoringModifiers.UTF8String
                                          : "";
            bool chord = false, shift = false;
            if (@available(iOS 13.4, *)) {
                chord = (key.modifierFlags & (UIKeyModifierCommand | UIKeyModifierControl)) != 0;
                shift = (key.modifierFlags & UIKeyModifierShift) != 0;
            }
            if (chord) {
                const char c = chars.size() == 1 ? char(std::tolower(static_cast<unsigned char>(chars[0]))) : 0;
                if (c == 'z') g_shell->command(shift ? "REDO" : "UNDO");
                else if (c == '3') g_shell->toggle_lens();
                else if (c == 'c') g_shell->copy_selection();
                else if (c == 'v') {
                    NSString* pasted = UIPasteboard.generalPasteboard.string;
                    if (pasted.length) {
                        std::string text = pasted.UTF8String;
                        for (char& ch : text)
                            if (ch == '\n' || ch == '\r' || ch == '\t') ch = ' ';
                        g_shell->key_char(text);
                    }
                }
            } else if (!chars.empty() && chars.rfind("UIKeyInput", 0) != 0)
                g_shell->key_char(chars);
        }
    }
    if (!handled) [super pressesBegan:presses withEvent:event];
}

@end

@interface AppDelegate : UIResponder <UIApplicationDelegate>
@property(nonatomic, strong) UIWindow* window;
@end

@implementation AppDelegate

- (BOOL)application:(UIApplication*)application
    didFinishLaunchingWithOptions:(NSDictionary*)options {
    (void)application;
    (void)options;
    return YES;
}

- (UISceneConfiguration*)application:(UIApplication*)application
    configurationForConnectingSceneSession:(UISceneSession*)session
                                   options:(UISceneConnectionOptions*)options {
    (void)application;
    (void)options;
    UISceneConfiguration* cfg =
        [[UISceneConfiguration alloc] initWithName:@"Default"
                                       sessionRole:session.role];
    // Fallback for a stale plist with no scene manifest.
    cfg.delegateClass = NSClassFromString(@"GLCADSceneDelegate");
    return cfg;
}

@end

@interface GLCADSceneDelegate : UIResponder <UIWindowSceneDelegate>
@property(nonatomic, strong) UIWindow* window;
@end

@implementation GLCADSceneDelegate

- (void)scene:(UIScene*)scene
    willConnectToSession:(UISceneSession*)session
                 options:(UISceneConnectionOptions*)options {
    (void)session;
    (void)options;
    UIWindowScene* ws = (UIWindowScene*)scene;

    const CFAbsoluteTime t0 = CFAbsoluteTimeGetCurrent();
    static Shell shell;
    g_shell = &shell;
    std::string error;
    if (!shell.load_font(&error)) {
        NSLog(@"glcad: %s", error.c_str());
    }
    NSLog(@"glcad: font atlas in %.0f ms", (CFAbsoluteTimeGetCurrent() - t0) * 1000.0);

    self.window = [[UIWindow alloc] initWithWindowScene:ws];
    CanvasController* c = [[CanvasController alloc] init];
    self.window.rootViewController = c;
    [self.window makeKeyAndVisible];
    [c becomeFirstResponder];

    CanvasView* view = c.canvas;
    CAMetalLayer* layer = (CAMetalLayer*)view.layer;
    [view layoutSubviews];

    const CFAbsoluteTime t1 = CFAbsoluteTimeGetCurrent();
    static Renderer renderer;
    if (!renderer.init(Renderer::SurfaceHandle{(__bridge void*)layer},
                       (uint32_t)layer.drawableSize.width,
                       (uint32_t)layer.drawableSize.height, error)) {
        NSLog(@"glcad: the renderer did not start: %s", error.c_str());
        return;
    }
    if (!renderer.set_font_atlas(shell.font().atlas(), shell.font().atlas_width(),
                                 shell.font().atlas_height(), error)) {
        NSLog(@"glcad: the font atlas did not upload: %s", error.c_str());
    }
    NSLog(@"glcad: renderer up in %.0f ms", (CFAbsoluteTimeGetCurrent() - t1) * 1000.0);
    g_render = &renderer;
    shell.set_dpr(UIScreen.mainScreen.nativeScale * kPadUiScale);
    shell.scale_ui(kPadTextBoost);
    shell.set_tablet_mode(true);
    shell.load_app_config();
    shell.enable_project_ask();
    shell.clipboard_set_hook = [](const std::string& text) {
        [UIPasteboard generalPasteboard].string =
            [NSString stringWithUTF8String:text.c_str()];
    };
    __weak CanvasController* weakController = c;
    shell.set_file_hook([weakController](Shell::FileAsk kind, std::function<void(std::string)> done) {
        CanvasController* controller = weakController;
        if (controller) [controller askFile:kind completion:std::move(done)];
        else done({});
    });
    shell.set_product_hook([] { gc_product_show(); });
    gc_product_start((__bridge void*)c, [](int allowed, int paid, void* context) {
        (void)paid;
        static_cast<Shell*>(context)->set_product_edit_access(allowed != 0);
        want_frames();
    }, &shell);
    gc_product_set_sample_callback([](const char* path, void* context) {
        if (!g_shell) return;
        CanvasController* controller = (__bridge CanvasController*)context;
        if (!path || !*path) { g_shell->new_tab(); want_frames(); return; }
        NSURL* source = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path]];
        NSURL* local = [controller copyIncomingFile:source folder:@"Examples"];
        if (!local) return;
        g_shell->new_tab();
        g_shell->open_file(local.fileSystemRepresentation);
        want_frames();
    }, (__bridge void*)c);
    shell.set_viewport(layer.drawableSize.width, layer.drawableSize.height);
#ifndef NDEBUG
    if (std::getenv("GLCAD_IOS_NATIVE_SMOKE")) {
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC), dispatch_get_main_queue(), ^{
            [c runNativeSmokeChecks];
        });
    }
#endif
    for (UIOpenURLContext* context in options.URLContexts) [c openIncomingURL:context.URL];
}

- (void)scene:(UIScene*)scene openURLContexts:(NSSet<UIOpenURLContext*>*)contexts {
    (void)scene;
    CanvasController* controller = (CanvasController*)self.window.rootViewController;
    for (UIOpenURLContext* context in contexts) [controller openIncomingURL:context.URL];
}

@end

int main(int argc, char* argv[]) {
    @autoreleasepool {
#ifdef GLCAD_WITH_PGLITE
        NSString* pglite = [[[NSBundle mainBundle] bundlePath]
            stringByAppendingPathComponent:@"pglite"];
        setenv("GLCAD_PGLITE_RUNTIME", pglite.fileSystemRepresentation, 1);
#endif
        return UIApplicationMain(argc, argv, nil,
                                 NSStringFromClass([AppDelegate class]));
    }
}
