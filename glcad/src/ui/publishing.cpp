#include "ui/shell.hpp"
#include "engine/document_file.hpp"
#include "exchange/dwf.hpp"
#include "ui/frame_types.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace gcapp {
namespace {
std::string extension(const std::string& path) {
    auto ext = std::filesystem::path(path).extension().string();
    for (auto& c : ext) c = char(std::tolower(static_cast<unsigned char>(c)));
    return ext;
}
bool no_more(std::istringstream& in) {
    in >> std::ws;
    return in.eof();
}
}

bool Shell::save_sheet_set(const std::string& path) {
    if (path.empty() || extension(path) != ".gss") { log_.push_back("SHEETSET: save to a .gss file"); return false; }
    const auto base = std::filesystem::absolute(path).parent_path();
    std::ostringstream text;
    text << "GLCAD_SHEET_SET 1\n";
    for (const auto& sheet : publish_sheets_) {
        std::error_code ec;
        auto relative = std::filesystem::relative(sheet.drawing, base, ec);
        const auto source = ec || relative.empty() ? sheet.drawing : relative.generic_string();
        text << std::quoted(source) << ' ' << std::quoted(sheet.layout) << ' '
             << std::quoted(sheet.name) << '\n';
    }
    const auto bytes = text.str();
    gio::FileWriter writer;
    if (!writer.open(path).ok ||
        writer.write(reinterpret_cast<const gcm::u8*>(bytes.data()), bytes.size()) != bytes.size() ||
        !writer.close().ok) {
        log_.push_back("SHEETSET: could not save " + path);
        return false;
    }
    log_.push_back("saved sheet set: " + path);
    return true;
}

bool Shell::load_sheet_set(const std::string& path) {
    std::ifstream file(std::filesystem::u8path(path));
    std::string line;
    std::vector<PublishSheet> sheets;
    const auto fail = [&] { log_.push_back("SHEETSET: invalid or unreadable set: " + path); return false; };
    if (!std::getline(file, line) || line != "GLCAD_SHEET_SET 1") return fail();
    const auto base = std::filesystem::absolute(path).parent_path();
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        if (sheets.size() >= 10000 || line.size() > 65536) return fail();
        std::istringstream in(line);
        PublishSheet sheet;
        if (!(in >> std::quoted(sheet.drawing) >> std::quoted(sheet.layout) >> std::quoted(sheet.name)) ||
            !no_more(in) || sheet.drawing.empty() || sheet.layout.empty() || sheet.name.empty()) return fail();
        sheet.drawing = (base / sheet.drawing).lexically_normal().string();
        sheets.push_back(std::move(sheet));
    }
    if (!file.eof()) return fail();
    publish_sheets_ = std::move(sheets);
    log_.push_back("loaded " + std::to_string(publish_sheets_.size()) + " sheet(s)");
    return true;
}

bool Shell::sheet_set_command(const std::string& arg) {
    std::istringstream in(arg);
    std::string verb;
    in >> verb;
    for (auto& c : verb) c = char(std::toupper(static_cast<unsigned char>(c)));
    const auto usage = [&] {
        log_.push_back("SHEETSET NEW | ADD \"drawing\" \"layout\" [\"sheet name\"] | REMOVE n | MOVE n to | LIST | SAVE/OPEN \"set.gss\"; PUBLISH SET \"output.pdf\"");
        return true;
    };
    if (verb == "NEW" && no_more(in)) {
        publish_sheets_.clear();
        log_.push_back("new empty sheet set");
    } else if (verb == "ADD") {
        PublishSheet sheet;
        if (!(in >> std::quoted(sheet.drawing) >> std::quoted(sheet.layout))) return usage();
        if (!no_more(in) && !(in >> std::quoted(sheet.name))) return usage();
        if (!no_more(in) || sheet.drawing.empty() || sheet.layout.empty() || publish_sheets_.size() >= 10000) return usage();
        if (sheet.name.empty()) sheet.name = sheet.layout;
        sheet.drawing = std::filesystem::absolute(sheet.drawing).lexically_normal().string();
        publish_sheets_.push_back(std::move(sheet));
        log_.push_back("added sheet " + std::to_string(publish_sheets_.size()));
    } else if (verb == "REMOVE" || verb == "MOVE") {
        std::size_t from = 0, to = 0;
        if (!(in >> from) || from == 0 || from > publish_sheets_.size()) return usage();
        if (verb == "MOVE" && (!(in >> to) || to == 0 || to > publish_sheets_.size())) return usage();
        if (!no_more(in)) return usage();
        auto sheet = std::move(publish_sheets_[from - 1]);
        publish_sheets_.erase(publish_sheets_.begin() + from - 1);
        if (verb == "MOVE") publish_sheets_.insert(publish_sheets_.begin() + to - 1, std::move(sheet));
        log_.push_back("sheet set updated");
    } else if (verb == "SAVE" || verb == "OPEN") {
        std::string path;
        if (!(in >> std::quoted(path)) || !no_more(in)) return usage();
        if (verb == "SAVE") save_sheet_set(path); else load_sheet_set(path);
    } else if ((verb.empty() || verb == "LIST") && no_more(in)) {
        log_.push_back(std::to_string(publish_sheets_.size()) + " sheet(s)");
        for (std::size_t i = 0; i < publish_sheets_.size(); ++i) {
            const auto& s = publish_sheets_[i];
            log_.push_back(std::to_string(i + 1) + ": " + s.name + " — " + s.drawing + " / " + s.layout);
        }
    } else return usage();
    return true;
}

bool Shell::prepare_sheet_set(gplot::PdfWriter& pdf) {
    const auto fail=[&](std::string message){log_.push_back("PUBLISH: "+message);return false;};
    if(publish_sheets_.empty()) return fail("sheet set is empty; use SHEETSET ADD");
    for (const auto& entry : publish_sheets_) {
        Shell source;
        const auto source_ext = extension(entry.drawing);
        bool loaded = false;
        if (source_ext == ".glcad") {
            gfile::Contents contents;
            const auto result = gfile::load_file(entry.drawing, contents);
            loaded = result.ok && source.load_contents(contents, entry.drawing, true, "publishing");
        } else if (source_ext == ".dwg" || source_ext == ".dxf") {
            loaded = source.import_file(entry.drawing);
            source.document_path_ = entry.drawing;
        }
        if (!loaded) return fail("cannot read " + entry.drawing);
        const auto found = std::find_if(source.layouts_.begin(), source.layouts_.end(),
            [&](const Layout& l) { return l.name == entry.layout; });
        if (found == source.layouts_.end()) return fail("layout '" + entry.layout + "' missing in " + entry.drawing);
        if (!source.select_layout(int(found - source.layouts_.begin()))) return fail("cannot select " + entry.layout);
        const auto first = pdf.page_count();
        const auto result = source.plot_sheet(pdf);
        if (!result.ok) return fail(entry.name + ": " + result.message);
        if(pdf.page_count()!=first+1) return fail("layout did not produce exactly one page: "+entry.layout);
    }
    return true;
}

bool Shell::publish_sheet_set(const std::string& path) {
    const auto ext=extension(path);
    if(ext!=".pdf" && ext!=".dwfx") {log_.push_back("PUBLISH: use an output .pdf or .dwfx path");return false;}
    gplot::PdfWriter pdf;pdf.set_title(file_title(path));
    if(!prepare_sheet_set(pdf)) return false;
    bool written=false;
    if(ext==".pdf") written=pdf.write_file(path);
    else {
        gplot::DwfWriter dwf;dwf.set_title(file_title(path));
        for(std::size_t i=0;i<pdf.page_count();++i) {
            const auto [w,h]=pdf.page_size(i);dwf.begin_page(publish_sheets_[i].name,w,h);
            for(const auto& p:pdf.paths_of_page(i)) {dwf.declare_layer(p.layer);dwf.add(p);}
            for(const auto& t:pdf.texts_of_page(i)) {dwf.declare_layer(t.layer);dwf.add(t);}
            for(const auto& image:pdf.images_of_page(i)) dwf.add(image);
            dwf.end_page();
        }
        written=dwf.write_file(path);
    }
    if(!written) {log_.push_back("PUBLISH: could not write "+path);return false;}
    log_.push_back("published "+std::to_string(pdf.page_count())+" sheet(s): "+path);
    return true;
}
}

namespace gcapp {
Shell::PlotPreview::~PlotPreview() {
    std::error_code ec;
    if (!path.empty()) std::filesystem::remove(path, ec);
}

bool Shell::open_plot_preview(bool sheet_set) {
    auto preview = std::make_shared<PlotPreview>();
    if(sheet_set) {
        preview->document.set_title("Sheet set");
        if(!prepare_sheet_set(preview->document)) return false;
        for(const auto& sheet:publish_sheets_) preview->names.push_back(sheet.name);
    } else {
        const auto result = plot_sheet(preview->document);
        if (!result.ok) { log_.push_back("PREVIEW: " + result.message); return false; }
    }
    static std::atomic<unsigned> sequence{0};
    std::error_code ec;
    const auto temp = std::filesystem::temp_directory_path(ec);
    if (ec) { log_.push_back("PREVIEW: temporary directory unavailable"); return false; }
    preview->path = (temp / ("glcad-plot-" + recovery_id_ + "-" + std::to_string(++sequence) + ".pdf")).string();
    if (!preview->document.write_file(preview->path)) { log_.push_back("PREVIEW: cannot prepare page"); return false; }
    std::string error;
    auto image = std::make_shared<gplot::RasterImage>(gplot::load_underlay(preview->path, 1, error, 4096));
    if (!image->valid()) { log_.push_back("PREVIEW: " + error); return false; }
    preview->image = std::move(image);
    plot_preview_ = std::move(preview);
    ui_wants_mouse_ = true;
    hover_pending_ = false;
    input_.pressed = input_.released = input_.down = false;
    log_.push_back("Plot preview — drag to pan, scroll to zoom, Escape to return");
    return true;
}

bool Shell::set_plot_preview_page(std::size_t page) {
    if(!plot_preview_ || page<1 || page>plot_preview_->document.page_count()) {
        log_.push_back("PREVIEW: page is outside the prepared document");return false;
    }
    if(page==plot_preview_->page) return true;
    std::string error;
    auto image=std::make_shared<gplot::RasterImage>(gplot::load_underlay(plot_preview_->path,int(page),error,4096));
    if(!image->valid()) {log_.push_back("PREVIEW: "+error);return false;}
    plot_preview_->image=std::move(image);plot_preview_->page=page;
    plot_preview_->zoom=1;plot_preview_->pan_x=plot_preview_->pan_y=0;
    plot_preview_->dragging=false;
    return true;
}

void Shell::close_plot_preview() {
    plot_preview_.reset();
    input_.pressed = input_.released = input_.down = false;
    ui_wants_mouse_ = false;
    lifted_key_ = ~gcm::u64{0};
    mesh_dirty_ = true;
    layout_frame_pending_ = true;
}

bool Shell::save_plot_preview(const std::string& path) const {
    return plot_preview_ && extension(path) == ".pdf" && plot_preview_->document.write_file(path);
}
}

namespace gcapp {
void Shell::build_plot_preview_frame() {
    const auto preview = plot_preview_;
    ui_.clear(); text_.clear(); tris_.clear(); images_.clear();
    lines_.clear(); points_.clear(); arcs_.clear(); mesh_.clear();
    mesh_state_.clear(); ++mesh_version_;
    static_lines_.clear(); static_points_.clear(); static_arcs_.clear();
    sketch_lines_ = 0;
    lifted_key_ = ~gcm::u64{0};
    mesh_dirty_ = true;
    const gui::Rect screen{0, 0, float(vw_), float(vh_)};
    const float pad = float(px(12));
    int rows = 1;
    float available_x = pad;
    std::vector<double> widths{72.,88.,36.,36.,96.,72.};
    if(preview->document.page_count()>1) {widths.push_back(96);widths.push_back(72);}
    for (double width : widths) {
        if (available_x + px(width) > screen.w - pad && available_x > pad) { ++rows; available_x = pad; }
        available_x += float(px(width)) + pad;
    }
    const float header = float(px(56 + (rows - 1) * 44 + (preview->document.page_count()>1?28:0)));
    preview->toolbar_height = header;
    const gui::Rect page_area{pad, header + pad, std::max(1.0f, screen.w - pad * 2),
                              std::max(1.0f, screen.h - header - pad * 2)};
    const auto& theme = themes_.active();
    const auto saved_style = ui_ctx_.style();
    ui_ctx_.style().font_size = float(px(14));
    ui_ctx_.begin(screen, input_, theme);
    const auto background = theme.get("ui.surface");
    ui_ctx_.draw().rect({0, 0, screen.w, header}, {background.r, background.g, background.b, 1});
    float x = pad, y = pad;
    const auto button = [&](const char* label, float width, bool enabled=true) {
        if (x + width > screen.w - pad && x > pad) { x = pad; y += float(px(44)); }
        const bool hit = gui::button(ui_ctx_, {x, y, width, float(px(32))}, label, enabled);
        x += width + pad;
        return hit;
    };
    const bool close = button("Close", float(px(72)));
    if (button("Fit page", float(px(88)))) { preview->zoom = 1; preview->pan_x = preview->pan_y = 0; }
    if (button("-", float(px(36)))) preview->zoom = std::max(.25, preview->zoom / 1.25);
    if (button("+", float(px(36)))) preview->zoom = std::min(8.0, preview->zoom * 1.25);
    if (button("Save PDF", float(px(96)))) {
        if (file_hook_) request_file(FileAsk::ExportPdf, [this, preview](std::string path) {
            if (path.empty()) return;
            if (extension(path) != ".pdf") path += ".pdf";
            log_.push_back(preview->document.write_file(path) ? "saved preview PDF: " + path : "could not save preview PDF");
        });
        else log_.push_back("PREVIEW SAVE \"file.pdf\"");
    }
    if (button("Print", float(px(72)))) print_plot();
    if(preview->document.page_count()>1) {
        if(button("Previous",float(px(96)),preview->page>1)) set_plot_preview_page(preview->page-1);
        if(button("Next",float(px(72)),preview->page<preview->document.page_count())) set_plot_preview_page(preview->page+1);
        std::string title="Page "+std::to_string(preview->page)+" / "+std::to_string(preview->document.page_count());
        if(preview->page<=preview->names.size()) title+=" — "+preview->names[preview->page-1];
        gui::label(ui_ctx_,{pad,header-float(px(26)),screen.w-2*pad,float(px(24))},title);
    }
    const auto& image = *preview->image;
    const double fit = std::min(page_area.w / double(image.width), page_area.h / double(image.height));
    const float w = float(image.width * fit * preview->zoom);
    const float h = float(image.height * fit * preview->zoom);
    const float left = page_area.x + (page_area.w - w) / 2 + float(preview->pan_x);
    const float top = page_area.y + (page_area.h - h) / 2 + float(preview->pan_y);
    ImageDraw draw; draw.image = preview->image;
    TextVertex vertices[4]{};
    for (int i = 0; i < 4; ++i) {
        const float u = i == 1 || i == 2 ? 1.0f : 0.0f;
        const float v = i >= 2 ? 1.0f : 0.0f;
        vertices[i] = {{left + u * w, top + v * h}, {u, v}, {1, 1, 1, 1},
                       {page_area.x, page_area.y, page_area.w, page_area.h}};
    }
    for (int i : {0, 1, 2, 0, 2, 3}) draw.vertices.push_back(vertices[i]);
    images_.push_back(std::move(draw));
    ui_ctx_.end();
    ui_ctx_.style() = saved_style;
    emit_ui_commands();
    ui_wants_mouse_ = true;
    input_.pressed = input_.released = input_.right_pressed = false;
    input_.typed.clear(); input_.wheel = 0;
    if (close) close_plot_preview();
}
}

namespace gcapp {
bool Shell::print_plot(const std::string& printer) {
    if (!print_hook_) { log_.push_back("PRINT: printer service unavailable"); return false; }
    if (!plot_preview_ && !open_plot_preview()) return false;
    const auto preview = plot_preview_;
    input_.pressed = input_.released = input_.down = false;
    std::string message;
    const bool submitted = print_hook_(preview->path, printer, message);
    log_.push_back(message.empty() ? (submitted ? "print job submitted" : "printing cancelled") : message);
    return submitted;
}
}
