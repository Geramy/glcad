#pragma once

#include <functional>

namespace gcapp {

class Shell;

struct ScenarioConfig {
    double seconds{5.0};
    double distance{2.0};   // metres
    int    fps{60};
    bool  paced{true};
    bool  side_face{false};
    std::function<void()> render;
};

bool run_box_circle_extrude(Shell& s, const ScenarioConfig& cfg);

bool run_walls_zoom(Shell& s, const ScenarioConfig& cfg);

bool run_draw_inside_regions(Shell& s, const ScenarioConfig& cfg);

}
