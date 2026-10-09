#pragma once
#include "engine/block.hpp"
#include <string_view>

namespace gcapp {
class Canvas;
struct SymbolEntry { const char* id; const char* label; };
const std::vector<SymbolEntry>& symbol_catalog();
std::vector<gsnap::Shape> symbol_preview(std::string_view id,const Canvas& canvas);
struct SymbolUsage {
    std::string block, label;
    std::size_t count;
    std::vector<gsnap::Shape> geometry;
};
std::vector<SymbolUsage> used_symbols(const gdoc::BlockTable& blocks);
std::vector<SymbolUsage> used_symbols(const Canvas& canvas);
bool is_symbol_legend(const gdoc::BlockTable& blocks, std::string_view name);
}
