#include "ui/shell.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>

#include "engine/document_file.hpp"
#include "ui/doc_entities.hpp"

namespace gcapp {

void Shell::load_app_config() {
    config_loaded_ = true;
    gfile::AppConfig cfg;
    if (gfile::load_app_config(gfile::app_config_path(), cfg)) app_config_ = cfg;
    {
        const auto path=(std::filesystem::path(gfile::app_config_path()).parent_path()/"object-library.gllib").string();
        std::error_code ec;
        if(std::filesystem::exists(path,ec)) {
            BlockLibrary saved;std::string error;
            if(read_native_library(path,saved,error))block_library_=std::move(saved);
            else log_line("Could not restore object library: "+error);
        }
    }
    for (std::size_t i = 0; i < app_config_.ai_servers.size(); ++i)
        for (std::size_t j = i + 1; j < app_config_.ai_servers.size();) {
            auto& a = app_config_.ai_servers[i];
            const auto& b = app_config_.ai_servers[j];
            if (a.url == b.url) {
                if (a.name.empty()) a.name = b.name;
                if (a.api_key.empty()) a.api_key = b.api_key;
                if (a.default_model.empty()) a.default_model = b.default_model;
                if (a.default_tts.empty()) a.default_tts = b.default_tts;
                if (a.default_stt.empty()) a.default_stt = b.default_stt;
                if (a.tts_voice.empty()) a.tts_voice = b.tts_voice;
                if (a.default_img.empty()) a.default_img = b.default_img;
                if (a.default_embed.empty()) a.default_embed = b.default_embed;
                app_config_.ai_servers.erase(app_config_.ai_servers.begin() + long(j));
            } else {
                ++j;
            }
        }
    if (!app_config_.default_unit.empty())
        if (const auto u = gunit::length_from_suffix(app_config_.default_unit))
            canvas_.set_display_unit(*u);
    if (!app_config_.theme.empty()) {
        themes_.set_follow_system(false);
        themes_.activate(app_config_.theme);
    }
    if (app_config_.dim_precision >= 0)
        precision_ = std::clamp(app_config_.dim_precision, 0, 4);
    if (app_config_.polar >= 0) canvas_.polar().enabled = app_config_.polar != 0;
    if (app_config_.ortho >= 0) canvas_.polar().ortho = app_config_.ortho != 0;
    if (app_config_.smoothing >= 0 || app_config_.smoothing_angle > 0.0)
        modeller_.set_smoothing(app_config_.smoothing < 0 ? modeller_.smoothing_on()
                                                          : app_config_.smoothing != 0,
                                app_config_.smoothing_angle > 0.0
                                    ? app_config_.smoothing_angle
                                    : modeller_.smoothing_angle());
    if (app_config_.curve_quality >= 0) {
        static const double curve_tol[3] = {0.005, 0.002, 0.0005};
        curve_quality_ = std::clamp(app_config_.curve_quality, 0, 2);
        canvas_.spline_tolerance() = curve_tol[curve_quality_];
    }
    if (app_config_.command_history >= 0) history_hidden_ = app_config_.command_history == 0;
    if (app_config_.msaa > 0) {
        graphics_msaa_ = static_cast<uint32_t>(app_config_.msaa);
        static const uint32_t aa_v[5] = {1, 2, 4, 8, 16};
        for (int i = 0; i < 5; ++i)
            if (aa_v[i] == graphics_msaa_) msaa_choice_ = i;
    }

    {
        namespace fs = std::filesystem;
        const fs::path lib =
            fs::path(gfile::app_config_path()).parent_path() / "library";
        std::error_code ec;
        fs::create_directories(lib, ec);
        const fs::path seed = lib / "residential-baseline-us.md";
        const char* kFraming =
            "\n## Opening framing (R602.7)\n"
            "- Every opening in a bearing wall carries a HEADER, and the "
            "header lands on JACK (trimmer) studs inside KING studs "
            "(IRC R602.7).\n"
            "- Wide openings DOUBLE the jacks per Table R602.7(1); the "
            "customary tier is 6ft (1830mm). The plan SHOWS the pack: "
            "stud sections at each jamb, doubled where required.\n"
            "- Garage and overhead doors ROLL UP: the plan shows a panel "
            "in the opening and tracks running back -- never a swing arc.\n"
            "- Window glazing hatches ANSI36 (the marble/slate/glass "
            "convention); walls hatch ANSI31. Glazing never wears a "
            "wall's poche.\n"
            "- After placing doors and windows, wall faces are CUT across "
            "every opening and the jambs capped (merge_overlaps does "
            "this); a wall line through an opening fails plan check.\n";
        if (fs::exists(seed, ec)) {
            std::ifstream in(seed.string());
            const std::string have((std::istreambuf_iterator<char>(in)),
                                   std::istreambuf_iterator<char>());
            if (have.find("R602.7") == std::string::npos) {
                std::ofstream f(seed.string(), std::ios::app);
                f << kFraming;
            }
        } else {
            std::ofstream f(seed.string());
            f << "# US residential baseline (IRC-derived, as enacted)\n\n"
                 "Figures common to enacted US residential codes; the "
                 "project's jurisdiction overrides.\n\n"
                 "## Egress and doors (R311)\n"
                 "- Required egress door: 32in (813mm) CLEAR width minimum; "
                 "a 36in (914mm) leaf is the standard entrance door.\n"
                 "- Hallways: 36in (914mm) minimum width.\n"
                 "- Stairs: 7-3/4in (196mm) max riser, 10in (254mm) min tread; "
                 "36in min width; 6ft-8in headroom.\n\n"
                 "## Habitable rooms (R304, R305)\n"
                 "- Minimum area 70 sqft (6.5 m2); minimum dimension 7ft.\n"
                 "- Ceiling height 7ft (2134mm) for habitable rooms and halls; "
                 "6ft-8in for bathrooms and laundry.\n\n"
                 "## Emergency escape (R310)\n"
                 "- Sleeping rooms and basements: escape opening of 5.7 sqft "
                 "(0.53 m2) net clear (5.0 sqft at grade), min 24in high, "
                 "20in wide, sill no higher than 44in above the floor.\n\n"
                 "## Drafting conventions\n"
                 "- Walls: double lines with hatched cores; exterior ~6.5in "
                 "(165mm) finished at 2x6, interior ~4.5in (115mm) at 2x4.\n"
                 "- Every door shows its swing; bedroom and bathroom doors "
                 "swing into the room.\n"
                 "- Layers: A-WALL, A-DOOR, A-GLAZ, A-ANNO-DIMS.\n"
                 "- Every room is labelled; exterior walls and openings are "
                 "dimensioned.\n";
            f << kFraming;
        }
    }

    if (ai_client()) ai_refresh_status();
    if(app_config_.mcp_enabled) set_mcp_enabled(true);
}

void Shell::apply_sysvars() {
    if(!drafting_seen_.contains("INSUNITS")) {
        const auto unit=canvas_.display_unit();
        vars_.set_int("INSUNITS",unit==gunit::Length::Inch?1:unit==gunit::Length::Foot?2:unit==gunit::Length::Millimetre?4:unit==gunit::Length::Centimetre?5:6);
    }
    sync_drafting_vars(false);
    canvas_.set_text_styles(text_styles_);
    std::map<std::string,gdoc::Lineweight> weights;
    for (const auto& layer:layers_.all()) weights.emplace(layer.name,layer.lineweight);
    canvas_.set_lineweight_display(vars_.get_int("LWDISPLAY",0)!=0,std::move(weights));
    const auto changed = [&](const char* name, double& last, double now) {
        if (std::fabs(now - last) < 1e-12) return false;
        last = now;
        return true;
    };
    double v = vars_.get_real("FILLETRAD", canvas_.corner_radius());
    if (changed("FILLETRAD", var_filletrad_, v)) canvas_.corner_radius() = v;
    v = vars_.get_real("CHAMFERA", canvas_.chamfer_distance());
    if (changed("CHAMFERA", var_chamfera_, v)) canvas_.chamfer_distance() = v;
    v = vars_.get_real("TEXTSIZE", text_height_);
    if (changed("TEXTSIZE", var_textsize_, v) && v > 0.0) text_height_ = v;
    v = vars_.get_real("LTSCALE", linetype_scale_);
    if (changed("LTSCALE", var_ltscale_, v) && v > 0.0) linetype_scale_ = v;
    {
        const double mode = double(vars_.get_int("PDMODE", canvas_.point_mode()));
        if (changed("PDMODE", var_pdmode_, mode)) {
            canvas_.point_mode() = int(mode);

        }
        const double size = vars_.get_real("PDSIZE", canvas_.point_size());
        if (changed("PDSIZE", var_pdsize_, size)) {
            canvas_.point_size() = size;

        }
    }
    const double osmode = static_cast<double>(
        vars_.get_int("OSMODE", static_cast<gcm::i64>(canvas_.snaps().settings.running)));
    if (changed("OSMODE", var_osmode_, osmode)) {
        canvas_.snaps().settings.running = static_cast<gcm::u32>(osmode)&~gcm::u32(gcmd::Osnap_Off);
        canvas_.snaps().settings.enabled=(static_cast<gcm::u32>(osmode)&gcmd::Osnap_Off)==0;
    }
}

void Shell::publish_sysvars() {
    sync_drafting_vars(true);
    vars_.set_real("FILLETRAD", canvas_.corner_radius());
    var_filletrad_ = canvas_.corner_radius();
    vars_.set_real("CHAMFERA", canvas_.chamfer_distance());
    var_chamfera_ = canvas_.chamfer_distance();
    vars_.set_real("TEXTSIZE", text_height_);
    var_textsize_ = text_height_;
    vars_.set_int("PDMODE", canvas_.point_mode());
    vars_.set_real("PDSIZE", canvas_.point_size());
    var_pdmode_ = double(canvas_.point_mode());
    var_pdsize_ = canvas_.point_size();
    vars_.set_real("LTSCALE", linetype_scale_);
    var_ltscale_ = linetype_scale_;
    vars_.set_int("OSMODE", static_cast<gcm::i64>(canvas_.snaps().settings.running)|(canvas_.snaps().settings.enabled?0:gcmd::Osnap_Off));
    var_osmode_ = static_cast<double>(vars_.get_int("OSMODE"));
}

void Shell::save_app_config() const { (void)save_app_config_checked(); }

void Shell::set_command_history_hidden(bool hidden) {
    history_hidden_ = hidden;
    app_config_.command_history = hidden ? 0 : 1;
    save_app_config();
}

void Shell::adopt_display_unit(gunit::Length u) {
    canvas_.set_display_unit(u);
    app_config_.default_unit = gunit::suffix(u);
    save_app_config();
    log_.push_back(std::string("units: ") + gunit::name(u));
}

bool Shell::save_app_config_checked() const {
    // Only a shell that loaded the config may save it, or defaults overwrite it.
    if (!config_loaded_) return false;
    return gfile::save_app_config(gfile::app_config_path(), app_config_);
}
}
