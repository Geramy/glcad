#include "geometry/clip.hpp"
#include "geometry/region.hpp"
#include "geometry/triangulate.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>

namespace gclip {
bool valid_boundary(const std::vector<gcm::Lattice2>& points) {
    if(points.empty()) return true;
    if(points.size()<2 || points.size()>4096) return false;
    if(points.size()==2) return points[0].x!=points[1].x && points[0].y!=points[1].y;
    const auto cross=[](auto a,auto b,auto c) {
        return (static_cast<long double>(b.x)-a.x)*(static_cast<long double>(c.y)-a.y)
             - (static_cast<long double>(b.y)-a.y)*(static_cast<long double>(c.x)-a.x);
    };
    const auto on=[&](auto a,auto b,auto p) {
        return cross(a,b,p)==0 && p.x>=std::min(a.x,b.x) && p.x<=std::max(a.x,b.x)
            && p.y>=std::min(a.y,b.y) && p.y<=std::max(a.y,b.y);
    };
    long double area=0;
    for(std::size_t i=0;i<points.size();++i) {
        const auto a=points[i],b=points[(i+1)%points.size()];
        if(a.x==b.x && a.y==b.y) return false;
        area+=static_cast<long double>(a.x)*b.y-static_cast<long double>(a.y)*b.x;
        for(std::size_t j=i+1;j<points.size();++j) {
            if(j==i+1 || (i==0 && j+1==points.size())) continue;
            const auto c=points[j],d=points[(j+1)%points.size()];
            const auto p=cross(a,b,c),q=cross(a,b,d),r=cross(c,d,a),s=cross(c,d,b);
            if((p*q<0 && r*s<0) || on(a,b,c) || on(a,b,d) || on(c,d,a) || on(c,d,b)) return false;
        }
    }
    return area!=0;
}
namespace {
constexpr double tau=6.28318530717958647692;
struct Point {double x,y;};
Point point(gcm::Lattice2 p){return {double(p.x),double(p.y)};}
Point minus(Point a,Point b){return {a.x-b.x,a.y-b.y};}
double cross(Point a,Point b){return a.x*b.y-a.y*b.x;}
double dot(Point a,Point b){return a.x*b.x+a.y*b.y;}
gcm::Lattice2 lattice(Point p){return {gcm::i64(std::llround(p.x)),gcm::i64(std::llround(p.y))};}
bool inside(const std::vector<gcm::Lattice2>& polygon,Point p) {
    bool result=false;
    for(std::size_t i=0,j=polygon.size()-1;i<polygon.size();j=i++) {
        const auto a=point(polygon[j]),b=point(polygon[i]),d=minus(b,a),q=minus(p,a);
        const double length=std::hypot(d.x,d.y);
        if(length>0 && std::abs(cross(d,q))<=length*.5 && dot(q,d)>=0 && dot(q,d)<=dot(d,d))return true;
        if((a.y>p.y)!=(b.y>p.y) && p.x<(b.x-a.x)*(p.y-a.y)/(b.y-a.y)+a.x)result=!result;
    }
    return result;
}
void unique(std::vector<double>& cuts) {
    std::sort(cuts.begin(),cuts.end());
    cuts.erase(std::unique(cuts.begin(),cuts.end(),[](auto a,auto b){return a==b || (std::isfinite(a)&&std::isfinite(b)&&std::abs(a-b)<=16*std::numeric_limits<double>::epsilon()*std::max({1.,std::abs(a),std::abs(b)}));}),cuts.end());
}
void linear(const gsnap::Shape& shape,const std::vector<gcm::Lattice2>& polygon,bool inverted,std::vector<gsnap::Shape>& out) {
    const auto a=point(shape.a),d=minus(point(shape.b),a);const double dd=dot(d,d);
    if(dd==0){if(inside(polygon,a)!=inverted)out.push_back(shape);return;}
    const double inf=std::numeric_limits<double>::infinity();
    const double lo=shape.kind==gsnap::ShapeKind::Xline?-inf:0,hi=shape.is_infinite()?inf:1;
    std::vector<double> cuts{lo,hi};
    for(std::size_t i=0;i<polygon.size();++i) {
        const auto b=point(polygon[i]),e=minus(point(polygon[(i+1)%polygon.size()]),b),q=minus(b,a);
        const double det=cross(d,e);
        if(std::abs(det)>1e-12*std::sqrt(dd*dot(e,e))) {
            const double t=cross(q,e)/det,u=cross(q,d)/det;
            if(t>lo&&t<hi&&u>=-1e-12&&u<=1+1e-12)cuts.push_back(t);
        } else if(std::abs(cross(d,q))<=std::sqrt(dd)*.5) {
            for(auto p:{b,point(polygon[(i+1)%polygon.size()])}) {const auto t=dot(minus(p,a),d)/dd;if(t>lo&&t<hi)cuts.push_back(t);}
        }
    }
    unique(cuts);
    const auto at=[&](double t){return Point{a.x+d.x*t,a.y+d.y*t};};
    for(std::size_t i=1;i<cuts.size();++i) {
        const double left=cuts[i-1],right=cuts[i];
        const double middle=!std::isfinite(left)?(!std::isfinite(right)?0:right-1):!std::isfinite(right)?left+1:(left+right)/2;
        if(inside(polygon,at(middle))==inverted)continue;
        auto result=shape;
        if(!std::isfinite(left)&&!std::isfinite(right)){out.push_back(shape);continue;}
        result.kind=(!std::isfinite(left)||!std::isfinite(right))?gsnap::ShapeKind::Ray:gsnap::ShapeKind::Segment;
        if(!std::isfinite(left)){result.a=lattice(at(right));result.b=lattice(at(right-1));}
        else {result.a=lattice(at(left));result.b=lattice(at(std::isfinite(right)?right:left+1));}
        if(result.a.x!=result.b.x||result.a.y!=result.b.y)out.push_back(std::move(result));
    }
}
void conic(const gsnap::Shape& shape,const std::vector<gcm::Lattice2>& polygon,bool inverted,std::vector<gsnap::Shape>& out) {
    const auto centre=point(shape.a);Point u{double(shape.radius),0},v{0,double(shape.radius)};
    if(shape.is_elliptical()) {u=minus(point(shape.b),centre);const auto length=std::hypot(u.x,u.y);if(length==0)return;v={-u.y*shape.radius/length,u.x*shape.radius/length};}
    const double start=shape.kind==gsnap::ShapeKind::Circle?0:shape.start_angle;
    double sweep=shape.kind==gsnap::ShapeKind::Circle?tau:shape.end_angle-shape.start_angle;
    if(shape.is_elliptical())sweep=shape.ellipse_sweep();else {sweep=std::fmod(sweep,tau);if(sweep<=0)sweep+=tau;}
    std::vector<double> cuts{0,sweep};
    const auto at=[&](double offset){const auto t=start+offset;return Point{centre.x+u.x*std::cos(t)+v.x*std::sin(t),centre.y+u.y*std::cos(t)+v.y*std::sin(t)};};
    for(std::size_t i=0;i<polygon.size();++i) {
        const auto a=point(polygon[i]),e=minus(point(polygon[(i+1)%polygon.size()]),a);
        const double aa=cross(e,u),bb=cross(e,v),cc=cross(e,minus(centre,a)),length=std::hypot(aa,bb);
        if(length==0||std::abs(cc)>length)continue;
        const double phase=std::atan2(bb,aa),delta=std::acos(std::clamp(-cc/length,-1.,1.));
        for(double t:{phase-delta,phase+delta}) {
            double offset=std::fmod(t-start,tau);if(offset<0)offset+=tau;
            const double along=dot(minus(at(offset),a),e)/dot(e,e);
            if(offset>0&&offset<sweep&&along>=-1e-12&&along<=1+1e-12)cuts.push_back(offset);
        }
    }
    unique(cuts);
    for(std::size_t i=1;i<cuts.size();++i) {
        if(inside(polygon,at((cuts[i-1]+cuts[i])*.5))==inverted)continue;
        auto result=shape;
        if(shape.kind==gsnap::ShapeKind::Circle&&cuts[i]-cuts[i-1]<tau-1e-12)result.kind=gsnap::ShapeKind::Arc;
        result.start_angle=start+cuts[i-1];result.end_angle=start+cuts[i];out.push_back(std::move(result));
    }
}
}

Result shapes(const std::vector<gsnap::Shape>& input,const std::vector<gcm::Lattice2>& boundary,bool inverted) {
    if(!valid_boundary(boundary))return {false,"invalid clipping boundary",{}};
    if(boundary.empty())return {true,{},input};
    auto polygon=boundary;
    if(polygon.size()==2){const auto a=polygon[0],b=polygon[1];polygon={a,{b.x,a.y},b,{a.x,b.y}};}
    Result result;result.ok=true;
    std::map<gcm::u32,std::vector<gsnap::Shape>> regions;
    for(const auto& shape:input) {
        if(shape.region){regions[shape.poly].push_back(shape);continue;}
        if(shape.is_linear())linear(shape,polygon,inverted,result.shapes);
        else if(shape.is_curve()||shape.is_elliptical())conic(shape,polygon,inverted,result.shapes);
        else if(inside(polygon,point(shape.a))!=inverted)result.shapes.push_back(shape);
    }
    if(!regions.empty()) {
        std::vector<gsnap::Shape> edges;
        for(std::size_t i=0;i<polygon.size();++i){gsnap::Shape edge;edge.a=polygon[i];edge.b=polygon[(i+1)%polygon.size()];edges.push_back(edge);}
        const auto clip=gregion::from_shapes(edges);if(!clip.ok)return {false,clip.error,{}};
        for(const auto& [id,members]:regions) {
            const auto subject=gregion::from_members(members);if(!subject.ok)return {false,subject.error,{}};
            const auto clipped=gregion::boolean_op(subject.region,clip.region,inverted?gregion::Operation::Subtract:gregion::Operation::Intersect);
            if(!clipped.ok)return {false,clipped.error,{}};
            gcm::u32 index=0;
            for(gcm::u32 loop=0;loop<clipped.region.loops.size();++loop)for(const auto& edge:clipped.region.loops[loop]) {
                auto shape=edge.shape;shape.attrs=members.front().attrs;shape.layer=members.front().layer;
                shape.region=true;shape.poly=id;shape.poly_index=index++;shape.region_loop=loop;shape.region_reversed=edge.reversed;
                result.shapes.push_back(std::move(shape));
            }
        }
    }
    return result;
}
}

namespace gclip {
namespace {
using Polygon = std::vector<Vertex>;
long double side(const Vertex& a,const Vertex& b,const Vertex& p) {
    return (static_cast<long double>(b.x)-a.x)*(static_cast<long double>(p.y)-a.y)-
           (static_cast<long double>(b.y)-a.y)*(static_cast<long double>(p.x)-a.x);
}
Polygon half_plane(const Polygon& input,const Vertex& a,const Vertex& b,bool inside) {
    Polygon out;
    if(input.empty())return out;
    auto previous=input.back();auto d0=side(a,b,previous);
    for(const auto& current:input) {
        const auto d1=side(a,b,current);
        const bool in0=inside?d0>=0:d0<=0,in1=inside?d1>=0:d1<=0;
        if(in0!=in1) {
            const double t=double(d0/(d0-d1));
            out.push_back({previous.x+t*(current.x-previous.x),previous.y+t*(current.y-previous.y),
                           previous.u+t*(current.u-previous.u),previous.v+t*(current.v-previous.v)});
        }
        if(in1)out.push_back(current);
        previous=current;d0=d1;
    }
    return out;
}
bool has_area(const Polygon& polygon) {
    for(std::size_t i=2;i<polygon.size();++i)if(side(polygon[0],polygon[i-1],polygon[i])!=0)return true;
    return false;
}
void append_triangles(std::vector<Vertex>& out,const Polygon& polygon) {
    for(std::size_t i=2;i<polygon.size();++i)
        if(side(polygon[0],polygon[i-1],polygon[i])!=0)
            out.insert(out.end(),{polygon[0],polygon[i-1],polygon[i]});
}
}
MeshResult triangles(const std::vector<Vertex>& input,const std::vector<Boundary>& boundaries,
                     const gcm::WorkingVolume& volume) {
    if(input.size()%3)return {false,"incomplete clip triangle",{}};
    for(const auto& p:input)if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.u)||!std::isfinite(p.v))
        return {false,"non-finite clip vertex",{}};
    auto current=input;
    constexpr std::size_t limit=3000000;
    for(const auto& boundary:boundaries) {
        if(boundary.points.empty())continue;
        if(!valid_boundary(boundary.points))return {false,"invalid clip boundary",{}};
        auto points=boundary.points;
        if(points.size()==2){const auto a=points[0],b=points[1];points={a,{b.x,a.y},b,{a.x,b.y}};}
        std::vector<gcm::u32> loop;for(gcm::u32 i=0;i<points.size();++i)loop.push_back(i);
        const auto indices=gtopo::triangulate_face({loop},points);
        if(indices.empty())return {false,"clip boundary cannot be triangulated",{}};
        std::vector<std::array<Vertex,3>> cutters;
        for(std::size_t i=0;i<indices.size();i+=3) {
            std::array<Vertex,3> triangle;
            for(int k=0;k<3;++k)gcm::to_world(volume,points[indices[i+k]],triangle[k].x,triangle[k].y);
            if(side(triangle[0],triangle[1],triangle[2])<0)std::swap(triangle[1],triangle[2]);
            cutters.push_back(triangle);
        }
        std::vector<Vertex> next;
        for(std::size_t i=0;i<current.size();i+=3) {
            const Polygon source{current[i],current[i+1],current[i+2]};
            if(!boundary.inverted) {
                for(const auto& cutter:cutters) {
                    auto polygon=source;
                    for(int k=0;k<3;++k)polygon=half_plane(polygon,cutter[k],cutter[(k+1)%3],true);
                    append_triangles(next,polygon);
                    if(next.size()>limit)return {false,"clipped mesh exceeds vertex limit",{}};
                }
            } else {
                std::vector<Polygon> pieces{source};
                for(const auto& cutter:cutters) {
                    std::vector<Polygon> outside;
                    for(auto piece:pieces)for(int k=0;k<3&&!piece.empty();++k) {
                        auto cut=half_plane(piece,cutter[k],cutter[(k+1)%3],false);
                        if(has_area(cut))outside.push_back(std::move(cut));
                        piece=half_plane(piece,cutter[k],cutter[(k+1)%3],true);
                    }
                    pieces=std::move(outside);
                    if(pieces.size()>limit/3)return {false,"clipped mesh exceeds vertex limit",{}};
                }
                for(const auto& piece:pieces)append_triangles(next,piece);
                if(next.size()>limit)return {false,"clipped mesh exceeds vertex limit",{}};
            }
        }
        current=std::move(next);
    }
    return {true,{},std::move(current)};
}
}
