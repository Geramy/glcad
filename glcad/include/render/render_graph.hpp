#pragma once

#include <functional>
#include <string>
#include <vector>

namespace grender {

class Scene;

using ResourceId = std::string;

struct PassContext {
    const Scene* scene{nullptr};
    void* target{nullptr};
};

struct Pass {
    std::string             name;
    std::vector<ResourceId> reads;
    std::vector<ResourceId> writes;
    bool                    presents{false};
    std::function<void(PassContext&)> execute;
};

enum class GraphError { None, Cycle, MissingProducer };

struct CompileResult {
    GraphError               error{GraphError::None};
    std::string              detail;
    std::vector<std::size_t> order;
    std::vector<std::string> culled;

    bool ok() const noexcept { return error == GraphError::None; }
};

class RenderGraph {
public:
    std::size_t add(Pass p);
    void        clear();
    std::size_t size() const noexcept { return passes_.size(); }
    const std::vector<Pass>& passes() const noexcept { return passes_; }

    CompileResult compile() const;

    bool execute(PassContext& ctx) const;

    bool execute(PassContext& ctx, const CompileResult& plan) const;

private:
    std::vector<Pass> passes_;
};

}
