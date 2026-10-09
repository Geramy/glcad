#include "exchange/pdf.hpp"
#include "exchange/plot_style.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <sstream>
#include <zlib.h>

namespace gplot {

namespace {
double cross(PaperPoint a,PaperPoint b,PaperPoint c) { return (b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x); }
}
namespace {
template<class Map> ContentClip mapped_clip(ContentClip clip,Map map) {
    auto points=clip.boundary.boundary();
    for(auto& p:points)p=map(p);
    clip.boundary.polygon=points;
    if(!points.empty()) {
        clip.boundary.lo=clip.boundary.hi=points.front();
        for(auto p:points){clip.boundary.lo.x=std::min(clip.boundary.lo.x,p.x);clip.boundary.lo.y=std::min(clip.boundary.lo.y,p.y);clip.boundary.hi.x=std::max(clip.boundary.hi.x,p.x);clip.boundary.hi.y=std::max(clip.boundary.hi.y,p.y);}
    }
    return clip;
}
template<class Map> std::vector<ContentClip> note_clips(const PlotJob::Note& note,const gcm::WorkingVolume& volume,Map map) {
    std::vector<ContentClip> result;
    for(const auto& source:note.clips) {
        if(source.points.empty())continue;
        ContentClip clip;clip.inverted=source.inverted;
        auto points=source.points;
        if(points.size()==2){const auto a=points[0],b=points[1];points={a,{b.x,a.y},b,{a.x,b.y}};}
        for(auto p:points){PaperPoint q;gcm::to_world(volume,p,q.x,q.y);clip.boundary.polygon.push_back(q);}
        result.push_back(mapped_clip(std::move(clip),map));
    }
    return result;
}
}
std::vector<PaperPoint> PaperClip::boundary() const {
    return polygon.empty()?std::vector<PaperPoint>{lo,{hi.x,lo.y},hi,{lo.x,hi.y}}:polygon;
}
bool PaperClip::contains(PaperPoint p) const {
    if(p.x<lo.x-1e-12||p.x>hi.x+1e-12||p.y<lo.y-1e-12||p.y>hi.y+1e-12) return false;
    if(polygon.empty()) return true;
    bool inside=false;
    for(std::size_t i=0,j=polygon.size()-1;i<polygon.size();j=i++) {
        const auto a=polygon[j],b=polygon[i];
        if(std::abs(cross(a,b,p))<1e-14 && p.x>=std::min(a.x,b.x)-1e-12 && p.x<=std::max(a.x,b.x)+1e-12 && p.y>=std::min(a.y,b.y)-1e-12 && p.y<=std::max(a.y,b.y)+1e-12) return true;
        if((a.y>p.y)!=(b.y>p.y) && p.x<(b.x-a.x)*(p.y-a.y)/(b.y-a.y)+a.x) inside=!inside;
    }
    return inside;
}
bool PaperClip::valid() const {
    if(!std::isfinite(lo.x)||!std::isfinite(lo.y)||!std::isfinite(hi.x)||!std::isfinite(hi.y)||lo.x>=hi.x||lo.y>=hi.y) return false;
    if(polygon.empty()) return true;
    if(polygon.size()<3||polygon.size()>256) return false;
    double area=0;
    for(std::size_t i=0;i<polygon.size();++i) {
        auto a=polygon[i],b=polygon[(i+1)%polygon.size()];
        if(!std::isfinite(a.x)||!std::isfinite(a.y)||a.x<lo.x||a.x>hi.x||a.y<lo.y||a.y>hi.y||std::hypot(a.x-b.x,a.y-b.y)<1e-12) return false;
        area+=a.x*b.y-a.y*b.x;
        for(std::size_t j=i+1;j<polygon.size();++j) {
            if(j==i+1||(i==0&&j==polygon.size()-1)) continue;
            auto c=polygon[j],d=polygon[(j+1)%polygon.size()];
            if(cross(a,b,c)*cross(a,b,d)<=0 && cross(c,d,a)*cross(c,d,b)<=0 && std::max(std::min(a.x,b.x),std::min(c.x,d.x))<=std::min(std::max(a.x,b.x),std::max(c.x,d.x)) && std::max(std::min(a.y,b.y),std::min(c.y,d.y))<=std::min(std::max(a.y,b.y),std::max(c.y,d.y))) return false;
        }
    }
    return std::abs(area)>1e-14;
}
std::vector<std::array<PaperPoint,2>> PaperClip::segments(PaperPoint a,PaperPoint b) const {
    std::vector<double> cuts{0,1}; auto points=boundary();
    const PaperPoint delta{b.x-a.x,b.y-a.y};
    for(std::size_t i=0;i<points.size();++i) {
        auto c=points[i],d=points[(i+1)%points.size()];
        const double ex=d.x-c.x,ey=d.y-c.y,den=delta.x*ey-delta.y*ex;
        if(std::abs(den)<1e-20) continue;
        const double t=((c.x-a.x)*ey-(c.y-a.y)*ex)/den,u=((c.x-a.x)*delta.y-(c.y-a.y)*delta.x)/den;
        if(t>0&&t<1&&u>=0&&u<=1) cuts.push_back(t);
    }
    std::sort(cuts.begin(),cuts.end()); std::vector<std::array<PaperPoint,2>> out;
    const auto at=[&](double t){return PaperPoint{a.x+t*delta.x,a.y+t*delta.y};};
    for(std::size_t i=1;i<cuts.size();++i) if(cuts[i]-cuts[i-1]>1e-12&&contains(at((cuts[i]+cuts[i-1])/2))) out.push_back({at(cuts[i-1]),at(cuts[i])});
    return out;
}
std::vector<std::array<PaperPoint,3>> PaperClip::triangles() const {
    auto p=boundary(); std::vector<std::array<PaperPoint,3>> out;
    p.erase(std::unique(p.begin(),p.end(),[](auto a,auto b){return std::hypot(a.x-b.x,a.y-b.y)<1e-12;}),p.end());
    if(p.size()>1&&std::hypot(p.front().x-p.back().x,p.front().y-p.back().y)<1e-12) p.pop_back();
    for(std::size_t i=0;p.size()>3&&i<p.size();) {
        if(std::abs(cross(p[(i+p.size()-1)%p.size()],p[i],p[(i+1)%p.size()]))<1e-16) { p.erase(p.begin()+i); i=0; } else ++i;
    }
    if(p.size()<3) return out;
    double area=0; for(std::size_t i=0;i<p.size();++i) area+=cross({0,0},p[i],p[(i+1)%p.size()]);
    if(area<0) std::reverse(p.begin(),p.end());
    while(p.size()>3) {
        bool found=false;
        for(std::size_t i=0;i<p.size();++i) {
            auto a=p[(i+p.size()-1)%p.size()],b=p[i],c=p[(i+1)%p.size()];
            if(cross(a,b,c)<=1e-16) continue;
            bool occupied=false;
            for(std::size_t j=0;j<p.size();++j) if(j!=i&&j!=(i+p.size()-1)%p.size()&&j!=(i+1)%p.size()) {
                auto q=p[j]; if(cross(a,b,q)>=-1e-16&&cross(b,c,q)>=-1e-16&&cross(c,a,q)>=-1e-16) { occupied=true; break; }
            }
            if(occupied) continue;
            out.push_back({a,b,c}); p.erase(p.begin()+i); found=true; break;
        }
        if(!found) return {};
    }
    if(p.size()==3) out.push_back({p[0],p[1],p[2]});
    return out;
}

bool PaperClip::clip_segment(f64& x0, f64& y0, f64& x1, f64& y1) const {
    if(!polygon.empty()) { auto spans=segments({x0,y0},{x1,y1}); if(spans.empty()) return false; x0=spans[0][0].x; y0=spans[0][0].y; x1=spans[0][1].x; y1=spans[0][1].y; return true; }
    const f64 dx=x1-x0, dy=y1-y0;
    f64 first=0, last=1;
    const auto edge = [&](f64 p, f64 q) {
        if (p==0) return q>=0;
        const f64 t=q/p;
        if (p<0) first=std::max(first,t); else last=std::min(last,t);
        return first<=last;
    };
    if (!edge(-dx,x0-lo.x)||!edge(dx,hi.x-x0)||!edge(-dy,y0-lo.y)||!edge(dy,hi.y-y0)) return false;
    x1=x0+last*dx; y1=y0+last*dy;
    x0+=first*dx; y0+=first*dy;
    return true;
}

namespace {

// PDF works in points: 1 pt = 1/72 inch, and 1 inch = 0.0254 m.
constexpr f64 kMetresToPoints = 72.0 / 0.0254;

std::string escape(const std::string& s) {
    std::string o;
    o.reserve(s.size() + 8);
    for (char c : s) {
        if (c == '(' || c == ')' || c == '\\') o.push_back('\\');
        o.push_back(c);
    }
    return o;
}

std::string num(f64 v) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.4f", v);
    std::string s = buf;
    if (s.find('.') != std::string::npos) {
        while (!s.empty() && s.back() == '0') s.pop_back();
        if (!s.empty() && s.back() == '.') s.pop_back();
    }
    return s.empty() ? "0" : s;
}

}

void PdfWriter::begin_page(f64 w, f64 h) {
    pages_.push_back({w, h, {}, {}});
    in_page_ = true;
}
void PdfWriter::end_page() { in_page_ = false; }

void PdfWriter::add(const PdfPath& source) {
    if (pages_.empty()) begin_page(0.297, 0.210);
    PdfPath p=source;
    if(p.points.size()<2) return;
    if(!p.subpaths.empty()&&(!p.filled||(p.fill_style>=65&&p.fill_style<=72))) {
        error_="compound contours require a solid fill";return;
    }
    if(p.adaptive_dashes&&!p.dashes_mm.empty()) {
        double length=0,cycle=0;
        for(std::size_t i=1;i<p.points.size();++i) length+=std::hypot(p.points[i].first-p.points[i-1].first,p.points[i].second-p.points[i-1].second)*1000;
        if(p.closed) length+=std::hypot(p.points.back().first-p.points.front().first,p.points.back().second-p.points.front().second)*1000;
        for(double dash:p.dashes_mm) cycle+=dash;
        if(cycle>0&&length>0) { double factor=length/(std::max(1.0,std::round(length/cycle))*cycle); for(auto& dash:p.dashes_mm) dash*=factor; }
    }
    p.adaptive_dashes=false;
    const bool pattern=p.fill_style>=65&&p.fill_style<=72;
    const bool diamond=p.endcap==LinecapStyle::Diamond||p.join==LinejoinStyle::Diamond;
    if((pattern||diamond)&&!p.filled&&!p.dashes_mm.empty()) {
        gdoc::Linetype lt; for(std::size_t i=0;i<p.dashes_mm.size();++i) lt.pattern.push_back({p.dashes_mm[i]*.001*(i%2?-1:1)});
        double total=0; std::vector<double> distances{0};
        const std::size_t count=p.points.size()-1+(p.closed?1:0);
        for(std::size_t i=0;i<count;++i) { auto a=p.points[i],b=p.points[(i+1)%p.points.size()]; total+=std::hypot(b.first-a.first,b.second-a.second); distances.push_back(total); }
        for(auto span:lt.spans(total)) {
            PdfPath dash=p; dash.points.clear(); dash.dashes_mm.clear(); dash.closed=false;
            for(std::size_t i=0;i<count;++i) {
                double lo=std::max(span.first,distances[i]),hi=std::min(span.second,distances[i+1]);
                if(hi<=lo) continue;
                auto a=p.points[i],b=p.points[(i+1)%p.points.size()]; double len=distances[i+1]-distances[i];
                auto at=[&](double d){double t=(d-distances[i])/len; return std::pair{a.first+t*(b.first-a.first),a.second+t*(b.second-a.second)};};
                if(dash.points.empty()) dash.points.push_back(at(lo)); dash.points.push_back(at(hi));
            }
            add(dash);
        }
        return;
    }
    if(pattern) {
        auto fill=[&](const std::vector<std::pair<double,double>>& region) {
            PaperClip clip; clip.lo=clip.hi={region[0].first,region[0].second};
            for(auto q:region) { clip.polygon.push_back({q.first,q.second}); clip.lo.x=std::min(clip.lo.x,q.first); clip.lo.y=std::min(clip.lo.y,q.second); clip.hi.x=std::max(clip.hi.x,q.first); clip.hi.y=std::max(clip.hi.y,q.second); }
            if(!clip.valid()) return;
            const double step=std::max(0.00005,p.fill_size_mm*.001);
            const auto triangles=clip.triangles();
            const auto polygon=[&](std::vector<PaperPoint> cell) {
                for(auto mask:triangles) {
                    auto poly=cell;
                    for(int edge=0;edge<3&&!poly.empty();++edge) {
                        auto a=mask[edge],b=mask[(edge+1)%3]; std::vector<PaperPoint> out;
                        auto prev=poly.back(); double pd=cross(a,b,prev);
                        for(auto cur:poly) { double cd=cross(a,b,cur); if((pd>=0)!=(cd>=0)) { double t=pd/(pd-cd); out.push_back({prev.x+t*(cur.x-prev.x),prev.y+t*(cur.y-prev.y)}); } if(cd>=0) out.push_back(cur); prev=cur; pd=cd; }
                        poly=std::move(out);
                    }
                    if(poly.size()<3) continue;
                    PdfPath q=p; q.points.clear(); for(auto point:poly) q.points.emplace_back(point.x,point.y);
                    q.filled=true; q.closed=true; q.fill_style=73; q.endcap=LinecapStyle::Butt; q.join=LinejoinStyle::Miter; q.dashes_mm.clear(); pages_.back().paths.push_back(std::move(q));
                }
            };
            const auto stroke=[&](PaperPoint a,PaperPoint b) {
                double dx=b.x-a.x,dy=b.y-a.y,len=std::hypot(dx,dy); if(len<=0) return;
                double half=std::min(.0001,step*.25)/2,nx=-dy/len*half,ny=dx/len*half;
                polygon({{a.x+nx,a.y+ny},{b.x+nx,b.y+ny},{b.x-nx,b.y-ny},{a.x-nx,a.y-ny}});
            };
            if(p.fill_style==65||p.fill_style==71) {
                auto x0=std::floor(clip.lo.x/step),x1=std::ceil(clip.hi.x/step),y0=std::floor(clip.lo.y/step),y1=std::ceil(clip.hi.y/step);
                if((x1-x0)*(y1-y0)>100000) { error_="plot-style fill exceeds the output complexity limit; increase its pattern size"; return; }
                for(double iy=y0;iy<y1;++iy) for(double ix=x0;ix<x1;++ix) {
                    if(p.fill_style==65&&std::fmod(std::abs(ix+iy),2)>0.5) continue;
                    const double inset=p.fill_style==71?step*.3:0;
                    polygon({{ix*step+inset,iy*step+inset},{(ix+1)*step-inset,iy*step+inset},{(ix+1)*step-inset,(iy+1)*step-inset},{ix*step+inset,(iy+1)*step-inset}});
                }
            } else {
                std::vector<double> angles;
                if(p.fill_style==66) angles={0,1.5707963267948966};
                else if(p.fill_style==67) angles={0.7853981633974483,-0.7853981633974483};
                else angles={p.fill_style==68?0:p.fill_style==72?1.5707963267948966:p.fill_style==69?-0.7853981633974483:0.7853981633974483};
                for(double angle:angles) {
                    double dx=std::cos(angle),dy=std::sin(angle),lo=1e300,hi=-1e300,alonglo=1e300,alonghi=-1e300;
                    for(auto q:clip.polygon) { lo=std::min(lo,-dy*q.x+dx*q.y); hi=std::max(hi,-dy*q.x+dx*q.y); alonglo=std::min(alonglo,dx*q.x+dy*q.y); alonghi=std::max(alonghi,dx*q.x+dy*q.y); }
                    if((hi-lo)/step>100000) { error_="plot-style fill exceeds the output complexity limit; increase its pattern size"; return; }
                    for(double offset=std::floor(lo/step)*step;offset<=hi;offset+=step) stroke({alonglo*dx-offset*dy,alonglo*dy+offset*dx},{alonghi*dx-offset*dy,alonghi*dy+offset*dx});
                }
            }
        };
        if(p.filled) fill(p.points);
        else for(std::size_t i=0;i<p.points.size()-1+(p.closed?1:0);++i) {
            auto a=p.points[i],b=p.points[(i+1)%p.points.size()]; double dx=b.first-a.first,dy=b.second-a.second,len=std::hypot(dx,dy);
            if(len<=0) continue; double nx=-dy/len*p.width_mm*.0005,ny=dx/len*p.width_mm*.0005;
            fill({{a.first+nx,a.second+ny},{b.first+nx,b.second+ny},{b.first-nx,b.second-ny},{a.first-nx,a.second-ny}});
        }
        if(!p.filled) for(std::size_t i=0;i<p.points.size();++i) {
            bool end=!p.closed&&(i==0||i+1==p.points.size());
            const bool round=end?p.endcap==LinecapStyle::Round:p.join==LinejoinStyle::Round;
            const bool diamond=end?p.endcap==LinecapStyle::Diamond:p.join==LinejoinStyle::Diamond;
            const bool square=end&&p.endcap==LinecapStyle::Square;
            auto point=p.points[i]; double half=p.width_mm*.0005;
            if(round||diamond||square) {
                auto next=p.points[i+1<p.points.size()?i+1:i-1]; double angle=std::atan2(next.second-point.second,next.first-point.first);
                const int count=round?32:4; if(square) angle+=.7853981633974483;
                const double radius=round?half:half*std::sqrt(2.0); std::vector<std::pair<double,double>> poly;
                for(int j=0;j<count;++j) poly.emplace_back(point.first+radius*std::cos(angle+j*6.283185307179586/count),point.second+radius*std::sin(angle+j*6.283185307179586/count));
                fill(poly);
            } else if(!end) {
                auto prev=p.points[(i+p.points.size()-1)%p.points.size()],next=p.points[(i+1)%p.points.size()];
                double ax=point.first-prev.first,ay=point.second-prev.second,bx=next.first-point.first,by=next.second-point.second,al=std::hypot(ax,ay),bl=std::hypot(bx,by);
                if(al<=0||bl<=0) continue; ax/=al; ay/=al; bx/=bl; by/=bl;
                double sign=ax*by-ay*bx>0?-1:1;
                std::pair a{point.first-ay*half*sign,point.second+ax*half*sign},b{point.first-by*half*sign,point.second+bx*half*sign};
                std::vector<std::pair<double,double>> poly{point,a};
                double den=ax*by-ay*bx;
                if(p.join==LinejoinStyle::Miter&&std::abs(den)>1e-12) {
                    double t=((b.first-a.first)*by-(b.second-a.second)*bx)/den;
                    if(std::abs(t)<half*10) poly.emplace_back(a.first+t*ax,a.second+t*ay);
                }
                poly.push_back(b); fill(poly);
            }
        }
        return;
    }
    pages_.back().paths.push_back(p);
    if(diamond&&!p.filled) {
        const double radius=p.width_mm*.001/std::sqrt(2.0);
        for(std::size_t i=0;i<p.points.size();++i) {
            bool end=!p.closed&&(i==0||i+1==p.points.size());
            if((end&&p.endcap!=LinecapStyle::Diamond)||(!end&&p.join!=LinejoinStyle::Diamond)) continue;
            auto a=p.points[i],b=p.points[(i+1)%p.points.size()]; if(i+1==p.points.size()) b=p.points[i-1];
            double angle=std::atan2(b.second-a.second,b.first-a.first);
            PdfPath q=p; q.points.clear(); q.filled=true; q.closed=true; q.dashes_mm.clear(); q.endcap=LinecapStyle::Butt; q.join=LinejoinStyle::Miter;
            for(int j=0;j<4;++j) q.points.push_back({a.first+radius*std::cos(angle+j*1.5707963267948966),a.second+radius*std::sin(angle+j*1.5707963267948966)});
            pages_.back().paths.push_back(std::move(q));
        }
    }
}
void PdfWriter::add(const PdfText& t) {
    if (pages_.empty()) begin_page(0.297, 0.210);
    pages_.back().texts.push_back(t);
}

void PdfWriter::add(const PdfImage& image) {
    if(!image.raster||!image.raster->valid())return;
    if(pages_.empty())begin_page(.297,.210);
    auto result=image;
    if(image.clip||!image.clips.empty()) {
        auto raster=std::make_shared<RasterImage>(*image.raster);
        for(int y=0;y<raster->height;++y)for(int x=0;x<raster->width;++x){
            double a=(x+.5)/raster->width,b=1-(y+.5)/raster->height;
            const PaperPoint p{image.origin.x+a*image.x_axis.x+b*image.y_axis.x,image.origin.y+a*image.x_axis.y+b*image.y_axis.y};
            if((image.clip&&!image.clip->contains(p))||std::any_of(image.clips.begin(),image.clips.end(),[&](const auto& c){return !c.contains(p);}))raster->rgba[(std::size_t(y)*raster->width+x)*4+3]=0;
        }
        result.raster=std::move(raster);
    }
    pages_.back().images.push_back(std::move(result));
}
const std::vector<PdfImage>& PdfWriter::images_of_page(std::size_t i) const {
    static const std::vector<PdfImage> empty;
    return i<pages_.size()?pages_[i].images:empty;
}

void PdfWriter::declare_layer(const std::string& n, bool visible) {
    for (auto& l : layers_)
        if (l.first == n) { l.second = visible; return; }
    layers_.emplace_back(n, visible);
}

std::size_t PdfWriter::path_count() const noexcept {
    std::size_t n = 0;
    for (const auto& p : pages_) n += p.paths.size();
    return n;
}

std::string PdfWriter::build() const {
    if(!error_.empty()) return {};
    std::vector<std::string> objects;
    auto add_object = [&](std::string body) {
        objects.push_back(std::move(body));
        return static_cast<int>(objects.size());   // 1-based object numbers
    };

    // Reserve 1 = catalog, 2 = pages, 3 = font.
    objects.emplace_back();
    objects.emplace_back();
    objects.emplace_back(
        "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica "
        "/Encoding /WinAnsiEncoding >>");

    std::vector<int> page_ids;
    for (const auto& page : pages_) {
        std::ostringstream cs;
        cs.setf(std::ios::fixed);

        std::map<std::string,int> alpha_states;
        std::ostringstream alpha_resources;
        const auto opacity=[&](double alpha) {
            const auto key=num(std::isfinite(alpha)?std::clamp(alpha,0.0,1.0):1.0);
            auto [it,added]=alpha_states.emplace(key,int(alpha_states.size()+1));
            if(added) {
                const int object=add_object("<< /Type /ExtGState /CA "+key+" /ca "+key+" >>");
                alpha_resources<<"/A"<<it->second<<" "<<object<<" 0 R ";
            }
            cs<<"/A"<<it->second<<" gs\n";
        };

        const auto content_clips=[&](const std::vector<ContentClip>& clips) {
            for(const auto& clip:clips) {
                if(clip.inverted)cs<<"0 0 "<<num(page.width*kMetresToPoints)<<" "<<num(page.height*kMetresToPoints)<<" re\n";
                const auto points=clip.boundary.boundary();
                for(std::size_t j=0;j<points.size();++j)cs<<num(points[j].x*kMetresToPoints)<<" "<<num(points[j].y*kMetresToPoints)<<(j?" l\n":" m\n");
                cs<<"h W* n\n";
            }
        };
        std::ostringstream image_resources;
        for(std::size_t i=0;i<page.images.size();++i) {
            const auto& im=page.images[i];if(!im.raster||!im.raster->valid())continue;
            const auto& raster=*im.raster;std::string rgb,alpha;rgb.reserve(std::size_t(raster.width)*raster.height*3);alpha.reserve(std::size_t(raster.width)*raster.height);
            for(std::size_t p=0;p<raster.rgba.size();p+=4){rgb.append(reinterpret_cast<const char*>(raster.rgba.data()+p),3);alpha.push_back(char(raster.rgba[p+3]));}
            auto stream=[&](const std::string& bytes,bool mask,int mask_id){
                uLongf size=compressBound(bytes.size());std::string packed(size,'\0');
                if(compress2(reinterpret_cast<Bytef*>(packed.data()),&size,reinterpret_cast<const Bytef*>(bytes.data()),bytes.size(),Z_BEST_SPEED)!=Z_OK)return std::string{};packed.resize(size);
                std::ostringstream obj;obj<<"<< /Type /XObject /Subtype /Image /Width "<<raster.width<<" /Height "<<raster.height<<" /ColorSpace /"<<(mask?"DeviceGray":"DeviceRGB")<<" /BitsPerComponent 8 /Filter /FlateDecode /Length "<<size;
                if(mask_id)obj<<" /SMask "<<mask_id<<" 0 R";obj<<" >>\nstream\n"<<packed<<"\nendstream";return obj.str();
            };
            const int mask_id=add_object(stream(alpha,true,0)),image_id=add_object(stream(rgb,false,mask_id));
            image_resources<<" /Im"<<i<<" "<<image_id<<" 0 R";
            cs<<"q\n";
            if(im.clip){auto boundary=im.clip->boundary();if(!boundary.empty()){cs<<num(boundary[0].x*kMetresToPoints)<<" "<<num(boundary[0].y*kMetresToPoints)<<" m\n";for(std::size_t j=1;j<boundary.size();++j)cs<<num(boundary[j].x*kMetresToPoints)<<" "<<num(boundary[j].y*kMetresToPoints)<<" l\n";cs<<"h W n\n";}}
            content_clips(im.clips);
            cs<<num(im.x_axis.x*kMetresToPoints)<<" "<<num(im.x_axis.y*kMetresToPoints)<<" "<<num(im.y_axis.x*kMetresToPoints)<<" "<<num(im.y_axis.y*kMetresToPoints)<<" "<<num(im.origin.x*kMetresToPoints)<<" "<<num(im.origin.y*kMetresToPoints)<<" cm\n/Im"<<i<<" Do\nQ\n";
        }

        // Lineweights are paper millimetres, not model units.
        for (const auto& p : page.paths) {
            if (p.points.size() < 2) continue;
            cs << "q\n";
            opacity(p.color.a);
            if (p.clip) cs << num(p.clip->lo.x*kMetresToPoints) << " " << num(p.clip->lo.y*kMetresToPoints)
                << " " << num((p.clip->hi.x-p.clip->lo.x)*kMetresToPoints) << " "
                << num((p.clip->hi.y-p.clip->lo.y)*kMetresToPoints) << " re W n\n";
            if (p.clip && !p.clip->polygon.empty()) {
                const auto& pts=p.clip->polygon;
                cs<<num(pts[0].x*kMetresToPoints)<<" "<<num(pts[0].y*kMetresToPoints)<<" m\n";
                for(std::size_t i=1;i<pts.size();++i) cs<<num(pts[i].x*kMetresToPoints)<<" "<<num(pts[i].y*kMetresToPoints)<<" l\n";
                cs<<"h W n\n";
            }
            cs << num(p.color.r) << " " << num(p.color.g) << " " << num(p.color.b);
            cs << (p.filled ? " rg\n" : " RG\n");
            cs << num(std::max(0.01, p.width_mm * 0.001 * kMetresToPoints)) << " w\n";

            cs << (p.endcap==LinecapStyle::Round ? 1 : p.endcap==LinecapStyle::Square ? 2 : 0) << " J\n";
            cs << (p.join==LinejoinStyle::Round ? 1 : p.join==LinejoinStyle::Bevel ? 2 : 0) << " j\n";
            if (!p.dashes_mm.empty()) {
                cs << "[";
                for (f64 d : p.dashes_mm)
                    cs << num(std::fabs(d) * 0.001 * kMetresToPoints) << " ";
                cs << "] 0 d\n";
            }

            const auto contour=[&](const auto& points) {
                if(points.empty())return;
                cs<<num(points.front().first*kMetresToPoints)<<" "<<num(points.front().second*kMetresToPoints)<<" m\n";
                for(std::size_t i=1;i<points.size();++i)cs<<num(points[i].first*kMetresToPoints)<<" "<<num(points[i].second*kMetresToPoints)<<" l\n";
                if(p.closed||p.filled)cs<<"h\n";
            };
            contour(p.points);for(const auto& points:p.subpaths)contour(points);
            cs << (p.filled ? (p.even_odd?"f*\n":"f\n") : "S\n");
            cs << "Q\n";
        }

        for (const auto& t : page.texts) {
            cs << "q\n";
            opacity(t.color.a);
            if (t.clip) cs << num(t.clip->lo.x*kMetresToPoints) << " " << num(t.clip->lo.y*kMetresToPoints)
                << " " << num((t.clip->hi.x-t.clip->lo.x)*kMetresToPoints) << " "
                << num((t.clip->hi.y-t.clip->lo.y)*kMetresToPoints) << " re W n\n";
            if (t.clip && !t.clip->polygon.empty()) {
                const auto& pts=t.clip->polygon;
                cs<<num(pts[0].x*kMetresToPoints)<<" "<<num(pts[0].y*kMetresToPoints)<<" m\n";
                for(std::size_t i=1;i<pts.size();++i) cs<<num(pts[i].x*kMetresToPoints)<<" "<<num(pts[i].y*kMetresToPoints)<<" l\n";
                cs<<"h W n\n";
            }
            content_clips(t.clips);
            cs << "BT\n";
            if(t.invisible)cs << "3 Tr\n";
            cs << num(t.color.r) << " " << num(t.color.g) << " " << num(t.color.b)
               << " rg\n";
            cs << "/F1 " << num(t.height_mm * 0.001 * kMetresToPoints) << " Tf\n";
            if (std::fabs(t.rotation) > 1e-9 || t.width_factor!=1 || t.oblique!=0 || t.y_factor!=1) {
                const f64 c = std::cos(t.rotation), s = std::sin(t.rotation);
                const double sh=std::tan(t.oblique*3.141592653589793/180)*t.width_factor;
                cs << num(c*t.width_factor) << " " << num(s*t.width_factor) << " " << num(c*sh-s*t.y_factor) << " " << num(s*sh+c*t.y_factor)
                   << " " << num(t.x * kMetresToPoints) << " "
                   << num(t.y * kMetresToPoints) << " Tm\n";
            } else {
                cs << num(t.x * kMetresToPoints) << " "
                   << num(t.y * kMetresToPoints) << " Td\n";
            }
            cs << "(" << escape(t.text) << ") Tj\nET\nQ\n";
        }

        const std::string content = cs.str();
        std::ostringstream stream;
        stream << "<< /Length " << content.size() << " >>\nstream\n" << content
               << "\nendstream";
        const int content_id = add_object(stream.str());

        std::ostringstream pg;
        pg << "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 "
           << num(page.width * kMetresToPoints) << " "
           << num(page.height * kMetresToPoints) << "] "
           << "/Resources << /Font << /F1 3 0 R >> /XObject << " << image_resources.str()
           << " >> /ExtGState << "<<alpha_resources.str()<<" >> >> "
           << "/Contents " << content_id << " 0 R >>";
        page_ids.push_back(add_object(pg.str()));
    }

    std::ostringstream kids;
    kids << "<< /Type /Pages /Count " << page_ids.size() << " /Kids [";
    for (int id : page_ids) kids << id << " 0 R ";
    kids << "] >>";
    objects[1] = kids.str();
    objects[0] = "<< /Type /Catalog /Pages 2 0 R >>";

    std::ostringstream out;
    out << "%PDF-1.4\n%\xE2\xE3\xCF\xD3\n";

    std::vector<std::size_t> offsets(objects.size() + 1, 0);
    for (std::size_t i = 0; i < objects.size(); ++i) {
        offsets[i + 1] = static_cast<std::size_t>(out.tellp());
        out << (i + 1) << " 0 obj\n" << objects[i] << "\nendobj\n";
    }

    const std::size_t xref = static_cast<std::size_t>(out.tellp());
    out << "xref\n0 " << (objects.size() + 1) << "\n";
    out << "0000000000 65535 f \n";
    for (std::size_t i = 1; i <= objects.size(); ++i) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%010zu 00000 n \n", offsets[i]);
        out << buf;
    }
    out << "trailer\n<< /Size " << (objects.size() + 1)
        << " /Root 1 0 R /Info << /Title (" << escape(title_)
        << ") /Creator (" << escape(creator_) << ") >> >>\n";
    out << "startxref\n" << xref << "\n%%EOF\n";
    return out.str();
}

bool PdfWriter::write(gio::Writer& w) const {
    const std::string s = build();
    return w.write_all(reinterpret_cast<const gcm::u8*>(s.data()), s.size()) &&
           w.flush();
}

bool PdfWriter::write_file(const std::string& path) const {
    return write_url("file://" + path);
}

bool PdfWriter::write_url(std::string_view url) const {
    gio::Status st;
    auto conn = gio::TransportRegistry::instance().open(url, gio::Mode::Write, st);
    if (!conn || !conn->output()) return false;
    const bool ok = write(*conn->output());
    return conn->close().ok && ok;
}

namespace {

void tessellate(const gsnap::Shape& s, const gcm::WorkingVolume& v,
                std::vector<std::pair<f64, f64>>& out, bool& closed) {
    closed = false;
    if (s.kind == gsnap::ShapeKind::Segment) {
        f64 ax, ay, bx, by;
        gcm::to_world(v, s.a, ax, ay);
        gcm::to_world(v, s.b, bx, by);
        out.push_back({ax, ay});
        out.push_back({bx, by});
        return;
    }
    if (!s.is_curve()) return;

    f64 cx, cy;
    gcm::to_world(v, s.a, cx, cy);
    const f64 r = static_cast<f64>(s.radius) * v.inv_scale();

    f64 a0 = 0.0, a1 = 6.28318530717958647692;
    if (s.kind == gsnap::ShapeKind::Arc) {
        a0 = s.start_angle;
        a1 = s.end_angle;
        while (a1 < a0) a1 += 6.28318530717958647692;
    } else {
        closed = true;
    }

    const int steps = std::clamp(static_cast<int>((a1 - a0) * r / 0.0003), 24, 720);
    for (int i = 0; i <= steps; ++i) {
        const f64 t = a0 + (a1 - a0) * (static_cast<f64>(i) / steps);
        out.push_back({cx + r * std::cos(t), cy + r * std::sin(t)});
    }
}

}

std::pair<f64, f64> PdfWriter::page_size(std::size_t i) const noexcept {
    return i < pages_.size() ? std::pair{pages_[i].width, pages_[i].height} : std::pair{0.0, 0.0};
}

const std::vector<PdfPath>& PdfWriter::paths_of_page(std::size_t i) const {
    static const std::vector<PdfPath> none;
    return i < pages_.size() ? pages_[i].paths : none;
}

const std::vector<PdfText>& PdfWriter::texts_of_page(std::size_t i) const {
    static const std::vector<PdfText> none;
    return i < pages_.size() ? pages_[i].texts : none;
}

bool content_extents(const std::vector<PlotJob::SpaceContent>& content,
                     const gcm::WorkingVolume& v,
                     f64& lo_x, f64& lo_y, f64& hi_x, f64& hi_y) {
    bool have_box = false;
    for (const auto& c : content) {
        if (!c.fit) continue;
        for (std::size_t si = 0; si < c.shapes.size() + c.notes.size() + c.images.size(); ++si) {
            std::vector<std::pair<f64, f64>> pts;
            bool closed = false;
            std::vector<gclip::Boundary> masks;
            if (si < c.shapes.size()) {
                tessellate(c.shapes[si], v, pts, closed);
            } else if(si<c.shapes.size()+c.notes.size()) {
                const auto& n = c.notes[si - c.shapes.size()];
                if(n.invisible)continue;
                masks=n.clips;
                const f64 w = 0.6 * n.height * static_cast<f64>(n.text.size());
                for(double x:{0.,w}) for(double y:{0.,n.height}) {
                    double u=n.width_factor*(x+y*std::tan(n.oblique*3.141592653589793/180)), v=y*n.y_factor;
                    pts.push_back({n.x+u*std::cos(n.rotation)-v*std::sin(n.rotation), n.y+u*std::sin(n.rotation)+v*std::cos(n.rotation)});
                }
            }
            if(si>=c.shapes.size()+c.notes.size()) {
                const auto& im=c.images[si-c.shapes.size()-c.notes.size()];
                for(int a:{0,1})for(int b:{0,1})pts.push_back({im.origin.x+a*im.x_axis.x+b*im.y_axis.x,im.origin.y+a*im.x_axis.y+b*im.y_axis.y});
                const auto add=[&](const PaperClip& clip,bool inverted) {
                    gclip::Boundary mask;mask.inverted=inverted;
                    for(auto p:clip.boundary())mask.points.push_back(gcm::to_lattice(v,p.x,p.y));
                    masks.push_back(std::move(mask));
                };
                if(im.clip)add(*im.clip,false);
                for(const auto& clip:im.clips)add(clip.boundary,clip.inverted);
            }
            if(!masks.empty()&&pts.size()==4) {
                std::vector<gclip::Vertex> quad;
                for(int k:{0,2,3,0,3,1})quad.push_back({pts[k].first,pts[k].second});
                const auto clipped=gclip::triangles(quad,masks,v);
                pts.clear();for(const auto& p:clipped.vertices)pts.push_back({p.x,p.y});
            }
            for (const auto& q : pts) {
                if (!have_box) {
                    lo_x = hi_x = q.first;
                    lo_y = hi_y = q.second;
                    have_box = true;
                } else {
                    lo_x = std::min(lo_x, q.first);
                    hi_x = std::max(hi_x, q.first);
                    lo_y = std::min(lo_y, q.second);
                    hi_y = std::max(hi_y, q.second);
                }
            }
        }
    }
    return have_box;
}

PlotResult plot(const PlotJob& job, PdfWriter& out) {
    PlotResult r;
    if (!job.sheet) { r.message = "no sheet"; return r; }

    const Sheet& sh = *job.sheet;
    out.begin_page(sh.setup.page_width(), sh.setup.page_height());

    auto style_for = [&](const std::string& layer, const gdoc::Attributes* s,
                         f64 mm_per_unit, PdfPath& p) {
        const gdoc::Layer* l = job.layers ? job.layers->find(layer) : nullptr;
        i32 aci = 7;
        if (l && l->color.kind == gdoc::Color::Kind::Index) aci = l->color.index;
        if (s && s->color.kind == gdoc::Color::Kind::Index) aci = s->color.index;

        PlotStyle ps;
        std::string style_name=l ? l->plot_style : "Normal";
        if(s) if(const auto it=s->user.find("plot_style");it!=s->user.end()) style_name=it->second;
        // CTB entries apply to indexed colours; RGB objects keep their colour.
        const bool true_color=s && s->color.kind==gdoc::Color::Kind::True ||
            ((!s||s->color.kind==gdoc::Color::Kind::ByLayer) && l && l->color.kind==gdoc::Color::Kind::True);
        if (job.styles && !(true_color && job.styles->indexed_only())) ps = job.styles->resolve(aci, style_name);

        if (!ps.use_object_color) p.color = PdfColor::rgb255(ps.r, ps.g, ps.b);
        else if (l && l->color.kind == gdoc::Color::Kind::True)
            p.color = PdfColor::rgb255(l->color.r, l->color.g, l->color.b);
        else if (l && l->color.kind == gdoc::Color::Kind::Index) {
            gcm::u8 cr = 0, cg = 0, cb = 0;
            gdoc::aci_rgb(l->color.index, cr, cg, cb);
            // ACI 7 is the foreground: black on white paper.
            p.color = l->color.index == 7 ? PdfColor::black()
                                          : PdfColor::rgb255(cr, cg, cb);
        }
        else p.color = PdfColor::black();

        if (s && ps.use_object_color) {
            if (s->color.kind == gdoc::Color::Kind::True)
                p.color = PdfColor::rgb255(s->color.r, s->color.g, s->color.b);
            else if (s->color.kind == gdoc::Color::Kind::Index &&
                     s->color.index != 7) {
                gcm::u8 cr = 0, cg = 0, cb = 0;
                gdoc::aci_rgb(s->color.index, cr, cg, cb);
                p.color = PdfColor::rgb255(cr, cg, cb);
            }
        }

        gdoc::Lineweight lw =
            ps.use_object_lineweight && l ? l->lineweight : ps.lineweight;
        if (s && ps.use_object_lineweight &&
            s->lineweight != gdoc::Lineweight::ByLayer &&
            s->lineweight != gdoc::Lineweight::ByBlock)
            lw = s->lineweight;
        p.width_mm = sh.setup.plot_lineweights ? (!ps.use_object_lineweight && ps.width_mm>=0 ? ps.width_mm : gdoc::lineweight_mm(lw))*ps.lineweight_scale : 0.25;
        p.endcap=ps.endcap; p.join=ps.join;
        if(ps.grayscale) { const auto gray=0.299*p.color.r+0.587*p.color.g+0.114*p.color.b; p.color={gray,gray,gray}; }
        const auto screen=std::clamp(ps.screening,0.0,100.0)/100;
        p.color={1-(1-p.color.r)*screen,1-(1-p.color.g)*screen,1-(1-p.color.b)*screen};
        p.color.a=1.0-gdoc::transparency_percent(s?*s:gdoc::Attributes{},l?l->transparency:0)/100.0;
        if(s&&!s->visible)p.color.a=0;

        if (job.linetypes && mm_per_unit > 0.0) {
            std::string name = ps.use_object_linetype ? (s ? s->linetype : std::string()) : ps.linetype;
            if (name.empty() || name == "ByLayer" || name == "ByBlock")
                name = l ? l->linetype : std::string("Continuous");
            if (name != "Continuous" && !name.empty()) {
                if (const gdoc::Linetype* lt = job.linetypes->find(name); lt && !lt->solid()) {
                    for (const auto& d : lt->pattern) {
                        f64 mm = std::fabs(d.length) * job.linetype_scale * (s?s->linetype_scale:1) * mm_per_unit;
                        // PDF draws a zero-length dash as nothing; dots become the thinnest mark.
                        if (mm <= 0.0) mm = std::max(p.width_mm, 0.05);
                        p.dashes_mm.push_back(mm);
                    }
                    // An odd dash count flips phase each cycle; doubling the list evens it.
                    if (p.dashes_mm.size() % 2)
                        p.dashes_mm.insert(p.dashes_mm.end(), p.dashes_mm.begin(),
                                           p.dashes_mm.end());
                }
            }
        }
        p.layer = layer;
        return ps.plot && ps.screening>0;
    };

    for (const auto& vp : sh.viewports) {
        if (!vp.on) continue;
        if (!std::isfinite(vp.scale) || vp.scale <= 0 ||
            !std::isfinite(vp.rotation) || !std::isfinite(vp.centre_x) || !std::isfinite(vp.centre_y) ||
            !std::isfinite(vp.lo.x) || !std::isfinite(vp.lo.y) ||
            !std::isfinite(vp.hi.x) || !std::isfinite(vp.hi.y) ||
            vp.lo.x >= vp.hi.x || vp.lo.y >= vp.hi.y) continue;
        for (const auto& c : job.content) {
            if (c.space != vp.source_space) continue;
            for(const auto& fill:c.fills) {
                const auto* layer=job.layers?job.layers->find(fill.layer):nullptr;
                if(!vp.layer_visible(fill.layer)||(layer&&(!layer->visible()||!layer->plottable)))continue;
                PdfPath p;p.filled=true;p.closed=true;p.clip=PaperClip{vp.lo,vp.hi,vp.clip_polygon};
                if(!style_for(fill.layer,&fill.attrs,1000.0/vp.scale,p))continue;
                for(auto q:fill.points){double x,y;vp.to_paper(q.first,q.second,x,y);p.points.emplace_back(x,y);}
                out.declare_layer(fill.layer);out.add(p);++r.paths;
            }
            for (std::size_t i = 0; i < c.shapes.size(); ++i) {
                const std::string layer =
                    i < c.shape_layers.size() ? c.shape_layers[i] : "0";

                if (!vp.layer_visible(layer)) { ++r.clipped; continue; }
                const gdoc::Layer* l = job.layers ? job.layers->find(layer) : nullptr;
                if (l && (!l->visible() || !l->plottable)) { ++r.clipped; continue; }

                std::vector<std::pair<f64, f64>> pts;
                bool closed = false;
                tessellate(c.shapes[i], job.volume, pts, closed);
                if (pts.size() < 2) continue;

                PdfPath p;
                p.closed = closed;
                p.clip = PaperClip{vp.lo,vp.hi,vp.clip_polygon};
                if (!style_for(layer, &c.shapes[i].attrs,
                               job.paper_linetype_scale ? 1000.0 : vp.scale != 0.0 ? 1000.0 / vp.scale : 0.0, p)) {
                    ++r.clipped;
                    continue;
                }
                for (auto& q : pts) {
                    f64 px, py;
                    vp.to_paper(q.first, q.second, px, py);
                    q = {px, py};
                }
                p.points = std::move(pts);
                out.declare_layer(layer);
                out.add(p);
                ++r.paths;
            }
            for(auto im:c.images) {
                PaperPoint x,y;vp.to_paper(im.origin.x+im.x_axis.x,im.origin.y+im.x_axis.y,x.x,x.y);vp.to_paper(im.origin.x+im.y_axis.x,im.origin.y+im.y_axis.y,y.x,y.y);
                for(auto& clip:im.clips)clip=mapped_clip(std::move(clip),[&](PaperPoint p){PaperPoint q;vp.to_paper(p.x,p.y,q.x,q.y);return q;});
                vp.to_paper(im.origin.x,im.origin.y,im.origin.x,im.origin.y);im.x_axis={x.x-im.origin.x,x.y-im.origin.y};im.y_axis={y.x-im.origin.x,y.y-im.origin.y};im.clip=PaperClip{vp.lo,vp.hi,vp.clip_polygon};out.add(im);
            }
            for (const auto& n : c.notes) {
                if (!vp.layer_visible(n.layer)) { ++r.clipped; continue; }
                const gdoc::Layer* l = job.layers ? job.layers->find(n.layer) : nullptr;
                if (l && (!l->visible() || !l->plottable)) { ++r.clipped; continue; }
                PdfText t;
                t.clip = PaperClip{vp.lo,vp.hi,vp.clip_polygon};
                t.clips=note_clips(n,job.volume,[&](PaperPoint p){PaperPoint q;vp.to_paper(p.x,p.y,q.x,q.y);return q;});
                vp.to_paper(n.x, n.y, t.x, t.y);
                t.text = n.text;
                t.invisible = n.invisible;
                t.height_mm = n.height / (vp.scale != 0.0 ? vp.scale : 1.0) * 1000.0;
                t.rotation = n.rotation + vp.rotation;
                t.width_factor=n.width_factor; t.oblique=n.oblique; t.y_factor=n.y_factor;
                t.layer = n.layer;
                PdfPath pen; if(!style_for(n.layer,&n.attrs,1000.0,pen)) { ++r.clipped; continue; } t.color=pen.color;
                out.declare_layer(n.layer);
                out.add(t);
                ++r.texts;
            }
        }
    }

    if (sh.viewports.empty()) {
        f64 cx = 0.0, cy = 0.0;
        f64 mw = 0.0, mh = 0.0;
        bool have_box = false;
        f64 lo_x = 0, lo_y = 0, hi_x = 0, hi_y = 0;

        if (sh.setup.area == PlotArea::Window) {
            f64 ax, ay, bx, by;
            gcm::to_world(job.volume, sh.setup.window_lo, ax, ay);
            gcm::to_world(job.volume, sh.setup.window_hi, bx, by);
            lo_x = std::min(ax, bx); hi_x = std::max(ax, bx);
            lo_y = std::min(ay, by); hi_y = std::max(ay, by);
            have_box = true;
        } else if (sh.setup.area == PlotArea::Extents ||
                   sh.setup.fit_to_paper) {
            have_box = content_extents(job.content, job.volume, lo_x, lo_y, hi_x, hi_y);
        }
        if (have_box) {
            cx = (lo_x + hi_x) * 0.5;
            cy = (lo_y + hi_y) * 0.5;
            mw = hi_x - lo_x;
            mh = hi_y - lo_y;
        }

        PageSetup setup = sh.setup;
        if (setup.fit_to_paper && have_box) {
            const f64 want = setup.fitted_ratio(mw, mh);
            if (const NamedScale* n = nearest_standard_scale(want, setup.scale_family))
                setup.set_scale(*n);
        }

        for (const auto& c : job.content) {
            for(const auto& fill:c.fills) {
                const auto* layer=job.layers?job.layers->find(fill.layer):nullptr;
                if(layer&&(!layer->visible()||!layer->plottable))continue;
                PdfPath p;p.filled=true;p.closed=true;
                if(!style_for(fill.layer,&fill.attrs,1000.0/std::max(1e-12,setup.drawing_per_paper()),p))continue;
                for(auto q:fill.points){double x,y;setup.to_paper(q.first,q.second,cx,cy,x,y);p.points.emplace_back(x,y);}
                out.declare_layer(fill.layer);out.add(p);++r.paths;
            }
            for (std::size_t i = 0; i < c.shapes.size(); ++i) {
                const std::string layer =
                    i < c.shape_layers.size() ? c.shape_layers[i] : "0";
                const gdoc::Layer* l = job.layers ? job.layers->find(layer) : nullptr;
                if (l && (!l->visible() || !l->plottable)) { ++r.clipped; continue; }

                std::vector<std::pair<f64, f64>> pts;
                bool closed = false;
                tessellate(c.shapes[i], job.volume, pts, closed);
                if (pts.size() < 2) continue;

                PdfPath p;
                p.closed = closed;
                {
                    const f64 dpp = setup.drawing_per_paper();
                    if (!style_for(layer, &c.shapes[i].attrs, dpp != 0.0 ? 1000.0 / dpp : 0.0, p)) {
                        ++r.clipped;
                        continue;
                    }
                }
                for (auto& q : pts) {
                    f64 px, py;
                    setup.to_paper(q.first, q.second, cx, cy, px, py);
                    q = {px, py};
                }
                p.points = std::move(pts);
                out.declare_layer(layer);
                out.add(p);
                ++r.paths;
            }
            for(auto im:c.images) {
                PaperPoint x,y;setup.to_paper(im.origin.x+im.x_axis.x,im.origin.y+im.x_axis.y,cx,cy,x.x,x.y);setup.to_paper(im.origin.x+im.y_axis.x,im.origin.y+im.y_axis.y,cx,cy,y.x,y.y);
                for(auto& clip:im.clips)clip=mapped_clip(std::move(clip),[&](PaperPoint p){PaperPoint q;setup.to_paper(p.x,p.y,cx,cy,q.x,q.y);return q;});
                setup.to_paper(im.origin.x,im.origin.y,cx,cy,im.origin.x,im.origin.y);im.x_axis={x.x-im.origin.x,x.y-im.origin.y};im.y_axis={y.x-im.origin.x,y.y-im.origin.y};out.add(im);
            }
            for (const auto& n : c.notes) {
                const gdoc::Layer* l = job.layers ? job.layers->find(n.layer) : nullptr;
                if (l && (!l->visible() || !l->plottable)) { ++r.clipped; continue; }
                PdfText t;
                t.clips=note_clips(n,job.volume,[&](PaperPoint p){PaperPoint q;setup.to_paper(p.x,p.y,cx,cy,q.x,q.y);return q;});
                setup.to_paper(n.x, n.y, cx, cy, t.x, t.y);
                t.text = n.text;
                t.invisible = n.invisible;
                const f64 dpp = setup.drawing_per_paper();
                t.height_mm = n.height / (dpp != 0.0 ? dpp : 1.0) * 1000.0;
                t.rotation = n.rotation;
                t.width_factor=n.width_factor; t.oblique=n.oblique; t.y_factor=n.y_factor;
                t.layer = n.layer;
                PdfPath pen; if(!style_for(n.layer,&n.attrs,1000.0,pen)) { ++r.clipped; continue; } t.color=pen.color;
                out.declare_layer(n.layer);
                out.add(t);
                ++r.texts;
            }
        }
        r.scale_name = setup.scale_name();
        r.scale_ratio = setup.drawing_per_paper();
    } else {
        r.scale_name = sh.setup.scale_name();
        r.scale_ratio = sh.setup.drawing_per_paper();
    }

    for(const auto& im:job.sheet_images)out.add(im);

    for(const auto& fill:job.sheet_fills) {
        const auto* layer=job.layers?job.layers->find(fill.layer):nullptr;
        if(layer&&(!layer->visible()||!layer->plottable))continue;
        PdfPath p;p.filled=true;p.closed=true;
        if(!style_for(fill.layer,&fill.attrs,1000.0,p))continue;
        p.points=fill.points;out.declare_layer(fill.layer);out.add(p);++r.paths;
    }
    for (std::size_t i = 0; i < job.sheet_shapes.size(); ++i) {
        const std::string layer =
            i < job.sheet_layers.size() ? job.sheet_layers[i] : "0";
        const gdoc::Layer* l = job.layers ? job.layers->find(layer) : nullptr;
        if (l && (!l->visible() || !l->plottable)) { ++r.clipped; continue; }

        std::vector<std::pair<f64, f64>> pts;
        bool closed = false;
        tessellate(job.sheet_shapes[i], job.volume, pts, closed);
        if (pts.size() < 2) continue;

        PdfPath p;
        p.closed = closed;
        // Sheet geometry is in paper units: one drawing unit is one millimetre.
        if (!style_for(layer, &job.sheet_shapes[i].attrs, 1000.0, p)) { ++r.clipped; continue; }
        p.points = std::move(pts);
        out.declare_layer(layer);
        out.add(p);
        ++r.paths;
    }

    for (const auto& n : job.sheet_notes) {
        const gdoc::Layer* l = job.layers ? job.layers->find(n.layer) : nullptr;
        if (l && (!l->visible() || !l->plottable)) { ++r.clipped; continue; }
        PdfText t;
        t.clips=note_clips(n,job.volume,[](PaperPoint p){return p;});
        t.x = n.x;
        t.y = n.y;
        t.text = n.text;
        t.invisible = n.invisible;
        t.height_mm = n.height * 1000.0;
        t.rotation = n.rotation;
                t.width_factor=n.width_factor; t.oblique=n.oblique; t.y_factor=n.y_factor;
        t.layer = n.layer;
        PdfPath pen; if(!style_for(n.layer,&n.attrs,1000.0,pen)) { ++r.clipped; continue; } t.color=pen.color;
        out.declare_layer(n.layer);
        out.add(t);
        ++r.texts;
    }

    out.end_page();
    if(!out.error().empty()) { r.message=out.error(); return r; }
    r.ok = true;
    r.message = "plotted";
    return r;
}

}
