#include "ui/shell.hpp"
#include "exchange/underlay_image.hpp"
#include <filesystem>
#include <sstream>
#include <cmath>
#include <iomanip>

namespace gcapp {
bool Shell::attach_underlay(const std::string& path,int page,bool preserve_reference) {
    if(gdoc::underlay_kind_of(path)==gdoc::UnderlayKind::Unknown){log_.push_back("UNDERLAY: unsupported file type");return false;}
    if(page<1){log_.push_back("UNDERLAY: page numbers start at 1");return false;}
    std::string error;auto raster=gplot::load_underlay(path,page,error);
    if(!raster.valid()&&!preserve_reference){log_.push_back("PDFIMPORT: "+error);return false;}
    if(!raster.valid())log_.push_back("UNDERLAY: reference retained but cannot display: "+error);
    std::error_code path_error;const auto absolute=std::filesystem::absolute(path,path_error);
    gdoc::Underlay u;u.path=path_error?path:absolute.string();u.name=std::filesystem::path(path).filename().string();
    if(page!=1)u.name+=" ["+std::to_string(page)+"]";
    u.kind=gdoc::underlay_kind_of(path);u.sheet=std::to_string(page);u.found=raster.valid();
    u.extent_lo=gcm::to_lattice(canvas_.volume(),raster.metres_x,raster.metres_y);
    u.extent_hi=gcm::to_lattice(canvas_.volume(),raster.metres_x+raster.metres_w,raster.metres_y+raster.metres_h);
    if(raster.source_unit>0){u.source_unit=raster.source_unit;u.source_unit_known=true;}
    for(const auto& warning:raster.warnings)log_.push_back("UNDERLAY: "+warning);
    canvas_.underlays().attach(u);invalidate_frame_caches();
    log_.push_back("underlay "+u.name+" attached, page "+std::to_string(page)+" of "+std::to_string(raster.pages));return true;
}
void Shell::resolve_pending_underlays(const std::string& document_path) {
    bool pending=false;
    const auto inspect=[&](const auto& items){for(const auto& u:items)pending|=u.source_coordinates_pending;};
    inspect(canvas_.underlays().all());
    for(const auto& [id,state]:space_states_)inspect(state.underlays.all());
    for(const auto& definition:canvas_.blocks().all())inspect(definition.underlays);
    if(!pending)return;
    store_active_sketch();
    const auto resolve=[&](gdoc::Underlay& u) {
        if(!u.source_coordinates_pending)return;
        auto path=std::filesystem::path(u.path);
        if(path.is_relative()&&!document_path.empty())path=std::filesystem::path(document_path).parent_path()/path;
        u.path=path.lexically_normal().string();
        std::string error;std::vector<std::string> warnings;
        u.found=gplot::resolve_underlay(u,vol_,error,0,&warnings);
        if(!u.found)log_.push_back("UNDERLAY retained: "+u.path+" ("+error+")");
        for(const auto& warning:warnings)log_.push_back("UNDERLAY: "+warning);
    };
    for(auto& [id,state]:space_states_) {
        gdoc::UnderlayTable updated;
        for(auto u:state.underlays.all()){resolve(u);updated.attach(std::move(u));}
        state.underlays=std::move(updated);
    }
    for(auto definition:canvas_.blocks().all()) {
        bool changed=false;
        for(auto& u:definition.underlays)if(u.source_coordinates_pending){resolve(u);changed=true;}
        if(changed)canvas_.blocks().define(std::move(definition));
    }
    load_active_space();
}
bool Shell::import_pdf(const std::string& path,int page) {
    auto result=gplot::import_pdf_paths(path,page,canvas_.volume());
    if(!result.error.empty()){log_.push_back("PDFIMPORT: "+result.error);return false;}
    for(const auto& warning:result.warnings)log_.push_back("PDFIMPORT: "+warning);
    if(result.shapes.empty()){log_.push_back("PDFIMPORT: no editable paths; use PDFIMPORT IMAGE <file> for a page image");return false;}
    for(auto& shape:result.shapes){shape.layer=canvas_.current_layer();shape.attrs=canvas_.current_attributes;}
    const auto count=result.shapes.size();canvas_.modify_shapes([&](auto& shapes){shapes.insert(shapes.end(),result.shapes.begin(),result.shapes.end());});
    log_.push_back("PDFIMPORT: imported "+std::to_string(count)+" editable segments (curves tessellated; fills imported as boundaries)");
    invalidate_frame_caches();return true;
}
std::vector<gdoc::BlockTable::UnderlayInstance> Canvas::underlay_instances() const {
    std::vector<gdoc::BlockTable::UnderlayInstance> result;
    for(const auto* underlay:underlays_.draw_order())if(!layer_hidden(underlay->layer)) {
        auto u=*underlay;if(const auto* layer=appearance_layers_.find(u.layer))u.plottable=u.plottable&&layer->plottable;
        result.push_back({std::move(u)});
    }
    for(const auto& ref:blocks_.references())if(!layer_hidden(ref.layer))
        for(auto item:blocks_.underlay_instances(ref,vol_,8,&appearance_layers_))result.push_back(std::move(item));
    return result;
}
namespace {
void place_underlay(const std::array<double,6>& m,double& x,double& y) {
    const auto px=x,py=y;x=m[0]*px+m[1]*py+m[2];y=m[3]*px+m[4]*py+m[5];
}
}
std::vector<gplot::PdfImage> Shell::underlay_content(const Canvas& source,bool preview) const {
    std::vector<gplot::PdfImage> result;
    for(const auto& instance:source.underlay_instances()) {
        const auto* u=&instance.underlay;const auto& transform=instance.transform;
        if(!u->visible||u->source_coordinates_pending||(!preview&&!u->plottable)||u->fade>=100)continue;
        std::string error;auto raster=std::make_shared<gplot::RasterImage>(gplot::load_underlay(*u,source.volume(),error));
        if(!raster->valid())continue;
        gplot::PdfImage im;im.raster=raster;double x,y;
        u->to_drawing(gcm::to_lattice(source.volume(),raster->metres_x,raster->metres_y),source.volume(),im.origin.x,im.origin.y);
        place_underlay(transform,im.origin.x,im.origin.y);
        u->to_drawing(gcm::to_lattice(source.volume(),raster->metres_x+raster->metres_w,raster->metres_y),source.volume(),x,y);place_underlay(transform,x,y);im.x_axis={x-im.origin.x,y-im.origin.y};
        u->to_drawing(gcm::to_lattice(source.volume(),raster->metres_x,raster->metres_y+raster->metres_h),source.volume(),x,y);place_underlay(transform,x,y);im.y_axis={x-im.origin.x,y-im.origin.y};
        for(const auto& boundary:instance.clips) {
            gplot::ContentClip clip;clip.inverted=boundary.inverted;
            for(auto p:boundary.points) {gplot::PaperPoint q;gcm::to_world(source.volume(),p,q.x,q.y);clip.boundary.polygon.push_back(q);}
            if(!clip.boundary.polygon.empty()) {
                clip.boundary.lo=clip.boundary.hi=clip.boundary.polygon.front();
                for(auto p:clip.boundary.polygon){clip.boundary.lo.x=std::min(clip.boundary.lo.x,p.x);clip.boundary.lo.y=std::min(clip.boundary.lo.y,p.y);clip.boundary.hi.x=std::max(clip.boundary.hi.x,p.x);clip.boundary.hi.y=std::max(clip.boundary.hi.y,p.y);}
                im.clips.push_back(std::move(clip));
            }
        }
        result.push_back(std::move(im));
    }
    return result;
}
void Shell::build_underlays() {
    std::vector<gsnap::Shape> snap_shapes;
    std::unordered_map<std::string,std::shared_ptr<const gplot::RasterImage>> used;
    const auto vp=camera_.view_projection();const auto& plane=sketch_plane();
    for(const auto& instance:canvas_.underlay_instances()) {
        const auto* u=&instance.underlay;const auto& transform=instance.transform;
        if(!u->visible || u->source_coordinates_pending || u->fade>=100)continue;
        std::error_code ec;auto stamp=std::filesystem::last_write_time(u->path,ec);
        if(ec)continue;
        std::ostringstream key;key<<u->path<<'\n'<<u->sheet<<'\n'<<static_cast<long long>(stamp.time_since_epoch().count())<<' '<<u->fade<<' '<<u->contrast<<' '<<u->contrast_neutral<<' '<<u->monochrome<<' '<<u->clip_inverted<<' '<<u->clip_enabled<<' '<<u->source_unit_known<<' '<<std::setprecision(17)<<u->source_unit;
        for(auto p:u->clip)key<<' '<<p.x<<','<<p.y;
        key<<" hole";for(auto p:u->clip_hole)key<<' '<<p.x<<','<<p.y;
        auto found=image_cache_.find(key.str());std::shared_ptr<const gplot::RasterImage> bitmap;
        if(found!=image_cache_.end())bitmap=found->second;
        else {
            std::string error;auto image=std::make_shared<gplot::RasterImage>(gplot::load_underlay(*u,canvas_.volume(),error));
            if(!image->valid())continue;
            bitmap=image;
        }
        used[key.str()]=bitmap;ImageDraw draw;draw.image=bitmap;
        std::array<gclip::Vertex,4> corners;
        for(int i=0;i<4;++i) {
            auto& p=corners[i];p.u=i==1||i==2?1:0;p.v=i>=2?1:0;
            u->to_drawing(gcm::to_lattice(canvas_.volume(),bitmap->metres_x+p.u*bitmap->metres_w,bitmap->metres_y+(1-p.v)*bitmap->metres_h),canvas_.volume(),p.x,p.y);
            place_underlay(transform,p.x,p.y);
        }
        std::vector<gclip::Vertex> quad;for(int i:{0,1,2,0,2,3})quad.push_back(corners[i]);
        const auto clipped=gclip::triangles(quad,instance.clips,canvas_.volume());
        if(!clipped.ok)continue;
        for(std::size_t i=0;i<clipped.vertices.size();i+=3) {
            TextVertex triangle[3]{};bool projected=true;
            for(int k=0;k<3;++k) {
                const auto& p=clipped.vertices[i+k];double wx,wy,wz,sx,sy;
                plane.to_world(p.x,p.y,wx,wy,wz);
                if(!camera_.project(vp,wx,wy,wz,sx,sy)){projected=false;break;}
                triangle[k]={{float(sx),float(sy)},{float(p.u),float(p.v)},{1,1,1,1},{canvas_rect_.x,canvas_rect_.y,canvas_rect_.w,canvas_rect_.h}};
            }
            if(projected)draw.vertices.insert(draw.vertices.end(),std::begin(triangle),std::end(triangle));
        }
        if(!draw.vertices.empty()) {
            images_.push_back(std::move(draw));
            const auto boundary=gplot::underlay_snap_boundary(*u,*bitmap,canvas_.volume(),transform,instance.clips);
            snap_shapes.insert(snap_shapes.end(),boundary.begin(),boundary.end());
        }
    }
    image_cache_=std::move(used);
    canvas_.set_underlay_snap_shapes(std::move(snap_shapes));
}
}
