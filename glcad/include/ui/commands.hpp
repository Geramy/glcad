#pragma once

#include <functional>
#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "math/fixed.hpp"

namespace gcapp {

class Shell;
class Canvas;

class CommandContext {
public:
    CommandContext(Shell& shell, std::string arg);

    Shell&  shell()  const noexcept { return shell_; }
    Canvas& canvas() const noexcept;

    const std::string& arg() const noexcept { return arg_; }
    std::string upper_arg() const;
    bool has_arg() const noexcept { return !arg_.empty(); }

    const std::vector<double>& nums() const noexcept { return nums_; }
    bool have_nums() const noexcept { return have_nums_; }

    void log(const std::string& line) const;

    bool need_selection(const char* what) const;

    void          world(gcm::Lattice2 p, double& x, double& y) const;
    gcm::Lattice2 lat(double x, double y) const;

    void ask(const char* cmd, const char* hint, const char* usage) const;

    void pick(int n, std::string prompt,
              std::function<void(const std::vector<gcm::Lattice2>&)> done) const;

    const gcm::WorkingVolume& vol() const noexcept;

    void invalidate_frame_caches() const;
    void after_commit() const;
    void mark_dirty() const;
    void run(const std::string& text) const;

    std::vector<std::size_t> selected_indices() const;

    bool refuse_region(const char* what) const;

private:
    Shell&              shell_;
    std::string         arg_;
    std::vector<double> nums_;
    bool                have_nums_{false};
};

template <class Self>
struct CommandBase {
    static constexpr bool wants_selection = false;
    static constexpr bool refuses_regions = false;

    void run(CommandContext& c) const {
        if constexpr (Self::wants_selection)
            if (!c.need_selection(Self::name)) return;
        if constexpr (Self::refuses_regions)
            if (!c.refuse_region(Self::name)) return;
        static_cast<const Self*>(this)->execute(c);
    }
};

using CommandHandler = void (*)(CommandContext&);

class CommandTable {
public:
    template <class T>
    void add() {
        add(T::name, +[](CommandContext& c) { T{}.run(c); });
    }
    void add(std::string_view name, CommandHandler handler);

    CommandHandler find(std::string_view name) const;

    std::size_t size() const noexcept { return by_name_.size(); }

private:
    std::unordered_map<std::string, CommandHandler> by_name_;
};

void register_draw_commands(CommandTable& t);
void register_modify_commands(CommandTable& t);
void register_annotate_commands(CommandTable& t);
void register_organise_commands(CommandTable& t);
void register_parametric_commands(CommandTable& t);
void register_inquire_commands(CommandTable& t);
void register_tools_commands(CommandTable& t);

void register_all_commands(CommandTable& t);

}
