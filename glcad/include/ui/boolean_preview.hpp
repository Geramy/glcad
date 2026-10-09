#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>
#include "ui/frame_types.hpp"

namespace gcapp::gbool {
enum class Operation : uint32_t { Union, Subtract, Intersect };

struct PreviewState {
    Operation operation{Operation::Union};
    std::size_t first{}, count{}, body_first{};
    std::size_t target_body_first{}, target_body_count{};
    std::size_t tool_body_first{}, tool_body_count{};
    std::vector<MeshVertex> body, tool;
};
}
