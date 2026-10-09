// First, so X11's macros are undone before anything else sees them.
#include "ui/xlib.hpp"
#include <X11/Xatom.h>
#include <X11/cursorfont.h>

#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <spawn.h>
#include <sys/wait.h>
#include <cerrno>
#include <sstream>
#include <iomanip>
#include <locale>
extern char** environ;
#include <string>

#include "ui/shell.hpp"
#include "ui/frame_demand.hpp"
#include <poll.h>
#include "ui/renderer.hpp"

using namespace gcapp;

namespace {

Shell*    g_shell = nullptr;
Renderer* g_render = nullptr;
bool      g_running = true;

double detect_scale(Display* display) {
    if (const char* s = std::getenv("GDK_SCALE")) {
        const double v = std::atof(s);
        if (v > 0.0) return v;
    }
    if (const char* s = std::getenv("QT_SCALE_FACTOR")) {
        const double v = std::atof(s);
        if (v > 0.0) return v;
    }
    if (char* resources = XResourceManagerString(display)) {
        const char* found = std::strstr(resources, "Xft.dpi:");
        if (found) {
            const double dpi = std::atof(found + 8);
            if (dpi > 0.0) return dpi / 96.0;
        }
    }
    return 1.0;
}

int special_for(KeySym key) {
    switch (key) {
        case XK_Return:
        case XK_KP_Enter:  return 0;
        case XK_Escape:    return 1;
        case XK_BackSpace: return 2;
        case XK_Tab:       return 3;
        case XK_Delete:    return 4;
        default:           return -1;
    }
}

}

int gcapp::run(int argc, char** argv) {
    Display* display = XOpenDisplay(nullptr);
    if (!display) {
        std::fprintf(stderr, "glcad: no X display\n");
        return 1;
    }

    const int screen = DefaultScreen(display);
    const double scale = detect_scale(display);
    const int width = 1440, height = 900;

    XSetWindowAttributes attributes{};
    attributes.event_mask = ExposureMask | KeyPressMask | KeyReleaseMask |
                            ButtonPressMask | ButtonReleaseMask |
                            PointerMotionMask | StructureNotifyMask |
                            FocusChangeMask;
    ::Window window = XCreateWindow(
        display, RootWindow(display, screen), 0, 0,
        static_cast<unsigned>(width * scale), static_cast<unsigned>(height * scale),
        0, CopyFromParent, InputOutput, CopyFromParent,
        CWEventMask, &attributes);

    XStoreName(display, window, "GLCAD");

    Atom wm_delete = XInternAtom(display, "WM_DELETE_WINDOW", 0);
    const Atom clip_atom = XInternAtom(display, "CLIPBOARD", 0);
    const Atom utf8_atom = XInternAtom(display, "UTF8_STRING", 0);
    const Atom targets_atom = XInternAtom(display, "TARGETS", 0);
    static Shell shell;
    static std::string g_clipboard_text;
    shell.clipboard_set_hook = [display, window, clip_atom](const std::string& text) {
        g_clipboard_text = text;
        XSetSelectionOwner(display, clip_atom, window, CurrentTime);
    };
    XSetWMProtocols(display, window, &wm_delete, 1);

    XMapWindow(display, window);
    XFlush(display);

    XIM input_method = XOpenIM(display, nullptr, nullptr, nullptr);
    XIC input_context = nullptr;
    if (input_method) {
        input_context = XCreateIC(input_method, XNInputStyle,
                                  XIMPreeditNothing | XIMStatusNothing,
                                  XNClientWindow, window, XNFocusWindow, window,
                                  nullptr);
        if (input_context) XSetICFocus(input_context);
    }

    shell.set_print_hook([](const std::string& path, const std::string& printer, std::string& error) {
        auto source=gplot::open_pdf_raster(path,error);
        if(!source) {error="PRINT: "+error;return false;}
        struct Group {int first,last;double w,h;};
        std::vector<Group> groups;
        for(int page=1;page<=source->page_count();++page) {
            const auto [w,h]=source->page_size(page,error);if(w<=0||h<=0)return false;
            if(!groups.empty() && std::abs(groups.back().w-w)<1e-6 && std::abs(groups.back().h-h)<1e-6) groups.back().last=page;
            else groups.push_back({page,page,w,h});
        }
        int submitted=0;
        for(const auto& group:groups) {
            std::ostringstream media;media.imbue(std::locale::classic());
            media<<"media=Custom."<<std::fixed<<std::setprecision(2)<<group.w*1000<<'x'<<group.h*1000<<"mm";
            std::vector<std::string> words{"lp","-t","GLCAD drawing","-o","print-scaling=none","-o",media.str(),
                "-o","page-ranges="+std::to_string(group.first)+"-"+std::to_string(group.last)};
            if(!printer.empty()){words.push_back("-d");words.push_back(printer);}
            words.push_back("--");words.push_back(path);
            std::vector<char*> args;for(auto& word:words)args.push_back(word.data());args.push_back(nullptr);
            pid_t child{};
            const int started=posix_spawnp(&child,"lp",nullptr,nullptr,args.data(),environ);
            if(started) {error="PRINT: cannot start CUPS lp after "+std::to_string(submitted)+" submitted page(s): "+std::strerror(started);return false;}
            int status{};pid_t waited;
            do {waited=waitpid(child,&status,0);}while(waited<0&&errno==EINTR);
            if(waited<0||!WIFEXITED(status)||WEXITSTATUS(status)!=0) {
                error="PRINT: CUPS rejected pages "+std::to_string(group.first)+"–"+std::to_string(group.last)+
                    " after "+std::to_string(submitted)+" submitted page(s)";return false;
            }
            submitted+=group.last-group.first+1;
        }
        error="submitted "+std::to_string(submitted)+" page(s) to "+(printer.empty()?std::string("the default printer"):printer);

        return true;
    });
    g_shell = &shell;
    std::string error;
    if (!shell.load_font(&error))
        std::fprintf(stderr, "glcad: %s\n", error.c_str());

    static Renderer renderer;
    Renderer::SurfaceHandle surface;
    surface.xlib_display = display;
    surface.xlib_window = window;
    if (!renderer.init(surface, static_cast<uint32_t>(width * scale),
                       static_cast<uint32_t>(height * scale), error)) {
        std::fprintf(stderr, "glcad: the renderer did not start: %s\n",
                     error.c_str());
        return 1;
    }
    g_render = &renderer;
    if (!renderer.set_font_atlas(shell.font().atlas(), shell.font().atlas_width(),
                                 shell.font().atlas_height(), error))
        std::fprintf(stderr, "glcad: the font atlas did not upload: %s\n",
                     error.c_str());

    shell.set_dpr(scale);
    shell.scale_ui(1.6);
    shell.set_viewport(width * scale, height * scale);
    open_from_argv(shell, argc, argv);

    std::string last_title;
    const unsigned cursor_shapes[]={XC_left_ptr,XC_hand2,XC_fleur,XC_fleur,XC_crosshair,XC_xterm};
    ::Cursor cursors[6]{};
    for (unsigned i=0;i<6;++i)cursors[i]=XCreateFontCursor(display,cursor_shapes[i]);
    unsigned current_cursor=6;
    FrameDemand demand;
    while (g_running) {
        if (!demand.pending(shell.wants_frame()) && !XPending(display)) {
            pollfd input{ConnectionNumber(display),POLLIN,0};
            poll(&input,1,250);
        }
        while (XPending(display) > 0) {
            XEvent e;
            XNextEvent(display, &e);
            demand.request();
            if (input_context && XFilterEvent(&e, window)) continue;

            switch (e.type) {
                case ClientMessage:
                    if (static_cast<Atom>(e.xclient.data.l[0]) == wm_delete)
                        g_running = false;
                    break;

                case SelectionRequest: {
                    const XSelectionRequestEvent& rq = e.xselectionrequest;
                    XSelectionEvent note{};
                    note.type = SelectionNotify;
                    note.display = rq.display;
                    note.requestor = rq.requestor;
                    note.selection = rq.selection;
                    note.target = rq.target;
                    note.time = rq.time;
                    note.property = rq.property;
                    if (rq.target == targets_atom) {
                        Atom offered[3] = {targets_atom, utf8_atom, XA_STRING};
                        XChangeProperty(display, rq.requestor, rq.property,
                                        XA_ATOM, 32, PropModeReplace,
                                        reinterpret_cast<unsigned char*>(offered), 3);
                    } else if (rq.target == utf8_atom || rq.target == XA_STRING) {
                        XChangeProperty(display, rq.requestor, rq.property,
                                        rq.target, 8, PropModeReplace,
                                        reinterpret_cast<const unsigned char*>(
                                            g_clipboard_text.data()),
                                        int(g_clipboard_text.size()));
                    } else {
                        note.property = 0L;
                    }
                    XSendEvent(display, rq.requestor, 0, 0,
                               reinterpret_cast<XEvent*>(&note));
                    break;
                }

                case ConfigureNotify: {
                    // Rebuild the drawable before the next frame or the swapchain size is wrong.
                    const uint32_t w = static_cast<uint32_t>(e.xconfigure.width);
                    const uint32_t h = static_cast<uint32_t>(e.xconfigure.height);
                    renderer.resize(w, h);
                    shell.set_dpr(scale);
                    shell.set_viewport(w, h);
                    break;
                }

                case MotionNotify:
                    shell.pointer_move(e.xmotion.x, e.xmotion.y);
                    break;

                case ButtonPress:
                    switch (e.xbutton.button) {
                        case Button1:
                            shell.pointer_down(e.xbutton.x, e.xbutton.y);
                            break;
                        case Button2:
                            shell.middle_down(e.xbutton.x, e.xbutton.y);
                            break;
                        case Button3:
                            shell.set_modifiers((e.xbutton.state & ShiftMask) != 0, (e.xbutton.state & ControlMask) != 0,
                                                (e.xbutton.state & Mod1Mask) != 0, false);
                            shell.right_down(e.xbutton.x, e.xbutton.y);
                            break;
                        // X11 delivers the wheel as buttons 4 and 5.
                        case Button4:
                            shell.scroll(0.0, 1.0, false);
                            break;
                        case Button5:
                            shell.scroll(0.0, -1.0, false);
                            break;
                        default:
                            break;
                    }
                    break;

                case ButtonRelease:
                    if (e.xbutton.button == Button1)
                        shell.pointer_up(e.xbutton.x, e.xbutton.y);
                    else if (e.xbutton.button == Button2)
                        shell.middle_up(e.xbutton.x, e.xbutton.y);
                    else if (e.xbutton.button == Button3)
                        shell.right_up(e.xbutton.x, e.xbutton.y);
                    break;

                case XlibKeyPress: {
                    const unsigned state = e.xkey.state;
                    shell.set_modifiers((state & ShiftMask) != 0,
                                        (state & ControlMask) != 0,
                                        (state & Mod1Mask) != 0,
                                        (state & Mod4Mask) != 0);

                    KeySym key = NoSymbol;
                    char buffer[64] = {};
                    int length = 0;
                    if (input_context) {
                        XlibStatus status = 0;
                        length = Xutf8LookupString(input_context, &e.xkey, buffer,
                                                   sizeof buffer - 1, &key, &status);
                        if (status == XLookupNone) break;
                    } else {
                        length = XLookupString(&e.xkey, buffer, sizeof buffer - 1,
                                               &key, nullptr);
                    }

                    if ((state & ControlMask) != 0 && (key == XK_z || key == XK_Z)) {
                        shell.command((state & ShiftMask) != 0 ? "REDO" : "UNDO");
                        break;
                    }
                    if ((state & ControlMask) != 0 && (key == XK_y || key == XK_Y)) {
                        shell.command("REDO");
                        break;
                    }
                    if ((state & ControlMask) != 0 && (key == XK_c || key == XK_C)) {
                        shell.copy_selection();
                        break;
                    }
                    const int special = special_for(key);
                    if (special >= 0) {
                        shell.key_special(special);
                    } else if (length > 0) {
                        buffer[length] = '\0';
                        shell.key_char(std::string(buffer));
                    }
                    break;
                }

                case FocusIn:
                    if (input_context) XSetICFocus(input_context);
                    break;
                case FocusOut:
                    if (input_context) XUnsetICFocus(input_context);
                    break;

                default:
                    break;
            }
        }

        if (!g_running) break;
        if (!demand.consume(shell.wants_frame())) continue;
        shell.build_frame();
        const auto cursor=static_cast<unsigned>(shell.cursor());
        if (cursor<6 && cursor!=current_cursor) {
            XDefineCursor(display,window,cursors[cursor]);
            XFlush(display);
            current_cursor=cursor;
        }

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

        renderer.draw(fd);

        const std::string title = shell.title();
        if (title != last_title) {
            last_title = title;
            XStoreName(display, window, title.c_str());
        }
    }

    renderer.shutdown();
    for (const auto cursor:cursors)if(cursor)XFreeCursor(display,cursor);
    if (input_context) XDestroyIC(input_context);
    if (input_method) XCloseIM(input_method);
    XDestroyWindow(display, window);
    XCloseDisplay(display);
    return 0;
}

int main(int argc, char** argv) { return gcapp::run(argc, argv); }
