#!/bin/sh
# Run inside the installer-test image, with /packages mounted read-only.
set -eu
platform=${1:?linux, windows or windows-setup}
case "$platform" in
  linux)
    package=${2:?Path to a DEB package}
    dpkg -i "$package"
    app=$(command -v glcad)
    ;;
  windows-setup)
    package=${2:?Path to a Windows setup EXE}
    export WINEDEBUG=-all WINEPREFIX=/tmp/glcad-wine
    WINEARCH=win64 xvfb-run -a /usr/lib/wine/wine "$package" /S '/D=C:\GLCAD'
    app=/tmp/glcad-wine/drive_c/GLCAD/glcad.exe
    test -f "$app"
    test -f /tmp/glcad-wine/drive_c/GLCAD/Uninstall.exe
    platform=windows
    ;;
  windows)
    package=${2:?Path to a Windows ZIP}
    python3 - "$package" <<'PY'
import sys, zipfile
with zipfile.ZipFile(sys.argv[1]) as archive:
    archive.extractall('/tmp/glcad-windows')
PY
    app=$(find /tmp/glcad-windows -type f -name glcad.exe -print -quit)
    test -n "$app"
    export WINEDEBUG=-all WINEPREFIX=/tmp/glcad-wine
    ;;
  *) exit 2 ;;
esac
export GLCAD_CONFIG_DIR=/tmp/glcad-package-config
export GLCAD_PACKAGE_APP="$app" GLCAD_PACKAGE_PLATFORM="$platform"
# Send WM_DELETE_WINDOW; destroying the window skips the close handler.
cc -x c - -o /tmp/glcad-close-window -lX11 <<'CLOSE'
#include <X11/Xlib.h>
#include <stdlib.h>
int main(int argc, char **argv) {
    if (argc != 2) return 2;
    Display *display = XOpenDisplay(NULL);
    if (!display) return 3;
    Window window = strtoul(argv[1], NULL, 0);
    XEvent event = {0};
    event.xclient.type = ClientMessage;
    event.xclient.display = display;
    event.xclient.window = window;
    event.xclient.message_type = XInternAtom(display, "WM_PROTOCOLS", False);
    event.xclient.format = 32;
    event.xclient.data.l[0] = XInternAtom(display, "WM_DELETE_WINDOW", False);
    event.xclient.data.l[1] = CurrentTime;
    int sent = XSendEvent(display, window, False, NoEventMask, &event);
    XFlush(display);
    XCloseDisplay(display);
    return sent ? 0 : 4;
}
CLOSE
xvfb-run -a -s '-screen 0 1280x900x24' sh -eu -c '
    if [ "$GLCAD_PACKAGE_PLATFORM" = windows ]; then
        /usr/lib/wine/wine64 "$GLCAD_PACKAGE_APP" > /tmp/packaged-app.log 2>&1 &
    else
        "$GLCAD_PACKAGE_APP" > /tmp/packaged-app.log 2>&1 &
    fi
    app_pid=$!
    trap "kill $app_pid 2>/dev/null || true" EXIT
    window=""
    for attempt in $(seq 1 45); do
        if ! kill -0 "$app_pid" 2>/dev/null; then cat /tmp/packaged-app.log; exit 1; fi
        window=$(xdotool search --onlyvisible --name "GLCAD" 2>/dev/null | head -1 || true)
        if [ -n "$window" ]; then break; fi
        sleep 1
    done
    if [ -z "$window" ]; then cat /tmp/packaged-app.log; echo "No application window"; exit 1; fi
    sleep 3
    kill -0 "$app_pid"
    xdotool getwindowname "$window"
    /tmp/glcad-close-window "$window"
    for attempt in $(seq 1 30); do
        if ! kill -0 "$app_pid" 2>/dev/null; then break; fi
        sleep 1
    done
    if kill -0 "$app_pid" 2>/dev/null; then
        cat /tmp/packaged-app.log; echo "Application did not close"; exit 1
    fi
    if wait "$app_pid"; then app_status=0; else app_status=$?; fi
    cat /tmp/packaged-app.log
    if [ "$app_status" -ne 0 ]; then echo "Application exited with $app_status"; exit "$app_status"; fi
    echo "PASS: packaged application opened, remained running and closed cleanly"
'
