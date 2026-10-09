#include "ui/shell.hpp"
#include "ui/widgets.hpp"
#include "ui/symbols.hpp"
#include "ui/ai_tools.hpp"
#include <cmath>
#include <sstream>
#include <functional>

namespace gcapp {
namespace {
struct LibraryItem {std::string label,command;int depth;bool folder;};
std::vector<gsnap::Shape> object_preview(const std::string& command,const Canvas& canvas) {
    if(command.starts_with("SYMBOL "))return symbol_preview(command.substr(7),canvas);
    if(command.starts_with("BLOCK:")) {
        gdoc::BlockRef ref;ref.name=command.substr(6);
        return canvas.blocks().expand(ref,canvas.volume());
    }
    const auto& volume=canvas.volume();std::vector<gsnap::Shape> shapes;
    const auto p=[&](double x,double y){return gcm::to_lattice(volume,x,y);};
    const auto line=[&](double ax,double ay,double bx,double by){gsnap::Shape s;s.a=p(ax,ay);s.b=p(bx,by);shapes.push_back(s);};
    const auto arc=[&](double radius,double end){gsnap::Shape s;s.kind=gsnap::ShapeKind::Arc;s.a=p(0,0);s.radius=gcm::i64(std::llround(radius/volume.inv_scale()));s.end_angle=end;shapes.push_back(s);};
    if(command=="DOOR") {line(0,0,0,1);arc(1,1.5707963267948966);}
    else if(command=="HINGE" || command=="CIRCLE") {arc(command=="HINGE"?.12:.5,6.283185307179586);shapes.back().kind=gsnap::ShapeKind::Circle;}
    else if(command=="ARC")arc(1,1.5707963267948966);
    else if(command=="WALL" || command=="WINDOW" || command=="DOORFRAME") {
        line(0,-.1,0,.1);line(1,-.1,1,.1);
        if(command!="DOORFRAME") {line(0,-.1,1,-.1);line(0,.1,1,.1);}
        if(command=="WINDOW") {line(0,-.025,1,-.025);line(0,.025,1,.025);}
    } else if(command=="PLINE") {line(0,0,.4,.7);line(.4,.7,.7,.2);line(.7,.2,1,.6);}
    else {
        line(0,0,1,0);line(1,0,1,.7);line(1,.7,0,.7);line(0,.7,0,0);
        if(command=="HATCH" || command=="BACKGROUND")for(int i=1;i<5;++i)line(i*.2,0,std::max(0.,i*.2-.5),.7);
    }
    return shapes;
}
void draw_preview(gui::DrawList& draw,const gui::Rect& box,const std::vector<gsnap::Shape>& shapes,
                  const gcm::WorkingVolume& volume,gui::Color color,float stroke) {
    std::vector<std::pair<gui::Vec2,gui::Vec2>> lines;
    double lx=1e100,ly=1e100,hx=-1e100,hy=-1e100;
    const auto add=[&](double ax,double ay,double bx,double by) {
        lx=std::min({lx,ax,bx});ly=std::min({ly,ay,by});hx=std::max({hx,ax,bx});hy=std::max({hy,ay,by});
        lines.push_back({{float(ax),float(ay)},{float(bx),float(by)}});
    };
    for(const auto& shape:shapes) {
        double ax,ay,bx,by;gcm::to_world(volume,shape.a,ax,ay);gcm::to_world(volume,shape.b,bx,by);
        if(shape.kind==gsnap::ShapeKind::Segment)add(ax,ay,bx,by);
        else if(shape.kind==gsnap::ShapeKind::Circle || shape.kind==gsnap::ShapeKind::Arc) {
            const double radius=shape.radius*volume.inv_scale(),start=shape.kind==gsnap::ShapeKind::Circle?0:shape.start_angle;
            double sweep=shape.kind==gsnap::ShapeKind::Circle?6.283185307179586:shape.end_angle-start;
            while(sweep<=0)sweep+=6.283185307179586;
            for(int i=0;i<48;++i) {const double a=start+sweep*i/48,b=start+sweep*(i+1)/48;
                add(ax+radius*std::cos(a),ay+radius*std::sin(a),ax+radius*std::cos(b),ay+radius*std::sin(b));}
        }
    }
    if(lines.empty())return;
    const double scale=std::min(box.w/std::max(hx-lx,1e-8),box.h/std::max(hy-ly,1e-8));
    const auto map=[&](gui::Vec2 p){return gui::Vec2{float(box.centre().x+(p.x-(lx+hx)*.5)*scale),float(box.centre().y-(p.y-(ly+hy)*.5)*scale)};};
    for(const auto& [a,b]:lines)draw.line(map(a),map(b),stroke,color);
}
}

void Shell::draw_object_library(const gui::Rect& canvas) {
    const auto& theme=themes_.active();
    const auto color=[&](auto token){return theme.get(token);};
    const float top=float(px(50));
    library_panel_={canvas.x+float(px(12)),canvas.y+top,
        std::min(float(px(360)),canvas.w-float(px(24))),std::min(float(px(630)),canvas.h-top-float(px(12)))};
    const auto panel=library_panel_;const float pad=float(px(12));
    ui_ctx_.begin_overlay(panel);ui_ctx_.push_id("object-library");
    auto& draw=ui_ctx_.draw();
    draw.round_rect(panel,float(px(8)),color(gtheme::token::ui_surface_raised));
    gui::Rect row{panel.x+pad,panel.y+pad,panel.w-2*pad,float(px(26))};
    {
        auto title=row;
        const auto picker=title.cut_right(std::min(float(px(150)),title.w*.5f));
        draw.text(title,"Object library",ui_ctx_.style().font_size_title,color(gtheme::token::ui_text));
        draw_discipline_picker(picker);
    }
    if(library_save_selection_open_) {
        row.y+=float(px(38));draw.text(row,"Save selection as a reusable symbol",ui_ctx_.style().font_size_small,color(gtheme::token::ui_text_dim));
        row.y+=float(px(32));gui::text_field(ui_ctx_,row,"Symbol name",library_symbol_name_);
        row.y+=float(px(34));gui::text_field(ui_ctx_,row,"Library folder",library_symbol_folder_);
        row.y+=float(px(38));
        if(gui::button(ui_ctx_,row,"Pick insertion point and save")) {
            const auto name=library_symbol_name_,folder=library_symbol_folder_;
            library_save_selection_open_=false;drafting_feature_panel_=0;
            begin_pick(1,"Pick the symbol's insertion point",[this,name,folder](const auto& points){save_selection_to_library(name,folder,points[0]);});
        }
        row.y+=float(px(34));if(gui::button(ui_ctx_,row,"Cancel"))library_save_selection_open_=false;
        ui_ctx_.pop_id();ui_ctx_.end_overlay();return;
    }
    row.y+=float(px(26));draw.text(row,"Drag to place · click to draw",ui_ctx_.style().font_size_small,color(gtheme::token::ui_text_dim));
    row.y+=float(px(30));auto label=row;label.w=float(px(48));draw.text(label,"Size",ui_ctx_.style().font_size,color(gtheme::token::ui_text));
    auto field=row;field.x+=label.w;field.w-=label.w;gui::text_field(ui_ctx_,field,"Library object size",library_size_);
    row.y+=float(px(34));
    std::vector<LibraryItem> items;
    const auto folder=[&](const char* title,const char* path,int depth) {items.push_back({title,path,depth,true});return library_folders_.count(path)!=0;};
    const auto item=[&](const char* title,const char* command,int depth){items.push_back({title,command,depth,false});};
    if(!block_library_.entries.empty() && folder("Library","Library",0)) {
        std::function<void(const std::string&,int)> branch=[&](const std::string& prefix,int depth) {
            std::set<std::string> children;
            for(std::size_t i=0;i<block_library_.entries.size();++i) {
                const auto& entry=block_library_.entries[i];
                if(entry.folder==prefix)items.push_back({entry.name,"LIBRARY:"+std::to_string(i),depth,false});
                else if(prefix.empty()||entry.folder.starts_with(prefix+"/")) {
                    const auto rest=entry.folder.substr(prefix.empty()?0:prefix.size()+1);
                    children.insert(rest.substr(0,rest.find('/')));
                }
            }
            for(const auto& child:children) {
                const auto path=prefix.empty()?child:prefix+"/"+child;
                if(folder(child.c_str(),("Library/"+path).c_str(),depth))branch(path,depth+1);
            }
        };
        branch({},1);
    }
    if(folder("Architecture","Architecture",0)) {
        item("Wall","WALL",1);
        if(folder("Doors and windows","Architecture/Doors and windows",1)) {
            item("Swing door","DOOR",2);item("Window","WINDOW",2);item("Door frame","DOORFRAME",2);item("Door hinge","HINGE",2);
        }
    }
    if(folder("Symbols","Symbols",0))for(const auto& symbol:symbol_catalog())items.push_back({symbol.label,std::string("SYMBOL ")+symbol.id,1,false});
    if(folder("Drawing tools","Drawing tools",0)) {
        item("Rectangle","RECTANG",1);item("Circle","CIRCLE",1);item("Arc","ARC",1);item("Polyline","PLINE",1);
        item("Hatch","HATCH",1);item("Area background","BACKGROUND",1);
    }
    if(folder("Drawing blocks","Drawing blocks",0))for(const auto& block:canvas_.blocks().all())
        if(!block.is_xref && block.source_owner!="glcad.wall" && !is_symbol_legend(canvas_.blocks(),block.name) && !block.name.starts_with("__glcad_symbol_"))
            items.push_back({block.name,"BLOCK:"+block.name,1,false});
    if(drawing_discipline_==DrawingDiscipline::Mechanical) {
        const auto first=std::find_if(items.begin(),items.end(),[](const auto& item){return item.command=="Drawing tools";});
        const auto end=std::find_if(first,items.end(),[](const auto& item){return item.command=="Drawing blocks";});
        std::rotate(items.begin(),first,end);
    }
    const float bottom=panel.bottom()-float(px(114));
    const int visible=std::max(1,int((bottom-row.y)/px(48)));
    if(panel.contains(input_.mouse))library_scroll_-=input_.wheel;
    library_scroll_=std::clamp(library_scroll_,0.f,float(std::max(0,int(items.size())-visible)));
    for(int i=int(library_scroll_);i<std::min(int(items.size()),int(library_scroll_)+visible);++i) {
        const auto& entry=items[i];row.h=float(px(44));auto bounds=row;bounds.x+=float(px(12*std::min(entry.depth,6)));bounds.w-=float(px(12*std::min(entry.depth,6)));
        const auto id=ui_ctx_.id(entry.command);const bool enabled=entry.folder || !read_only();
        const auto state=ui_ctx_.interact(id,bounds,enabled);
        ui_ctx_.register_focusable(id,bounds,entry.label);
        ui_ctx_.announce(id,gui::Context::Role::Button,entry.label,entry.folder?(library_folders_.count(entry.command)?"expanded":"collapsed"):"Drag to place",bounds,false,!enabled);
        if(state.hovered || state.held)draw.round_rect(bounds,float(px(4)),color(gtheme::token::ui_hover));
        if(state.focused)draw.border(bounds,float(px(4)),float(px(1)),color(gtheme::token::ui_accent));
        auto text=bounds;
        if(entry.folder) {
            const bool open=library_folders_.count(entry.command);const float x=bounds.x+float(px(6)),y=bounds.centre().y;
            if(open) {draw.line({x,y-3},{x+4,y+2},1.5,color(gtheme::token::ui_text_dim));draw.line({x+4,y+2},{x+8,y-3},1.5,color(gtheme::token::ui_text_dim));}
            else {draw.line({x+2,y-4},{x+6,y},1.5,color(gtheme::token::ui_text_dim));draw.line({x+6,y},{x+2,y+4},1.5,color(gtheme::token::ui_text_dim));}
            gui::Rect icon{bounds.x+float(px(22)),y-float(px(7)),float(px(20)),float(px(14))};
            draw.border(icon,float(px(2)),float(px(1)),color(gtheme::token::ui_text_dim));
            draw.line({icon.x,icon.y-float(px(3))},{icon.x+float(px(9)),icon.y-float(px(3))},float(px(1)),color(gtheme::token::ui_text_dim));
            text.x+=float(px(52));text.w-=float(px(52));
            if(state.clicked || ui_ctx_.activated(id)) {if(open)library_folders_.erase(entry.command);else library_folders_.insert(entry.command);}
        } else {
            gui::Rect preview{bounds.x+float(px(3)),bounds.y+float(px(3)),float(px(42)),float(px(38))};
            draw.round_rect(preview,float(px(3)),color(gtheme::token::ui_surface));
            draw.border(preview,float(px(3)),float(px(1)),color(gtheme::token::ui_border));
            auto inset=preview;inset.x+=float(px(6));inset.y+=float(px(6));inset.w-=float(px(12));inset.h-=float(px(12));
            auto geometry=object_preview(entry.command,canvas_);
            if(entry.command.starts_with("LIBRARY:")) {
                const auto index=std::stoul(entry.command.substr(8));
                gdoc::BlockRef ref;ref.name=block_library_.entries[index].block;
                geometry=block_library_.contents.blocks.expand(ref,vol_);
                if(geometry.empty())if(const auto* def=block_library_.contents.blocks.find(ref.name);def&&!def->notes.empty())
                    draw.text(inset,def->notes.front().text,ui_ctx_.style().font_size_small,color(gtheme::token::ui_text),gui::TextAlign::Centre);
            }
            draw_preview(draw,inset,geometry,vol_,color(gtheme::token::ui_text),float(px(1.2)));
            text.x+=float(px(54));text.w-=float(px(54));
            if(enabled && state.hovered && input_.pressed) {library_drag_command_=entry.command;library_drag_start_=input_.mouse;library_drag_preview_=geometry;}
            if(state.clicked || (enabled && ui_ctx_.activated(id))) {
                drafting_feature_panel_=0;
                if(entry.command.starts_with("LIBRARY:")) {
                    const auto index=std::stoul(entry.command.substr(8));
                    begin_pick(1,"Pick the symbol position",[this,index](const auto& points){insert_library_block(index,points[0]);});
                } else if(entry.command.starts_with("BLOCK:")) {
                    const auto name=entry.command.substr(6);
                    begin_pick(1,"Pick the block position",[this,name](const auto& points){double x,y,sx,sy;gcm::to_world(vol_,points[0],x,y);canvas_.view().screen_of(x,y,sx,sy);canvas_.insert_block(name,sx,sy);mesh_dirty_=true;});
                } else run_command(entry.command+(entry.command.starts_with("SYMBOL ")?" "+library_size_:""));
            }
        }
        draw.text(text,entry.label,ui_ctx_.style().font_size,color(enabled?gtheme::token::ui_text:gtheme::token::ui_text_dim));
        row.y+=float(px(48));
    }
    if(int(items.size())>visible) {
        const float track_top=panel.y+float(px(128)),track_height=bottom-track_top;
        const float thumb_height=track_height*visible/items.size();
        const float position=(track_height-thumb_height)*library_scroll_/(items.size()-visible);
        draw.round_rect({panel.right()-float(px(7)),track_top+position,float(px(3)),thumb_height},float(px(2)),color(gtheme::token::ui_border));
    }
    row={panel.x+pad,panel.bottom()-float(px(106)),panel.w-2*pad,float(px(28))};
    auto selection=row;selection.w=(row.w-float(px(8)))*.5f;
    auto collect=selection;collect.x=selection.right()+float(px(8));
    if(gui::button(ui_ctx_,selection,"Save selection…",canvas_.selection_count()>0))library_save_selection_open_=true;
    if(gui::button(ui_ctx_,collect,"Add drawing blocks",canvas_.blocks().size()>0))add_drawing_blocks_to_library();
    row.y+=float(px(33));auto load=row;load.w=(row.w-float(px(8)))*.5f;
    auto save=load;save.x=load.right()+float(px(8));
    if(gui::button(ui_ctx_,load,"Load library…"))ask_file(FileAsk::LoadLibrary);
    if(gui::button(ui_ctx_,save,"Save library…",!block_library_.entries.empty()))ask_file(FileAsk::SaveLibrary);
    row.y+=float(px(33));
    if(gui::button(ui_ctx_,row,"Generate / update legend")) {drafting_feature_panel_=0;run_command("SYMBOLLEGEND");}
    ui_ctx_.pop_id();ui_ctx_.end_overlay();
    if(!library_drag_command_.empty() && std::hypot(input_.mouse.x-library_drag_start_.x,input_.mouse.y-library_drag_start_.y)>px(4)) {
        gui::Rect ghost{input_.mouse.x+float(px(16)),input_.mouse.y+float(px(16)),float(px(64)),float(px(64))};
        ui_ctx_.begin_overlay(ghost);draw.round_rect(ghost,float(px(6)),color(gtheme::token::ui_surface_raised));
        auto inset=ghost;inset.x+=float(px(10));inset.y+=float(px(10));inset.w-=float(px(20));inset.h-=float(px(20));
        draw_preview(draw,inset,library_drag_preview_,vol_,color(gtheme::token::ui_accent),float(px(1.5)));ui_ctx_.end_overlay();
    }
}

void Shell::finish_library_drag(double x,double y) {
    const auto command=std::exchange(library_drag_command_,{});
    if(std::hypot(x-library_drag_start_.x,y-library_drag_start_.y)<px(4))return;
    if(!canvas_rect_.contains({float(x),float(y)}) || library_panel_.contains({float(x),float(y)}) || read_only())return;
    for(const auto& control:ui_ctx_.accessibility_tree())if(control.bounds.contains({float(x),float(y)}) &&
        (control.role==gui::Context::Role::Button || control.role==gui::Context::Role::Field))return;
    place_library_object(command,x,y);
}

void Shell::place_library_object(const std::string& command,double x,double y) {
    double sx,sy;if(mode_!=Mode::Sketch || !to_plane(x,y,sx,sy))return;
    canvas_.hover(sx,sy);double ax,ay;canvas_.view().world_of(sx,sy,ax,ay);
    if(command.starts_with("LIBRARY:")) {insert_library_block(std::stoul(command.substr(8)),gcm::to_lattice(vol_,ax,ay));return;}
    if(command.starts_with("BLOCK:")) {canvas_.insert_block(command.substr(6),sx,sy);mesh_dirty_=true;return;}
    const auto size=gunit::parse_length(library_size_,canvas_.display_unit());
    if(!size || !std::isfinite(*size) || *size<=0 || *size>10000) {log_.push_back("Library size must be a positive length");return;}
    double ux=1,uy=0;
    if(command=="DOOR" || command=="WINDOW" || command=="DOORFRAME" || command=="HINGE") {
        double nearest=.5;
        for(const auto& edge:canvas_.shapes()) {
            if(edge.layer!="A-WALL" || edge.kind!=gsnap::ShapeKind::Segment)continue;
            const auto type=edge.attrs.user.find("sketch_object");if(type!=edge.attrs.user.end() && type->second!="wall")continue;
            double a,b,c,d;gcm::to_world(vol_,edge.a,a,b);gcm::to_world(vol_,edge.b,c,d);
            const double len=std::hypot(c-a,d-b);if(len<*size)continue;
            double vx=(c-a)/len,vy=(d-b)/len;if(vx<-.001 || (std::abs(vx)<=.001 && vy<0)){vx=-vx;vy=-vy;}
            const double along=std::clamp(((ax-a)*(c-a)+(ay-b)*(d-b))/(len*len),0.,1.);
            const double distance=std::hypot(ax-a-along*(c-a),ay-b-along*(d-b));
            if(distance<nearest){nearest=distance;ux=vx;uy=vy;}
        }
    }
    const auto point=[](double x,double y){std::ostringstream out;out.precision(9);out<<std::fixed<<x<<"m,"<<y<<"m";return out.str();};
    gai::ToolCall call;gai::Json args;
    if(command.starts_with("SYMBOL ")) {call.function.name="draw_symbol";args={{"symbol",command.substr(7)},{"at",point(ax,ay)},{"size",library_size_}};}
    else if(command=="DOOR" || command=="DOORFRAME" || command=="HINGE") {
        call.function.name="draw_door";args={{"hinge",point(ax,ay)},{"latch",point(ax+ux* *size,ay+uy* *size)},{"framing","none"}};
        if(command!="DOOR")args["component"]=command=="HINGE"?"hinge":"frame";
    } else if(command=="WALL" || command=="WINDOW") {call.function.name=command=="WALL"?"draw_wall":"draw_window";args={{"from",point(ax,ay)},{"to",point(ax+ux* *size,ay+uy* *size)},{"framing","none"}};}
    else if(command=="HATCH" || command=="BACKGROUND") {
        if(command=="HATCH")canvas_.hatch_at_world(gcm::to_lattice(vol_,ax,ay),"ANSI31");
        else canvas_.background_at_world(gcm::to_lattice(vol_,ax,ay),gdoc::Color::rgb(116,169,207));
        log_.push_back(canvas_.message());mesh_dirty_=true;return;
    } else {
        auto geometry=object_preview(command,canvas_);const auto group=canvas_.next_polyline_id();
        canvas_.modify_shapes([&](auto& shapes){for(auto s:geometry) {
            const auto transform=[&](gcm::Lattice2 p){double a,b;gcm::to_world(vol_,p,a,b);return gcm::to_lattice(vol_,ax+a* *size,ay+b* *size);};
            s.a=transform(s.a);s.b=transform(s.b);s.radius=gcm::i64(std::llround(s.radius* *size));s.poly=group;s.poly_index=gcm::u32(shapes.size());
            s.layer=canvas_.current_layer();s.attrs=canvas_.current_attributes;shapes.push_back(s);
        }});mesh_dirty_=true;return;
    }
    call.function.arguments=args.dump();ai_tools::execute(*this,call,ai_tools::Origin::Library);mesh_dirty_=true;
}
}
