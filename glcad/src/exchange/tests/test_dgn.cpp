#include "exchange/dgn.hpp"
#include "dgnlib.h"
#include "test.hpp"
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>

namespace {
struct Scratch {
    std::filesystem::path dir=std::filesystem::temp_directory_path()/
        ("glcad-dgn-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Scratch(){std::filesystem::create_directories(dir);}
    ~Scratch(){std::error_code ec;std::filesystem::remove_all(dir,ec);}
    std::string path(const char* name)const{return (dir/name).string();}
};
void write_element(DGNHandle handle,DGNElemCore* element) {
    CHECK(element!=nullptr);if(!element)return;
    CHECK(DGNWriteElement(handle,element));DGNFreeElement(handle,element);
}
}
TEST(dgn_reader_preserves_master_coordinates_arcs_text_and_late_palette) {
    Scratch scratch;const auto path=scratch.path("drawing.dgn");
    auto file=DGNCreate(path.c_str(),GLCAD_DGN_SEED,0,0,0,0,1000,100,"m","mm");CHECK(file!=nullptr);if(!file)return;
    DGNPoint points[]={{10,20,0},{12,23,0}};
    auto* line=DGNCreateMultiPointElem(file,DGNT_LINE,2,points);CHECK(line!=nullptr);
    if(line)CHECK(DGNUpdateElemCore(file,line,12,4,7,3,DGNS_LONG_DASH));write_element(file,line);
    auto* arc=DGNCreateArcElem2D(file,DGNT_ARC,4,5,2,1,30,20,120);write_element(file,arc);
    auto* text=DGNCreateTextElem(file,"Survey",3,DGNJ_LEFT_BOTTOM,.02,.03,45,nullptr,6,7,0);write_element(file,text);
    GByte palette[256][3]{};palette[7][0]=12;palette[7][1]=90;palette[7][2]=180;
    write_element(file,DGNCreateColorTableElem(file,0,palette));DGNClose(file);
    std::filesystem::resize_file(path,std::filesystem::file_size(path)-2);
    {std::ofstream output(path,std::ios::binary|std::ios::app);std::array<unsigned char,1536> extra{};
     extra[0]=8;extra[1]=9;extra[2]=254;extra[3]=2;output.write(reinterpret_cast<const char*>(extra.data()),extra.size());
     output.put(char(255));output.put(char(255));}
    const auto drawing=gdgn::read_file(path);CHECK_MSG(drawing.ok,"%s",drawing.error.c_str());
    CHECK(drawing.dimension==2);CHECK(drawing.master_unit=="m");CHECK(drawing.subunit=="mm");
    CHECK(drawing.subunits_per_master==1000);CHECK(drawing.uor_per_subunit==100);
    const auto find=[&](gdgn::Element::Kind kind){return std::find_if(drawing.elements.begin(),drawing.elements.end(),[&](const auto& e){return e.kind==kind;});};
    const auto l=find(gdgn::Element::Kind::Polyline);CHECK(l!=drawing.elements.end());
    if(l!=drawing.elements.end()) {
        CHECK(l->points.size()==2);CHECK(l->level==12);CHECK(l->group==4);CHECK(l->weight==3);CHECK(l->style==DGNS_LONG_DASH);
        CHECK(l->color_index==7);CHECK(l->color[0]==12&&l->color[1]==90&&l->color[2]==180);
        if(l->points.size()==2){CHECK(std::abs(l->points[0].x-10)<1e-5);CHECK(std::abs(l->points[1].y-23)<1e-5);}
    }
    const auto a=find(gdgn::Element::Kind::Arc);CHECK(a!=drawing.elements.end());
    if(a!=drawing.elements.end()) {CHECK(std::abs(a->origin.x-4)<1e-5);CHECK(std::abs(a->primary_axis-2)<1e-5);CHECK(std::abs(a->secondary_axis-1)<1e-5);CHECK(std::abs(a->rotation-30)<1e-5);CHECK(std::abs(a->start_angle-20)<1e-5);CHECK(std::abs(a->sweep_angle-120)<1e-5);}
    const auto t=find(gdgn::Element::Kind::Text);CHECK(t!=drawing.elements.end());
    if(t!=drawing.elements.end()) {CHECK(t->text=="Survey");CHECK(t->font==3);CHECK(t->justification==DGNJ_LEFT_BOTTOM);CHECK(std::abs(t->origin.x-6)<1e-5);CHECK(std::abs(t->text_height-.03)<1e-5);CHECK(std::abs(t->rotation-45)<1e-5);}
    std::filesystem::resize_file(path,std::filesystem::file_size(path)-8);
    const auto broken=gdgn::read_file(path);CHECK(!broken.ok);CHECK(broken.error.find("truncated")!=std::string::npos);
}
TEST(dgn_reader_rejects_non_dgn_and_missing_files) {
    Scratch scratch;const auto path=scratch.path("foreign.dgn");
    {std::ofstream file(path,std::ios::binary);file<<"This is not a drawing";}
    CHECK(!gdgn::read_file(path).ok);CHECK(!gdgn::read_file(scratch.path("missing.dgn")).ok);
}
TEST(dgn_reader_accepts_utf8_source_paths) {
    Scratch scratch;const auto file=scratch.dir/std::filesystem::path(u8"survéy_日本.dgn");
    std::filesystem::copy_file(GLCAD_DGN_SEED,file);const auto name=file.u8string();
    const auto drawing=gdgn::read_file(std::string(name.begin(),name.end()));
    CHECK_MSG(drawing.ok,"%s",drawing.error.c_str());CHECK(drawing.dimension==2);
}

#if defined(GLCAD_PORTABLE_UNDERLAYS) || defined(__APPLE__)
#include "exchange/underlay_image.hpp"
TEST(dgn_underlay_renders_offset_geometry_and_resolves_source_units_once) {
    Scratch scratch;const auto path=scratch.path("offset.dgn");
    auto file=DGNCreate(path.c_str(),GLCAD_DGN_SEED,0,0,0,0,1000,100,"mm","um");CHECK(file!=nullptr);if(!file)return;
    DGNPoint points[]={{-4,8,0},{6,8,0},{6,12,0},{-4,12,0},{-4,8,0}};
    auto* shape=DGNCreateMultiPointElem(file,DGNT_SHAPE,5,points);
    CHECK(shape!=nullptr);if(shape){CHECK(DGNAddShapeFillInfo(file,shape,7)>=0);write_element(file,shape);}
    GByte palette[256][3]{};palette[7][0]=255;write_element(file,DGNCreateColorTableElem(file,0,palette));DGNClose(file);
    std::string error;const auto raster=gplot::load_underlay(path,1,error,400);
    CHECK_MSG(raster.valid(),"%s",error.c_str());if(!raster.valid())return;
    // The fixture writer truncates to one source UOR (1e-8 metres here).
    CHECK_MSG(std::abs(raster.metres_x+.0041)<2e-8,"x=%.12g",raster.metres_x);CHECK(std::abs(raster.metres_y-.0079)<1e-8);
    CHECK_MSG(std::abs(raster.metres_w-.0102)<2e-8,"w=%.12g",raster.metres_w);CHECK(std::abs(raster.metres_h-.0042)<1e-8);CHECK(raster.source_unit==.001);
    const auto center=(std::size_t(raster.height/2)*raster.width+raster.width/2)*4;
    CHECK(raster.rgba[center]>245&&raster.rgba[center+1]<10&&raster.rgba[center+2]<10);
    CHECK(!gplot::load_underlay(path,2,error).valid());
    gcm::WorkingVolume volume;volume.extent=500;volume.resolution_bits=32;
    gdoc::Underlay u;u.path=path;u.kind=gdoc::UnderlayKind::Dgn;u.sheet="Model";u.fade=0;
    u.scale=2;u.y_factor=-1;u.insertion=gcm::to_lattice(volume,10,20);u.source_coordinates_pending=true;u.source_unit_known=true;
    u.clip={gcm::to_lattice(volume,-3,9),gcm::to_lattice(volume,1,9),gcm::to_lattice(volume,1,11),gcm::to_lattice(volume,-3,11)};
    CHECK(gplot::resolve_underlay(u,volume,error));CHECK(!u.source_coordinates_pending);CHECK(u.source_unit_known);CHECK(u.source_unit==.001);CHECK(u.scale==2000);
    double x,y;gcm::to_world(volume,u.clip.front(),x,y);CHECK(std::abs(x+.003)<1e-6&&std::abs(y-.009)<1e-6);
    const auto before=u;CHECK(gplot::resolve_underlay(u,volume,error));CHECK(u.scale==before.scale);CHECK(u.clip.front().x==before.clip.front().x);
    const auto image=gplot::load_underlay(u,volume,error);CHECK_MSG(image.valid(),"%s",error.c_str());if(!image.valid())return;
    const auto alpha=[&](double px,double py){const int ix=int((px-image.metres_x)/image.metres_w*image.width),iy=int((1-(py-image.metres_y)/image.metres_h)*image.height);return image.rgba[(std::size_t(iy)*image.width+ix)*4+3];};
    CHECK(alpha(-.001,.01)==255);CHECK(alpha(.004,.01)==0);
    const auto snaps=gplot::underlay_snap_boundary(u,image,volume);CHECK(snaps.size()==4);
    for(const auto& sh:snaps){gcm::to_world(volume,sh.a,x,y);CHECK(x>=4-.001&&x<=12+.001&&y>=-2-.001&&y<=2+.001);}
    u.sheet="1trailing";CHECK(!gplot::resolve_underlay(u,volume,error));CHECK(u.scale==before.scale);
    u=before;u.sheet="Missing model";CHECK(!gplot::load_underlay(u,volume,error).valid());
}
TEST(dgn_custom_units_require_explicit_mapping_without_recentering) {
    Scratch scratch;const auto path=scratch.path("custom.dgn");
    auto file=DGNCreate(path.c_str(),GLCAD_DGN_SEED,0,0,0,0,10,1000,"mu","su");CHECK(file!=nullptr);if(!file)return;
    DGNPoint points[]={{100,200,0},{102,202,0}};write_element(file,DGNCreateMultiPointElem(file,DGNT_LINE,2,points));DGNClose(file);
    std::string error;CHECK(!gplot::load_underlay(path,1,error).valid());CHECK(error.find("explicit")!=std::string::npos);
    gcm::WorkingVolume volume;volume.extent=500;volume.resolution_bits=32;
    gdoc::Underlay u;u.kind=gdoc::UnderlayKind::Dgn;u.path=path;
    CHECK(!gplot::resolve_underlay(u,volume,error));CHECK(!u.source_unit_known);
    CHECK(gplot::resolve_underlay(u,volume,error,.01));CHECK(u.source_unit_known);CHECK(u.scale==1);
    const auto image=gplot::load_underlay(u,volume,error);CHECK_MSG(image.valid(),"%s",error.c_str());
    std::size_t dark=0;for(std::size_t i=0;i<image.rgba.size();i+=4)if(image.rgba[i]<100&&image.rgba[i+1]<100&&image.rgba[i+2]<100)++dark;
    CHECK(dark>100);
    CHECK(std::abs(image.metres_x-.9998)<1e-8);CHECK(std::abs(image.metres_y-1.9998)<1e-8);
    CHECK(!gplot::resolve_underlay(u,volume,error,1000));CHECK(u.source_unit==.01);
}
#endif

#if defined(GLCAD_PORTABLE_UNDERLAYS) || defined(__APPLE__)
TEST(dgn_complex_fills_and_grouped_holes_reveal_earlier_geometry) {
    Scratch scratch;const auto path=scratch.path("holes.dgn");
    auto file=DGNCreate(path.c_str(),GLCAD_DGN_SEED,0,0,0,0,1000,100,"m","mm");CHECK(file!=nullptr);if(!file)return;
    const auto shape=[&](double lo,double hi,int fill){DGNPoint points[]={{lo,lo,0},{hi,lo,0},{hi,hi,0},{lo,hi,0},{lo,lo,0}};
        auto* result=DGNCreateMultiPointElem(file,DGNT_SHAPE,5,points);CHECK(result!=nullptr);
        if(result)CHECK(DGNAddShapeFillInfo(file,result,fill)>=0);return result;};
    write_element(file,shape(0,10,1));
    std::vector<DGNElemCore*> members;
    const auto complex=[&](double lo,double hi,bool hole,int fill) {
        DGNPoint points[]={{lo,lo,0},{hi,lo,0},{hi,hi,0},{lo,hi,0}};
        std::vector<DGNElemCore*> edges;
        for(const int index:{2,0,3,1}) {DGNPoint pair[]={points[(index+1)%4],points[index]};edges.push_back(DGNCreateMultiPointElem(file,DGNT_LINE,2,pair));}
        auto* header=DGNCreateComplexHeaderFromGroup(file,DGNT_COMPLEX_SHAPE_HEADER,int(edges.size()),edges.data());CHECK(header!=nullptr);
        if(header){CHECK(DGNAddShapeFillInfo(file,header,fill)>=0);if(hole){header->properties|=DGNPF_HOLE;DGNUpdateElemCoreExtended(file,header);}}
        members.push_back(header);members.insert(members.end(),edges.begin(),edges.end());
    };
    complex(1,9,false,3);complex(3,6,true,2);
    DGNPoint origin={12,24,0};auto* cell=DGNCreateCellHeaderFromGroup(file,"HOLES",0,nullptr,int(members.size()),members.data(),&origin,2,3,45);
    CHECK(cell!=nullptr);write_element(file,cell);for(auto* member:members)write_element(file,member);
    write_element(file,shape(11,12,4));
    GByte palette[256][3]{};palette[0][0]=palette[0][1]=palette[0][2]=255;palette[1][2]=255;palette[2][1]=255;palette[3][0]=255;palette[4][0]=palette[4][1]=255;
    write_element(file,DGNCreateColorTableElem(file,0,palette));DGNClose(file);
    const auto drawing=gdgn::read_file(path);CHECK_MSG(drawing.ok,"%s",drawing.error.c_str());if(!drawing.ok)return;
    const auto parent=std::find_if(drawing.elements.begin(),drawing.elements.end(),[](const auto& e){return e.kind==gdgn::Element::Kind::CellHeader;});
    CHECK(parent!=drawing.elements.end());if(parent==drawing.elements.end())return;
    CHECK(parent->group_end>parent->offset+parent->bytes);
    CHECK(std::count_if(drawing.elements.begin(),drawing.elements.end(),[](const auto& e){return e.hole;})==1);
    std::string error;const auto image=gplot::load_underlay(path,1,error,600);CHECK_MSG(image.valid(),"%s",error.c_str());if(!image.valid())return;
    const auto pixel=[&](double x,double y,int channel){const int ix=int((x-image.metres_x)/image.metres_w*image.width),iy=int((1-(y-image.metres_y)/image.metres_h)*image.height);return image.rgba[(std::size_t(iy)*image.width+ix)*4+channel];};
    CHECK(pixel(7,7,0)>245&&pixel(7,7,1)<10&&pixel(7,7,2)<10);
    CHECK(pixel(4,4,2)>245&&pixel(4,4,0)<10&&pixel(4,4,1)<10);
    CHECK(pixel(.5,.5,2)>245&&pixel(.5,.5,0)<10);
    CHECK(pixel(11.5,11.5,0)>245&&pixel(11.5,11.5,1)>245&&pixel(11.5,11.5,2)<10);
    CHECK(std::none_of(image.warnings.begin(),image.warnings.end(),[](const auto& w){return w.find("cannot fill")!=std::string::npos||w.find("hole")!=std::string::npos;}));
    {std::fstream corrupt(path,std::ios::in|std::ios::out|std::ios::binary);corrupt.seekp(parent->offset+36);corrupt.put(char(255));corrupt.put(char(255));}
    const auto broken=gdgn::read_file(path);CHECK(!broken.ok);CHECK(broken.error.find("byte range")!=std::string::npos);
}
#endif

#if defined(GLCAD_PORTABLE_UNDERLAYS) || defined(__APPLE__)
TEST(dgn_grouped_holes_support_arc_boundaries_and_ellipses) {
    Scratch scratch;const auto path=scratch.path("curved-hole.dgn");
    auto file=DGNCreate(path.c_str(),GLCAD_DGN_SEED,0,0,0,0,1000,100,"m","mm");CHECK(file!=nullptr);if(!file)return;
    DGNPoint diameter[]={{1,5,0},{9,5,0}};
    DGNElemCore* edges[]={DGNCreateArcElem2D(file,DGNT_ARC,5,5,4,4,0,0,180),DGNCreateMultiPointElem(file,DGNT_LINE,2,diameter)};
    auto* outer=DGNCreateComplexHeaderFromGroup(file,DGNT_COMPLEX_SHAPE_HEADER,2,edges);CHECK(outer!=nullptr);
    auto* hole=DGNCreateArcElem2D(file,DGNT_ELLIPSE,5,7,1,.5,30,0,360);CHECK(hole!=nullptr);
    if(hole){hole->properties|=DGNPF_HOLE;DGNUpdateElemCoreExtended(file,hole);}
    DGNElemCore* members[]={outer,edges[0],edges[1],hole};DGNPoint origin{};
    auto* cell=DGNCreateCellHeaderFromGroup(file,"CURVED",0,nullptr,4,members,&origin,1,1,0);
    CHECK(cell!=nullptr);if(cell) {
        CHECK(DGNAddShapeFillInfo(file,cell,3)>=0);
        // The fixture writer leaves cell spans for the caller to update.
        auto* header=reinterpret_cast<DGNElemCellHeader*>(cell);header->totlength+=8;
        cell->raw_data[36]=header->totlength&255;cell->raw_data[37]=(header->totlength>>8)&255;
    }
    write_element(file,cell);for(auto* e:members)write_element(file,e);DGNClose(file);
    std::string error;const auto image=gplot::load_underlay(path,1,error,400);CHECK_MSG(image.valid(),"%s",error.c_str());if(!image.valid())return;
    const auto pixel=[&](double x,double y,int channel){const int ix=int((x-image.metres_x)/image.metres_w*image.width),iy=int((1-(y-image.metres_y)/image.metres_h)*image.height);return image.rgba[(std::size_t(iy)*image.width+ix)*4+channel];};
    CHECK_MSG(pixel(5,7,0)>245&&pixel(5,7,1)>245&&pixel(5,7,2)>245,"hole RGB %d,%d,%d",pixel(5,7,0),pixel(5,7,1),pixel(5,7,2));
    CHECK(pixel(5,8.4,0)>245&&pixel(5,8.4,1)<10&&pixel(5,8.4,2)<10);
    CHECK(std::none_of(image.warnings.begin(),image.warnings.end(),[](const auto& w){return w.find("cannot fill")!=std::string::npos;}));
}
#endif
