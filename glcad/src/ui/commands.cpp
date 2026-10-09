#include "ui/commands.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

#include "ui/shell.hpp"

namespace gcapp {

bool parse_numbers(const std::string& text, std::vector<double>& out) {
    out.clear();
    std::istringstream in(text);
    double v;
    while (in >> v) out.push_back(v);
    return !out.empty();
}

CommandContext::CommandContext(Shell& shell, std::string arg)
    : shell_(shell), arg_(std::move(arg)) {
    have_nums_ = parse_numbers(arg_, nums_);
}

Canvas& CommandContext::canvas() const noexcept { return shell_.canvas_; }

std::string CommandContext::upper_arg() const { return uppercase(arg_); }

void CommandContext::log(const std::string& line) const {
    shell_.log_.push_back(line);
}

bool CommandContext::need_selection(const char* what) const {
    if (!shell_.canvas_.selection().empty()) return true;
    log(std::string(what) + ": select objects first");
    return false;
}

void CommandContext::world(gcm::Lattice2 p, double& x, double& y) const {
    gcm::to_world(shell_.vol_, p, x, y);
}

gcm::Lattice2 CommandContext::lat(double x, double y) const {
    return gcm::to_lattice(shell_.vol_, x, y);
}

void CommandContext::ask(const char* cmd, const char* hint, const char* usage) const {
    shell_.pending_args_ = cmd;
    shell_.pending_hint_ = hint;
    log(usage);
}

void CommandContext::pick(int n, std::string prompt,
                          std::function<void(const std::vector<gcm::Lattice2>&)> done) const {
    shell_.begin_pick(n, std::move(prompt), std::move(done));
}

const gcm::WorkingVolume& CommandContext::vol() const noexcept { return shell_.vol_; }

void CommandContext::invalidate_frame_caches() const { shell_.invalidate_frame_caches(); }
void CommandContext::after_commit() const { shell_.after_commit(); }
void CommandContext::mark_dirty() const { shell_.mesh_dirty_ = true; }
void CommandContext::run(const std::string& text) const { shell_.run_command(text); }

std::vector<std::size_t> CommandContext::selected_indices() const {
    std::vector<std::size_t> out;
    const Canvas& canvas = shell_.canvas_;
    for (std::size_t i : canvas.selection())
        if (i < canvas.shapes().size()) out.push_back(i);
    return out;
}

bool CommandContext::refuse_region(const char* what) const {
    const auto& sel = shell_.canvas_.selection();
    const bool any = std::any_of(sel.begin(), sel.end(),
                                 [&](auto i) { return shell_.canvas_.is_region(i); });
    if (!any) return true;
    log(std::string(what) +
        ": use region booleans, or EXPLODE to edit individual boundaries");
    return false;
}

void CommandTable::add(std::string_view name, CommandHandler handler) {
    by_name_[std::string(name)] = handler;
}

CommandHandler CommandTable::find(std::string_view name) const {
    const auto it = by_name_.find(std::string(name));
    return it == by_name_.end() ? nullptr : it->second;
}

void register_all_commands(CommandTable& t) {
    register_draw_commands(t);
    register_modify_commands(t);
    register_annotate_commands(t);
    register_organise_commands(t);
    register_parametric_commands(t);
    register_inquire_commands(t);
    register_tools_commands(t);
}

}
