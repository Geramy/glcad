#include "geometry/clip.hpp"
#include "geometry/region.hpp"
#include "test.hpp"
#include <algorithm>
#include <cmath>
namespace {
gcm::WorkingVolume volume(){gcm::WorkingVolume v;v.extent=100;v.resolution_bits=40;return v;}
gcm::Lattice2 point(double x,double y){return gcm::to_lattice(volume(),x,y);}
gsnap::Shape line(double x0,double y0,double x1,double y1){gsnap::Shape s;s.a=point(x0,y0);s.b=point(x1,y1);return s;}
}
TEST(clipping_concave_windows_splits_every_crossing_and_preserves_the_exterior) {
    const std::vector<gcm::Lattice2> boundary={point(0,0),point(4,0),point(4,4),point(3,4),point(3,1),point(1,1),point(1,4),point(0,4)};
    const auto inside=gclip::shapes({line(-1,2,5,2)},boundary),outside=gclip::shapes({line(-1,2,5,2)},boundary,true);
    CHECK(inside.ok);CHECK(inside.shapes.size()==2);CHECK(outside.ok);CHECK(outside.shapes.size()==3);
    if(inside.shapes.size()==2){CHECK(std::abs(inside.shapes[0].a.x-point(0,2).x)<=1);CHECK(std::abs(inside.shapes[0].b.x-point(1,2).x)<=1);CHECK(std::abs(inside.shapes[1].a.x-point(3,2).x)<=1);CHECK(std::abs(inside.shapes[1].b.x-point(4,2).x)<=1);}
}
TEST(clipping_circular_and_elliptical_arcs_keeps_analytic_intersections) {
    gsnap::Shape circle;circle.kind=gsnap::ShapeKind::Circle;circle.a=point(0,0);circle.radius=point(2,0).x;
    const std::vector<gcm::Lattice2> half={point(0,-3),point(5,3)};
    for(bool inverted:{false,true}) {
        const auto clipped=gclip::shapes({circle},half,inverted);CHECK(clipped.ok);double sweep=0;
        for(const auto& arc:clipped.shapes){CHECK(arc.kind==gsnap::ShapeKind::Arc);CHECK(arc.radius==circle.radius);sweep+=arc.end_angle-arc.start_angle;}
        CHECK(std::abs(sweep-std::acos(-1.))<1e-10);
    }
    auto ellipse=circle;ellipse.kind=gsnap::ShapeKind::Ellipse;ellipse.b=point(4,0);ellipse.start_angle=.1;ellipse.end_angle=2.8;
    const auto clipped=gclip::shapes({ellipse},half);CHECK(clipped.ok);CHECK(clipped.shapes.size()==1);
    if(!clipped.shapes.empty()){CHECK(clipped.shapes[0].kind==gsnap::ShapeKind::Ellipse);CHECK(std::abs(clipped.shapes[0].start_angle-.1)<1e-10);CHECK(std::abs(clipped.shapes[0].end_angle-std::acos(-1.)/2)<1e-10);}
}
TEST(inverted_clipping_of_filled_regions_creates_a_real_hole) {
    auto circle=line(0,0,0,0);circle.kind=gsnap::ShapeKind::Circle;circle.radius=point(4,0).x;circle.region=true;circle.poly=1;
    const auto result=gclip::shapes({circle},{point(-1,-1),point(1,1)},true);CHECK_MSG(result.ok,"%s",result.error.c_str());
    const auto region=gregion::from_members(result.shapes);CHECK_MSG(region.ok,"%s",region.error.c_str());
    if(region.ok){CHECK(region.region.loops.size()==2);CHECK(!region.region.contains(point(0,0)));CHECK(std::abs(region.region.area(volume())-(16*std::acos(-1.)-4))<1e-7);}
}
TEST(clipping_infinite_geometry_retains_unbounded_exterior_rays) {
    auto xline=line(0,0,1,0);xline.kind=gsnap::ShapeKind::Xline;
    const auto result=gclip::shapes({xline},{point(-2,-1),point(2,1)},true);CHECK(result.ok);CHECK(result.shapes.size()==2);
    for(const auto& ray:result.shapes)CHECK(ray.kind==gsnap::ShapeKind::Ray);
    CHECK(!gclip::shapes({xline},{point(0,0),point(1,1),point(0,1),point(1,0)}).ok);
}

TEST(rotated_ellipse_clipping_matches_independent_point_membership) {
    gsnap::Shape ellipse;ellipse.kind=gsnap::ShapeKind::Ellipse;ellipse.a=point(1,1);ellipse.b=point(4,5);ellipse.radius=point(2,0).x;
    ellipse.start_angle=-1;ellipse.end_angle=4;
    for(bool inverted:{false,true}) {
        const auto clipped=gclip::shapes({ellipse},{point(0,-1),point(4,3)},inverted);CHECK(clipped.ok);
        for(int i=1;i<300;++i) {
            const double t=-1+5.*i/300,x=1+3*std::cos(t)-1.6*std::sin(t),y=1+4*std::cos(t)+1.2*std::sin(t);
            if(std::min({std::abs(x),std::abs(x-4),std::abs(y+1),std::abs(y-3)})<1e-5)continue;
            bool retained=false;for(const auto& part:clipped.shapes)if(part.ellipse_contains(t))retained=true;
            const bool inside=x>0&&x<4&&y>-1&&y<3;CHECK(retained==(inside!=inverted));
        }
    }
}

TEST(textured_mesh_clipping_preserves_area_uvs_and_nested_holes) {
    const std::vector<gclip::Vertex> mesh={{0,0,0,0},{4,0,1,0},{4,4,1,1},
                                          {0,0,0,0},{4,4,1,1},{0,4,0,1}};
    const std::vector<gcm::Lattice2> notch={point(0,0),point(4,0),point(4,4),point(3,4),point(3,1),point(1,1),point(1,4),point(0,4)};
    auto area=[](const auto& vertices) {
        double a=0;for(std::size_t i=0;i<vertices.size();i+=3) {
            const auto p=vertices[i],q=vertices[i+1],r=vertices[i+2];
            a+=std::abs((q.x-p.x)*(r.y-p.y)-(q.y-p.y)*(r.x-p.x))*.5;
        }return a;
    };
    for(bool inverted:{false,true}) {
        const auto result=gclip::triangles(mesh,{{notch,inverted}},volume());
        CHECK_MSG(result.ok,"%s",result.error.c_str());
        CHECK(std::abs(area(result.vertices)-(inverted?6:10))<1e-7);
        for(const auto& vertex:result.vertices){CHECK(std::abs(vertex.u-vertex.x/4)<1e-12);CHECK(std::abs(vertex.v-vertex.y/4)<1e-12);}
    }
    const auto nested=gclip::triangles(mesh,{{{point(0,0),point(3,3)},false},{{point(1,1),point(2,2)},true}},volume());
    CHECK(nested.ok);CHECK(std::abs(area(nested.vertices)-8)<1e-7);
    const auto empty=gclip::triangles(mesh,{{{point(-1,-1),point(5,5)},true}},volume());
    CHECK(empty.ok);CHECK(empty.vertices.empty());
    CHECK(!gclip::triangles(mesh,{{{point(0,0),point(1,1),point(0,1),point(1,0)},false}},volume()).ok);
    CHECK(!gclip::triangles({{0,0,0,0}}, {},volume()).ok);
}
