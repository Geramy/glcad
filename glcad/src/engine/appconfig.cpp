#include "engine/appconfig.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <vector>

#include "config_generated.h"

namespace gfile {

std::string app_config_path() {
    namespace fs = std::filesystem;
    fs::path dir;
    if (const char* forced = std::getenv("GLCAD_CONFIG_DIR")) dir = forced;
    if (!dir.empty()) {
        std::error_code ec;
        fs::create_directories(dir, ec);
        return (dir / "config.bin").string();
    }
#if defined(_WIN32)
    if (const char* appdata = std::getenv("APPDATA")) dir = fs::path(appdata) / "GLCAD";
#elif defined(__APPLE__)
    if (const char* home = std::getenv("HOME"))
        dir = fs::path(home) / "Library" / "Application Support" / "GLCAD";
#else
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME")) dir = fs::path(xdg) / "glcad";
    else if (const char* home = std::getenv("HOME")) dir = fs::path(home) / ".config" / "glcad";
#endif
    if (dir.empty()) dir = fs::temp_directory_path() / "glcad";
    std::error_code ec;
    fs::create_directories(dir, ec);
    return (dir / "config.bin").string();
}

bool load_app_config(const std::string& path, AppConfig& out) {
    out = AppConfig{};
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    const long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (n <= 0) { std::fclose(f); return false; }
    std::vector<uint8_t> bytes(static_cast<std::size_t>(n));
    const std::size_t got = std::fread(bytes.data(), 1, bytes.size(), f);
    std::fclose(f);
    bytes.resize(got);
    flatbuffers::Verifier v(bytes.data(), bytes.size());
    if (!glcad::cfg::VerifyConfigBuffer(v)) return false;
    const glcad::cfg::Config* c = glcad::cfg::GetConfig(bytes.data());
    if (c->default_unit()) out.default_unit = c->default_unit()->str();
    if (c->ai_servers())
        for (const auto* s : *c->ai_servers()) {
            AiServerRecord r;
            if (s->name()) r.name = s->name()->str();
            if (s->url()) r.url = s->url()->str();
            r.manual = s->manual();
            if (s->api_key()) r.api_key = s->api_key()->str();
            if (s->default_model()) r.default_model = s->default_model()->str();
            if (s->default_tts()) r.default_tts = s->default_tts()->str();
            if (s->default_stt()) r.default_stt = s->default_stt()->str();
            if (s->tts_voice()) r.tts_voice = s->tts_voice()->str();
            if (s->default_img()) r.default_img = s->default_img()->str();
            if (s->default_embed()) r.default_embed = s->default_embed()->str();
            out.ai_servers.push_back(std::move(r));
        }
    if (c->ai_active_url()) out.ai_active_url = c->ai_active_url()->str();
    if (c->ai_model()) out.ai_model = c->ai_model()->str();
    out.ai_show_thinking = c->ai_show_thinking();
    if (c->audio_output()) out.audio_output = c->audio_output()->str();
    if (c->audio_input()) out.audio_input = c->audio_input()->str();
    out.ai_voice = c->ai_voice();
    if (c->theme()) out.theme = c->theme()->str();
    out.dim_precision = c->dim_precision();
    out.polar = c->polar();
    out.ortho = c->ortho();
    out.smoothing = c->smoothing();
    out.smoothing_angle = c->smoothing_angle();
    out.curve_quality = c->curve_quality();
    out.msaa = c->msaa();
    out.command_history = c->command_history();
    out.mcp_enabled = c->mcp_enabled();
    out.mcp_require_token = c->mcp_require_token();
    out.mcp_port = c->mcp_port();
    if(c->mcp_token()) out.mcp_token = c->mcp_token()->str();
    if (c->ai_reasoning_effort() && c->ai_reasoning_effort()->size())
        out.ai_reasoning_effort = c->ai_reasoning_effort()->str();
    if (c->recent_locations())
        for (const auto* l : *c->recent_locations())
            out.recent_locations.push_back(l->str());
    return true;
}

bool save_app_config(const std::string& path, const AppConfig& cfg) {
    flatbuffers::FlatBufferBuilder b(1024);
    std::vector<flatbuffers::Offset<glcad::cfg::AiServer>> servers;
    for (const auto& s : cfg.ai_servers)
        servers.push_back(glcad::cfg::CreateAiServer(
            b, b.CreateString(s.name), b.CreateString(s.url), s.manual,
            b.CreateString(s.api_key), b.CreateString(s.default_model),
            b.CreateString(s.default_tts), b.CreateString(s.default_stt),
            b.CreateString(s.tts_voice), b.CreateString(s.default_img),
            b.CreateString(s.default_embed)));
    const auto root = glcad::cfg::CreateConfig(
        b,
        b.CreateString(cfg.default_unit),
        b.CreateVector(servers),
        b.CreateString(cfg.ai_active_url),
        b.CreateString(cfg.ai_model),
        cfg.ai_show_thinking,
        b.CreateString(cfg.audio_output),
        b.CreateString(cfg.audio_input),
        cfg.ai_voice,
        b.CreateString(cfg.theme),
        cfg.dim_precision,
        static_cast<int8_t>(cfg.polar),
        static_cast<int8_t>(cfg.ortho),
        static_cast<int8_t>(cfg.smoothing),
        cfg.smoothing_angle,
        static_cast<int8_t>(cfg.curve_quality),
        cfg.msaa,
        b.CreateString(cfg.ai_reasoning_effort),
        b.CreateVectorOfStrings(cfg.recent_locations),
        cfg.mcp_enabled, cfg.mcp_port, b.CreateString(cfg.mcp_token),cfg.mcp_require_token,
        static_cast<int8_t>(cfg.command_history));
    b.Finish(root);
    // Write beside, rename over: a crash mid-save must not eat the config.
    const std::string tmp = path + ".tmp";
    std::FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) return false;
    const std::size_t wrote = std::fwrite(b.GetBufferPointer(), 1, b.GetSize(), f);
    std::fclose(f);
    if (wrote != b.GetSize()) { std::remove(tmp.c_str()); return false; }
    std::remove(path.c_str());
    return std::rename(tmp.c_str(), path.c_str()) == 0;
}

}
