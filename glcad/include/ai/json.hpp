#pragma once

#include <optional>
#include <string_view>

#include <nlohmann/json.hpp>

#include "math/config.hpp"

namespace gai {

using Json = nlohmann::json;

// Depth cap: the recursive-descent parser spends one stack frame per level.
inline std::optional<Json> parse_json(std::string_view text) {
    class CappedSax : public nlohmann::detail::json_sax_dom_parser<Json> {
    public:
        using Base = nlohmann::detail::json_sax_dom_parser<Json>;
        explicit CappedSax(Json& j) : Base(j, /*allow_exceptions=*/false) {}
        bool start_object(std::size_t n) {
            return descend() && Base::start_object(n);
        }
        bool start_array(std::size_t n) {
            return descend() && Base::start_array(n);
        }
        bool end_object() { --depth_; return Base::end_object(); }
        bool end_array() { --depth_; return Base::end_array(); }

    private:
        bool descend() noexcept { return ++depth_ <= 128; }
        int  depth_{0};
    };
    Json v;
    CappedSax sax(v);
    if (!Json::sax_parse(text, &sax)) return std::nullopt;
    return v;
}

}
