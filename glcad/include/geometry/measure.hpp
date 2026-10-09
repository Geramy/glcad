#pragma once

#include <string>
#include <vector>

#include "geometry/body.hpp"
#include "geometry/solid_edit.hpp"
#include "geometry/surface.hpp"

namespace gtopo {

struct MassProperties {
    bool ok{false};
    const char* reason{""};

    f64 volume{0.0};          // cubic metres
    f64 area{0.0};            // square metres of boundary
    f64 density{1.0};         // kg per cubic metre, as supplied
    f64 mass{0.0};

    f64 centroid[3]{0, 0, 0};

    f64 inertia[9]{};         // row-major, kg m^2
    f64 principal[3]{};
    f64 radius_of_gyration[3]{};

    f64 lo[3]{0, 0, 0}, hi[3]{0, 0, 0};
};

MassProperties mass_properties(const Body& b, const Volume& v, f64 density = 1.0);

struct SectionLoop {
    std::vector<f64> points;      // flat x,y,z triples, closed
    bool outer{true};
    f64  area{0.0};
};

struct SectionResult {
    bool ok{false};
    const char* reason{""};
    std::vector<SectionLoop> loops;
    f64 cut_area{0.0};
    std::vector<f64> segments;    // flat x0,y0,z0,x1,y1,z1
};

SectionResult section(const Body& b, const Plane& p, const Volume& v);

struct Measurement {
    bool ok{false};
    const char* reason{""};
    f64 value{0.0};
    std::string units;       // "m", "m^2" or "rad"
};

Measurement measure_edge_length(const Body& b, HalfedgeId e, const Volume& v);
Measurement measure_face_area(const Body& b, FaceId f, const Volume& v);
Measurement measure_face_perimeter(const Body& b, FaceId f, const Volume& v);
Measurement measure_angle_between_faces(const Body& b, FaceId a, FaceId c,
                                        const Volume& v);
Measurement measure_distance(const Body& b, VertexId a, VertexId c,
                             const Volume& v);

Measurement measure_clearance(const Body& a, const Body& b, const Volume& v);

}
