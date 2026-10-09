#include "exchange/dgn.hpp"
#include "dgnlib.h"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <set>

namespace gdgn {
namespace {
struct Close {void operator()(void* handle)const {if(handle)DGNClose(handle);}};
struct Free {DGNHandle handle;void operator()(DGNElemCore* p)const {if(p)DGNFreeElement(handle,p);}};
Point point(DGNPoint p){return {p.x,p.y,p.z};}
bool finite(Point p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);}
bool complete_records(const std::filesystem::path& path,std::uintmax_t size,std::vector<std::uint64_t>& boundaries) {
    std::ifstream input(path,std::ios::binary);if(!input)return false;
    std::uintmax_t offset=0;
    boundaries.push_back(0);
    while(offset<size) {
        unsigned char header[4]{};input.read(reinterpret_cast<char*>(header),4);
        if(input.gcount()>=2&&header[0]==255&&header[1]==255)return true;
        if(input.gcount()!=4)return false;
        const auto bytes=std::uintmax_t(header[2]+256u*header[3])*2+4;
        if(bytes>size-offset||((header[1]&127)==DGNT_TCB&&bytes<1536))return false;
        offset+=bytes;boundaries.push_back(offset);input.seekg(std::streamoff(bytes-4),std::ios::cur);if(!input)return false;
    }
    return true;
}
}
Drawing read_file(const std::string& path) {
    Drawing out;gdgn_port::clear_error();
    try {
        const auto file=std::filesystem::path(std::u8string(path.begin(),path.end()));
        std::error_code ec;const auto size=std::filesystem::file_size(file,ec);
        if(ec||size<4||size>64*1024*1024){out.error="DGN source unavailable or outside the 64 MiB input limit";return out;}
        std::vector<std::uint64_t> boundaries;
        if(!complete_records(file,size,boundaries)){out.error="truncated or invalid DGN element records";return out;}
        std::unique_ptr<void,Close> handle(DGNOpen(path.c_str(),FALSE));
        if(!handle){out.error=gdgn_port::error.empty()?"not a DGN v7 drawing":gdgn_port::error;return out;}
        std::vector<std::pair<int,int>> colors;
        std::set<int> unsupported;
        std::size_t count=0,vertices=0;
        bool units=false;
        while(auto* raw=DGNReadElement(handle.get())) {
            std::unique_ptr<DGNElemCore,Free> item(raw,Free{handle.get()});
            if(++count>1000000){out.error="DGN element count limit exceeded";return out;}
            if(raw->deleted)continue;
            if(raw->stype==DGNST_TCB) {
                if(units)continue;
                const auto& t=*reinterpret_cast<const DGNElemTCB*>(raw);
                out.dimension=t.dimension;out.master_unit=t.master_units;out.subunit=t.sub_units;
                out.subunits_per_master=t.subunits_per_master;out.uor_per_subunit=t.uor_per_subunit;
                out.origin={t.origin_x,t.origin_y,t.origin_z};units=true;continue;
            }
            if(raw->stype==DGNST_COLORTABLE)continue;
            Element e;e.type=raw->type;e.id=raw->element_id;e.level=raw->level;e.group=raw->graphic_group;
            e.offset=raw->offset;e.bytes=raw->size;e.hole=(raw->properties&DGNPF_HOLE)!=0;
            e.weight=raw->weight;e.style=raw->style;e.properties=raw->properties;e.complex=raw->complex;
            int fill=-1;e.filled=DGNGetShapeFillInfo(handle.get(),raw,&fill)!=0;
            if(raw->stype==DGNST_MULTIPOINT&&raw->type!=DGNT_BSPLINE_POLE) {
                const auto& line=*reinterpret_cast<const DGNElemMultiPoint*>(raw);
                if(line.num_vertices<2||line.num_vertices>65535){out.error="invalid DGN vertex count";return out;}
                e.kind=Element::Kind::Polyline;e.closed=raw->type==DGNT_SHAPE;
                if(raw->type==DGNT_CURVE) {
                    const auto n=std::max(32,std::min(8192,line.num_vertices*16));
                    std::vector<DGNPoint> sampled(n);
                    if(!DGNStrokeCurve(handle.get(),const_cast<DGNElemMultiPoint*>(&line),n,sampled.data())){out.error="invalid DGN curve";return out;}
                    for(auto p:sampled)e.points.push_back(point(p));
                } else for(int i=0;i<line.num_vertices;++i)e.points.push_back(point(line.vertices[i]));
            } else if(raw->stype==DGNST_ARC) {
                const auto& arc=*reinterpret_cast<const DGNElemArc*>(raw);e.kind=Element::Kind::Arc;
                e.origin=point(arc.origin);e.primary_axis=arc.primary_axis;e.secondary_axis=arc.secondary_axis;
                e.rotation=arc.rotation;e.start_angle=arc.startang;e.sweep_angle=arc.sweepang;
                std::copy(std::begin(arc.quat),std::end(arc.quat),e.quaternion.begin());
                e.closed=raw->type==DGNT_ELLIPSE;
            } else if(raw->stype==DGNST_TEXT) {
                const auto& text=*reinterpret_cast<const DGNElemText*>(raw);e.kind=Element::Kind::Text;
                e.origin=point(text.origin);e.text=text.string;e.font=text.font_id;e.justification=text.justification;
                e.text_width=text.length_mult;e.text_height=text.height_mult;e.rotation=text.rotation;
            } else if(raw->stype==DGNST_COMPLEX_HEADER) {
                const auto& complex=*reinterpret_cast<const DGNElemComplexHeader*>(raw);e.kind=Element::Kind::ComplexHeader;
                e.children=complex.numelems;e.closed=raw->type==DGNT_COMPLEX_SHAPE_HEADER;
                e.group_end=e.offset+38+2*std::uint64_t(complex.totlength);
            } else if(raw->stype==DGNST_CELL_HEADER) {
                e.kind=Element::Kind::CellHeader;
                const auto& cell=*reinterpret_cast<const DGNElemCellHeader*>(raw);
                e.group_end=e.offset+38+2*std::uint64_t(cell.totlength);
            } else if(raw->type==DGNT_TEXT_NODE||raw->type==DGNT_LEVEL_SYMBOLOGY||raw->type==DGNT_DIGITIZER_SETUP) {
                continue;
            } else {
                unsupported.insert(raw->type);
            }
            if(e.group_end&&(e.group_end<e.offset+e.bytes||e.group_end>size||
               !std::binary_search(boundaries.begin(),boundaries.end(),e.group_end))) {
                out.error="invalid DGN complex element byte range";return out;
            }
            vertices+=e.points.size();
            if(vertices>2000000){out.error="DGN vertex limit exceeded";return out;}
            if(!finite(e.origin)||!std::isfinite(e.primary_axis)||!std::isfinite(e.secondary_axis)||!std::isfinite(e.rotation)||
               !std::isfinite(e.start_angle)||!std::isfinite(e.sweep_angle)||!std::isfinite(e.text_width)||!std::isfinite(e.text_height)||
               std::any_of(e.points.begin(),e.points.end(),[](auto p){return !finite(p);})) {
                out.error="non-finite DGN coordinates";return out;
            }
            e.color_index=raw->color;e.fill_index=fill;
            colors.emplace_back(raw->color,fill);out.elements.push_back(std::move(e));
        }
        if(!gdgn_port::error.empty()){out.error=gdgn_port::error;return out;}
        if(!units||out.subunits_per_master<=0||out.uor_per_subunit<=0||!finite(out.origin)) {out.error="missing or invalid DGN drawing units";return out;}
        const auto palette=[&](int index) {
            int r=0,g=0,b=0;DGNLookupColor(handle.get(),index,&r,&g,&b);
            return std::array<std::uint8_t,3>{std::uint8_t(r),std::uint8_t(g),std::uint8_t(b)};
        };
        std::vector<std::uint64_t> groups;
        for(const auto& e:out.elements) {
            while(!groups.empty()&&e.offset>=groups.back())groups.pop_back();
            if(e.group_end) {
                if(!groups.empty()&&e.group_end>groups.back()){out.error="DGN complex elements overlap their parent byte range";return out;}
                if(groups.size()>=64){out.error="DGN complex nesting exceeds 64 levels";return out;}
                groups.push_back(e.group_end);
            }
        }
        for(std::size_t i=0;i<out.elements.size();++i){out.elements[i].color=palette(colors[i].first);if(out.elements[i].filled)out.elements[i].fill_color=palette(colors[i].second);}
        for(auto type:unsupported)out.warnings.push_back("DGN element type "+std::to_string(type)+" has no drawable decoder");
        out.warnings.insert(out.warnings.end(),gdgn_port::warnings.begin(),gdgn_port::warnings.end());
        out.ok=true;
    } catch(const std::exception& e) {out.error=std::string("DGN reader: ")+e.what();}
    return out;
}
}
