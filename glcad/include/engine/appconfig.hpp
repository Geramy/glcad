#pragma once

#include <string>
#include <vector>

namespace gfile {

struct AiServerRecord {
    std::string name;
    std::string url;
    bool        manual{true};
    std::string api_key;
    std::string default_model;
    std::string default_tts;
    std::string default_stt;
    std::string tts_voice;
    std::string default_img;
    std::string default_embed;
};

struct AppConfig {
    bool mcp_enabled{false};
    bool mcp_require_token{true};
    int mcp_port{5912};
    std::string mcp_token;
    std::string default_unit;
    std::vector<AiServerRecord> ai_servers;
    std::string ai_active_url;
    std::string ai_model;
    bool        ai_show_thinking{false};
    std::string audio_output;
    std::string audio_input;
    bool        ai_voice{false};
    std::string ai_reasoning_effort{"low"};
    std::vector<std::string> recent_locations;

    std::string theme;
    int         dim_precision{-1};  // decimals index 0..4; -1 = app default
    int         polar{-1};          // -1 unset, 0 off, 1 on
    int         ortho{-1};
    int         smoothing{-1};
    double      smoothing_angle{0.0};
    int         curve_quality{-1};  // 0 draft, 1 standard, 2 fine
    int         msaa{0};            // samples; 0 = unset
    int         command_history{-1};
};

std::string app_config_path();

bool load_app_config(const std::string& path, AppConfig& out);
bool save_app_config(const std::string& path, const AppConfig& cfg);

}
