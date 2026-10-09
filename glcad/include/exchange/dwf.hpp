#pragma once

#include <string>
#include <vector>

#include "exchange/pdf.hpp"

namespace gplot {

struct DwfPage {
    std::string          name{"Sheet"};
    f64                  width{0.297}, height{0.210};   // metres
    std::vector<PdfPath> paths;
    std::vector<PdfText> texts;
    std::vector<PdfImage> images;
};

class DwfWriter {
public:
    void begin_page(std::string name, f64 width, f64 height);
    void end_page();

    void add(const PdfPath& p);
    void add(const PdfText& t);
    void add(const PdfImage& image);

    void declare_layer(const std::string& name, bool visible = true);

    void set_title(std::string t) { title_ = std::move(t); }
    void set_author(std::string a) { author_ = std::move(a); }
    void set_font(std::shared_ptr<const std::vector<gcm::u8>> bytes) { font_ = std::move(bytes); }

    std::vector<gcm::u8> build() const;

    bool write(gio::Writer& w) const;
    bool write_file(const std::string& path) const;
    bool write_url(std::string_view url) const;

    std::size_t page_count() const noexcept { return pages_.size(); }
    std::size_t path_count() const noexcept;

private:
    std::vector<DwfPage>                      pages_;
    std::vector<std::pair<std::string, bool>> layers_;
    std::string                               title_{"GLCAD Drawing"};
    std::string                               author_{"GLCAD"};
    bool                                      in_page_{false};
    std::shared_ptr<const std::vector<gcm::u8>> font_;
};

struct DwfJob {
    const SheetSet*         sheets{nullptr};
    const PlotStyleTable*   styles{nullptr};
    const gdoc::LayerTable* layers{nullptr};
    gcm::WorkingVolume      volume{};
    std::vector<PlotJob::SpaceContent> content;
};

PlotResult publish(const DwfJob& job, DwfWriter& out);

struct ZipEntry {
    std::string          name;
    std::vector<gcm::u8> data;
};

std::vector<gcm::u8> zip(const std::vector<ZipEntry>& entries);

bool unzip_entry(const std::vector<gcm::u8>& archive, std::string_view name,
                 std::vector<gcm::u8>& out);

std::vector<std::string> zip_names(const std::vector<gcm::u8>& archive);

}
