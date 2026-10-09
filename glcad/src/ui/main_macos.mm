#import <ApplicationServices/ApplicationServices.h>
#include <atomic>
#include <mutex>
#include <vector>
#import <AVFoundation/AVFoundation.h>
#import <CoreAudio/CoreAudio.h>
#import <Cocoa/Cocoa.h>
#import <PDFKit/PDFKit.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include <string>

#include "ui/shell.hpp"
#include "ui/frame_demand.hpp"
#include "ui/menus.hpp"
#include "ui/scenario.hpp"
#include <memory>
#include <chrono>
#include <cstdlib>
#include <thread>
#include "ui/renderer.hpp"

using namespace gcapp;

namespace {
Shell*    g_shell = nullptr;
Renderer* g_render = nullptr;
void (*g_render_frame)() = nullptr;
NSWindow* g_window = nil;
bool      g_running = true;
bool      g_panning = false;
}

@interface CanvasView : NSView {
    NSCursor* frameCursor_;
}
- (void)updateCursor;
- (void)refreshCursor;
@end

@interface MenuTarget : NSObject
- (void)fire:(NSMenuItem*)item;
@end
@implementation MenuTarget
- (void)fire:(NSMenuItem*)item {
    if (g_shell && [item.representedObject isKindOfClass:[NSString class]])
        g_shell->command([(NSString*)item.representedObject UTF8String]);
}
@end

static MenuTarget* g_menu_target = nil;

static void install_main_menu() {
    g_menu_target = [MenuTarget new];
    NSMenu* bar = [[NSMenu alloc] initWithTitle:@"MainMenu"];

    NSMenuItem* appItem = [[NSMenuItem alloc] initWithTitle:@"GLCAD" action:nil keyEquivalent:@""];
    NSMenu* appMenu = [[NSMenu alloc] initWithTitle:@"GLCAD"];
    NSMenuItem* about = [[NSMenuItem alloc] initWithTitle:@"About GLCAD" action:@selector(fire:) keyEquivalent:@""];
    about.target = g_menu_target;
    about.representedObject = @"ABOUT";
    [appMenu addItem:about];
    [appMenu addItem:[NSMenuItem separatorItem]];
    [appMenu addItem:[[NSMenuItem alloc] initWithTitle:@"Quit GLCAD" action:@selector(terminate:) keyEquivalent:@"q"]];
    appItem.submenu = appMenu;
    [bar addItem:appItem];

    for (const auto& m : gcapp::app_menus()) {
        if (m.title == "Help") continue;
        NSMenuItem* top = [[NSMenuItem alloc] initWithTitle:[NSString stringWithUTF8String:m.title.c_str()]
                                                     action:nil keyEquivalent:@""];
        NSMenu* menu = [[NSMenu alloc] initWithTitle:top.title];
        for (const auto& it : m.items) {
            if (it.separator()) { [menu addItem:[NSMenuItem separatorItem]]; continue; }
            NSString* key = @"";
            NSEventModifierFlags mods = NSEventModifierFlagCommand;
            if (!it.shortcut.empty()) {
                const std::size_t plus = it.shortcut.rfind('+');
                std::string k = plus == std::string::npos ? it.shortcut : it.shortcut.substr(plus + 1);
                const bool shift = it.shortcut.find("shift") != std::string::npos;
                if (!shift) for (char& c : k) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                else mods |= NSEventModifierFlagShift;
                key = [NSString stringWithUTF8String:k.c_str()];
            }
            NSMenuItem* mi = [[NSMenuItem alloc] initWithTitle:[NSString stringWithUTF8String:it.label.c_str()]
                                                        action:@selector(fire:) keyEquivalent:key];
            if (!it.shortcut.empty()) mi.keyEquivalentModifierMask = mods;
            mi.target = g_menu_target;
            mi.representedObject = [NSString stringWithUTF8String:it.command.c_str()];
            [menu addItem:mi];
        }
        top.submenu = menu;
        [bar addItem:top];
    }
    [NSApp setMainMenu:bar];
}

@interface FilterPopup : NSObject
@property (strong) NSSavePanel* panel;
@property (strong) NSPopUpButton* popup;
@property (assign) std::vector<gcapp::Shell::FileFilter>* filters;
- (void)changed:(id)sender;
@end
@implementation FilterPopup
- (void)changed:(id)sender {
    const NSInteger i = self.popup.indexOfSelectedItem;
    if (!self.filters || i < 0 || i >= (NSInteger)self.filters->size()) return;
    NSMutableArray<NSString*>* types = [NSMutableArray array];
    for (const auto& e : (*self.filters)[(std::size_t)i].extensions)
        [types addObject:[NSString stringWithUTF8String:e.c_str()]];
    self.panel.allowedFileTypes = types;
}
@end

static void install_file_hook(gcapp::Shell& shell) {
    shell.set_print_hook([](const std::string& path, const std::string& printer, std::string& error) {
        @autoreleasepool {
            NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]];
            PDFDocument* document = [[PDFDocument alloc] initWithURL:url];
            if (!document) { error = "PRINT: cannot open prepared PDF"; return false; }
            NSPrintInfo* info = [[NSPrintInfo sharedPrintInfo] copy];
            if (!printer.empty()) {
                NSPrinter* selected = [NSPrinter printerWithName:[NSString stringWithUTF8String:printer.c_str()]];
                if (!selected) { error = "PRINT: printer not found: " + printer; return false; }
                info.printer = selected;
            }
            const NSUInteger count=document.pageCount;
            if(count==0) {error="PRINT: document is empty";return false;}
            const NSSize firstSize=[[document pageAtIndex:0] boundsForBox:kPDFDisplayBoxMediaBox].size;
            info.paperSize=firstSize;info.scalingFactor=1;
            info.dictionary[NSPrintAllPages]=@YES;
            info.dictionary[NSPrintFirstPage]=@1;info.dictionary[NSPrintLastPage]=@(count);
            NSPrintPanel* panel=[NSPrintPanel printPanel];
            panel.options=(panel.options|NSPrintPanelShowsCopies|NSPrintPanelShowsPageRange)&~NSPrintPanelShowsScaling;
            if([panel runModalWithPrintInfo:info]!=NSModalResponseOK) return false;
            NSUInteger first=1,last=count;
            if(![info.dictionary[NSPrintAllPages] boolValue]) {
                first=[info.dictionary[NSPrintFirstPage] unsignedIntegerValue];last=[info.dictionary[NSPrintLastPage] unsignedIntegerValue];
            }
            if(first<1||last<first||last>count) {error="PRINT: invalid page range";return false;}
            if([info.jobDisposition isEqualToString:NSPrintSaveJob]) {
                PDFDocument* output=[[PDFDocument alloc] init];
                for(NSUInteger i=first-1;i<last;++i) [output insertPage:[[document pageAtIndex:i] copy] atIndex:output.pageCount];
                NSURL* target=info.dictionary[NSPrintJobSavingURL];NSError* problem=nil;
                if(!target.isFileURL || ![[output dataRepresentation] writeToURL:target options:NSDataWritingAtomic error:&problem]) {
                    error="PRINT: could not save PDF";return false;
                }
                error="saved print PDF";return true;
            }
            if(![info.jobDisposition isEqualToString:NSPrintSpoolJob]) {error="Plot preview remains open";return false;}
            NSMutableArray<PDFDocument*>* groups=[NSMutableArray array];
            NSMutableArray<NSPrintInfo*>* settings=[NSMutableArray array];
            NSSize previous=NSZeroSize;
            for(NSUInteger i=first-1;i<last;++i) {
                PDFPage* page=[document pageAtIndex:i];const NSSize size=[page boundsForBox:kPDFDisplayBoxMediaBox].size;
                if(groups.count==0 || std::abs(previous.width-size.width)>.01 || std::abs(previous.height-size.height)>.01) {
                    NSPrintInfo* groupInfo=[info copy];groupInfo.scalingFactor=1;
                    groupInfo.orientation=NSPaperOrientationPortrait;
                    groupInfo.paperSize=NSMakeSize(std::min(size.width,size.height),std::max(size.width,size.height));
                    groupInfo.orientation=size.width>size.height?NSPaperOrientationLandscape:NSPaperOrientationPortrait;
                    groupInfo.leftMargin=groupInfo.rightMargin=groupInfo.topMargin=groupInfo.bottomMargin=0;
                    groupInfo.horizontallyCentered=groupInfo.verticallyCentered=NO;
                    groupInfo.dictionary[NSPrintAllPages]=@YES;groupInfo.dictionary[NSPrintCopies]=@1;
                    PMPageFormat format=(PMPageFormat)[groupInfo PMPageFormat];Boolean changed=false;PMRect paper{};
                    if(PMSessionValidatePageFormat((PMPrintSession)[groupInfo PMPrintSession],format,&changed)!=noErr ||
                       PMGetAdjustedPaperRect(format,&paper)!=noErr || std::abs(paper.right-paper.left-size.width)>2 ||
                       std::abs(paper.bottom-paper.top-size.height)>2) {
                        error="PRINT: printer cannot match page "+std::to_string(i+1)+" paper dimensions";return false;
                    }
                    [groupInfo updateFromPMPageFormat];[settings addObject:groupInfo];
                    [groups addObject:[[PDFDocument alloc] init]];previous=size;
                }
                PDFDocument* group=groups.lastObject;[group insertPage:[page copy] atIndex:group.pageCount];
            }
            const NSInteger copies=std::max<NSInteger>(1,[info.dictionary[NSPrintCopies] integerValue]);
            NSUInteger submitted=0;
            for(NSInteger copy=0;copy<copies;++copy)for(NSUInteger g=0;g<groups.count;++g) {
                NSPrintOperation* operation=[groups[g] printOperationForPrintInfo:settings[g] scalingMode:kPDFPrintPageScaleNone autoRotate:NO];
                if(!operation) {error="PRINT: cannot create print operation";return false;}
                operation.showsPrintPanel=NO;operation.showsProgressPanel=YES;
                if(![operation runOperation]) {error="PRINT: stopped after "+std::to_string(submitted)+" submitted page(s)";return false;}
                submitted+=groups[g].pageCount;
            }
            error="submitted "+std::to_string(submitted)+" page(s)";return true;
        }
    });
    shell.set_file_hook([](gcapp::Shell::FileAsk kind, std::function<void(std::string)> done) {
        auto filters = std::make_shared<std::vector<gcapp::Shell::FileFilter>>(gcapp::Shell::file_filters(kind));
        const bool reading = kind == gcapp::Shell::FileAsk::Open || kind == gcapp::Shell::FileAsk::Import ||
                             kind == gcapp::Shell::FileAsk::Attach || kind == gcapp::Shell::FileAsk::LoadLibrary;
        NSSavePanel* panel = reading ? (NSSavePanel*)[NSOpenPanel openPanel] : [NSSavePanel savePanel];
        if (reading) {
            NSOpenPanel* op = (NSOpenPanel*)panel;
            op.canChooseDirectories = kind == gcapp::Shell::FileAsk::LoadLibrary;
            op.allowsMultipleSelection = NO;
        } else {
            panel.nameFieldStringValue = kind == gcapp::Shell::FileAsk::Save ? @"drawing.glcad" :
                kind == gcapp::Shell::FileAsk::SaveLibrary ? @"symbols.gllib" :
                kind == gcapp::Shell::FileAsk::ExportPdf ? @"drawing.pdf" : @"drawing.dxf";
        }

        NSPopUpButton* popup = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(0, 0, 260, 26) pullsDown:NO];
        for (const auto& f : *filters) {
            std::string label = f.label + " (";
            for (std::size_t i = 0; i < f.extensions.size(); ++i)
                label += (i ? ", ." : ".") + f.extensions[i];
            label += ")";
            [popup addItemWithTitle:[NSString stringWithUTF8String:label.c_str()]];
        }
        FilterPopup* fp = [FilterPopup new];
        fp.panel = panel;
        fp.popup = popup;
        fp.filters = filters.get();
        popup.target = fp;
        popup.action = @selector(changed:);

        NSView* accessory = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 360, 40)];
        NSTextField* label = [NSTextField labelWithString:@"Show:"];
        label.frame = NSMakeRect(8, 10, 48, 20);
        popup.frame = NSMakeRect(60, 7, 290, 26);
        [accessory addSubview:label];
        [accessory addSubview:popup];
        panel.accessoryView = accessory;
        [fp changed:nil];

        if ([panel runModal] == NSModalResponseOK && panel.URL)
            done([panel.URL.path UTF8String]);
        (void)fp;
    });
}

@implementation CanvasView

- (BOOL)wantsUpdateLayer { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)isFlipped { return YES; }

- (CALayer*)makeBackingLayer {
    CAMetalLayer* l = [CAMetalLayer layer];
    l.device = MTLCreateSystemDefaultDevice();
    l.pixelFormat = MTLPixelFormatBGRA8Unorm;
    l.framebufferOnly = YES;
    return l;
}

- (void)viewDidChangeBackingProperties {
    [super viewDidChangeBackingProperties];
    [self updateDrawableSize];
}

- (void)updateDrawableSize {
    CAMetalLayer* l = (CAMetalLayer*)self.layer;
    const CGFloat s = self.window.backingScaleFactor ?: 2.0;
    const CGSize sz = CGSizeMake(self.bounds.size.width * s,
                                 self.bounds.size.height * s);
    l.contentsScale = s;
    l.drawableSize = sz;
    if (g_render) g_render->resize((uint32_t)sz.width, (uint32_t)sz.height);
    if (g_shell) {
        g_shell->set_dpr(s);
        g_shell->set_viewport(sz.width, sz.height);
    }

    // AppKit's live-resize loop stalls the event loop; draw here or it freezes.
    if (g_render_frame) g_render_frame();
}

- (void)setFrameSize:(NSSize)s {
    [super setFrameSize:s];
    [self updateDrawableSize];
}

- (void)pointFor:(NSEvent*)e x:(double*)px y:(double*)py {
    const NSPoint p = [self convertPoint:e.locationInWindow fromView:nil];
    const CGFloat s = self.window.backingScaleFactor ?: 2.0;
    *px = p.x * s;
    *py = p.y * s;
}

- (void)syncModifiers:(NSEvent*)e {
    const NSEventModifierFlags f = e.modifierFlags;
    g_shell->set_modifiers((f & NSEventModifierFlagShift) != 0,
                           (f & NSEventModifierFlagControl) != 0,
                           (f & NSEventModifierFlagOption) != 0,
                           (f & NSEventModifierFlagCommand) != 0);
}

- (void)mouseDown:(NSEvent*)e {
    double x, y; [self pointFor:e x:&x y:&y];
    [self syncModifiers:e];
    g_shell->pointer_down(x, y);
    [self refreshCursor];
}
static NSCursor* shell_cursor() {
    if (!g_shell) return [NSCursor arrowCursor];
    switch (g_shell->cursor()) {
        case gcapp::Shell::CursorKind::Pick:  return [NSCursor pointingHandCursor];
        case gcapp::Shell::CursorKind::Move:  return [NSCursor openHandCursor];
        case gcapp::Shell::CursorKind::Grab:  return [NSCursor closedHandCursor];
        case gcapp::Shell::CursorKind::Cross: return [NSCursor crosshairCursor];
        case gcapp::Shell::CursorKind::Text:  return [NSCursor IBeamCursor];
        default:                            return [NSCursor arrowCursor];
    }
}

- (void)updateCursor {
    NSCursor* next = shell_cursor();
    if (frameCursor_ != next) {
        frameCursor_ = next;
        [self.window invalidateCursorRectsForView:self];
    }
    [self refreshCursor];
}
- (void)resetCursorRects {
    [self addCursorRect:self.visibleRect cursor:frameCursor_ ?: [NSCursor arrowCursor]];
}
- (void)refreshCursor {
    if (!g_shell || !self.window.isKeyWindow || self.window.attachedSheet) return;
    const NSPoint p = [self convertPoint:self.window.mouseLocationOutsideOfEventStream fromView:nil];
    if (NSPointInRect(p, self.visibleRect)) [frameCursor_ ?: [NSCursor arrowCursor] set];
}
- (void)cursorUpdate:(NSEvent*)e {
    // Must not enqueue another hover; that would invalidate this cursor.
    (void)e;
    [self refreshCursor];
}
- (void)mouseEntered:(NSEvent*)e {
    if (g_shell) {
        double x, y; [self pointFor:e x:&x y:&y];
        g_shell->pointer_move(x, y);
    }
    [self refreshCursor];
}
- (void)mouseExited:(NSEvent*)e {
    (void)e;
}

- (void)mouseUp:(NSEvent*)e {
    double x, y; [self pointFor:e x:&x y:&y];
    g_shell->pointer_up(x, y);
    [self refreshCursor];
}
- (void)mouseMoved:(NSEvent*)e {
    double x, y; [self pointFor:e x:&x y:&y];
    g_shell->pointer_move(x, y);
    [self refreshCursor];
}
- (void)mouseDragged:(NSEvent*)e {
    double x, y; [self pointFor:e x:&x y:&y];
    g_shell->pointer_move(x, y);
    [self refreshCursor];
}

- (void)otherMouseDown:(NSEvent*)e {
    if (e.buttonNumber != 2) return;
    [self syncModifiers:e];
    double x, y; [self pointFor:e x:&x y:&y];
    g_panning = true;
    g_shell->middle_down(x, y);
}
- (void)otherMouseUp:(NSEvent*)e {
    if (!g_panning) return;
    g_panning = false;
    double x, y; [self pointFor:e x:&x y:&y];
    g_shell->middle_up(x, y);
}
- (void)otherMouseDragged:(NSEvent*)e {
    if (!g_panning) return;
    [self syncModifiers:e];
    double x, y; [self pointFor:e x:&x y:&y];
    g_shell->pointer_move(x, y);
}
- (void)rightMouseDown:(NSEvent*)e {
    [self syncModifiers:e];
    double x, y; [self pointFor:e x:&x y:&y];
    g_shell->right_down(x, y);
}
- (void)rightMouseDragged:(NSEvent*)e {
    [self syncModifiers:e];
    double x, y; [self pointFor:e x:&x y:&y];
    g_shell->pointer_move(x, y);
}
- (void)rightMouseUp:(NSEvent*)e {
    double x, y; [self pointFor:e x:&x y:&y];
    g_shell->right_up(x, y);
}

- (void)scrollWheel:(NSEvent*)e {
    double x, y; [self pointFor:e x:&x y:&y];
    const CGFloat s = self.window.backingScaleFactor ?: 2.0;
    g_shell->pointer_move(x, y);
    g_shell->scroll(e.scrollingDeltaX * s, e.scrollingDeltaY * s,
                    e.hasPreciseScrollingDeltas &&
                        !(e.modifierFlags & NSEventModifierFlagCommand));
}

- (void)magnifyWithEvent:(NSEvent*)e {
    double x, y; [self pointFor:e x:&x y:&y];
    g_shell->pinch(x, y, e.magnification);
}

- (void)keyDown:(NSEvent*)e {
    [self syncModifiers:e];
    NSString* c = e.charactersIgnoringModifiers;
    if (c.length == 0) return;
    const unichar k = [c characterAtIndex:0];
    const bool cmd = (e.modifierFlags & NSEventModifierFlagCommand) != 0;

    if (k == 27)   { g_shell->key_special(1); return; }              // Esc
    if (k == 13 || k == 3) { g_shell->key_special(0); return; }      // Enter
    if (k == 127)  { g_shell->key_special(2); return; }              // Backspace
    if (k == NSDeleteFunctionKey) { g_shell->key_special(4); return; }
    if (k == 9)    { g_shell->key_special(3); return; }              // Tab

    if (k == NSLeftArrowFunctionKey)  { g_shell->key_special(cmd ? 9 : 5); return; }
    if (k == NSRightArrowFunctionKey) { g_shell->key_special(cmd ? 10 : 6); return; }
    if (k == NSUpArrowFunctionKey)    { g_shell->key_special(7); return; }
    if (k == NSDownArrowFunctionKey)  { g_shell->key_special(8); return; }
    if (k == NSHomeFunctionKey)       { g_shell->key_special(9); return; }
    if (k == NSEndFunctionKey)        { g_shell->key_special(10); return; }

    const bool shift = (e.modifierFlags & NSEventModifierFlagShift) != 0;
    if (cmd && (k == 'z' || k == 'Z')) { g_shell->command(shift ? "REDO" : "UNDO"); return; }
    if (cmd && (k == '3')) { g_shell->toggle_lens(); return; }

    if (cmd && (k == 'v' || k == 'V')) {
        NSString* s = [[NSPasteboard generalPasteboard] stringForType:NSPasteboardTypeString];
        if (s.length) {
            std::string text = [s UTF8String];
            for (char& ch : text)
                if (ch == '\n' || ch == '\r' || ch == '\t') ch = ' ';
            g_shell->key_char(text);
        }
        return;
    }
    if (cmd && (k == 'c' || k == 'C')) { g_shell->copy_selection(); return; }
    if (cmd) return;

    if (k == NSF8FunctionKey)  { g_shell->canvas().toggle_ortho(); return; }
    if (k == NSF10FunctionKey) { g_shell->canvas().toggle_polar(); return; }

    if (k >= 32 && k < 127)
        g_shell->key_char(std::string(1, static_cast<char>(k)));
}
@end

@interface AppDelegate : NSObject <NSApplicationDelegate, NSWindowDelegate>
@end
@implementation AppDelegate
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)a { return YES; }
- (void)windowWillClose:(NSNotification*)n { g_running = false; }
@end

namespace gcapp {

int run(int argc, char** argv) {
    @autoreleasepool {
        // Use only our ICD: a system MoltenVK loads duplicate Objective-C classes.
        if (!std::getenv("VK_DRIVER_FILES") && !std::getenv("VK_ICD_FILENAMES")) {
            NSString* driver = [[NSBundle mainBundle] pathForResource:@"MoltenVK_icd"
                ofType:@"json" inDirectory:@"vulkan/icd.d"];
            if (driver && setenv("VK_DRIVER_FILES", [driver fileSystemRepresentation], 1) != 0) {
                NSLog(@"GLCAD: could not select the bundled Vulkan driver");
                return 1;
            }
        }
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        AppDelegate* del = [[AppDelegate alloc] init];
        [NSApp setDelegate:del];

        const NSRect frame = NSMakeRect(0, 0, 1600, 1000);
        g_window = [[NSWindow alloc]
            initWithContentRect:frame
                      styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                 NSWindowStyleMaskResizable |
                                 NSWindowStyleMaskMiniaturizable)
                        backing:NSBackingStoreBuffered
                          defer:NO];
        [g_window setTitle:@"GLCAD"];
        [g_window setDelegate:del];
        [g_window center];

        CanvasView* view = [[CanvasView alloc] initWithFrame:frame];
        [view setWantsLayer:YES];
        [g_window setContentView:view];
        [g_window makeFirstResponder:view];

        NSTrackingArea* ta = [[NSTrackingArea alloc]
            initWithRect:frame
                 options:(NSTrackingActiveInKeyWindow | NSTrackingInVisibleRect |
                          NSTrackingMouseMoved | NSTrackingMouseEnteredAndExited |
                          NSTrackingCursorUpdate)
                   owner:view
                userInfo:nil];
        [view addTrackingArea:ta];

        Shell shell;
        Renderer renderer;
        g_shell = &shell;
        g_render = &renderer;

        [view updateDrawableSize];
        CAMetalLayer* layer = (CAMetalLayer*)view.layer;

        std::string err;
        if (!renderer.init(Renderer::SurfaceHandle{(__bridge void*)layer},
                           (uint32_t)layer.drawableSize.width,
                           (uint32_t)layer.drawableSize.height, err)) {
            NSLog(@"GLCAD: renderer init failed: %s", err.c_str());
            return 1;
        }
        if (!shell.load_font(&err))
            NSLog(@"GLCAD: font load failed: %s", err.c_str());
        else if (!renderer.set_font_atlas(shell.font().atlas(),
                                          shell.font().atlas_width(),
                                          shell.font().atlas_height(), err))
            NSLog(@"GLCAD: atlas upload failed: %s", err.c_str());

        shell.set_native_menus(true);
        install_main_menu();
        install_file_hook(shell);
        shell.set_dpr(view.window.backingScaleFactor ?: 2.0);
        shell.scale_ui(1.6);
        shell.set_viewport(layer.drawableSize.width, layer.drawableSize.height);

        shell.load_app_config();
        open_from_argv(shell, argc, argv);
        shell.enable_project_ask();

        {
            std::vector<std::string> outs, ins;
            AudioObjectPropertyAddress addr{kAudioHardwarePropertyDevices,
                                            kAudioObjectPropertyScopeGlobal,
                                            kAudioObjectPropertyElementMain};
            UInt32 size = 0;
            if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &addr, 0, nullptr, &size) == noErr) {
                std::vector<AudioObjectID> devices(size / sizeof(AudioObjectID));
                if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &addr, 0, nullptr, &size,
                                               devices.data()) == noErr) {
                    for (AudioObjectID dev : devices) {
                        CFStringRef name = nullptr;
                        UInt32 nsz = sizeof(name);
                        AudioObjectPropertyAddress na{kAudioObjectPropertyName,
                                                      kAudioObjectPropertyScopeGlobal,
                                                      kAudioObjectPropertyElementMain};
                        if (AudioObjectGetPropertyData(dev, &na, 0, nullptr, &nsz, &name) != noErr || !name)
                            continue;
                        char buf[256] = {};
                        CFStringGetCString(name, buf, sizeof(buf), kCFStringEncodingUTF8);
                        CFRelease(name);
                        const auto stream_count = [dev](AudioObjectPropertyScope scope) {
                            AudioObjectPropertyAddress sa{kAudioDevicePropertyStreams, scope,
                                                          kAudioObjectPropertyElementMain};
                            UInt32 ssz = 0;
                            if (AudioObjectGetPropertyDataSize(dev, &sa, 0, nullptr, &ssz) != noErr) return 0u;
                            return unsigned(ssz / sizeof(AudioStreamID));
                        };
                        if (stream_count(kAudioObjectPropertyScopeOutput) > 0) outs.push_back(buf);
                        if (stream_count(kAudioObjectPropertyScopeInput) > 0) ins.push_back(buf);
                    }
                }
            }
            shell.set_audio_devices(std::move(outs), std::move(ins));
        }

        // Samples live in a mutex-guarded C++ buffer; a generation flag drops late taps.
        static AVAudioEngine* g_mic_engine = nil;
        static std::mutex g_mic_mutex;
        static std::vector<int16_t> g_mic_samples;
        static std::atomic<bool> g_mic_on{false};
        static double g_mic_rate = 16000.0;
        static std::atomic<float> g_mic_level{0.0f};
        shell.audio_record_begin = []() -> bool {
            @try {
                const AVAuthorizationStatus st =
                    [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio];
                if (st == AVAuthorizationStatusNotDetermined)
                    [AVCaptureDevice requestAccessForMediaType:AVMediaTypeAudio
                                            completionHandler:^(BOOL){}];
                else if (st == AVAuthorizationStatusDenied ||
                         st == AVAuthorizationStatusRestricted) {
                    if (g_shell) g_shell->log_line("microphone access is off -- enable it in "
                                                   "System Settings > Privacy > Microphone");
                    return false;
                }
                g_mic_engine = [[AVAudioEngine alloc] init];
                AVAudioInputNode* input = g_mic_engine.inputNode;
                AVAudioFormat* fmt = [input outputFormatForBus:0];
                if (fmt.sampleRate <= 0) return false;
                g_mic_rate = fmt.sampleRate;
                {
                    std::lock_guard<std::mutex> lock(g_mic_mutex);
                    g_mic_samples.clear();
                }
                g_mic_on = true;
                [input installTapOnBus:0 bufferSize:4096 format:fmt
                                 block:^(AVAudioPCMBuffer* buf, AVAudioTime*) {
                    if (!g_mic_on.load() || !buf.floatChannelData) return;
                    const float* ch = buf.floatChannelData[0];
                    const AVAudioFrameCount n = buf.frameLength;
                    if (!n) return;
                    std::vector<int16_t> chunk(n);
                    float acc = 0.0f;
                    for (AVAudioFrameCount i = 0; i < n; ++i) {
                        const float v = std::max(-1.0f, std::min(1.0f, ch[i]));
                        acc += v * v;
                        chunk[i] = int16_t(v * 32767.0f);
                    }
                    g_mic_level = std::sqrt(acc / float(n));
                    std::lock_guard<std::mutex> lock(g_mic_mutex);
                    if (g_mic_on.load())
                        g_mic_samples.insert(g_mic_samples.end(), chunk.begin(), chunk.end());
                }];
                NSError* err = nil;
                [g_mic_engine startAndReturnError:&err];
                return err == nil;
            } @catch (NSException*) { g_mic_on = false; return false; }
        };
        shell.audio_record_level = []() -> float { return g_mic_level.load(); };
        static const auto wav_from = [](const std::vector<int16_t>& samples) {
            std::vector<gcm::u8> out;
            if (samples.size() < 1600) return out;   // < ~0.1s: a mis-tap
            const uint32_t data_len = uint32_t(samples.size() * 2);
            const uint32_t rate = uint32_t(g_mic_rate);
            const uint32_t byte_rate = rate * 2;
            const uint32_t riff_len = 36 + data_len;
            const auto put32 = [&out](uint32_t v) {
                for (int i = 0; i < 4; ++i) out.push_back(gcm::u8(v >> (8 * i)));
            };
            const auto put16 = [&out](uint16_t v) {
                out.push_back(gcm::u8(v)); out.push_back(gcm::u8(v >> 8));
            };
            out.insert(out.end(), {'R','I','F','F'}); put32(riff_len);
            out.insert(out.end(), {'W','A','V','E','f','m','t',' '});
            put32(16); put16(1); put16(1); put32(rate); put32(byte_rate);
            put16(2); put16(16);
            out.insert(out.end(), {'d','a','t','a'}); put32(data_len);
            const gcm::u8* pcm = reinterpret_cast<const gcm::u8*>(samples.data());
            out.insert(out.end(), pcm, pcm + data_len);
            return out;
        };
        shell.audio_record_end = []() -> std::vector<gcm::u8> {
            if (!g_mic_engine) return {};
            // Stop appending before teardown so a running tap block leaves the buffer.
            g_mic_on = false;
            @try {
                [g_mic_engine.inputNode removeTapOnBus:0];
                [g_mic_engine stop];
            } @catch (NSException*) {}
            g_mic_engine = nil;
            g_mic_level = 0.0f;
            std::vector<int16_t> samples;
            {
                std::lock_guard<std::mutex> lock(g_mic_mutex);
                samples.swap(g_mic_samples);
            }
            return wav_from(samples);
        };
        shell.audio_record_take = []() -> std::vector<gcm::u8> {
            if (!g_mic_engine) return {};
            std::vector<int16_t> samples;
            {
                std::lock_guard<std::mutex> lock(g_mic_mutex);
                samples.swap(g_mic_samples);
            }
            return wav_from(samples);
        };

        shell.clipboard_set_hook = [](const std::string& text) {
            NSPasteboard* pb = [NSPasteboard generalPasteboard];
            [pb clearContents];
            [pb setString:[NSString stringWithUTF8String:text.c_str()]
                  forType:NSPasteboardTypeString];
        };

        static AVAudioPlayer* g_tts_player = nil;

        // Engine state lives on the main queue; chunks arrive on the AI worker.
        static AVAudioEngine*       g_str_engine = nil;
        static AVAudioPlayerNode*   g_str_node = nil;
        static AVAudioFormat*       g_str_fmt = nil;
        static std::vector<gcm::u8> g_str_pending;
        static bool                 g_str_header_done = false;
        static double               g_str_rate = 24000.0;
        static int                  g_str_channels = 1;
        static std::atomic<int>     g_str_inflight{0};
        static std::atomic<bool>    g_str_open{false};

        static const auto str_drain = [](bool flush) {
            if (!g_str_header_done) {
                const auto& b = g_str_pending;
                std::size_t pos = 12;
                while (pos + 8 <= b.size()) {
                    const char* id = reinterpret_cast<const char*>(&b[pos]);
                    const uint32_t sz = uint32_t(b[pos + 4]) | uint32_t(b[pos + 5]) << 8 |
                                        uint32_t(b[pos + 6]) << 16 | uint32_t(b[pos + 7]) << 24;
                    if (std::memcmp(id, "fmt ", 4) == 0 && pos + 8 + 8 <= b.size()) {
                        g_str_channels = int(b[pos + 10]) | int(b[pos + 11]) << 8;
                        g_str_rate = double(uint32_t(b[pos + 12]) | uint32_t(b[pos + 13]) << 8 |
                                            uint32_t(b[pos + 14]) << 16 | uint32_t(b[pos + 15]) << 24);
                        pos += 8 + sz;
                    } else if (std::memcmp(id, "data", 4) == 0) {
                        g_str_pending.erase(g_str_pending.begin(),
                                            g_str_pending.begin() + long(pos + 8));
                        g_str_header_done = true;
                        break;
                    } else {
                        pos += 8 + sz;
                    }
                }
                if (!g_str_header_done) return;
                if (g_str_channels < 1) g_str_channels = 1;
                if (g_str_rate < 8000.0) g_str_rate = 24000.0;
                if (g_str_engine) {
                    @try { [g_str_node stop]; [g_str_engine stop]; } @catch (NSException*) {}
                    g_str_node = nil; g_str_engine = nil;
                }
                g_str_engine = [[AVAudioEngine alloc] init];
                g_str_node = [[AVAudioPlayerNode alloc] init];
                [g_str_engine attachNode:g_str_node];
                g_str_fmt = [[AVAudioFormat alloc]
                    initWithCommonFormat:AVAudioPCMFormatFloat32
                              sampleRate:g_str_rate
                                channels:AVAudioChannelCount(g_str_channels)
                             interleaved:NO];
                [g_str_engine connect:g_str_node
                                   to:g_str_engine.mainMixerNode
                               format:g_str_fmt];
                NSError* err = nil;
                if (![g_str_engine startAndReturnError:&err]) {
                    g_str_engine = nil; g_str_node = nil;
                    g_str_header_done = false;
                    return;
                }
                [g_str_node play];
            }
            if (!g_str_engine) return;
            const std::size_t frame_bytes = std::size_t(g_str_channels) * 2;
            const std::size_t frames = g_str_pending.size() / frame_bytes;
            if (!frames || (!flush && frames < 1024)) return;
            AVAudioPCMBuffer* buf =
                [[AVAudioPCMBuffer alloc] initWithPCMFormat:g_str_fmt
                                              frameCapacity:AVAudioFrameCount(frames)];
            if (!buf) return;
            buf.frameLength = AVAudioFrameCount(frames);
            const int16_t* pcm = reinterpret_cast<const int16_t*>(g_str_pending.data());
            for (int ch = 0; ch < g_str_channels; ++ch) {
                float* out = buf.floatChannelData[ch];
                for (std::size_t f = 0; f < frames; ++f)
                    out[f] = float(pcm[f * std::size_t(g_str_channels) + std::size_t(ch)]) / 32768.0f;
            }
            g_str_pending.erase(g_str_pending.begin(),
                                g_str_pending.begin() + long(frames * frame_bytes));
            ++g_str_inflight;
            [g_str_node scheduleBuffer:buf completionHandler:^{ --g_str_inflight; }];
        };

        shell.audio_stream_begin = []() -> bool {
            bool expected = false;
            if (!g_str_open.compare_exchange_strong(expected, true)) return false;
            dispatch_async(dispatch_get_main_queue(), ^{
                g_str_pending.clear();
                g_str_header_done = false;
            });
            return true;
        };
        shell.audio_stream_feed = [](const gcm::u8* p, std::size_t n) {
            NSData* d = [NSData dataWithBytes:p length:n];
            dispatch_async(dispatch_get_main_queue(), ^{
                const gcm::u8* q = static_cast<const gcm::u8*>(d.bytes);
                g_str_pending.insert(g_str_pending.end(), q, q + d.length);
                str_drain(false);
            });
        };
        shell.audio_stream_end = []() {
            dispatch_async(dispatch_get_main_queue(), ^{
                str_drain(true);
                g_str_open = false;
            });
        };

        shell.audio_playing = []() -> bool {
            if (g_tts_player != nil && g_tts_player.playing) return true;
            return g_str_open.load() || g_str_inflight.load() > 0;
        };
        shell.audio_play_hook = [](std::vector<gcm::u8> bytes, std::string) {
            NSData* data = [NSData dataWithBytes:bytes.data() length:bytes.size()];
            dispatch_async(dispatch_get_main_queue(), ^{
                NSError* err = nil;
                AVAudioPlayer* p = [[AVAudioPlayer alloc] initWithData:data error:&err];
                if (!p || err) return;
                g_tts_player = p;
                [p play];
            });
        };

        [g_window makeKeyAndOrderFront:nil];
        [NSApp activateIgnoringOtherApps:YES];

        std::string last_title;

        auto render_frame = [&] {
            shell.build_frame();
            [view updateCursor];

            FrameData fd;
            fd.static_lines   = &shell.static_lines();
            fd.static_arcs    = &shell.static_arcs();
            fd.canvas_arcs    = &shell.canvas_arcs();
            fd.static_version = shell.static_lines_version();
            fd.canvas_lines  = &shell.canvas_lines();
            fd.canvas_points = &shell.canvas_points();
            fd.sketch_lines  = shell.sketch_line_count();
            fd.ui            = &shell.ui();
            fd.text          = &shell.text();
            fd.tris          = &shell.tris();
        fd.images = &shell.images();
            fd.layers        = &shell.ui_layers();
            fd.mesh          = &shell.mesh();
            fd.mesh_version  = shell.mesh_version();
            fd.mesh_items    = &shell.scene().items();
            static gbool::PreviewState gpu_preview_cache;
            {
                const auto& preview_modeller = static_cast<const Shell&>(shell).modeller();
                gpu_preview_cache = preview_modeller.gpu_preview();
                auto [pf, pc] = preview_modeller.preview_range();
                fd.preview_first = pf;
                fd.preview_count = pc;
                fd.preview_cut   = preview_modeller.preview().cut;
                fd.preview_gpu = gpu_preview_cache.tool.empty() ? nullptr : &gpu_preview_cache;
            }
            fd.uniforms = shell.frame_uniforms();

            renderer.set_msaa(g_shell->graphics_msaa());
            renderer.draw(fd);

            const std::string t = shell.title();
            if (t != last_title) {
                last_title = t;
                [g_window setTitle:[NSString stringWithUTF8String:t.c_str()]];
            }
        };

        static auto* s_frame = &render_frame;
        g_render_frame = [] { (*s_frame)(); };

        if (const char* scenario_name = std::getenv("GLCAD_SCENARIO")) {
            ScenarioConfig cfg;
            cfg.render = render_frame;
            if (const char* v = std::getenv("GLCAD_SCENARIO_SECONDS")) cfg.seconds = std::atof(v);
            if (const char* v = std::getenv("GLCAD_SCENARIO_DISTANCE")) cfg.distance = std::atof(v);
            if (const char* v = std::getenv("GLCAD_SCENARIO_FPS")) cfg.fps = std::atoi(v);
            if (const char* v = std::getenv("GLCAD_SCENARIO_SIDE_FACE")) cfg.side_face = std::atoi(v) != 0;
            const bool ok = std::string(scenario_name) == "box-circle-extrude"
                ? run_box_circle_extrude(shell, cfg)
                : std::string(scenario_name) == "walls-zoom" ? run_walls_zoom(shell, cfg)
                : std::string(scenario_name) == "draw-inside-regions" ? run_draw_inside_regions(shell, cfg) : false;
            if (!ok) NSLog(@"GLCAD: scenario '%s' did not run clean", scenario_name);
            if (std::getenv("GLCAD_SCENARIO_QUIT") && std::atoi(std::getenv("GLCAD_SCENARIO_QUIT"))) {
                for (int i = 0; i < 20; ++i) {
                    render_frame();
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                }
                g_running = false;
            }
        }

        FrameDemand demand;
        while (g_running) {
            @autoreleasepool {
                const bool active = demand.pending(shell.wants_frame());
                NSEvent* e = [NSApp nextEventMatchingMask:NSEventMaskAny
                                                untilDate:(active ? nil
                                                                  : [NSDate dateWithTimeIntervalSinceNow:0.25])
                                                   inMode:NSDefaultRunLoopMode
                                                  dequeue:YES];
                while (e) {
                    demand.request();
                    [NSApp sendEvent:e];
                    e = [NSApp nextEventMatchingMask:NSEventMaskAny
                                           untilDate:nil
                                              inMode:NSDefaultRunLoopMode
                                             dequeue:YES];
                }
                if (demand.consume(shell.wants_frame())) {
                    render_frame();
                }
            }
        }
        renderer.shutdown();
    }
    return 0;
}

}

int main(int argc, char** argv) { return gcapp::run(argc, argv); }
