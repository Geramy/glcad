#include "ui/shell.hpp"
#include "ui/frame_types.hpp"
#include "geometry/triangulate.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace gcapp {

namespace T = gtheme::token;

const Shell::PlotContent& Shell::plot_content() const {
    gcm::u64 key = canvas_.draw_version() * 1000003ull;
    key ^= static_cast<gcm::u64>(canvas_.dimensions().size()) << 40;
    key ^= static_cast<gcm::u64>(canvas_.texts().size()) << 24;
    key ^= static_cast<gcm::u64>(canvas_.blocks().references().size()) << 8;
    key ^= static_cast<gcm::u64>(plot_scale_den_ + 1) * 0x9E3779B97F4A7C15ull;
    key ^= (static_cast<gcm::u64>(canvas_.display_unit()) + 1) << 58;
    key ^= static_cast<gcm::u64>(plot_landscape_ ? 1 : 2) << 56;
    for (char c : plot_paper_) key = key * 131ull + static_cast<unsigned char>(c);
    if (plot_cache_.valid && plot_cache_.key == key) return plot_cache_;
    plot_cache_ = PlotContent{};
    plot_cache_.key = key;

    std::optional<Canvas> model;
    if (in_paper_space_) model = canvas_for_space(sketches_[std::size_t(active_sketch_)].space_id);
    const Canvas& source = model ? *model : canvas_;
    plot_cache_=build_plot_content(source,0);
    plot_cache_.key=key;
    return plot_cache_;
}

static void append_plot_labels(const std::vector<Canvas::Label>& labels, const gcm::WorkingVolume& volume,
    std::vector<gsnap::Shape>& shapes, std::vector<std::string>& layers,
    std::vector<gplot::PlotJob::Note>& notes, std::vector<gplot::PlotJob::Fill>& fills) {
    for(const auto& lab:labels) {
        for(const auto& triangle:Canvas::label_fills(lab,volume)) {
            gplot::PlotJob::Fill fill;fill.layer=lab.layer;fill.attrs=lab.attrs;
            if(triangle.color.a>=0)fill.attrs.color=gdoc::Color::rgb(gcm::u8(std::lround(triangle.color.r*255)),gcm::u8(std::lround(triangle.color.g*255)),gcm::u8(std::lround(triangle.color.b*255)));
            for(int k=0;k<3;++k)fill.points.emplace_back(triangle.xy[2*k],triangle.xy[2*k+1]);
            fills.push_back(std::move(fill));
        }
        if(lab.box_width>0)continue;
        if(lab.stroke_font) {
            for(auto line:Canvas::label_strokes(lab,volume)) {
                gsnap::Shape sh;sh.a=gcm::to_lattice(volume,line[0],line[1]);sh.b=gcm::to_lattice(volume,line[2],line[3]);
                sh.layer=lab.layer;sh.attrs=lab.attrs;
                shapes.push_back(sh);layers.push_back(lab.layer);
            }
        } else {
            gplot::PlotJob::Note n;n.x=lab.x;n.y=lab.y;n.text=lab.text;n.height=lab.height;n.rotation=lab.angle;
            n.invisible=bool(lab.outline_font);
            n.width_factor=lab.width_factor;n.oblique=lab.oblique;n.y_factor=lab.y_factor;
            n.layer=lab.layer;n.attrs=lab.attrs;n.clips=lab.clips;notes.push_back(std::move(n));
        }
    }
}

Shell::PlotContent Shell::build_plot_content(const Canvas& source, double annotation_scale) const {
    PlotContent result;
    auto& base = result.base;
    base.space = 0;
    base.shapes = source.shapes();
    for (const auto& sh : base.shapes)
        base.shape_layers.push_back(sh.layer.empty() ? "0" : sh.layer);
    for (const auto& ref : source.blocks().references()) {
        if (source.layer_hidden(ref.layer)) continue;
        for (auto& sh : source.blocks().expand(ref, vol_, 8, &layers_)) {
            base.shape_layers.push_back(sh.layer.empty() ? ref.layer : sh.layer);
            base.shapes.push_back(std::move(sh));
        }
    }
    for(const auto& arrow:source.door_flow_shapes()) {base.shapes.push_back(arrow);base.shape_layers.push_back(arrow.layer);}
    for (const auto& h : source.hatches()) {
        if (!h.valid || h.orphaned) continue;
        std::string layer = h.layer.empty() ? "0" : h.layer;
        if (h.layer.empty() && !h.boundary_shapes.empty() &&
            h.boundary_shapes.front() < source.shapes().size()) {
            const std::string& l = source.shapes()[h.boundary_shapes.front()].layer;
            if (!l.empty()) layer = l;
        }
        if(h.cached.solid) {
            std::vector<gcm::Lattice2> points;std::vector<std::vector<gcm::u32>> loops;
            for(const auto& loop:h.cached.fill_loops){loops.emplace_back();for(auto point:loop){loops.back().push_back(gcm::u32(points.size()));points.push_back(point);}}
            const auto indices=gtopo::triangulate_face(loops,points);
            for(std::size_t i=0;i+2<indices.size();i+=3){gplot::PlotJob::Fill fill;fill.layer=layer;fill.attrs=h.attrs;
                for(int j=0;j<3;++j){double x,y;gcm::to_world(vol_,points[indices[i+j]],x,y);fill.points.emplace_back(x,y);}base.fills.push_back(std::move(fill));}
        }
        for (const auto& seg : h.cached.lines) {
            gsnap::Shape sh;
            sh.kind = gsnap::ShapeKind::Segment;
            sh.a = seg.a;
            sh.b = seg.b;
            sh.layer = layer;
            sh.attrs = h.attrs;
            base.shape_layers.push_back(layer);
            base.shapes.push_back(std::move(sh));
        }
    }

    for (const auto& c : source.splines()) {
        const auto pts = c.tessellate(source.spline_tolerance());
        for (std::size_t i = 1; i < pts.size(); ++i) {
            gsnap::Shape sh;
            sh.kind = gsnap::ShapeKind::Segment;
            sh.a = gcm::to_lattice(vol_, pts[i - 1].x.to_f64(), pts[i - 1].y.to_f64());
            sh.b = gcm::to_lattice(vol_, pts[i].x.to_f64(), pts[i].y.to_f64());
            sh.layer = "0";
            base.shape_layers.push_back("0");
            base.shapes.push_back(std::move(sh));
        }
    }

    const auto emit_labels=[&](const std::vector<Canvas::Label>& labels, auto& target) {
        append_plot_labels(labels,vol_,target.shapes,target.shape_layers,target.notes,target.fills);
    };
    const auto emit_note=[&](const Canvas::TextItem& t, auto& target, double scale) {
        emit_labels(source.note_labels(t,scale),target);
    };
    for(const auto& t:source.texts()) if(!source.text_is_annotative(t)) emit_note(t,base,0);
    for(const auto& ref:source.blocks().references()) emit_labels(source.reference_labels(ref,annotation_scale),base);

    base.images=underlay_content(source);
    result.have_box = gplot::content_extents({base}, vol_,
        result.lo_x, result.lo_y, result.hi_x, result.hi_y);

    gplot::PageSetup setup;
    setup.paper=layout_paper(active_layout());
    setup.orientation = plot_landscape_ ? gplot::Orientation::Landscape : gplot::Orientation::Portrait;
    setup.scale_family = source.display_unit() == gunit::Length::Inch ||
                         source.display_unit() == gunit::Length::Foot
                             ? gplot::ScaleFamily::Imperial : gplot::ScaleFamily::Metric;
    if (plot_scale_den_ > 0) {
        result.den = double(plot_scale_den_);
        result.scale_name = scale_display(plot_scale_den_);
    } else if (result.have_box) {
        const double ratio = setup.fitted_ratio(result.hi_x - result.lo_x,
                                                result.hi_y - result.lo_y);
        if (const gplot::NamedScale* n = gplot::nearest_standard_scale(ratio, setup.scale_family)) {
            result.den = n->paper != 0.0 ? n->drawing / n->paper : ratio;
            result.scale_name = n->name;
        } else {
            result.den = ratio > 0.0 ? ratio : 1.0;
        }
    }

    auto& marks = result.marks;
    marks.space = 0;
    marks.fit = false;
    const double den = annotation_scale > 0.0 ? annotation_scale : (result.den > 0.0 ? result.den : 1.0);
    for(const auto& t:source.texts()) if(source.text_is_annotative(t)) emit_note(t,marks,den);
    for (const auto& d : source.dimensions()) {
        const gdim::Style* st = source.dim_styles().find(d.style);
        if (!st) continue;
        const gdim::Geometry g = gdim::build(d, source.styled_for(*st, den), vol_);
        if (!g.ok) continue;
        const std::string layer = d.layer.empty() ? "0" : d.layer;
        for (const auto& sh : g.lines)  { marks.shape_layers.push_back(layer); marks.shapes.push_back(sh); }
        for (const auto& sh : g.arrows) { marks.shape_layers.push_back(layer); marks.shapes.push_back(sh); }
        Canvas::TextItem note{g.text_at,g.text,g.text_height,g.text_angle,g.halign,g.valign,0,"__dimension__"};
        for(const auto& lab:source.note_labels(note)) {
            gplot::PlotJob::Note n;n.x=lab.x;n.y=lab.y;n.text=lab.text;n.height=lab.height;n.rotation=lab.angle;n.layer=layer;
            n.width_factor=lab.width_factor;n.oblique=lab.oblique;n.y_factor=lab.y_factor;
            marks.notes.push_back(std::move(n));
        }
    }
    if(!result.have_box && (!marks.shapes.empty() || !marks.notes.empty())) {
        marks.fit=true;result.have_box=gplot::content_extents({marks},vol_,result.lo_x,result.lo_y,result.hi_x,result.hi_y);
    }
    result.valid=true;
    return result;
}

bool Shell::begin_viewport_drag(double x,double y) {
    auto* layout=active_layout();
    if(mode_!=Mode::Drawing||active_tool_id_!=ToolId::Select||!layout||!canvas_rect_.contains({float(x),float(y)})) return false;
    double cx,cy,w,h; if(!paper_rect(cx,cy,w,h)) return false;
    double wx,wy; canvas_.view().world_of(x,y,wx,wy);
    const gplot::PaperPoint at{wx-cx+w/2,wy-cy+h/2};
    double one,unused; canvas_.view().world_of(x+8,y,one,unused);
    const double tol=std::abs(one-wx);
    for(std::size_t rev=layout->viewports.size();rev>0;--rev) {
        const auto& v=layout->viewports[rev-1]; const auto points=gplot::PaperClip{v.lo,v.hi,v.clip_polygon}.boundary();
        int corner=-1; bool hit=false;
        for(std::size_t i=0;i<points.size();++i) if(std::hypot(at.x-points[i].x,at.y-points[i].y)<=tol) { corner=int(i); hit=true; break; }
        if(!hit) for(std::size_t i=0;i<points.size();++i) {
            auto a=points[i],b=points[(i+1)%points.size()]; double dx=b.x-a.x,dy=b.y-a.y;
            double t=std::clamp(((at.x-a.x)*dx+(at.y-a.y)*dy)/(dx*dx+dy*dy),0.0,1.0);
            if(std::hypot(at.x-a.x-t*dx,at.y-a.y-t*dy)<=tol) { hit=true; break; }
        }
        if(hit) { flush_edits(); viewport_drag_index_=int(rev-1); viewport_drag_corner_=corner; viewport_drag_original_=v; viewport_drag_start_=at; return true; }
    }
    return false;
}
void Shell::move_viewport_drag(double x,double y) {
    auto* layout=active_layout(); if(!layout||viewport_drag_index_<0||std::size_t(viewport_drag_index_)>=layout->viewports.size()) return;
    double cx,cy,w,h; if(!paper_rect(cx,cy,w,h)) return;
    double wx,wy; canvas_.view().world_of(x,y,wx,wy);
    const double dx=wx-cx+w/2-viewport_drag_start_.x,dy=wy-cy+h/2-viewport_drag_start_.y;
    auto v=viewport_drag_original_;
    if(viewport_drag_corner_<0) {
        v.lo.x+=dx; v.lo.y+=dy; v.hi.x+=dx; v.hi.y+=dy;
        for(auto& p:v.clip_polygon) { p.x+=dx; p.y+=dy; }
    } else {
        auto points=gplot::PaperClip{v.lo,v.hi,v.clip_polygon}.boundary();
        if(v.clip_polygon.empty()) {
            if(viewport_drag_corner_==0||viewport_drag_corner_==3) v.lo.x+=dx; else v.hi.x+=dx;
            if(viewport_drag_corner_==0||viewport_drag_corner_==1) v.lo.y+=dy; else v.hi.y+=dy;
        } else {
            points[viewport_drag_corner_].x+=dx; points[viewport_drag_corner_].y+=dy;
            v.lo=v.hi=points[0]; for(auto p:points) { v.lo.x=std::min(v.lo.x,p.x); v.lo.y=std::min(v.lo.y,p.y); v.hi.x=std::max(v.hi.x,p.x); v.hi.y=std::max(v.hi.y,p.y); } v.clip_polygon=std::move(points);
        }
        viewport_drag_original_.to_model((v.lo.x+v.hi.x)/2,(v.lo.y+v.hi.y)/2,v.centre_x,v.centre_y);
    }
    if(v.lo.x<0||v.lo.y<0||v.hi.x>w||v.hi.y>h||!gplot::PaperClip{v.lo,v.hi,v.clip_polygon}.valid()) return;
    layout->viewports[viewport_drag_index_]=std::move(v); invalidate_frame_caches();
}
void Shell::cancel_viewport_drag() {
    if(viewport_drag_index_<0) return;
    auto* layout=active_layout();
    if(layout&&std::size_t(viewport_drag_index_)<layout->viewports.size()) layout->viewports[viewport_drag_index_]=viewport_drag_original_;
    viewport_drag_index_=-1; invalidate_frame_caches();
}

void Shell::draw_viewport_sheet() {
    const auto key=document_serial()*1000003ull + canvas_.draw_version()*131ull +
        gcm::u64(active_layout_+1)*17ull + gcm::u64(in_paper_space_);
    if (sheet_preview_key_!=key) {
        sheet_preview_=gplot::PdfWriter{};
        plot_sheet(sheet_preview_,true);
        sheet_preview_key_=key;
    }
    double cx,cy,w,h;
    if (!paper_rect(cx,cy,w,h)) return;
    const auto& plane=sketch_plane();
    const auto matrix=camera_.view_projection();
    const auto world = [&](double x,double y,double& wx,double& wy,double& wz) {
        plane.to_world(cx-w/2+x,cy-h/2+y,wx,wy,wz);
    };
    const auto screen = [&](double x,double y,double& sx,double& sy) {
        double wx,wy,wz; world(x,y,wx,wy,wz);
        return camera_.project(matrix,wx,wy,wz,sx,sy);
    };
    for(const auto& im:sheet_preview_.images_of_page(0)) {
        ImageDraw draw;draw.image=im.raster;
        TextVertex vertices[4]{};bool ok=true;
        for(int i=0;i<4;++i){double u=i==1||i==2?1:0,v=i>=2?1:0,sx,sy;
            if(!screen(im.origin.x+u*im.x_axis.x+(1-v)*im.y_axis.x,im.origin.y+u*im.x_axis.y+(1-v)*im.y_axis.y,sx,sy)){ok=false;break;}
            vertices[i]={{float(sx),float(sy)},{float(u),float(v)},{1,1,1,1},{canvas_rect_.x,canvas_rect_.y,canvas_rect_.w,canvas_rect_.h}};
        }
        if(ok){for(int i:{0,1,2,0,2,3})draw.vertices.push_back(vertices[i]);images_.push_back(std::move(draw));}
    }
    const auto& theme=themes_.active();
    const auto bg=theme.get(T::canvas_background);
    const auto ink=theme.get(T::geometry);
    const auto colour = [&](const gplot::PdfColor& c) {
        if (bg.r+bg.g+bg.b<1.5f && c.r+c.g+c.b<0.2) return gui::Color{ink.r,ink.g,ink.b,float(c.a)};
        return gui::Color{float(c.r),float(c.g),float(c.b),float(c.a)};
    };
    const auto line = [&](double x0,double y0,double x1,double y1,gui::Color c, float width=1.0f) {
        double wx,wy,wz; world(x0,y0,wx,wy,wz);
        lines_.push_back({{float(wx),float(wy),float(wz)},{c.r,c.g,c.b,c.a},width});
        world(x1,y1,wx,wy,wz);
        lines_.push_back({{float(wx),float(wy),float(wz)},{c.r,c.g,c.b,c.a},width});
    };
    for (const auto& path:sheet_preview_.paths_of_page(0)) {
        if (path.points.size()<2) continue;
        const auto c=colour(path.color);
        if(path.filled) {
            gplot::PaperClip region; region.lo=region.hi={path.points[0].first,path.points[0].second};
            for(auto point:path.points) { region.polygon.push_back({point.first,point.second}); region.lo.x=std::min(region.lo.x,point.first); region.lo.y=std::min(region.lo.y,point.second); region.hi.x=std::max(region.hi.x,point.first); region.hi.y=std::max(region.hi.y,point.second); }
            const auto clip_triangles=path.clip?path.clip->triangles():std::vector<std::array<gplot::PaperPoint,3>>{};
            for(const auto& tri:region.triangles()) {
                const auto emit=[&](const std::vector<gplot::PaperPoint>& poly) {
                    for(std::size_t i=1;i+1<poly.size();++i) for(auto point:{poly[0],poly[i],poly[i+1]}) {
                        double sx,sy; if(!screen(point.x,point.y,sx,sy)) continue;
                        TextVertex vertex{}; vertex.pos[0]=float(sx); vertex.pos[1]=float(sy);
                        vertex.color[0]=c.r; vertex.color[1]=c.g; vertex.color[2]=c.b; vertex.color[3]=c.a;
                        vertex.clip[0]=canvas_rect_.x; vertex.clip[1]=canvas_rect_.y; vertex.clip[2]=canvas_rect_.w; vertex.clip[3]=canvas_rect_.h; tris_.push_back(vertex);
                    }
                };
                if(!path.clip) { emit({tri[0],tri[1],tri[2]}); continue; }
                for(auto mask:clip_triangles) {
                    std::vector<gplot::PaperPoint> poly{tri[0],tri[1],tri[2]};
                    const auto cross=[](auto a,auto b,auto q){return (b.x-a.x)*(q.y-a.y)-(b.y-a.y)*(q.x-a.x);};
                    for(int edge=0;edge<3&&!poly.empty();++edge) {
                        auto a=mask[edge],b=mask[(edge+1)%3]; std::vector<gplot::PaperPoint> out;
                        auto prev=poly.back(); double pd=cross(a,b,prev);
                        for(auto cur:poly) { double cd=cross(a,b,cur); if((pd>=0)!=(cd>=0)) { double t=pd/(pd-cd); out.push_back({prev.x+t*(cur.x-prev.x),prev.y+t*(cur.y-prev.y)}); } if(cd>=0) out.push_back(cur); prev=cur; pd=cd; }
                        poly=std::move(out);
                    }
                    emit(poly);
                }
            }
            continue;
        }
        const auto count=path.points.size()-1+(path.closed ? 1 : 0);
        double length=0; for(std::size_t i=0;i<count;++i) { auto a=path.points[i],b=path.points[(i+1)%path.points.size()]; length+=std::hypot(b.first-a.first,b.second-a.second); }
        std::vector<std::pair<double,double>> dashes{{0,length}};
        if(!path.dashes_mm.empty()) { gdoc::Linetype lt; for(std::size_t i=0;i<path.dashes_mm.size();++i) lt.pattern.push_back({path.dashes_mm[i]*.001*(i%2?-1:1)}); dashes=lt.spans(length); }
        double walked=0;
        for (std::size_t i=0;i<count;++i) {
            auto [x0,y0]=path.points[i]; auto [x1,y1]=path.points[(i+1)%path.points.size()];
            const float width=vars_.get_int("LWDISPLAY",0) ? float(std::max(1.0,path.width_mm*96.0/25.4)) : 1.0f;
            const double len=std::hypot(x1-x0,y1-y0);
            if(len<=0) continue;
            for(auto dash:dashes) {
                double lo=std::max(walked,dash.first),hi=std::min(walked+len,dash.second); if(hi<=lo) continue;
                gplot::PaperPoint a{x0+(x1-x0)*(lo-walked)/len,y0+(y1-y0)*(lo-walked)/len},b{x0+(x1-x0)*(hi-walked)/len,y0+(y1-y0)*(hi-walked)/len};
                if(!path.clip) line(a.x,a.y,b.x,b.y,c,width);
                else for(const auto& span:path.clip->segments(a,b)) line(span[0].x,span[0].y,span[1].x,span[1].y,c,width);
            }
            walked+=len;
        }
    }
    const auto border=gui::Color{ink.r,ink.g,ink.b,0.7f};
    line(0,0,w,0,border); line(w,0,w,h,border); line(w,h,0,h,border); line(0,h,0,0,border);
    for (const auto& v:active_layout()->viewports) {
        const auto points=gplot::PaperClip{v.lo,v.hi,v.clip_polygon}.boundary();
        for(std::size_t i=0;i<points.size();++i) { auto a=points[i],b=points[(i+1)%points.size()]; line(a.x,a.y,b.x,b.y,border); }
    }
    for (const auto& text:sheet_preview_.texts_of_page(0)) {
        if(text.invisible)continue;
        double sx,sy,tx,ty;
        if (!screen(text.x,text.y,sx,sy) || !screen(text.x,text.y+text.height_mm*0.001,tx,ty)) continue;
        const float height=float(std::hypot(tx-sx,ty-sy));
        if (height<1 || height>10000) continue;
        gui::Rect clip=canvas_rect_;
        if (text.clip) {
            double minx=1e300,miny=1e300,maxx=-1e300,maxy=-1e300;
            bool visible=true;
            for(const auto point:text.clip->boundary()) {
                double x,y; if(!screen(point.x,point.y,x,y)) { visible=false; break; }
                minx=std::min(minx,x); miny=std::min(miny,y); maxx=std::max(maxx,x); maxy=std::max(maxy,y);
            }
            if(!visible) continue;
            const float left=std::max(clip.x,float(minx)), top=std::max(clip.y,float(miny));
            const float right=std::min(clip.right(),float(maxx)), bottom=std::min(clip.bottom(),float(maxy));
            if (right<=left || bottom<=top) continue;
            clip={left,top,right-left,bottom-top};
        }
        Canvas::Label lab{text.x,text.y,text.height_mm*.001,text.rotation,text.text,0,0};
        lab.width_factor=text.width_factor;lab.oblique=text.oblique;lab.y_factor=text.y_factor;
        const auto add_clip=[&](const gplot::PaperClip& boundary,bool inverted) {
            gclip::Boundary mask;mask.inverted=inverted;
            for(auto p:boundary.boundary())mask.points.push_back(gcm::to_lattice(canvas_.volume(),p.x,p.y));
            lab.clips.push_back(std::move(mask));
        };
        if(text.clip)add_clip(*text.clip,false);
        for(const auto& mask:text.clips)add_clip(mask.boundary,mask.inverted);
        emit_cad_text(lab,colour(text.color),clip,screen);
    }
}

gplot::PaperSize Shell::layout_paper(const Layout* layout) const {
    const auto& name=layout?layout->paper:plot_paper_;
    if(layout && layout->custom_paper && layout->imported_paper.name==name) return layout->imported_paper;
    if(const auto* paper=gplot::find_paper(name)) return *paper;
    return *gplot::find_paper("A3");
}

bool Shell::paper_rect(double& cx, double& cy, double& w, double& h) const {
    const auto size=layout_paper(active_layout());
    const auto* paper=&size;
    const auto& pc = plot_content();
    if(in_paper_space_) {
        w=plot_landscape_?paper->width:paper->height; h=plot_landscape_?paper->height:paper->width;
        cx=w*.5; cy=h*.5; return true;
    }
    if (active_layout() && (!active_layout()->direct_model_view || !active_layout()->viewports.empty())) {
        w=plot_landscape_ ? paper->width : paper->height;
        h=plot_landscape_ ? paper->height : paper->width;
        cx=pc.have_box ? (pc.lo_x+pc.hi_x)*0.5 : 0;
        cy=pc.have_box ? (pc.lo_y+pc.hi_y)*0.5 : 0;
        return true;
    }
    if (!pc.have_box) {
        w=plot_landscape_?paper->width:paper->height; h=plot_landscape_?paper->height:paper->width;
        cx=cy=0; return true;
    }
    const double pw = plot_landscape_ ? paper->printable_width() : paper->printable_height();
    const double ph = plot_landscape_ ? paper->printable_height() : paper->printable_width();
    w = pw;
    h = ph;
    cx = (pc.lo_x + pc.hi_x) * 0.5;
    cy = (pc.lo_y + pc.hi_y) * 0.5;
    return true;
}

bool Shell::plot_den(double& den, double& cx, double& cy) const {
    double w, h;
    if (!paper_rect(cx, cy, w, h)) return false;
    const auto& pc = plot_content();
    if (pc.den <= 0.0) return false;
    den = pc.den;
    return true;
}

void Shell::ensure_paper_in_view() {
    double cx, cy, w, h;
    if (!paper_rect(cx, cy, w, h)) return;
    double x0, y0, x1, y1;
    canvas_.view().screen_of(cx - w / 2, cy - h / 2, x0, y0);
    canvas_.view().screen_of(cx + w / 2, cy + h / 2, x1, y1);
    const double lo_x = std::min(x0, x1), hi_x = std::max(x0, x1);
    const double lo_y = std::min(y0, y1), hi_y = std::max(y0, y1);
    const double ix = std::max(0.0, std::min(hi_x, double(canvas_rect_.right())) -
                                    std::max(lo_x, double(canvas_rect_.x)));
    const double iy = std::max(0.0, std::min(hi_y, double(canvas_rect_.bottom())) -
                                    std::max(lo_y, double(canvas_rect_.y)));
    const double sheet_area = std::max(1.0, (hi_x - lo_x) * (hi_y - lo_y));
    const double canvas_area = std::max(1.0, double(canvas_rect_.w) * double(canvas_rect_.h));
    const double of_sheet = ix * iy / sheet_area;
    const double of_canvas = ix * iy / canvas_area;
    const double sheet_frac = sheet_area / canvas_area;
    if ((of_sheet < 0.25 && of_canvas < 0.5) || sheet_frac < 0.02) zoom_to_paper();
}

void Shell::zoom_to_paper() {
    double cx, cy, w, h;
    if (!paper_rect(cx, cy, w, h)) return;
    if(in_paper_space_ && canvas_rect_.w>0 && canvas_rect_.h>0) {
        const double scale=std::min(double(canvas_rect_.w)/w,double(canvas_rect_.h)/h)/1.12;
        fit_width(vw_/scale);
        cx+=(vw_*.5-canvas_rect_.x-canvas_rect_.w*.5)/scale;
        cy+=(canvas_rect_.y+canvas_rect_.h*.5-vh_*.5)/scale;
        camera_.set_target(gview::Point3::from(cx,cy,0));
    } else {
        camera_.set_target(gview::Point3::from(cx, cy, 0.0));
        fit_width(std::max(w, h * double(vw_) / std::max(1.0, double(vh_))) * 1.12);
    }
    navigated_ = true;
    sync_view_from_camera();
}

std::size_t Shell::refresh_views(Layout& l) {
    l.view_shapes.clear();
    l.view_layers.clear();
    if (l.view_body < 0 || l.view_body >= int(modeller_.bodies().size())) return 0;

    gdraft::SourceModel m;
    m.id = 1;
    m.body = modeller_.bodies()[std::size_t(l.view_body)];
    m.volume = vol_;

    l.views = gdraft::ViewSet(gdraft::ProjectionAngle::Third);
    const auto ids = l.views.add_layout(m, l.view_layout, 0.0, 0.0, l.view_scale);
    for (const auto& sd : l.sections)
        if (sd.parent < ids.size())
            l.views.add_section(m, ids[sd.parent], sd.line, l.view_scale);
    for (const auto& dd : l.details)
        if (dd.parent < ids.size())
            l.views.add_detail(m, ids[dd.parent], dd.circle, dd.scale);

    gdraft::to_sheet_shapes(l.views, vol_, l.view_shapes, l.view_layers);
    for (const auto& name : l.view_layers)
        if (!layers_.find(name)) {
            gdoc::Layer lay;
            lay.name = name;
            layers_.add(std::move(lay));
        }
    for (std::size_t i = 0; i < l.view_shapes.size(); ++i)
        l.view_shapes[i].layer = i < l.view_layers.size() ? l.view_layers[i] : std::string("0");
    return ids.size();
}

void Shell::ensure_space_ids() {
    for (auto& s : sketches_) if (!s.space_id) s.space_id = ++last_space_id_;
    for (auto& l : layouts_) if (!l.space_id) l.space_id = ++last_space_id_;
}

gcm::u32 Shell::active_space_id() const {
    if (in_paper_space_ && active_layout_ >= 0 && std::size_t(active_layout_) < layouts_.size())
        return layouts_[std::size_t(active_layout_)].space_id;
    if (active_sketch_ >= 0 && std::size_t(active_sketch_) < sketches_.size())
        return sketches_[std::size_t(active_sketch_)].space_id;
    return 0;
}

void Shell::load_active_space() {
    ensure_space_ids();
    auto state = space_states_[active_space_id()];
    if (in_paper_space_) {
        const auto& l = layouts_[std::size_t(active_layout_)];
        state.content.shapes = l.shapes; state.content.texts = l.notes;
    } else {
        const auto& s = sketches_[std::size_t(active_sketch_)];
        state.content.shapes = s.shapes; state.content.texts = s.texts;
    }
    canvas_.restore_space(state);
    vars_.set_text("CANNOSCALE",canvas_.annotation_scale_name());
    drafting_text_seen_["CANNOSCALE"]=canvas_.annotation_scale_name();
    canvas_.set_field_values(in_paper_space_ && active_layout() ? sheet_fields(*active_layout()) : std::map<std::string,std::string>{});
    invalidate_frame_caches();
}

Canvas Shell::canvas_for_space(gcm::u32 id) const {
    Canvas result;
    result.set_text_font(&font_); result.set_text_styles(text_styles_); result.set_font_registry(&fonts_);
    result.blocks() = canvas_.blocks();
    result.dim_styles() = canvas_.dim_styles();
    result.set_display_unit(canvas_.display_unit());
    Canvas::SpaceState state;
    if (const auto it = space_states_.find(id); it != space_states_.end()) {
        state = it->second;
    }
    for (const auto& sk : sketches_) if (sk.space_id == id) {
        state.content.shapes = sk.shapes; state.content.texts = sk.texts;
    }
    for (const auto& l : layouts_) if (l.space_id == id) {
        state.content.shapes = l.shapes; state.content.texts = l.notes;
    }
    result.restore_space(state);
    return result;
}

void Shell::append_sheet_annotations(const Layout& layout, gplot::PlotJob& job) const {
    const Canvas source = canvas_for_space(layout.space_id);
    const auto shape = [&](gsnap::Shape sh, const std::string& layer) {
        job.sheet_shapes.push_back(std::move(sh)); job.sheet_layers.push_back(layer);
    };
    for (const auto& ref : source.blocks().references())
        for (auto sh : source.blocks().expand(ref, vol_, 8, &layers_)) shape(std::move(sh), ref.layer);
    for (const auto& ref : source.blocks().references())
        append_plot_labels(source.reference_labels(ref),vol_,job.sheet_shapes,job.sheet_layers,job.sheet_notes,job.sheet_fills);
    job.sheet_fills=build_plot_content(source,1).base.fills;
    for(const auto& arrow:source.door_flow_shapes())shape(arrow,arrow.layer);
    for (const auto& h : source.hatches()) if (h.valid && !h.orphaned) {
        std::string layer = h.layer.empty() ? "0" : h.layer;
        if (h.layer.empty() && !h.boundary_shapes.empty() && h.boundary_shapes.front() < source.shapes().size())
            layer = source.shapes()[h.boundary_shapes.front()].layer;
        for (const auto& line : h.cached.lines) {
            gsnap::Shape sh; sh.a = line.a; sh.b = line.b; sh.attrs=h.attrs; shape(std::move(sh), layer);
        }
    }
    for (const auto& curve : source.splines()) {
        const auto points = curve.tessellate(source.spline_tolerance());
        for (std::size_t i = 1; i < points.size(); ++i) {
            gsnap::Shape sh;
            sh.a = gcm::to_lattice(vol_, points[i-1].x.to_f64(), points[i-1].y.to_f64());
            sh.b = gcm::to_lattice(vol_, points[i].x.to_f64(), points[i].y.to_f64());
            shape(std::move(sh), "0");
        }
    }
    for (const auto& d : source.dimensions()) if (const auto* style = source.dim_styles().find(d.style)) {
        const auto g = gdim::build(d, source.styled_for(*style, source.annotation_factor_for(*style)), vol_);
        if (!g.ok) continue;
        for (auto sh : g.lines) shape(std::move(sh), d.layer);
        for (auto sh : g.arrows) shape(std::move(sh), d.layer);
        Canvas::TextItem note{g.text_at,g.text,g.text_height,g.text_angle,g.halign,g.valign,0,"__dimension__"};
        for(const auto& lab:source.note_labels(note)) {
            gplot::PlotJob::Note n;n.x=lab.x;n.y=lab.y;n.text=lab.text;n.height=lab.height;n.rotation=lab.angle;n.layer=d.layer;
            n.width_factor=lab.width_factor;n.oblique=lab.oblique;n.y_factor=lab.y_factor;
            job.sheet_notes.push_back(std::move(n));
        }
    }
}

bool Shell::activate_space(gcm::u32 id) {
    if (!id || id == active_space_id()) return true;
    for (std::size_t i = 0; i < sketches_.size(); ++i) if (sketches_[i].space_id == id) {
        leave_paper_space(); edit_sketch(int(i)); return true;
    }
    for (std::size_t i = 0; i < layouts_.size(); ++i) if (layouts_[i].space_id == id) {
        leave_paper_space(); layout_load(int(i)); enter_paper_space(); return true;
    }
    return false;
}

void Shell::enter_paper_space() {
    if (in_paper_space_) return;
    Layout* l = active_layout();
    if (!l) { log_.push_back("PSPACE: make a sheet first (LAYOUT NEW)"); return; }
    flush_edits(); store_active_sketch();
    in_paper_space_ = true;
    load_active_space();
    if (mode_ != Mode::Drawing) enter_drawing_mode();
    log_.push_back("paper space: " + l->name + " -- drawing on the sheet");
}

void Shell::leave_paper_space() {
    if (!in_paper_space_) return;
    flush_edits(); store_active_sketch();
    in_paper_space_ = false;
    load_active_space();
    plot_cache_.valid = false;
    log_.push_back("model space");
}

void Shell::layout_load(int index) {
    if (index < 0 || index >= int(layouts_.size())) return;
    const Layout& l = layouts_[std::size_t(index)];
    plot_paper_ = l.paper;
    plot_landscape_ = l.landscape;
    plot_scale_den_ = l.scale_den;
    plot_dx_ = l.dx;
    plot_dy_ = l.dy;
    active_layout_ = index;
    plot_cache_.valid = false;
}

bool Shell::select_layout(int index) {
    if(index < -1 || index>=int(layouts_.size())) return false;
    if(index==selected_layout() && (index>=0 || mode_!=Mode::Drawing)) {
        if(index>=0) layout_frame_pending_=true;
        return true;
    }
    leave_paper_space(); layout_store();
    if(index<0) { set_mode(Mode::Sketch); run_command("ZOOMEXTENTS"); }
    else { layout_load(index); enter_paper_space(); zoom_to_paper(); }
    reveal_layout_tab(index);
    layout_frame_pending_=index>=0;
    invalidate_frame_caches(); return true;
}

void Shell::reveal_layout_tab(int index) {
    layout_tab_start_=0;
    double used=0; const auto available=std::max(72.0,double(vw_)-px(200));
    for(int i=0;i<=index;++i) {
        used+=std::clamp(double(ui_ctx_.measure(layouts_[std::size_t(i)].name,ui_ctx_.style().font_size))+px(27),double(px(75)),double(px(183)));
        while(used>available && layout_tab_start_<std::size_t(i)) {
            used-=std::clamp(double(ui_ctx_.measure(layouts_[layout_tab_start_].name,ui_ctx_.style().font_size))+px(27),double(px(75)),double(px(183))); ++layout_tab_start_;
        }
    }
}

void Shell::layout_store() {
    if (Layout* l = active_layout()) {
        l->paper = plot_paper_;
        l->landscape = plot_landscape_;
        l->scale_den = plot_scale_den_;
        l->dx = plot_dx_;
        l->dy = plot_dy_;
    }
}

void Shell::enter_drawing_mode() {
    store_active_sketch();
    set_mode(Mode::Drawing);
    if (!looking_at_sketch()) {
        history_.record(camera_);
        camera_.look_plan();
        sync_view_from_camera();
    }
    log_.push_back("drawing: " + plot_paper_ + (plot_landscape_ ? " landscape" : " portrait") +
                   (plot_scale_den_ > 0 ? ", " + scale_display(plot_scale_den_) : ", fit to paper") +
                   " -- Export PDF from the Drawing panel");
    zoom_to_paper();
}
}
