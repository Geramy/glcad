#include "exchange/dgn.hpp"
#include "exchange/pdf.hpp"
#include "geometry/region.hpp"
#include <functional>
#include <algorithm>
#include <cmath>
#include <limits>

namespace gplot {
namespace {
constexpr double radians=3.14159265358979323846/180;
double master_metres(std::string label) {
    while(!label.empty()&&(label.back()==' '||label.back()=='\0'))label.pop_back();
    for(char& c:label)if(c>='A'&&c<='Z')c=char(c-'A'+'a');
    if(label=="m")return 1;
    if(label=="mm")return .001;
    if(label=="cm")return .01;
    if(label=="km")return 1000;
    if(label=="in")return .0254;
    // A two-character foot label cannot distinguish survey from international feet.
    return 0;
}
PdfColor color(const std::array<std::uint8_t,3>& rgb,int index) {
    // White foreground indices invert on a white page; other whites stay white.
    if((index==0||index==7)&&rgb[0]>=250&&rgb[1]>=250&&rgb[2]>=250)return PdfColor::black();
    return PdfColor::rgb255(rgb[0],rgb[1],rgb[2]);
}
using XY=std::pair<double,double>;
bool assemble_boundary(std::vector<PdfPath> pieces,double tolerance,PdfPath& out,std::string& error) {
    if(pieces.size()>4096){error="complex boundary exceeds 4096 pieces";return false;}
    std::vector<XY> ends;
    const auto join=[&](XY p) {
        for(auto q:ends)if(std::hypot(p.first-q.first,p.second-q.second)<=tolerance)return q;
        ends.push_back(p);return p;
    };
    double lx=std::numeric_limits<double>::infinity(),ly=lx,hx=-lx,hy=-lx;
    for(auto& p:pieces) {
        if(p.points.size()<2)continue;
        p.points.front()=join(p.points.front());p.points.back()=join(p.points.back());
        for(auto [x,y]:p.points){lx=std::min(lx,x);ly=std::min(ly,y);hx=std::max(hx,x);hy=std::max(hy,y);}
    }
    const double span=std::max(hx-lx,hy-ly);
    if(!std::isfinite(span)||span<=0){error="empty complex boundary";return false;}
    gcm::WorkingVolume volume;volume.extent=2;volume.resolution_bits=40;
    const auto point=[&](XY p){return gcm::to_lattice(volume,(p.first-lx)/span,(p.second-ly)/span);};
    std::vector<gsnap::Shape> edges;
    for(const auto& p:pieces)for(std::size_t j=1;j<p.points.size()+(p.closed?1:0);++j) {
        gsnap::Shape edge;edge.a=point(p.points[j-1]);edge.b=point(p.points[j%p.points.size()]);
        if(edge.a.x==edge.b.x&&edge.a.y==edge.b.y)continue;
        edges.push_back(edge);
        if(edges.size()>4096){error="complex boundary exceeds 4096 edges";return false;}
    }
    const auto region=gregion::from_shapes(edges);
    if(!region.ok){error=region.error;return false;}
    if(region.region.loops.size()!=1){error="complex shape contains multiple unclassified boundaries";return false;}
    out.closed=true;
    for(const auto& edge:region.region.loops.front()) {
        double x,y;gcm::to_world(volume,gregion::endpoints(edge).first,x,y);out.points.emplace_back(lx+x*span,ly+y*span);
    }
    return true;
}
struct DrawPath {PdfPath path;std::vector<PdfPath> holes;};
bool add_path(PdfWriter& writer,DrawPath item,double lx,double ly,double scale,std::string& error) {
    const auto map=[&](PdfPath& p){for(auto& [x,y]:p.points){x=(x-lx)*scale;y=(y-ly)*scale;}};
    map(item.path);for(auto& hole:item.holes)map(hole);
    if(item.holes.empty()||!item.path.filled){writer.add(item.path);return true;}
    gcm::WorkingVolume volume;volume.extent=1;volume.resolution_bits=40;
    const auto region=[&](const PdfPath& p) {
        std::vector<gsnap::Shape> edges;
        for(std::size_t i=0;i<p.points.size();++i) {
            const auto a=p.points[i],b=p.points[(i+1)%p.points.size()];gsnap::Shape edge;
            edge.a=gcm::to_lattice(volume,a.first,a.second);edge.b=gcm::to_lattice(volume,b.first,b.second);
            if(edge.a.x!=edge.b.x||edge.a.y!=edge.b.y)edges.push_back(edge);
        }
        return gregion::from_shapes(edges);
    };
    auto filled=region(item.path);
    if(!filled.ok){error="DGN filled boundary: "+filled.error;return false;}
    for(const auto& hole:item.holes) {
        const auto cut=region(hole);
        if(!cut.ok){error="DGN hole boundary: "+cut.error;return false;}
        filled=gregion::boolean_op(filled.region,cut.region,gregion::Operation::Subtract);
        if(!filled.ok){error="DGN hole subtraction: "+filled.error;return false;}
    }
    auto fill=item.path;fill.points.clear();fill.subpaths.clear();fill.even_odd=true;
    for(const auto& loop:filled.region.loops) {
        std::vector<XY> points;
        for(const auto& edge:loop) {
            double x,y;gcm::to_world(volume,gregion::endpoints(edge).first,x,y);points.emplace_back(x,y);
        }
        if(fill.points.empty())fill.points=std::move(points);else fill.subpaths.push_back(std::move(points));
    }
    writer.add(fill);
    return true;
}

}
RasterImage load_underlay_dgn(const std::string& path,int page,std::string& error,int limit,double unit_override,double placement_unit) {
    error.clear();
    if(page!=1||limit<1||limit>4096){error="DGN v7 has one model; invalid model or raster size";return {};}
    const auto drawing=gdgn::read_file(path);
    if(!drawing.ok){error=drawing.error;return {};}
    if(drawing.dimension!=2){error="DGN 3D view projection is not available";return {};}
    const double declared_unit=master_metres(drawing.master_unit);
    const double unit=unit_override!=0?unit_override:(declared_unit>0?declared_unit:placement_unit);
    if(!std::isfinite(unit)||unit<=0){error="DGN master unit '"+drawing.master_unit+"' requires an explicit metres-per-unit mapping";return {};}
    const auto count=drawing.elements.size();
    std::vector<PdfPath> primitives(count);std::vector<PdfText> texts;
    std::vector<std::optional<PdfPath>> contours(count);
    std::vector<std::vector<std::size_t>> children(count);std::vector<std::size_t> roots,parents;
    for(std::size_t i=0;i<count;++i) {
        const auto& e=drawing.elements[i];
        while(!parents.empty()&&e.offset>=drawing.elements[parents.back()].group_end)parents.pop_back();
        (parents.empty()?roots:children[parents.back()]).push_back(i);
        if(e.group_end)parents.push_back(i);
    }
    auto warnings=drawing.warnings;
    if(unit_override==0&&declared_unit==0&&placement_unit>0)warnings.push_back("DGN custom master units use the retained DWG placement transform");
    bool font_warning=false,weight_warning=false;
    std::size_t vertices=0;
    double lx=std::numeric_limits<double>::infinity(),ly=lx,hx=-lx,hy=-lx;
    const auto see=[&](double x,double y) {
        lx=std::min(lx,x);ly=std::min(ly,y);hx=std::max(hx,x);hy=std::max(hy,y);
    };
    for(std::size_t index=0;index<count;++index) {
        const auto& e=drawing.elements[index];
        if(e.kind==gdgn::Element::Kind::ComplexHeader)continue;
        if(e.kind==gdgn::Element::Kind::CellHeader||e.kind==gdgn::Element::Kind::Unsupported)continue;
        if(e.kind==gdgn::Element::Kind::Text) {
            if(e.text.empty()||e.text_height<=0)continue;
            PdfText t;t.text=e.text;t.x=e.origin.x;t.y=e.origin.y;
            t.height_mm=e.text_height*1000;t.rotation=e.rotation*radians;t.color=color(e.color,e.color_index);
            // Source origin is the lower-left.
            t.width_factor=e.text_width>0?e.text_width/e.text_height:1;
            const double w=std::max(e.text_width,e.text_height)*e.text.size(),h=e.text_height;
            const double c=std::cos(t.rotation),s=std::sin(t.rotation);
            for(auto p:{PaperPoint{0,-h*.3},PaperPoint{w,-h*.3},PaperPoint{w,h*1.3},PaperPoint{0,h*1.3}})
                see(t.x+c*p.x-s*p.y,t.y+s*p.x+c*p.y);
            texts.push_back(std::move(t));font_warning=true;continue;
        }
        PdfPath p;p.closed=e.closed;p.color=color(e.color,e.color_index);p.width_mm=.1+.05*e.weight;
        weight_warning|=e.weight!=0||e.style!=0;
        if(e.kind==gdgn::Element::Kind::Polyline) {
            for(auto v:e.points)p.points.emplace_back(v.x,v.y);
        } else if(e.kind==gdgn::Element::Kind::Arc) {
            if(e.primary_axis<=0||e.secondary_axis<=0||std::abs(e.sweep_angle)>360.0001){error="invalid DGN arc dimensions";return {};}
            const double start=e.start_angle*radians,sweep=e.sweep_angle*radians,rotation=e.rotation*radians;
            const double c=std::cos(rotation),s=std::sin(rotation);
            const int count=std::clamp(int(std::ceil(std::abs(sweep)*std::sqrt(double(limit))*2)),8,4096);
            for(int i=0;i<=count;++i) {
                const double a=start+sweep*i/count,x=e.primary_axis*std::cos(a),y=e.secondary_axis*std::sin(a);
                p.points.emplace_back(e.origin.x+c*x-s*y,e.origin.y+s*x+c*y);
            }
        }
        vertices+=p.points.size();
        if(vertices>4000000){error="DGN rasterization exceeds the vertex limit";return {};}
        if(p.points.size()<2)continue;
        for(auto [x,y]:p.points)see(x,y);
        switch(e.style) {
        case 1:p.dashes_mm={.1,1};break;case 2:p.dashes_mm={3,1};break;
        case 3:p.dashes_mm={6,1};break;case 4:p.dashes_mm={.1,1,3,1};break;
        case 5:p.dashes_mm={1,1};break;case 6:p.dashes_mm={3,1,.1,1,.1,1};break;
        case 7:p.dashes_mm={6,1,1,1};break;
        }
        if(p.closed)contours[index]=p;
        primitives[index]=std::move(p);
    }
    std::function<bool(std::size_t,std::vector<PdfPath>&)> collect;
    collect=[&](std::size_t index,std::vector<PdfPath>& pieces) {
        if(contours[index]){pieces.push_back(*contours[index]);return true;}
        if(!primitives[index].points.empty()){pieces.push_back(primitives[index]);return true;}
        if(drawing.elements[index].kind!=gdgn::Element::Kind::ComplexHeader||children[index].empty())return false;
        for(auto child:children[index])if(!collect(child,pieces))return false;
        return true;
    };
    const double tolerance=2/(drawing.subunits_per_master*drawing.uor_per_subunit);
    for(std::size_t i=count;i-->0;)if(drawing.elements[i].kind==gdgn::Element::Kind::ComplexHeader&&drawing.elements[i].closed) {
        std::vector<PdfPath> pieces;PdfPath contour;std::string problem;
        if(collect(i,pieces)&&assemble_boundary(std::move(pieces),tolerance,contour,problem))contours[i]=std::move(contour);
        else warnings.push_back("DGN complex shape "+std::to_string(drawing.elements[i].id)+" cannot fill: "+(problem.empty()?"unsupported child geometry":problem));
    }
    std::vector<DrawPath> paths;
    std::function<void(std::size_t,const std::vector<PdfPath>&,bool,const gdgn::Element*)> emit;
    emit=[&](std::size_t index,const std::vector<PdfPath>& masks,bool suppress_fill,const gdgn::Element* inherited_fill) {
        const auto& e=drawing.elements[index];
        if(e.kind==gdgn::Element::Kind::CellHeader) {
            auto holes=masks;bool missing_hole=false;
            for(auto child:children[index])if(drawing.elements[child].hole) {
                if(contours[child])holes.push_back(*contours[child]);
                else {missing_hole=true;warnings.push_back("DGN hole "+std::to_string(drawing.elements[child].id)+" has no supported closed boundary; cell fills omitted");}
            }
            for(auto child:children[index])emit(child,holes,suppress_fill||missing_hole||drawing.elements[child].hole,e.filled?&e:inherited_fill);
            return;
        }
        bool filled=false;
        const auto* fill_style=e.filled?&e:inherited_fill;
        if(!suppress_fill&&!e.hole&&fill_style&&contours[index]) {
            auto fill=*contours[index];fill.filled=true;fill.color=color(fill_style->fill_color,fill_style->fill_index);
            paths.push_back({std::move(fill),masks});filled=true;
        }
        if(!primitives[index].points.empty())paths.push_back({primitives[index],{}});
        for(auto child:children[index])emit(child,masks,suppress_fill||filled||e.hole,inherited_fill);
    };
    for(auto root:roots)emit(root,{},false,nullptr);
    if(paths.empty()&&texts.empty()){error="DGN model has no supported drawable elements";return {};}
    double span=std::max(hx-lx,hy-ly);
    if(!std::isfinite(span)||span<=0){error="DGN model has invalid or empty bounds";return {};}
    const double margin=span*.01;lx-=margin;ly-=margin;hx+=margin;hy+=margin;
    const double scale=.3/std::max(hx-lx,hy-ly);
    if(!std::isfinite((hx-lx)*unit)||!std::isfinite((hy-ly)*unit)||
       !std::isfinite(lx*unit)||!std::isfinite(ly*unit)){error="DGN physical bounds overflow";return {};}
    PdfWriter writer;writer.begin_page((hx-lx)*scale,(hy-ly)*scale);
    for(auto& item:paths)if(!add_path(writer,std::move(item),lx,ly,scale,error))return {};
    for(auto t:texts){t.x=(t.x-lx)*scale;t.y=(t.y-ly)*scale;t.height_mm*=scale;writer.add(t);}
    writer.end_page();auto image=rasterize_pdf(writer.build(),1,error,limit);
    if(!image.valid())return image;
    image.metres_x=lx*unit;image.metres_y=ly*unit;image.metres_w=(hx-lx)*unit;image.metres_h=(hy-ly)*unit;image.source_unit=unit;
    if(font_warning)warnings.push_back("DGN text uses substitute font metrics; original resource fonts and text draw order are not yet reproduced");
    if(weight_warning)warnings.push_back("DGN lineweights and dash lengths use display approximations");
    image.warnings=std::move(warnings);return image;
}
}
