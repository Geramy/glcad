#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "math/config.hpp"
#include "engine/appearance.hpp"

namespace gdoc {

using gcm::f64;
using gcm::i32;
using gcm::u32;
using gcm::u8;

struct Layer {
    bool operator==(const Layer&) const = default;
    std::string name{"0"};
    std::string description;

    bool on{true};
    bool frozen{false};
    bool locked{false};
    bool plottable{true};

    Color       color{Color::aci(7)};
    std::string linetype{"Continuous"};
    Lineweight  lineweight{Lineweight::Default};
    i32         transparency{0};
    std::string plot_style{"Normal"};

    std::vector<u32> vp_frozen;

    bool visible() const noexcept { return on && !frozen; }
    bool editable() const noexcept { return visible() && !locked; }
    bool frozen_in(u32 viewport) const noexcept;
};

struct LayerState {
    std::string        name;
    std::vector<Layer> layers;
};

class LayerTable {
public:
    LayerTable();

    Layer*       find(std::string_view name);
    const Layer* find(std::string_view name) const;
    Layer&       add(Layer l);
    bool         remove(std::string_view name);
    bool         rename(std::string_view from, std::string_view to);

    const std::vector<Layer>& all() const noexcept { return layers_; }
    std::size_t size() const noexcept { return layers_.size(); }

    void set_current(std::string_view name);
    const std::string& current() const noexcept { return current_; }

    void isolate(std::string_view name);
    void all_on();
    void thaw_all();

    std::vector<const Layer*> filter_by_name(std::string_view pattern) const;
    std::vector<const Layer*> visible_layers() const;

    void save_state(std::string_view name);
    bool restore_state(std::string_view name);
    std::vector<std::string> state_names() const;

private:
    std::vector<Layer>      layers_;
    std::vector<LayerState> states_;
    std::string             current_{"0"};
};

bool glob_match(std::string_view pattern, std::string_view text);

}
