#pragma once

#include <optional>
#include <string>
#include <vector>

#include "engine/block.hpp"
#include "engine/entity.hpp"
#include "engine/linetype.hpp"
#include "engine/frame.hpp"
#include "engine/transport.hpp"
#include "geometry/snap.hpp"

namespace gfile {

using gcm::f64;
using gcm::i64;
using gcm::u8;
using gcm::u16;
using gcm::u32;
using gcm::u64;
using gcm::i32;

inline constexpr int kSchemaVersion = 2;

struct Error {
    bool        ok{true};
    std::string message;
    explicit operator bool() const noexcept { return ok; }
    static Error good() { return {}; }
    static Error fail(std::string m) { return {false, std::move(m)}; }
};

struct Contents {
    gcm::WorkingVolume volume{};
    std::string        units{"mm"};
    int                precision{4};
    std::string        created_by{"GLCAD"};

    gdoc::LayerTable     layers;
    gdoc::LinetypeTable  linetypes;
    gdoc::MultilineTable multilines;
    gdoc::BlockTable     blocks;

    std::vector<gdoc::Space> spaces;
    gdoc::SpaceId            current_space{0};

    std::vector<gdoc::Entity> entities;

    struct SpaceGeometry {
        u32                       space{0};
        std::vector<gsnap::Shape> shapes;
        std::vector<std::string>  layers;
    };
    std::vector<SpaceGeometry> geometry;

    struct ForeignFrame {
        u16             kind{0};
        std::vector<u8> payload;
    };
    std::vector<ForeignFrame> foreign;

    std::size_t shape_count() const noexcept {
        std::size_t n = 0;
        for (const auto& g : geometry) n += g.shapes.size();
        return n;
    }
};

std::vector<u8> encode(const Contents& c);
Error           decode(const u8* data, std::size_t n, Contents& c);
inline Error    decode(const std::vector<u8>& v, Contents& c) {
    return decode(v.data(), v.size(), c);
}

void from_document(const gdoc::Document& d, Contents& c);
void to_document(const Contents& c, gdoc::Document& d);

Error validate_regions(const Contents& c);
Error validate_geometry(const Contents& c);
Error write_document(gio::Writer& w, const Contents& c);

struct ReadReport {
    int         format_version{0};
    std::size_t frames_read{0};
    std::size_t frames_skipped{0};
    std::size_t deltas_applied{0};
    bool        future_version{false};
};
Error read_document(gio::Reader& r, Contents& c, ReadReport* report = nullptr);

Error save(std::string_view url, const Contents& c);
Error load(std::string_view url, Contents& c, ReadReport* report = nullptr);

Error save_file(const std::string& path, const Contents& c);
Error load_file(const std::string& path, Contents& c, ReadReport* report = nullptr);

struct SpaceDelta {
    u32 space{0};
    u32 at{0};
    u32 removed{0};
    std::vector<gsnap::Shape> inserted;
    std::vector<std::string>  layers;

    bool empty() const noexcept { return removed == 0 && inserted.empty(); }
};

struct Delta {
    std::optional<Contents> metadata;
    std::optional<std::vector<u32>> geometry_order;
    std::vector<SpaceDelta>  spaces;
    std::vector<std::string> removed_layers;
    std::vector<gdoc::Layer> changed_layers;
    u32                      sequence{0};

    bool empty() const noexcept {
        if (metadata || geometry_order || !removed_layers.empty() || !changed_layers.empty()) return false;
        for (const auto& s : spaces)
            if (!s.empty()) return false;
        return true;
    }
};

Delta diff(const Contents& before, const Contents& after);
void  apply(Contents& c, const Delta& d);

std::vector<u8> encode_delta(const Delta& d);
Error           decode_delta(const u8* data, std::size_t n, Delta& d);

Error append_delta(const std::string& path, const Delta& d, u64* new_size = nullptr);

struct Transaction {
    u32         sequence{0};
    std::string command;
    std::string detail;
    i64         timestamp{0};
    std::vector<u8> payload;
};

class TransactionLog {
public:
    u32  record(std::string_view command, std::string_view detail = "",
                i64 timestamp = 0);
    u32  record(const Delta& d, std::string_view command, i64 timestamp = 0);

    std::size_t size() const noexcept { return entries_.size(); }
    const std::vector<Transaction>& all() const noexcept { return entries_; }
    void clear() { entries_.clear(); }

    Error write(gio::Writer& w) const;
    Error read(gio::Reader& r);
    Error append_to(const std::string& path) const;

    std::size_t replay(Contents& c, u32 through = 0xFFFFFFFFu) const;

private:
    std::vector<Transaction> entries_;
    u32                      next_{1};
};

struct TessellationCache {
    struct Entry {
        u32              space{0};
        u32              shape{0};
        std::vector<f64> points;
        u64              source_hash{0};
    };
    std::vector<Entry> entries;

    bool valid_for(std::size_t index, u64 hash) const;
    void invalidate(u32 space, u32 shape);
    std::size_t size() const noexcept { return entries.size(); }
};

std::vector<u8> encode_cache(const TessellationCache& c);
Error           decode_cache(const u8* data, std::size_t n, TessellationCache& c);

gdoc::BlockTable template_block_definitions(const gdoc::BlockTable& blocks);
Error save_template(const std::string& path, const Contents& c);
Error load_template(const std::string& path, Contents& c);

struct Package {
    Contents                 document;
    std::vector<std::string> fonts;
    std::vector<std::string> xrefs;
    std::vector<std::string> images;
    std::string              note;
};

Error save_package(const std::string& path, const Package& p);
Error load_package(const std::string& path, Package& p);

class DocumentFile {
public:
    Error create(const std::string& path);
    Error open(const std::string& path);
    void  close();
    bool  is_open() const noexcept { return !path_.empty(); }
    const std::string& path() const noexcept { return path_; }

    Error write(const Contents& c);
    Error read(Contents& c);

    int  file_version() const noexcept { return file_version_; }
    bool needs_upgrade() const noexcept { return file_version_ < kSchemaVersion; }
    bool is_future_version() const noexcept { return file_version_ > kSchemaVersion; }

    const ReadReport& last_read() const noexcept { return report_; }

private:
    std::string path_;
    int         file_version_{0};
    ReadReport  report_{};
};

}
