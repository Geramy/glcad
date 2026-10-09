#include "ui/commands.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include "geometry/boolean.hpp"
#include "ui/shell.hpp"

namespace gcapp {

namespace {

struct Bom : CommandBase<Bom> {
    static constexpr auto name = "BOM";

    void execute(CommandContext& c) const {
        Shell&  shell  = c.shell();
        Canvas& canvas = c.canvas();
        auto&   modeller = shell.modeller();
        if (modeller.bodies().empty() && canvas.blocks().references().empty()) {
            c.log("BOM: nothing to count");
            return;
        }
        std::string csv = "ITEM,QTY,MATERIAL,VOLUME,MASS\n";
        std::size_t rows = 0;
        struct Row { std::string material; double volume{0.0}, mass{0.0}; std::size_t qty{0}; };
        std::vector<Row> rowset;
        for (std::size_t i = 0; i < modeller.bodies().size(); ++i) {
            const double v = gtopo::volume(modeller.bodies()[i], c.vol());
            const auto& mats = modeller.materials();
            const gcm::u32 mi = modeller.material_of(i);
            const std::string name = mi < mats.size() ? mats[mi].name : std::string("Default");
            const double density = mi < mats.size() ? mats[mi].density : 0.0;
            bool merged = false;
            for (auto& r : rowset)
                if (r.material == name && std::fabs(r.volume / std::max(1.0, double(r.qty)) - v) <
                                              std::max(1e-9, std::fabs(v) * 1e-6)) {
                    r.volume += v;
                    r.mass += v * density;
                    ++r.qty;
                    merged = true;
                    break;
                }
            if (!merged) rowset.push_back({name, v, v * density, 1});
        }
        for (const auto& r : rowset) {
            char buf[224];
            std::snprintf(buf, sizeof(buf), "Solid,%zu,%s,%.9g,%.9g\n", r.qty,
                          r.material.c_str(), r.volume, r.mass);
            csv += buf;
            ++rows;
        }
        std::vector<std::pair<std::string, std::size_t>> blocks;
        for (const auto& r : canvas.blocks().references()) {
            bool found = false;
            for (auto& b : blocks) if (b.first == r.name) { ++b.second; found = true; }
            if (!found) blocks.push_back({r.name, 1});
        }
        for (const auto& b : blocks) {
            csv += b.first + "," + std::to_string(b.second) + ",,,\n";
            ++rows;
        }
        const std::string path = c.arg().empty() ? std::string("bom.csv") : c.arg();
        std::ofstream f(path);
        if (!f) { c.log("BOM: could not write " + path); return; }
        f << csv;
        c.log("bill of materials: " + std::to_string(rows) + " line(s) to " + path);
    }
};

}

void register_inquire_commands(CommandTable& t) {
    t.add<Bom>();
}

}
