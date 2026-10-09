#pragma once

#include <string>
#include <vector>

#include "geometry/solid_edit.hpp"
#include "geometry/surface.hpp"

namespace gtopo {

enum class FeatureKind : gcm::u8 {
    Unknown,
    Hole,
    Pocket,
    Boss,
    Fillet,
    Chamfer,
    Slot,
    Rib,
    Through,
    PlanarFace,
};

const char* feature_kind_name(FeatureKind k) noexcept;

struct Feature {
    FeatureKind kind{FeatureKind::Unknown};

    std::vector<FaceId> faces;
    std::vector<FaceId> primary_faces;

    f64 confidence{0.0};            // 0..1
    std::string why;

    f64 radius{0.0};
    f64 depth{0.0};
    f64 width{0.0}, length{0.0};
    f64 axis[3]{0, 0, 1};
    f64 origin[3]{0, 0, 0};
    bool through{false};

    bool certain() const noexcept { return confidence >= 0.9; }
};

struct RecognitionSettings {
    f64 min_fillet_radius{1e-5};
    f64 max_fillet_radius{1e3};
    f64 chamfer_angle_tolerance{0.35};   // radians
    f64 slot_aspect{3.0};                // length over width to call it a slot
    bool find_holes{true};
    bool find_pockets{true};
    bool find_blends{true};
    bool find_bosses{true};
};

struct RecognitionResult {
    std::vector<Feature> features;
    std::size_t faces_explained{0};
    std::size_t faces_total{0};
    bool ok{false};
    const char* reason{""};

    f64 coverage() const noexcept {
        return faces_total ? static_cast<f64>(faces_explained) / faces_total : 0.0;
    }
};

RecognitionResult recognise(const Body& b, const Volume& v,
                            const RecognitionSettings& s = {});

std::vector<Feature> find_holes(const Body& b, const Volume& v,
                                const RecognitionSettings& s = {});
std::vector<Feature> find_blends(const Body& b, const Volume& v,
                                 const RecognitionSettings& s = {});
std::vector<Feature> find_pockets(const Body& b, const Volume& v,
                                  const RecognitionSettings& s = {});

std::vector<std::vector<FaceId>> connected_face_groups(
    const Body& b, const Volume& v, f64 angle_tolerance = 0.02);

}
