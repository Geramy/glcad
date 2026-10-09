#pragma once

#include <string>
#include <vector>

#include "ai/types/tools.hpp"

namespace gcapp {

class Shell;

namespace ai_tools {

std::vector<gai::ToolDefinition> registry();

gai::Json drawing_context(const Shell& shell);

bool needs_confirmation(const gai::ToolCall& call);

bool pure(const gai::ToolCall& call);

// Thread-safe; called from the AI worker.
struct LibraryHit {
    std::string file;
    std::string text;
};
std::vector<LibraryHit> library_lookup(Shell& shell, const std::string& query,
                                       int k, bool* used_embeddings = nullptr);

std::string describe(const gai::ToolCall& call);

// UI thread only.
enum class Origin { Tool, Assistant, MCP, Library, Silent };
std::string activity_entry(const gai::ToolCall& call, Origin origin, const std::string& result = {});
std::string execute(Shell& shell, const gai::ToolCall& call, Origin origin = Origin::Tool);

}
}
