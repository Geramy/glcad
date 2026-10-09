#include "render/render_graph.hpp"

#include <algorithm>
#include <unordered_map>

namespace grender {

std::size_t RenderGraph::add(Pass p) {
    passes_.push_back(std::move(p));
    return passes_.size() - 1;
}

void RenderGraph::clear() { passes_.clear(); }

CompileResult RenderGraph::compile() const {
    CompileResult out;
    const std::size_t n = passes_.size();

    std::unordered_map<ResourceId, std::vector<std::size_t>> producers;
    for (std::size_t i = 0; i < n; ++i)
        for (const auto& w : passes_[i].writes) producers[w].push_back(i);

    // A read binds to the resource's current version: the last earlier writer.
    std::vector<std::vector<std::size_t>> depends(n);
    for (std::size_t i = 0; i < n; ++i) {
        for (const auto& r : passes_[i].reads) {
            const auto it = producers.find(r);
            if (it == producers.end()) {
                out.error = GraphError::MissingProducer;
                out.detail = passes_[i].name + " reads \"" + r + "\", which nothing writes";
                return out;
            }
            std::size_t current = n;   // n == "no version yet"
            for (std::size_t p : it->second)
                if (p < i) current = p;
            if (current != n) { depends[i].push_back(current); continue; }
            // No earlier writer: bind to pure producers; fellow modifiers would cycle.
            bool bound = false;
            for (std::size_t p : it->second) {
                if (p == i) continue;
                const auto& pr = passes_[p].reads;
                if (std::find(pr.begin(), pr.end(), r) != pr.end()) continue;
                depends[i].push_back(p);
                bound = true;
            }
            if (bound) continue;
            for (std::size_t p : it->second)
                if (p != i) depends[i].push_back(p);
        }
        for (const auto& w : passes_[i].writes)
            for (std::size_t p : producers[w])
                if (p < i) depends[i].push_back(p);
    }

    std::vector<bool> alive(n, false);
    std::vector<std::size_t> stack;
    for (std::size_t i = 0; i < n; ++i)
        if (passes_[i].presents) { alive[i] = true; stack.push_back(i); }
    const bool any_presents = !stack.empty();
    if (!any_presents) std::fill(alive.begin(), alive.end(), true);
    while (!stack.empty()) {
        const std::size_t at = stack.back();
        stack.pop_back();
        for (std::size_t d : depends[at])
            if (!alive[d]) { alive[d] = true; stack.push_back(d); }
    }
    for (std::size_t i = 0; i < n; ++i)
        if (!alive[i]) out.culled.push_back(passes_[i].name);

    // DFS topological sort: 0 unvisited, 1 on the stack, 2 done.
    std::vector<int> mark(n, 0);
    bool cycle = false;
    std::string cycle_at;
    const auto visit = [&](auto&& self, std::size_t i) -> void {
        if (cycle || !alive[i] || mark[i] == 2) return;
        if (mark[i] == 1) { cycle = true; cycle_at = passes_[i].name; return; }
        mark[i] = 1;
        for (std::size_t d : depends[i]) self(self, d);
        mark[i] = 2;
        out.order.push_back(i);
    };
    for (std::size_t i = 0; i < n && !cycle; ++i) visit(visit, i);

    if (cycle) {
        out.error = GraphError::Cycle;
        out.detail = "a cycle through \"" + cycle_at + "\"";
        out.order.clear();
    }
    return out;
}

bool RenderGraph::execute(PassContext& ctx) const {
    return execute(ctx, compile());
}

bool RenderGraph::execute(PassContext& ctx, const CompileResult& plan) const {
    if (!plan.ok()) return false;
    for (std::size_t i : plan.order)
        if (i < passes_.size() && passes_[i].execute) passes_[i].execute(ctx);
    return true;
}

}
