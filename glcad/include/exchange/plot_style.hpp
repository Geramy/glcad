#pragma once
#include "exchange/page.hpp"
#include <span>

namespace gplot {
struct PlotStyleReadResult {
    PlotStyleTable table;
    bool ok{false};
    std::string error;
    std::vector<std::string> warnings;
};
PlotStyleReadResult read_plot_style(std::span<const gcm::u8> bytes);
std::vector<gcm::u8> write_plot_style(const PlotStyleTable& table, std::string* error=nullptr);
std::vector<f64> plot_style_dashes(int pattern, f64 size, f64 scale);
}
