#include "ui/shell.hpp"
#include "ui/ai_tools.hpp"
#include "ui/snapshot.hpp"
#include "ai/encode.hpp"

#include <cctype>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

namespace gcapp {

void Shell::ai_ensure_discovery() {
    if (!ai_->discovery.running()) ai_->discovery.start();
}

void Shell::ai_join_worker() {
    if (!ai_->busy && ai_->worker.joinable()) ai_->worker.join();
    if (!ai_->tts_busy && ai_->tts_worker.joinable()) ai_->tts_worker.join();
}

gfile::AiServerRecord* Shell::ai_record(const std::string& url) {
    for (auto& s : app_config_.ai_servers)
        if (s.url == url) return &s;
    return nullptr;
}

std::optional<gai::Client> Shell::ai_server_client(const std::string& url) const {
    auto c = gai::Client::from_url(url);
    if (!c) return c;
    for (const auto& s : app_config_.ai_servers)
        if (s.url == url && !s.api_key.empty()) c->set_api_key(s.api_key);
    return c;
}

std::optional<gai::Client> Shell::ai_client() const {
    std::string url = app_config_.ai_active_url;
    if (url.empty()) {
        const auto found = ai_->discovery.snapshot();
        if (!found.empty()) url = found.front().url;
        else if (!app_config_.ai_servers.empty()) url = app_config_.ai_servers.front().url;
    }
    if (url.empty()) return std::nullopt;
    return ai_server_client(url);
}

void Shell::split_thinking(const std::string& raw, std::string& visible,
                           std::string& thinking) {
    visible.clear();
    thinking.clear();
    std::size_t at = 0;
    while (at < raw.size()) {
        const std::size_t open = raw.find("<think>", at);
        if (open == std::string::npos) { visible += raw.substr(at); break; }
        visible += raw.substr(at, open - at);
        const std::size_t close = raw.find("</think>", open + 7);
        if (close == std::string::npos) {
            thinking += raw.substr(open + 7);
            break;
        }
        thinking += raw.substr(open + 7, close - open - 7);
        at = close + 8;
    }
}

namespace {

char read_list_marker(const std::string& s, std::size_t& i) {
    std::size_t p = i;
    const auto skip_soft = [&] {
        while (p < s.size() && (s[p] == '*' || s[p] == ' ' || s[p] == '\t')) ++p;
    };
    skip_soft();
    if (p < s.size() && (s[p] == '-' || s[p] == '+')) { ++p; skip_soft(); }
    bool named = false;
    for (const char* word : {"option ", "choice "}) {
        const std::size_t n = std::strlen(word);
        if (p + n <= s.size()) {
            bool same = true;
            for (std::size_t k = 0; k < n && same; ++k)
                same = char(std::tolower(static_cast<unsigned char>(s[p + k]))) == word[k];
            if (same) { p += n; skip_soft(); named = true; break; }
        }
    }
    bool paren = false;
    if (p < s.size() && s[p] == '(') { paren = true; ++p; }
    if (p >= s.size()) return 0;
    const char raw = s[p];
    const char low = char(std::tolower(static_cast<unsigned char>(raw)));
    const bool letter = low >= 'a' && low <= 'h';
    const bool digit = raw >= '1' && raw <= '9';
    if (!letter && !digit) return 0;
    ++p;
    if (paren) {
        if (p >= s.size() || s[p] != ')') return 0;
        ++p;
    } else if (p < s.size() && s[p] == ')') {
        ++p;
    } else if (p < s.size() && s[p] == '.' && letter) {
        ++p;
    } else if (named && p < s.size() && (s[p] == ':' || s[p] == ' ')) {
    } else {
        return 0;
    }
    while (p < s.size() && s[p] == '*') ++p;
    if (p < s.size() && s[p] == ':') ++p;
    if (p < s.size() && s[p] != ' ' && s[p] != '\t' && s[p] != '\n') return 0;
    i = p;
    return letter ? low : raw;
}

std::string tidy_choice(std::string label) {
    const auto strip = [](std::string& t) {
        const auto junk = [](char c) {
            return c == ' ' || c == '\t' || c == '\r' || c == '*' || c == '_';
        };
        std::size_t a = 0, b = t.size();
        while (a < b && junk(t[a])) ++a;
        while (b > a && (junk(t[b - 1]) || t[b - 1] == '.' || t[b - 1] == ',' ||
                         t[b - 1] == ';')) --b;
        t = t.substr(a, b - a);
    };
    strip(label);
    return label;
}

bool is_open_ended(const std::string& label) {
    std::string low = label;
    for (char& c : low) c = char(std::tolower(static_cast<unsigned char>(c)));
    static const char* kOpen[] = {"or tell me", "or describe", "or say", "or type",
                                  "tell me in your own words", "something else",
                                  "or something else", "other", "none of these"};
    for (const char* o : kOpen)
        if (low.rfind(o, 0) == 0 || low == o) return true;
    return false;
}

}

std::vector<std::string> Shell::reply_choices(const std::string& text) {
    std::vector<std::string> out;
    if (text.find('?') == std::string::npos) return out;

    struct Hit { char mark; std::size_t marker_at, label_from; };
    std::vector<Hit> hits;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (i && text[i - 1] != '\n' &&
            !std::isspace(static_cast<unsigned char>(text[i - 1]))) continue;
        std::size_t j = i;
        const char m = read_list_marker(text, j);
        if (!m) continue;
        hits.push_back({m, i, j});
        i = j - 1;
    }
    std::size_t from = 0, len = 0;
    for (std::size_t k = 0; k < hits.size(); ++k) {
        if (hits[k].mark != 'a' && hits[k].mark != '1') continue;
        std::size_t n = 1;
        while (k + n < hits.size() && hits[k + n].mark == char(hits[k].mark + n)) ++n;
        if (n > len) { len = n; from = k; }
    }
    if (len < 2) return out;

    for (std::size_t k = 0; k < len && out.size() < 6; ++k) {
        const std::size_t start = hits[from + k].label_from;
        std::size_t end = k + 1 < len ? hits[from + k + 1].marker_at : text.size();
        const std::size_t nl = text.find('\n', start);
        if (nl != std::string::npos && nl < end) end = nl;
        if (end <= start) continue;
        const std::string label = tidy_choice(text.substr(start, end - start));
        if (label.empty() || label.size() > 160) continue;
        if (is_open_ended(label)) continue;
        out.push_back(label);
    }
    if (out.size() < 2) out.clear();
    return out;
}

void Shell::ai_refresh_status() {
    if (ai_->status_busy) return;
    if (ai_->status_worker.joinable()) ai_->status_worker.join();
    const auto client = (ai_page_ == 2 && !ai_page_url_.empty())
                            ? ai_server_client(ai_page_url_)
                            : ai_client();
    if (!client) {
        std::lock_guard<std::mutex> lock(ai_->mutex);
        ai_->status = "no server";
        return;
    }
    ai_->status_busy = true;
    ai_->status_worker = std::thread([st = ai_.get(), c = *client] {
        auto m = c.models();
        {
            std::lock_guard<std::mutex> lock(st->mutex);
            if (m.ok()) st->models = m->data;
        }
        const auto h = c.health();
        std::lock_guard<std::mutex> lock(st->mutex);
        if (h.ok()) st->status = "online (" + h->status + ")";
        else if (m.ok())
            st->status = "online (" + std::to_string(st->models.size()) + " models)";
        else st->status = h.error.message;
        if (!m.ok() && !h.ok()) st->models.clear();
        st->status_busy = false;
    });
}

static std::string pick_chat_model(const std::vector<gai::ModelInfo>& models,
                                   const std::string& want) {
    if (models.empty()) return want;
    for (const auto& m : models)
        if (m.id == want) return m.id;
    if (!want.empty())
        for (const auto& m : models)
            if (m.id.find(want) != std::string::npos) return m.id;
    bool any_labels = false;
    for (const auto& m : models) any_labels = any_labels || m.labelled();
    if (any_labels) {
        for (const auto& m : models)
            if (m.chats() && m.calls_tools() && m.id.find("Qwen") != std::string::npos) return m.id;
        for (const auto& m : models)
            if (m.chats() && m.calls_tools()) return m.id;
        for (const auto& m : models)
            if (m.chats()) return m.id;
    }
    const auto chatty = [](const std::string& id) {
        std::string low = id;
        for (char& c : low) c = char(std::tolower(static_cast<unsigned char>(c)));
        for (const char* no : {"whisper", "moonshine", "kokoro", "tts", "embedding",
                               "flux", "stable", "sdxl"})
            if (low.find(no) != std::string::npos) return false;
        return true;
    };
    for (const auto& m : models)
        if (m.id.find("Qwen") != std::string::npos && chatty(m.id)) return m.id;
    for (const auto& m : models)
        if (chatty(m.id)) return m.id;
    return models.front().id;
}

std::string pick_model_by_label(const std::vector<gai::ModelInfo>& models,
                                 const char* label, const char* name_hint) {
    for (const auto& m : models)
        if (m.has_label(label)) return m.id;
    for (const auto& m : models) {
        std::string low = m.id;
        for (char& c : low) c = char(std::tolower(static_cast<unsigned char>(c)));
        if (low.find(name_hint) != std::string::npos) return m.id;
    }
    return {};
}

void Shell::ai_send(std::string prompt, bool with_view) {
    if (prompt.empty() || ai_->busy) return;
    ai_join_worker();
    const auto client = ai_client();
    if (gui::PanelState* p = dock_.find("assistant")) p->open = true;
    dock_.collapse("assistant", false);
    if (!client) {
        std::lock_guard<std::mutex> lock(ai_->mutex);
        ai_->chat.push_back({"note",
            "(no AI server -- add one in Settings > AI, or start a Lemonade "
            "server on this network)", {}, {}, {}});
        return;
    }
    gai::ChatRequest req;
    {
        std::lock_guard<std::mutex> lock(ai_->mutex);
        AiChatLine said{"user", prompt, {}, {}, {}};
        if (!ai_->voice_clip.empty()) {
            said.audio = std::move(ai_->voice_clip);
            said.audio_mime = "audio/wav";
            ai_->voice_clip.clear();
        }
        ai_->chat.push_back(std::move(said));
        req.model = app_config_.ai_model;
        for (const auto& srv : app_config_.ai_servers)
            if (srv.url == app_config_.ai_active_url && !srv.default_model.empty())
                req.model = srv.default_model;
        {
            gai::Json kw = gai::Json::object();
            kw["reasoning_effort"] =
                !ai_effort_override_.empty() ? ai_effort_override_
                : app_config_.ai_reasoning_effort.empty()
                    ? "low"
                    : app_config_.ai_reasoning_effort;
            req.chat_template_kwargs.emplace() = std::move(kw);
        }
        ai_->stream.clear();
        ai_->stream_thinking.clear();
        ai_->error.clear();
        std::string persona;
        if (drawing_discipline() == DrawingDiscipline::Architectural) {
            persona =
                "\nTHIS IS AN ARCHITECTURE PROJECT. Draft to plan conventions, always:\n"
                "- WALLS: use draw_wall for every intended wall. It creates a closed "
                "2D wall region with thickness and a hatched core; adjoining runs "
                "share clean joined boundaries. Give centerlines that meet at corners "
                "or T-junctions. Do not construct walls as separate loose lines or "
                "overlapping capped rectangles. Specify the intended thickness.\n"
                "- DOORS: every opening gets draw_door. Swing doors show their "
                "swing; a GARAGE or overhead door is kind:\"rollup\" -- panel "
                "and tracks, NEVER a swing arc. Swing bedroom/bathroom doors "
                "INTO the room; 810mm/32in minimum clear width for "
                "accessibility unless the code says otherwise.\n"
                "- WINDOWS: every window gets draw_window -- frame, glass "
                "lines, and the ANSI36 glass hatch on A-GLAZ (glazing never "
                "wears a wall's ANSI31).\n"
                "- FRAMING SHOWS: draw_door and draw_window put the jack+king "
                "stud pack at every jamb, doubled for wide openings (IRC "
                "R602.7, Table R602.7(1)); never suppress it without a "
                "reason.\n"
                "- OPENINGS: draw_door and draw_window automatically cut their host "
                "wall faces. Use snap_to_wall when placing on an existing wall. "
                "Run plan_check after drafting and repair located gaps or crossings; "
                "use merge_overlaps for imported or legacy loose linework as needed.\n"
                "- CIRCULATION before area: get the FLOW right first, sizes "
                "are nominal. Every room is entered through a door from a "
                "hallway or an adjacent shared space -- no room is an "
                "island, and no bedroom is reached through another bedroom. "
                "Lay rooms along a hallway spine: living, kitchen and "
                "dining toward the entry, bedrooms and baths down the hall "
                "(36in/914mm minimum width), baths clustered near the "
                "bedrooms with plumbing back to back where it works.\n"
                "- ROOMS: label every room with label_room (name, and area when "
                "known). Check the library for minimum room sizes and egress.\n"
                "- DIMENSION exterior walls and every opening. Keep the layers: "
                "A-WALL, A-DOOR, A-GLAZ, A-ANNO-DIMS.\n"
                "- ALWAYS, without being asked: draw the STRUCTURE first (a "
                "closed exterior envelope of draw_wall runs, then interior "
                "partitions), include the MAIN ENTRANCE (a 36in/914mm door "
                "facing the street or approach), windows in habitable rooms, "
                "and finish by running plan_check -- fix every error and "
                "warning it reports before telling the user the plan is done.";
        } else if (drawing_discipline() == DrawingDiscipline::Mechanical) {
            persona = "\nThis is a MECHANICAL drawing: use part profiles, dimensions and "
                      "constraints. Do not interpret narrow closed part geometry as building walls.";
        } else if (project_type_ == "2d") {
            persona = "\nThis is a 2D DRAFTING project: precise linework, "
                      "dimensions on everything that matters, layers kept clean.";
        } else if (project_type_ == "3d") {
            persona = "\nThis is a 3D MODELLING project: sketch profiles, then "
                      "extrude/revolve into solids; keep sketches on their planes.";
        }
        persona += "\n" + ai_tools::drawing_context(*this)["drafting_guidance"].get<std::string>();
        if (!project_location_.empty())
            persona += "\nTHE PROJECT IS LOCATED IN: " + project_location_ +
                       ". That jurisdiction's codes govern every code answer; "
                       "check the library first -- they may already be archived.";
        req.messages.push_back(gai::ChatMessage::text(
            "system",
            "You are the assistant inside GLCAD, a CAD application. You have tools: "
            "read the drawing (document_summary, list_objects, measure), draft into it "
            "(draw_line, draw_rect, add_dimension, ...), and run_command for every GLCAD "
            "command. Coordinates are \"x,y\" in the user's display units. Prefer doing "
            "the work with tools over describing it. Be brief.\n"
            "THE DRAWING IS CURRENTLY IN " + std::string(gunit::name(canvas_.display_unit())) +
            " (" + gunit::suffix(canvas_.display_unit()) + "): every coordinate you "
            "pass and every number a tool hands back is a number of those, and each "
            "result repeats the unit and carries *_text fields already written with "
            "their marks. Quote the *_text form to the user.\n"
            "READING A DRAWING SOMEBODY ELSE DREW -- never say a plan cannot be "
            "measured before using these: count_objects for the layer tally, "
            "drawing_units when a measurement looks implausible (it settles the "
            "scale from the drawing's own door swings), list_text for every word on "
            "the sheet (room names, keynotes, equipment schedules, door specs), "
            "list_blocks for the symbols and their counts and attribute tags (doors, "
            "fixtures, exits -- the block NAME often states the size), measure_span "
            "for a clear width at a point (doors, corridors, aisles), measure_room "
            "for net floor area and perimeter, measure_path for travel distance to "
            "an exit. A code minimum is checked by measuring, not by guessing; if a "
            "measurement is genuinely unavailable, say which tool failed and what it "
            "reported.\n"
            "You can also reach the world: web_search and fetch_url for the open web, "
            "and a LOCAL standards library (search_standards / save_standard). "
            "Relevant library passages are included automatically with each message -- "
            "use them and cite the file. THE DEFAULT PROCESS for codes and standards: "
            "the library answers first; if it has nothing, INTERVIEW the user -- where "
            "is the project, what type of building -- then find the jurisdiction's "
            "codes online and fetch_url them WITH save_title, which archives and "
            "indexes them into the library in the same call. Never answer a code "
            "question from the open web without keeping the source.\n"
            "Draft OUTSIDE-IN, whatever the thing is: establish the OUTER "
            "envelope first, then subdivide inward: structure, then "
            "openings, then contents, then annotation. Never start with "
            "interior details and hope the outline comes out right. Begin "
            "an envelope with the plan_footprint tool and draw the walls it "
            "returns exactly as given. Stated sizes are NOMINAL: on-grid "
            "and close is correct practice -- treat a figure as exact only "
            "when the user says exact. NEVER do arithmetic in your "
            "thinking; run every product, area, unit conversion or derived "
            "coordinate through the calc tool, once, and trust its "
            "answer.\n"
            "BIAS TOWARD ACTING: when a tool can advance the work, CALL IT "
            "instead of reasoning about it. What plan_footprint returns IS "
            "the envelope -- settled, never re-derived -- and a number a "
            "tool computed is never checked again in your head. Spend your "
            "thinking only on what tools cannot decide: layout, flow, and "
            "what the user meant.\n"
            "When you are unsure of something that matters -- the project's "
            "location, a dimension, which of two layouts the user means -- ASK "
            "before drawing. Prefer a short multiple-choice list (a), (b), (c) "
            "ending with \"or tell me in your own words\", so answering is one "
            "keystroke and never a dead end." + persona));
        for (const auto& line : ai_->chat)
            if (line.role == "user" || line.role == "assistant")
                req.messages.push_back(gai::ChatMessage::text(line.role, line.text));
    }
    if (!ai_attach_.empty() && !req.messages.empty()) {
        gai::ChatMessage& last = req.messages.back();
        gai::ChatMessage with;
        with.role = "user";
        with.parts.push_back(gai::ContentPart::of_text(last.content));
        with.parts.push_back(gai::ContentPart::of_image(
            "data:" + ai_attach_mime_ + ";base64," + gai::base64(ai_attach_)));
        last = std::move(with);
        ai_attach_.clear();
        ai_attach_name_.clear();
    }
    if (with_view && !req.messages.empty()) {
        const auto png = snapshot_png(*this, int(viewport_width()), int(viewport_height()));
        if (!png.empty()) {
            gai::ChatMessage& last = req.messages.back();
            gai::ChatMessage looked;
            looked.role = "user";
            looked.parts.push_back(gai::ContentPart::of_text(last.content));
            looked.parts.push_back(gai::ContentPart::of_image(
                "data:image/png;base64," + gai::base64(png)));
            last = std::move(looked);
        }
    }
    req.tools.emplace() = ai_tools::registry();
    const bool speak = app_config_.ai_voice || ai_speak_once_;
    ai_speak_once_ = false;
    ai_->abort = false;
    ai_->busy = true;
    ai_->worker = gcm::BigStackThread([this, st = ai_.get(), c = *client,
                               req = std::move(req), speak,
                               user_prompt = prompt]() mutable {
        if (const auto m = c.models(); m.ok() && !m->data.empty())
            req.model = pick_chat_model(m->data, req.model);
        if (!st->abort && !req.messages.empty()) {
            const auto hits = ai_tools::library_lookup(*this, user_prompt, 3);
            if (!hits.empty()) {
                std::string ctx =
                    "Relevant passages from the user's local standards library "
                    "(cite the file when you rely on one):";
                for (const auto& h : hits)
                    ctx += "\n\n[" + h.file + "]\n" + h.text;
                // Appended to the leading system turn: Qwen's template rejects any other shape.
                if (!req.messages.empty() && req.messages.front().role == "system")
                    req.messages.front().content += "\n\n" + ctx;
                else
                    req.messages.insert(req.messages.begin(),
                                        gai::ChatMessage::text("system", ctx));
                std::lock_guard<std::mutex> lock(st->mutex);
                st->chat.push_back({"note",
                                    "(library: " + std::to_string(hits.size()) +
                                        " passage" + (hits.size() == 1 ? "" : "s") +
                                        " included)",
                                    {}, {}, {}});
            }
        }
        std::string final_text;
        while (!st->abort) {
            const auto r = c.chat_stream(
                req,
                [st](const std::string& delta, bool reasoning) {
                    std::lock_guard<std::mutex> lock(st->mutex);
                    (reasoning ? st->stream_thinking : st->stream) += delta;
                },
                &st->abort);
            if (!r.ok()) {
                if (st->abort) break;
                const bool no_tools_here =
                    r.error.message.find("enable-auto-tool-choice") != std::string::npos ||
                    r.error.message.find("tool-call-parser") != std::string::npos;
                if (no_tools_here && req.tools.has() && !req.tools.get().empty()) {
                    req.tools.clear();
                    {
                        std::lock_guard<std::mutex> lock(st->mutex);
                        st->chat.push_back(
                            {"note",
                             "(this server has tool calling disabled -- start vLLM with "
                             "--enable-auto-tool-choice --tool-call-parser hermes. "
                             "Answering without tools for now.)",
                             {}, {}, {}});
                        st->stream.clear();
                        st->stream_thinking.clear();
                    }
                    continue;
                }
                std::lock_guard<std::mutex> lock(st->mutex);
                st->chat.push_back({"note", "(" + r.error.message + ")", {}, {}, {}});
                break;
            }
            if (r->tool_calls.empty()) {
                AiChatLine line;
                line.role = "assistant";
                split_thinking(r->text, line.text, line.thinking);
                if (!r->reasoning.empty())
                    line.thinking = r->reasoning + (line.thinking.empty() ? "" : "\n" + line.thinking);
                final_text = line.text;
                std::lock_guard<std::mutex> lock(st->mutex);
                st->chat.push_back(std::move(line));
                break;
            }
            req.messages.push_back(gai::ChatMessage::assistant_calls(r->tool_calls, r->text));
            bool all_pure = !r->tool_calls.empty();
            for (const auto& tc : r->tool_calls)
                all_pure = all_pure && ai_tools::pure(tc);
            if (all_pure) {
                for (const auto& tc : r->tool_calls) {
                    if (st->abort) break;
                    {
                        std::lock_guard<std::mutex> lock(st->mutex);
                        st->chat.push_back({"tool", ai_tools::describe(tc), {}, {}, {}});
                        st->stream.clear();
            st->stream_thinking.clear();
                    }
                    {
                        std::lock_guard<std::mutex> lock(st->mutex);
                        st->pending_activity.push_back(ai_tools::activity_entry(tc,ai_tools::Origin::Assistant));
                    }
                    const auto result=ai_tools::execute(*this,tc,ai_tools::Origin::Silent);
                    {
                        std::lock_guard<std::mutex> lock(st->mutex);
                        st->pending_activity.push_back(ai_tools::activity_entry(tc,ai_tools::Origin::Assistant,result));
                    }
                    req.messages.push_back(gai::ChatMessage::tool_result(tc.id ? tc.id.get() : "",result));
                }
                continue;
            }
            std::unique_lock<std::mutex> lock(st->mutex);
            st->stream.clear();
            st->stream_thinking.clear();
            st->pending_calls = r->tool_calls;
            st->call_results.assign(r->tool_calls.size(), {});
            st->call_at = 0;
            st->confirm_state = 0;
            st->awaiting_tools = true;
            st->cv.wait(lock, [st] { return !st->awaiting_tools || st->abort; });
            if (st->abort) break;
            for (std::size_t i = 0; i < st->pending_calls.size(); ++i) {
                const auto& tc = st->pending_calls[i];
                req.messages.push_back(gai::ChatMessage::tool_result(
                    tc.id ? tc.id.get() : "", st->call_results[i]));
            }
            if (!st->shot.empty()) {
                gai::ChatMessage look;
                look.role = "user";
                look.parts.push_back(gai::ContentPart::of_text(
                    "Here is the screenshot you asked for."));
                look.parts.push_back(gai::ContentPart::of_image(
                    "data:image/png;base64," + gai::base64(st->shot)));
                req.messages.push_back(std::move(look));
                st->shot.clear();
            }
            st->pending_calls.clear();
            st->call_results.clear();
        }
        {
            std::lock_guard<std::mutex> lock(st->mutex);
            st->stream.clear();
            st->stream_thinking.clear();
        }
        if (st->abort) {
            std::lock_guard<std::mutex> lock(st->mutex);
            st->chat.push_back({"note", "(stopped)", {}, {}, {}});
        }
        if (speak && !final_text.empty() && !st->abort) ai_speak_reply(final_text);
        st->abort = false;
        st->busy = false;
    });
}

std::vector<Shell::AiChatLine> Shell::ai_transcript() const {
    std::lock_guard<std::mutex> lock(ai_->mutex);
    return ai_->chat;
}

bool Shell::ai_configured() const {
    if (!app_config_.ai_servers.empty()) return true;
    return !ai_->discovery.snapshot().empty();
}

void Shell::ai_stop_and_send_voice() {
    ai_recording_ = false;
    ai_call_ = false;
    call_heard_ = false;
    auto bytes = audio_record_end ? audio_record_end() : std::vector<gcm::u8>{};
    ai_transcribe_clip(std::move(bytes), true);
}

void Shell::ai_transcribe_clip(std::vector<gcm::u8> bytes, bool speak_back) {
    const auto note = [&](std::string m) {
        std::lock_guard<std::mutex> lock(ai_->mutex);
        ai_->chat.push_back({"note", "(" + std::move(m) + ")", {}, {}, {}});
    };
    if (ai_->busy) { note("still working on the last message -- try again in a moment"); return; }
    if (bytes.empty()) {
        note("heard nothing -- is the microphone on? macOS System Settings > "
             "Privacy & Security > Microphone");
        return;
    }
    const auto client = ai_client();
    if (!client) { note("no AI server -- add one in Settings > AI"); return; }
    std::string want_stt;
    for (const auto& sv : app_config_.ai_servers)
        if (sv.url == client->url()) want_stt = sv.default_stt;
    ai_join_worker();
    ai_->busy = true;
    ai_->worker = gcm::BigStackThread([st = ai_.get(), c = *client,
                               b = std::move(bytes), speak_back, want_stt] {
        std::string model = want_stt;
        if (const auto m = c.models(); m.ok() && !m->data.empty()) {
            if (!gai::has_model(m->data, model)) model.clear();
            if (model.empty())
                model = pick_model_by_label(m->data, "transcription", "whisper");
        }
        if (model.empty()) model = "whisper";
        const auto r = c.transcribe(model, b, "clip.wav", "audio/wav");
        std::string text = r.ok() ? r->text : std::string{};
        std::size_t a = text.find_first_not_of(" \t\r\n.,");
        const std::string trimmed = a == std::string::npos ? "" : text.substr(a);
        std::lock_guard<std::mutex> lock(st->mutex);
        if (!trimmed.empty() && trimmed != "[BLANK_AUDIO]") {
            st->voice_prompt = text;
            st->voice_from_call = speak_back;
            st->voice_clip = b;
        } else {
            st->chat.push_back({"note",
                r.ok() ? "(didn't catch that -- say it again?)"
                       : "(transcription failed: " + r.error.message + ")", {}, {}, {}});
        }
        st->busy = false;
    });
}

void Shell::ai_call_tick() {
    if (!(ai_recording_ && ai_call_)) { call_heard_ = false; return; }
    const double now = now_seconds();
    const float  lv = audio_record_level ? audio_record_level() : 0.0f;

    if (audio_playing && audio_playing()) {
        if (audio_record_take) (void)audio_record_take();
        call_heard_ = false;
        call_quiet_drop_ = now;
        return;
    }

    if (lv > 0.04f) {
        if (!call_heard_) call_started_ = now;
        call_heard_ = true;
        call_last_voice_ = now;
    }

    if (call_heard_ && now - call_last_voice_ > 1.0 &&
        call_last_voice_ - call_started_ > 0.25 && !ai_->busy) {
        call_heard_ = false;
        std::vector<gcm::u8> seg;
        if (audio_record_take) {
            seg = audio_record_take();
        } else if (audio_record_end && audio_record_begin) {
            seg = audio_record_end();
            ai_recording_ = audio_record_begin();
            if (!ai_recording_) ai_call_ = false;
        }
        if (!seg.empty()) ai_transcribe_clip(std::move(seg), true);
        call_quiet_drop_ = now;
        return;
    }

    if (!call_heard_ && audio_record_take && now - call_quiet_drop_ > 2.0) {
        (void)audio_record_take();
        call_quiet_drop_ = now;
    }
}

void Shell::ai_stash_screenshot(std::vector<gcm::u8> png) {
    std::lock_guard<std::mutex> lock(ai_->mutex);
    ai_->shot = std::move(png);
}

std::optional<std::vector<gcm::u8>> Shell::ai_speak_text(const gai::Client& c,
                                                         const std::string& text,
                                                         std::string& error) {
    gai::SpeechRequest req;
    req.input = text;
    for (const auto& sv : app_config_.ai_servers)
        if (sv.url == c.url()) {
            if (!sv.default_tts.empty()) req.model = sv.default_tts;
            if (!sv.tts_voice.empty()) req.voice = sv.tts_voice;
        }
    if (const auto m = c.models(); m.ok() && !m->data.empty()) {
        if (!gai::has_model(m->data, req.model)) req.model.clear();
        if (req.model.empty()) req.model = pick_model_by_label(m->data, "tts", "kokoro");
    }

    if (audio_stream_begin && audio_stream_feed && audio_stream_end &&
        audio_stream_begin()) {
        std::vector<gcm::u8> whole;
        const auto r = c.speech_stream(req, [&](const gcm::u8* p, std::size_t n) {
            whole.insert(whole.end(), p, p + n);
            audio_stream_feed(p, n);
        });
        audio_stream_end();
        if (r.ok()) return whole;
    }

    const auto r = c.speech(req);
    if (!r.ok()) { error = r.error.message; return std::nullopt; }
    if (audio_play_hook) audio_play_hook(*r, "audio/wav");
    return *r;
}

void Shell::ai_speak_reply(const std::string& text) {
    if (!audio_play_hook && !audio_stream_feed) return;
    const auto client = ai_client();
    if (!client) return;
    std::string err;
    auto clip = ai_speak_text(*client, text, err);
    if (!clip) {
        std::lock_guard<std::mutex> lock(ai_->mutex);
        ai_->chat.push_back({"note", "(speech failed: " + err + ")", {}, {}, {}});
        return;
    }
    std::lock_guard<std::mutex> lock(ai_->mutex);
    for (auto it = ai_->chat.rbegin(); it != ai_->chat.rend(); ++it)
        if (it->role == "assistant" && it->text == text) {
            it->audio = std::move(*clip);
            it->audio_mime = "audio/wav";
            break;
        }
}

void Shell::ai_replay_line(std::size_t index) {
    if (!audio_play_hook) return;
    std::string text;
    {
        std::lock_guard<std::mutex> lock(ai_->mutex);
        if (index >= ai_->chat.size()) return;
        AiChatLine& line = ai_->chat[index];
        if (line.role != "user" && line.role != "assistant") return;
        if (!line.audio.empty()) {
            audio_play_hook(line.audio,
                            line.audio_mime.empty() ? "audio/wav" : line.audio_mime);
            return;
        }
        text = line.text;
    }
    if (text.empty() || ai_->tts_busy) return;
    const auto client = ai_client();
    if (!client) return;
    if (ai_->tts_worker.joinable()) ai_->tts_worker.join();
    ai_->tts_busy = true;
    ai_->tts_worker = std::thread([this, st = ai_.get(), c = *client, text, index] {
        std::string err;
        if (auto clip = ai_speak_text(c, text, err)) {
            std::lock_guard<std::mutex> lock(st->mutex);
            if (index < st->chat.size()) {
                st->chat[index].audio = std::move(*clip);
                st->chat[index].audio_mime = "audio/wav";
            }
        }
        st->tts_busy = false;
    });
}

void Shell::ai_queue_tools(std::vector<gai::ToolCall> calls) {
    std::lock_guard<std::mutex> lock(ai_->mutex);
    ai_->call_results.assign(calls.size(), {});
    ai_->pending_calls = std::move(calls);
    ai_->call_at = 0;
    ai_->confirm_state = 0;
    ai_->awaiting_tools = true;
}

void Shell::process_ai_tools() {
    // Not re-entrant: view_screenshot builds a frame, and build_frame pumps this.
    if (in_ai_tools_) return;
    struct Reentry {
        bool& f;
        ~Reentry() { f = false; }
    } guard{in_ai_tools_};
    in_ai_tools_ = true;

    std::unique_lock<std::mutex> lock(ai_->mutex);
    for(auto& entry:ai_->pending_activity)log_line(std::move(entry));
    ai_->pending_activity.clear();
    if (!ai_->awaiting_tools) return;
    while (ai_->call_at < ai_->pending_calls.size()) {
        const gai::ToolCall call = ai_->pending_calls[ai_->call_at];
        if (ai_tools::needs_confirmation(call)) {
            if (ai_->confirm_state == 0) {
                ai_->confirm_state = 1;
                if (gui::PanelState* p = dock_.find("assistant")) p->open = true;
                dock_.collapse("assistant", false);
                return;
            }
            if (ai_->confirm_state == 1) return;
            if (ai_->confirm_state == 3) {
                ai_->chat.push_back({"tool", ai_tools::describe(call) + "  (denied)", {}});
                ai_->call_results[ai_->call_at] = "{\"error\":\"the user denied this action\"}";
                log_line(ai_tools::activity_entry(call,ai_tools::Origin::Assistant));
                log_line(ai_tools::activity_entry(call,ai_tools::Origin::Assistant,ai_->call_results[ai_->call_at]));
                ai_->confirm_state = 0;
                ++ai_->call_at;
                continue;
            }
            ai_->confirm_state = 0;
        }
        ai_->chat.push_back({"tool", ai_tools::describe(call), {}});
        lock.unlock();
        const std::string result = ai_tools::execute(*this, call, ai_tools::Origin::Assistant);
        lock.lock();
        ai_->call_results[ai_->call_at] = result;
        ++ai_->call_at;
    }
    ai_->awaiting_tools = false;
    lock.unlock();
    ai_->cv.notify_all();
}
}
