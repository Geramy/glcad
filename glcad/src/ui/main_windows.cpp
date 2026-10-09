#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>

#include <cstdio>
#include <string>

#include "ui/shell.hpp"
#include "ui/frame_demand.hpp"
#include <commdlg.h>
#include <algorithm>
#include <cmath>
#include "ui/renderer.hpp"

using namespace gcapp;

namespace {

Shell*    g_shell = nullptr;
Renderer* g_render = nullptr;
bool      g_running = true;
double    g_scale = 1.0;
void (*g_render_frame)() = nullptr;

HCURSOR shell_cursor() {
    LPCWSTR resource = IDC_ARROW;
    if (g_shell) switch (g_shell->cursor()) {
        case Shell::CursorKind::Pick: resource=IDC_HAND;break;
        case Shell::CursorKind::Move:
        case Shell::CursorKind::Grab: resource=IDC_SIZEALL;break;
        case Shell::CursorKind::Cross: resource=IDC_CROSS;break;
        case Shell::CursorKind::Text: resource=IDC_IBEAM;break;
        default: break;
    }
    return LoadCursorW(nullptr,resource);
}

void refresh_cursor(HWND window) {
    POINT point{};RECT client{};
    if (GetForegroundWindow()!=window || !GetCursorPos(&point) || WindowFromPoint(point)!=window) return;
    ScreenToClient(window,&point);GetClientRect(window,&client);
    if (PtInRect(&client,point))SetCursor(shell_cursor());
}

std::string to_utf8(const wchar_t* text, int length) {
    if (length <= 0) return {};
    const int needed = WideCharToMultiByte(CP_UTF8, 0, text, length, nullptr, 0,
                                           nullptr, nullptr);
    if (needed <= 0) return {};
    std::string out(static_cast<std::size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, length, out.data(), needed, nullptr,
                        nullptr);
    return out;
}

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

void resized(HWND window) {
    RECT r{};
    GetClientRect(window, &r);
    const uint32_t w = static_cast<uint32_t>(r.right - r.left);
    const uint32_t h = static_cast<uint32_t>(r.bottom - r.top);
    if (w == 0 || h == 0) return;
    if (g_render) g_render->resize(w, h);
    if (g_shell) {
        g_shell->set_dpr(g_scale);
        g_shell->set_viewport(w, h);
    }
    // The modal resize loop stalls the main loop; draw here or the window freezes.
    if (g_render_frame) g_render_frame();
}

LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    switch (message) {
        case WM_SETCURSOR:
            if (LOWORD(lp)==HTCLIENT && g_shell) {SetCursor(shell_cursor());return TRUE;}
            break;
        case WM_CLOSE:
            g_running = false;
            return 0;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;

        case WM_SIZE:
        case WM_EXITSIZEMOVE:
            resized(window);
            return 0;

        case WM_DPICHANGED: {
            g_scale = static_cast<double>(HIWORD(wp)) / 96.0;
            const RECT* suggested = reinterpret_cast<const RECT*>(lp);
            SetWindowPos(window, nullptr, suggested->left, suggested->top,
                         suggested->right - suggested->left,
                         suggested->bottom - suggested->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            resized(window);
            return 0;
        }

        case WM_MOUSEMOVE:
            if (g_shell)
                g_shell->pointer_move(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
            refresh_cursor(window);
            return 0;

        case WM_LBUTTONDOWN:
            SetCapture(window);
            if (g_shell) g_shell->pointer_down(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
            return 0;

        case WM_LBUTTONUP:
            ReleaseCapture();
            if (g_shell) g_shell->pointer_up(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
            return 0;

        case WM_RBUTTONDOWN:
            if (g_shell) {
                g_shell->set_modifiers((GetKeyState(VK_SHIFT) & 0x8000) != 0, (GetKeyState(VK_CONTROL) & 0x8000) != 0,
                                       (GetKeyState(VK_MENU) & 0x8000) != 0, false);
                SetCapture(window);
                g_shell->right_down(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
            }
            return 0;

        case WM_RBUTTONUP:
            ReleaseCapture();
            if (g_shell) g_shell->right_up(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
            return 0;

        case WM_MBUTTONDOWN:
            SetCapture(window);
            if (g_shell) g_shell->middle_down(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
            return 0;

        case WM_MBUTTONUP:
            ReleaseCapture();
            if (g_shell) g_shell->middle_up(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
            return 0;

        case WM_MOUSEWHEEL:
            if (g_shell) {
                const double ticks =
                    static_cast<double>(GET_WHEEL_DELTA_WPARAM(wp)) / WHEEL_DELTA;
                g_shell->scroll(0.0, ticks, false);
            }
            return 0;

        case WM_KEYDOWN:
            if (g_shell) {
                const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                const bool ctrl  = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
                const bool alt   = (GetKeyState(VK_MENU) & 0x8000) != 0;
                const bool win   = (GetKeyState(VK_LWIN) & 0x8000) != 0;
                g_shell->set_modifiers(shift, ctrl, alt, win);

                switch (wp) {
                    case VK_RETURN: g_shell->key_special(0); return 0;
                    case VK_ESCAPE: g_shell->key_special(1); return 0;
                    case VK_BACK:   g_shell->key_special(2); return 0;
                    case VK_TAB:    g_shell->key_special(3); return 0;
                    case VK_DELETE: g_shell->key_special(4); return 0;
                    case 'Z': if (ctrl) { g_shell->command(shift ? "REDO" : "UNDO"); return 0; } break;
                    case 'Y': if (ctrl) { g_shell->command("REDO"); return 0; } break;
                    case 'C': if (ctrl) { g_shell->copy_selection(); return 0; } break;
                    case 'V':
                        if (ctrl) {
                            if (OpenClipboard(window)) {
                                if (HANDLE mem = GetClipboardData(CF_UNICODETEXT)) {
                                    if (const wchar_t* w =
                                            static_cast<const wchar_t*>(GlobalLock(mem))) {
                                        std::string text = to_utf8(w, int(wcslen(w)));
                                        GlobalUnlock(mem);
                                        for (char& ch : text)
                                            if (ch == '\n' || ch == '\r' || ch == '\t')
                                                ch = ' ';
                                        if (!text.empty()) g_shell->key_char(text);
                                    }
                                }
                                CloseClipboard();
                            }
                            return 0;
                        }
                        break;
                    default: break;
                }
            }
            return 0;

        case WM_CHAR: {
            if (!g_shell) return 0;
            const wchar_t c = static_cast<wchar_t>(wp);
            if (c < 0x20 || c == 0x7F) return 0;

            static wchar_t pending_high = 0;
            if (c >= 0xD800 && c <= 0xDBFF) {
                pending_high = c;
                return 0;
            }
            if (c >= 0xDC00 && c <= 0xDFFF && pending_high) {
                const wchar_t pair[2] = {pending_high, c};
                pending_high = 0;
                g_shell->key_char(to_utf8(pair, 2));
                return 0;
            }
            pending_high = 0;
            g_shell->key_char(to_utf8(&c, 1));
            return 0;
        }

        default:
            break;
    }
    return DefWindowProcW(window, message, wp, lp);
}

}

int gcapp::run(int argc, char** argv) {
    // Must precede the first window.
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    const HINSTANCE instance = GetModuleHandleW(nullptr);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wc.lpfnWndProc = window_proc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = L"GlcadWindow";
    if (!RegisterClassExW(&wc)) {
        std::fprintf(stderr, "glcad: could not register the window class\n");
        return 1;
    }

    const HWND window = CreateWindowExW(
        0, wc.lpszClassName, L"GLCAD", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
        CW_USEDEFAULT, 1440, 900, nullptr, nullptr, instance, nullptr);
    if (!window) {
        std::fprintf(stderr, "glcad: could not create the window\n");
        return 1;
    }
    g_scale = static_cast<double>(GetDpiForWindow(window)) / 96.0;

    static Shell shell;
    g_shell = &shell;
    std::string error;
    if (!shell.load_font(&error))
        std::fprintf(stderr, "glcad: %s\n", error.c_str());

    RECT client{};
    GetClientRect(window, &client);

    static Renderer renderer;
    Renderer::SurfaceHandle surface;
    surface.win32_instance = instance;
    surface.win32_window = window;
    if (!renderer.init(surface,
                       static_cast<uint32_t>(client.right - client.left),
                       static_cast<uint32_t>(client.bottom - client.top), error)) {
        std::fprintf(stderr, "glcad: the renderer did not start: %s\n",
                     error.c_str());
        return 1;
    }
    g_render = &renderer;
    if (!renderer.set_font_atlas(shell.font().atlas(), shell.font().atlas_width(),
                                 shell.font().atlas_height(), error))
        std::fprintf(stderr, "glcad: the font atlas did not upload: %s\n",
                     error.c_str());

    shell.clipboard_set_hook = [window](const std::string& text) {
        if (!OpenClipboard(window)) return;
        EmptyClipboard();
        const int n = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
        if (n > 0) {
            if (HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, std::size_t(n) * sizeof(wchar_t))) {
                MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1,
                                    static_cast<wchar_t*>(GlobalLock(mem)), n);
                GlobalUnlock(mem);
                SetClipboardData(CF_UNICODETEXT, mem);
            }
        }
        CloseClipboard();
    };
    shell.set_print_hook([window](const std::string& path, const std::string& printer, std::string& error) {
        if (!printer.empty()) { error = "PRINT: choose the printer in the Windows print dialog"; return false; }
        auto source=gplot::open_pdf_raster(path,error);
        if(!source) {error="PRINT: "+error;return false;}
        if(source->page_count()>65535) {error="PRINT: too many pages for the printer dialog";return false;}
        const auto [first_w,first_h]=source->page_size(1,error);
        if(first_w<=0 || first_h<=0) return false;
        PRINTDLGW dialog{};dialog.lStructSize=sizeof(dialog);dialog.hwndOwner=window;
        dialog.Flags=PD_RETURNDEFAULT;PrintDlgW(&dialog);
        struct Cleanup {
            PRINTDLGW& d;
            ~Cleanup(){if(d.hDC)DeleteDC(d.hDC);if(d.hDevMode)GlobalFree(d.hDevMode);if(d.hDevNames)GlobalFree(d.hDevNames);}
        } cleanup{dialog};
        const auto media=[](DEVMODEW& mode,double w,double h) {
            const double width=std::min(w,h)*10000,height=std::max(w,h)*10000;
            if(width<1 || height>32767 || !std::isfinite(width) || !std::isfinite(height)) return false;
            mode.dmFields|=DM_PAPERSIZE|DM_PAPERWIDTH|DM_PAPERLENGTH|DM_ORIENTATION;
            mode.dmPaperSize=DMPAPER_USER;mode.dmPaperWidth=SHORT(std::lround(width));mode.dmPaperLength=SHORT(std::lround(height));
            mode.dmOrientation=w>h?DMORIENT_LANDSCAPE:DMORIENT_PORTRAIT;return true;
        };
        if(dialog.hDevMode) if(auto* mode=static_cast<DEVMODEW*>(GlobalLock(dialog.hDevMode))) {
            media(*mode,first_w,first_h);GlobalUnlock(dialog.hDevMode);
        }
        dialog.Flags=PD_RETURNDC|PD_USEDEVMODECOPIESANDCOLLATE|PD_NOSELECTION;
        dialog.nCopies=1;dialog.nMinPage=1;dialog.nMaxPage=WORD(source->page_count());
        dialog.nFromPage=1;dialog.nToPage=dialog.nMaxPage;
        if(!PrintDlgW(&dialog)) {if(CommDlgExtendedError())error="PRINT: could not open print dialog";return false;}
        const int first=(dialog.Flags&PD_PAGENUMS)?dialog.nFromPage:1;
        const int last=(dialog.Flags&PD_PAGENUMS)?dialog.nToPage:source->page_count();
        if(first<1 || last<first || last>source->page_count()) {error="PRINT: invalid page range";return false;}
        struct Metrics {int width{},height{},dx{},dy{};};
        const auto prepare=[&](int page,Metrics& out) {
            const auto [w,h]=source->page_size(page,error);if(w<=0||h<=0)return false;
            if(!dialog.hDevMode) {error="PRINT: printer paper settings unavailable";return false;}
            auto* mode=static_cast<DEVMODEW*>(GlobalLock(dialog.hDevMode));
            if(!mode) {error="PRINT: cannot access printer settings";return false;}
            const bool valid=media(*mode,w,h);
            HDC next=valid?ResetDCW(dialog.hDC,mode):nullptr;
            GlobalUnlock(dialog.hDevMode);
            if(!next) {error="PRINT: printer cannot use the requested sheet size";return false;}
            dialog.hDC=next;out.dx=GetDeviceCaps(next,LOGPIXELSX);out.dy=GetDeviceCaps(next,LOGPIXELSY);
            const double width=w/.0254*out.dx,height=h/.0254*out.dy;
            if(out.dx<=0||out.dy<=0||width<1||height<1||width>2147480000||height>2147480000) {error="PRINT: invalid printer resolution";return false;}
            out.width=int(std::lround(width));out.height=int(std::lround(height));
            if(std::abs(GetDeviceCaps(next,PHYSICALWIDTH)-out.width)>out.dx*.08 ||
               std::abs(GetDeviceCaps(next,PHYSICALHEIGHT)-out.height)>out.dy*.08) {
                error="PRINT: printer cannot match page "+std::to_string(page)+" paper dimensions";return false;
            }
            return true;
        };
        Metrics metrics;
        for(int page=first;page<=last;++page) if(!prepare(page,metrics))return false;
        if(!prepare(first,metrics))return false;
        DOCINFOW job{};job.cbSize=sizeof(job);job.lpszDocName=L"GLCAD drawing";
        if(StartDocW(dialog.hDC,&job)<=0) {error="PRINT: cannot start print job";return false;}
        const auto abort=[&] {AbortDoc(dialog.hDC);if(error.empty())error="PRINT: printer could not render the document";return false;};
        for(int page=first;page<=last;++page) {
            if(page!=first && !prepare(page,metrics))return abort();
            if(StartPage(dialog.hDC)<=0)return abort();
            const int ox=GetDeviceCaps(dialog.hDC,PHYSICALOFFSETX),oy=GetDeviceCaps(dialog.hDC,PHYSICALOFFSETY);
            for(int y=0;y<metrics.height;y+=2048)for(int x=0;x<metrics.width;x+=2048) {
                const int w=std::min(2048,metrics.width-x),h=std::min(2048,metrics.height-y);
                auto tile=source->tile(page,metrics.dx,metrics.dy,x,y,w,h,error);
                if(!tile.valid())return abort();
                for(std::size_t i=0;i<tile.rgba.size();i+=4)std::swap(tile.rgba[i],tile.rgba[i+2]);
                BITMAPINFO bitmap{};bitmap.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
                bitmap.bmiHeader.biWidth=w;bitmap.bmiHeader.biHeight=-h;
                bitmap.bmiHeader.biPlanes=1;bitmap.bmiHeader.biBitCount=32;bitmap.bmiHeader.biCompression=BI_RGB;
                const int rows=StretchDIBits(dialog.hDC,x-ox,y-oy,w,h,0,0,w,h,tile.rgba.data(),&bitmap,DIB_RGB_COLORS,SRCCOPY);
                if(rows==0 || rows==GDI_ERROR)return abort();
            }
            if(EndPage(dialog.hDC)<=0)return abort();
        }
        if(EndDoc(dialog.hDC)<=0) {error="PRINT: could not finish print job";return false;}
        error="submitted pages "+std::to_string(first)+"–"+std::to_string(last);

        return true;
    });
    shell.set_file_hook([window](gcapp::Shell::FileAsk kind, std::function<void(std::string)> done) {
        wchar_t path[MAX_PATH] = L"";
        OPENFILENAMEW ofn{};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = window;
        ofn.lpstrFile = path;
        ofn.nMaxFile = MAX_PATH;
        std::wstring filter;
        for (const auto& f : gcapp::Shell::file_filters(kind)) {
            std::string pats;
            for (std::size_t i = 0; i < f.extensions.size(); ++i) pats += (i ? ";*." : "*.") + f.extensions[i];
            const std::string label = f.label + " (" + pats + ")";
            filter += std::wstring(label.begin(), label.end()) + L'\0' + std::wstring(pats.begin(), pats.end()) + L'\0';
        }
        filter += L'\0';
        ofn.lpstrFilter = filter.c_str();
        ofn.Flags = OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        const bool opening = kind == gcapp::Shell::FileAsk::Open || kind == gcapp::Shell::FileAsk::Import || kind == gcapp::Shell::FileAsk::LoadLibrary;
        if (opening) ofn.Flags |= OFN_FILEMUSTEXIST;
        else ofn.Flags |= OFN_OVERWRITEPROMPT;
        const BOOL ok = opening ? GetOpenFileNameW(&ofn) : GetSaveFileNameW(&ofn);
        if (!ok) return;
        const int n = WideCharToMultiByte(CP_UTF8, 0, path, -1, nullptr, 0, nullptr, nullptr);
        std::string utf8(static_cast<std::size_t>(n > 0 ? n - 1 : 0), '\0');
        if (n > 0) WideCharToMultiByte(CP_UTF8, 0, path, -1, utf8.data(), n, nullptr, nullptr);
        done(utf8);
    });
    shell.set_dpr(g_scale);
    shell.scale_ui(1.6);
    shell.set_viewport(client.right - client.left, client.bottom - client.top);
    open_from_argv(shell, argc, argv);
    ShowWindow(window, SW_SHOW);

    std::string last_title;
    auto render_frame = [&] {
        shell.build_frame();
        refresh_cursor(window);

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
            SetWindowTextW(window, to_utf16(title).c_str());
        }
    };

    static auto* s_frame = &render_frame;
    g_render_frame = [] { (*s_frame)(); };

    FrameDemand demand;
    while (g_running) {
        if (!demand.pending(shell.wants_frame()))
            MsgWaitForMultipleObjectsEx(0,nullptr,250,QS_ALLINPUT,MWMO_INPUTAVAILABLE);
        MSG message;
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            demand.request();
            if (message.message == WM_QUIT) g_running = false;
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        if (!g_running) break;
        if (demand.consume(shell.wants_frame())) render_frame();
    }

    g_render_frame = nullptr;
    renderer.shutdown();
    DestroyWindow(window);
    return 0;
}

int main(int argc, char** argv) { return gcapp::run(argc, argv); }

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    int argc = 0;
    wchar_t** wide = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!wide) return gcapp::run(0, nullptr);
    std::vector<std::string> arguments;
    arguments.reserve(argc);
    for (int i = 0; i < argc; ++i)
        arguments.push_back(to_utf8(wide[i], static_cast<int>(wcslen(wide[i]))));
    LocalFree(wide);
    std::vector<char*> argv;
    argv.reserve(arguments.size() + 1);
    for (auto& argument : arguments) argv.push_back(argument.data());
    argv.push_back(nullptr);
    return gcapp::run(argc, argv.data());
}
