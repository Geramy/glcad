#pragma once

#include <string>
#include <vector>

#include "engine/block.hpp"
#include "engine/layer.hpp"
#include "geometry/dimension.hpp"
#include "geometry/nurbs.hpp"
#include "exchange/bitcode.hpp"
#include "exchange/dxf.hpp"
#include "geometry/snap.hpp"

namespace gdwg {

enum class Version : u8 { Unknown, R13, R14, R2000, R2004, R2007, R2010, R2013, R2018 };

const char* version_name(Version v) noexcept;
Version     version_of(const std::string& tag) noexcept;

struct SectionLocator {
    u8  number{0};
    u32 seeker{0};
    u32 size{0};
};

struct FileHeader {
    Version                     version{Version::Unknown};
    std::string                 tag;
    u8                          maintenance{0};
    u32                         preview_address{0};
    u16                         codepage{0};
    std::vector<SectionLocator> sections;

    const SectionLocator* section(u8 number) const;
};

// Entries are stored as deltas from the previous entry.
struct ObjectRef {
    u64 handle{0};
    u32 offset{0};
};

enum class ObjectType : u16 {
    Text = 1, Attrib = 2, AttDef = 3, Block = 4, EndBlk = 5, SeqEnd = 6,
    Insert = 7, MInsert = 8, Vertex2D = 10, Vertex3D = 11,
    Polyline2D = 15, Polyline3D = 16,
    Line = 19, DimOrdinate = 20, DimLinear = 21, DimAligned = 22, DimAng3Pt = 23,
    DimAng2Ln = 24, DimRadius = 25, DimDiameter = 26, Point = 27, Circle = 18, Arc = 17, MText = 44,
    Leader = 45,
    Viewport = 34, Ellipse = 35, LwPolyline = 77, Spline = 36, Ray = 40, Xline = 41,
    LayerTable = 50, Layer = 51, BlockHeader = 49,
    Hatch = 78,
};

const char* object_type_name(u16 type) noexcept;

struct RawObject {
    u64         handle{0};
    u32         offset{0};
    u32         size{0};
    u16         type{0};
    // End of object data in bits from body_at, not data_at; handle stream follows.
    u32         data_bits{0};
    // Bit after the size field; the size is a modular short, not fixed width.
    std::size_t body_at{0};
    std::size_t data_at{0};
};

inline std::size_t data_end_bits(const RawObject& o) noexcept {
    return o.body_at + o.data_bits;
}

bool read_object_header(const u8* data, std::size_t size, const ObjectRef& ref,
                        RawObject& out, Version version = Version::R2000);

struct EntityAppearance {
    gdoc::Attributes attrs;
    u64 layer_handle{0}, color_handle{0}, linetype_handle{0};
};
bool read_entity_appearance(const u8* data, std::size_t size, const RawObject& raw,
                           Version version, EntityAppearance& out);

struct LayerAppearance {
    gdoc::Layer layer;
    // Keyed by APPID handle; resolve the application name before interpreting.
    std::vector<std::pair<u64,u32>> extended_longs;
};
bool read_layer_appearance(const u8* data, std::size_t size, const RawObject& raw,
                          Version version, LayerAppearance& out);

// Returned in file drawing units.
bool read_dimension_style(const u8* data, std::size_t size, const RawObject& raw,
                          Version version, gdim::Style& out);

// Text style sizes are returned in file drawing units.
bool read_text_style(const u8* data, std::size_t size, const RawObject& raw,
                     Version version, gtext::TextStyleDef& out);

struct DimensionAssociation {
    struct PointRef {
        u8 index{0}, osnap{0};
        std::vector<u64> objects, intersection_objects;
        u32 subentity{0}, marker{0}, intersection_subentity{0}, intersection_marker{0};
        std::vector<std::string> paths, intersection_paths;
        double parameter{0}, x{0}, y{0}, z{0};
        bool last_point{false};
    };
    u64 handle{0}, owner{0}, dimension{0};
    u32 flags{0};
    bool trans_space{false};
    u8 rotated_type{0};
    std::vector<PointRef> points;
};
bool read_dimension_association(const u8* data, std::size_t size, const RawObject& raw,
                                Version version, DimensionAssociation& out);

struct Text {
    gcm::Lattice2 pos{};
    std::string   text;
    f64           height{0.0};
    f64           angle{0.0};
    std::string   layer{"0"};
    u64           layer_handle{0};
    i32           halign{0};     // 0 left, 1 centre, 2 right
    i32           valign{0};     // 0 baseline, 1 bottom, 2 middle, 3 top
    f64           width{0};
    std::string   style{"Standard"};
    u64           handle{0}, style_handle{0};
    gdoc::Attributes attrs;
};

struct R2004Sections {
    bool            ok{false};
    std::vector<u8> handles, objects, header, classes;
    std::vector<std::string> section_names;
    std::vector<std::string> notes;
};
bool read_r2004_sections(const u8* data, std::size_t size, R2004Sections& out, std::string& error);
bool read_r2007_sections(const u8* data, std::size_t size, R2004Sections& out, std::string& error);
bool decompress_r2007(const u8* src, std::size_t src_size, std::vector<u8>& dst, std::size_t dst_size,
                      std::string& error);
std::vector<u8> decompress_r2004(const u8* src, std::size_t src_size, std::size_t dst_size,
                                 std::string* error = nullptr, std::size_t* produced = nullptr,
                                 std::size_t* consumed = nullptr);
bool parse_object_map(const u8* map, std::size_t map_size, std::vector<ObjectRef>& out,
                      std::string& error);

struct Entity {
    gsnap::Shape shape;
    std::string  layer{"0"};
    // Name falls back to "0" both on layer 0 and when unresolved.
    u64          layer_handle{0};
    std::string  kind;
    u64          handle{0};
    u32          subentity{0};
};

struct DimensionRec {
    gdim::Dimension dim;
    std::string     layer{"0"};
    u64             layer_handle{0};
    u64             style_handle{0}, annotation_handle{0};
    u64             handle{0}, extension_handle{0};
};

struct HatchRec {
    std::string   pattern{"ANSI31"};
    bool          solid{false};
    f64           scale{1.0};
    f64           angle{0.0};        // degrees
    gcm::Lattice2 seed{};
    std::vector<gsnap::Shape> boundary;
    std::string   layer{"0"};
    u64           layer_handle{0};
};

struct ReadResult {
    FileHeader             header;
    std::vector<ObjectRef> objects;
    std::vector<Entity>    entities;
    std::vector<Text>      texts;
    std::vector<DimensionRec> dimensions;
    std::vector<DimensionAssociation> dimension_associations;
    std::vector<HatchRec>     hatches;
    std::vector<ggeo::NurbsCurve2<gcm::F64>> splines;
    std::vector<gdoc::Underlay> underlays;
    std::vector<gdxf::Layout> layouts; // paper geometry in metres; viewport views in model units
    int active_layout{-1};
    std::vector<std::pair<u16, std::string>> classes;
    gdoc::LayerTable       layers;
    std::vector<gtext::TextStyleDef> text_styles;
    std::vector<gdim::Style> dim_styles;
    gdoc::BlockTable       blocks;

    // -1 when the header walk could not be verified.
    i32         insunits{-1};

    bool        ok{false};
    std::string error;

    std::vector<std::string> skipped;
    std::size_t              unknown_objects{0};
    std::vector<RawObject>   raw;
    std::size_t              crc_failures{0};
};

bool read_header(const u8* data, std::size_t size, FileHeader& out,
                 std::string& error);

bool read_object_map(const u8* data, std::size_t size, const FileHeader& header,
                     std::vector<ObjectRef>& out, std::string& error);

ReadResult read(const u8* data, std::size_t size, const gcm::WorkingVolume& v);
ReadResult read_file(const std::string& path, const gcm::WorkingVolume& v);

}
