#pragma once

#include <string>
#include <vector>

namespace gcapp {

struct MenuItem {
    std::string label;      // empty = separator
    std::string command;
    std::string shortcut;   // "cmd+S"; empty for none
    bool separator() const noexcept { return label.empty(); }
};

struct Menu {
    std::string           title;
    std::vector<MenuItem> items;
};

const std::vector<Menu>& app_menus();

}
