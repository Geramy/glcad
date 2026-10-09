#pragma once

#include "ui/access.hpp"

namespace gcapp {

using NativeAccessAction = std::function<bool(gui::Id, int)>;
void* create_voice_over(void* view, NativeAccessAction action);
void destroy_voice_over(void* handle);
void update_voice_over(void* handle, const gui::AccessSnapshot& snapshot,
                       double scale, double height);

}
