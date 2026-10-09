#pragma once

#include <map>
#include "engine/document_file.hpp"
#include "render/registry.hpp"

namespace gcapp {

struct LibraryEntry {
    std::string name, folder, block;
};

struct BlockLibrary {
    gfile::Contents contents;
    std::vector<LibraryEntry> entries;
    std::vector<gtext::TextStyleDef> text_styles;
};

inline constexpr gcm::u16 kBlockLibraryFrame = 0x4C42;
bool read_native_library(const std::string& path, BlockLibrary& out, std::string& error);
bool write_block_library(const std::string& path, const BlockLibrary& library, std::string& error);
bool validate_block_library(const BlockLibrary& library, std::string& error);
bool merge_block_library(BlockLibrary& into, const BlockLibrary& source,
                         std::map<std::string,std::string>& names, std::string& error);
BlockLibrary library_subset(const BlockLibrary& source, const std::vector<std::string>& roots);
std::string library_folder(std::string folder);

}
