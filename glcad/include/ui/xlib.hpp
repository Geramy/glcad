// Include first: undefines X11 macros (True, None, Status, ...) that clash.
#pragma once

#include <X11/XKBlib.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

using XlibStatus = int;

#undef Status
#undef Bool
#undef True
#undef False
#undef None
#undef Success
#undef Always
#undef Complex
#undef index
#undef Above
#undef Below
#undef KeyPress
#undef KeyRelease

enum : int { XlibKeyPress = 2, XlibKeyRelease = 3 };
