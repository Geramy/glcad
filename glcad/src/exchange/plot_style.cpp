#include "exchange/plot_style.hpp"
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <functional>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <zlib.h>

namespace gplot {
namespace {
struct Node {
    std::set<std::string> strings;
    std::map<std::string,Node> children;
    std::map<std::string,std::string> values;
    std::string get(const std::string& key,const std::string& fallback="") const {
        auto it=values.find(key); return it==values.end() ? fallback : it->second;
    }
    double number(const std::string& key,double fallback) const {
        auto it=values.find(key); if(it==values.end()) return fallback;
        std::istringstream in(it->second); in.imbue(std::locale::classic()); double result;
        if(!(in>>result)||!std::isfinite(result)) throw std::runtime_error("invalid number: "+key);
        in>>std::ws;
        // Some Autodesk files append an encoded numeric representation.
        if(!in.eof() && in.peek()!='(') throw std::runtime_error("invalid number: "+key);
        return result;
    }
    long long integer(const std::string& key,long long fallback) const {
        double n=number(key,double(fallback));
        if(n!=std::floor(n)||n<-2147483648.0||n>4294967295.0) throw std::runtime_error("invalid integer: "+key);
        return static_cast<long long>(n);
    }
    bool boolean(const std::string& key,bool fallback) const {
        auto text=get(key,fallback ? "TRUE" : "FALSE");
        if(text=="TRUE") return true;
        if(text=="FALSE") return false;
        throw std::runtime_error("invalid boolean: "+key);
    }
};
std::string trim(std::string text) {
    auto begin=text.find_first_not_of(" \t\r"); if(begin==std::string::npos) return {};
    return text.substr(begin,text.find_last_not_of(" \t\r")-begin+1);
}
Node parse(std::string_view source) {
    Node root; std::vector<Node*> stack{&root};
    std::istringstream input{std::string(source)}; std::string line;
    while(std::getline(input,line)) {
        line=trim(line); if(line.empty()) continue;
        if(line=="}") { if(stack.size()==1) throw std::runtime_error("unmatched closing brace"); stack.pop_back(); continue; }
        auto eq=line.find('=');
        if(eq!=std::string::npos) {
            auto key=trim(line.substr(0,eq)), value=trim(line.substr(eq+1));
            if(key.empty()||stack.back()->values.contains(key)||stack.back()->children.contains(key)) throw std::runtime_error("duplicate or empty field");
            if(value.starts_with('"')) { value.erase(0,1); stack.back()->strings.insert(key); }
            stack.back()->values.emplace(std::move(key),std::move(value));
        } else if(line.ends_with('{')) {
            auto key=trim(line.substr(0,line.size()-1));
            if(key.empty()||stack.size()>=8||stack.back()->values.contains(key)||stack.back()->children.contains(key)) throw std::runtime_error("invalid group");
            stack.push_back(&stack.back()->children[key]);
        } else throw std::runtime_error("expected a field or group");
    }
    if(stack.size()!=1) throw std::runtime_error("unterminated group");
    return root;
}
}

PlotStyleReadResult read_plot_style(std::span<const gcm::u8> bytes) {
    PlotStyleReadResult result;
    try {
        constexpr std::size_t limit=4*1024*1024;
        if(bytes.size()<62||bytes.size()>limit) throw std::runtime_error("invalid file size");
        const std::string header(reinterpret_cast<const char*>(bytes.data()),48);
        if(header!="PIAFILEVERSION_2.0,CTBVER1,compress\r\npmzlibcodec" &&
           header!="PIAFILEVERSION_2.0,STBVER1,compress\r\npmzlibcodec") throw std::runtime_error("unsupported CTB/STB header");
        const auto word=[&](int at) { return std::uint32_t(bytes[at])|(std::uint32_t(bytes[at+1])<<8)|(std::uint32_t(bytes[at+2])<<16)|(std::uint32_t(bytes[at+3])<<24); };
        auto expanded=word(52), compressed=word(56);
        if(!expanded||expanded>limit||compressed!=bytes.size()-60) throw std::runtime_error("invalid compressed size");
        if(adler32(1,bytes.data()+60,compressed)!=word(48)) throw std::runtime_error("compressed checksum mismatch");
        std::string text(expanded,'\0'); z_stream stream{};
        stream.next_in=const_cast<Bytef*>(bytes.data()+60); stream.avail_in=compressed;
        stream.next_out=reinterpret_cast<Bytef*>(text.data()); stream.avail_out=expanded;
        if(inflateInit(&stream)!=Z_OK) throw std::runtime_error("cannot initialize decompression");
        const int status=inflate(&stream,Z_FINISH); inflateEnd(&stream);
        if(status!=Z_STREAM_END||stream.total_out!=expanded||stream.total_in!=compressed) throw std::runtime_error("invalid compressed data");
        if(text.back()=='\0') text.pop_back();
        if(text.find('\0')!=std::string::npos) throw std::runtime_error("embedded null in table");
        auto root=parse(text); const bool indexed=root.boolean("aci_table_available",header.find("CTBVER")!=std::string::npos);
        result.table=PlotStyleTable(indexed ? StyleMode::ColorDependent : StyleMode::Named);
        result.table.set_indexed_only(indexed);
        auto styles=root.children.find("plot_style"), weights=root.children.find("custom_lineweight_table");
        if(styles==root.children.end()||styles->second.children.empty()||styles->second.children.size()>4096) throw std::runtime_error("missing or oversized style list");
        if(indexed && styles->second.children.size()!=255) throw std::runtime_error("CTB requires 255 styles");
        result.table.source_text=text;
        result.table.pattern_scale=root.boolean("apply_factor",true)?root.number("scale_factor",1):1;
        if(result.table.pattern_scale<=0||result.table.pattern_scale>10000) throw std::runtime_error("invalid pattern scale");
        bool pens=false;
        std::vector<std::string> names;
        for(const auto& [key,node]:styles->second.children) {
            PlotStyle style; style.name=node.get("name");
            if(style.name.empty()||std::find(names.begin(),names.end(),style.name)!=names.end()) throw std::runtime_error("duplicate or empty style name");
            names.push_back(style.name);
            const auto color=node.integer("color",-1), mode=node.integer("mode_color",color);
            style.use_object_color=color==-1 || color==-1006632961 || color==4294967295LL || color==3288334335LL;
            if(!style.use_object_color) { auto rgb=std::uint32_t(mode); style.r=gcm::u8(rgb>>16); style.g=gcm::u8(rgb>>8); style.b=gcm::u8(rgb); }
            style.grayscale=(node.integer("color_policy",1)&2)!=0;
            style.screening=node.number("screen",100);
            if(style.screening<0||style.screening>100) throw std::runtime_error("screen outside 0..100");
            const auto weight=node.integer("lineweight",0);
            if(weight<0||weight>4096) throw std::runtime_error("invalid lineweight index");
            if(weight) {
                if(weights==root.children.end()||!weights->second.values.contains(std::to_string(weight))) throw std::runtime_error("missing lineweight entry");
                style.width_mm=weights->second.number(std::to_string(weight),0);
                if(style.width_mm<=0||style.width_mm>100) throw std::runtime_error("invalid lineweight");
                style.use_object_lineweight=false;
                style.lineweight=gdoc::nearest_lineweight(style.width_mm);
            }
            const auto cap=node.integer("end_style",4), join=node.integer("join_style",5);
            if(cap<0||cap>4||join<0||join>5) throw std::runtime_error("invalid cap or join");
            style.source_end_style=int(cap); style.source_join_style=int(join);
            if(cap<4) style.endcap=LinecapStyle(cap);
            if(join<4) style.join=LinejoinStyle(join);
            const auto pattern=node.integer("linetype",31);
            if(pattern<0||pattern>31) throw std::runtime_error("invalid linetype");
            style.pattern_id=int(pattern); style.use_object_linetype=pattern==31;
            style.pattern_size=node.number("linepattern_size",0.5);
            if(style.pattern_size<=0||style.pattern_size>10000) throw std::runtime_error("invalid pattern size");
            style.adaptive_linetype=node.boolean("adaptive_linetype",true);
            style.fill_style=int(node.integer("fill_style",73));
            if(style.fill_style<64||style.fill_style>73) throw std::runtime_error("invalid fill style");
            pens|=node.integer("physical_pen_number",0)!=0||node.integer("virtual_pen_number",0)!=0;
            if(indexed) {
                Node index; index.values["index"]=key; auto i=index.integer("index",-1);
                if(i<0||i>=255) throw std::runtime_error("invalid colour index");
                result.table.by_index(int(i+1))=style;
            } else result.table.add(std::move(style));
        }
        if(pens) result.warnings.push_back("Physical and virtual pen assignments are retained but not used by PDF/DWF");
        result.ok=true;
    } catch(const std::exception& error) { result.error=std::string("Plot style: ")+error.what(); }
    return result;
}
std::vector<gcm::u8> write_plot_style(const PlotStyleTable& table,std::string* error) {
    if(error) error->clear();
    try {
        const auto real=[](double value) { std::ostringstream out; out.imbue(std::locale::classic()); out<<std::setprecision(17)<<value; return out.str(); };
        Node root=table.source_text.empty()?Node{}:parse(table.source_text);
        const bool indexed=table.mode()==StyleMode::ColorDependent;
        root.values["aci_table_available"]=indexed?"TRUE":"FALSE";
        root.values["scale_factor"]=real(table.pattern_scale); root.values["apply_factor"]="TRUE";
        root.values.try_emplace("description",""); root.values.try_emplace("custom_lineweight_display_units","0");
        auto old_styles=root.children["plot_style"]; root.children["plot_style"]=Node{};
        auto& weights=root.children["custom_lineweight_table"];
        weights.values["0"]="0";
        if(indexed) root.children["aci_table"]=Node{};
        for(std::size_t i=0;i<table.all().size();++i) {
            const auto& style=table.all()[i]; const std::string key=std::to_string(i);
            if(style.name.empty()||style.name.find_first_of("\r\n")!=std::string::npos) throw std::runtime_error("invalid style name");
            Node node;
            for(const auto& [old_key,old]:old_styles.children) if((indexed&&old_key==key)||(!indexed&&old.get("name")==style.name)) { node=old; break; }
            node.values["name"]=style.name; node.values.try_emplace("localized_name",style.name); node.values.try_emplace("description","");
            auto rgb=std::uint32_t(style.r)<<16|std::uint32_t(style.g)<<8|style.b;
            node.values["color"]=style.use_object_color?"-1":std::to_string(std::int32_t(0xc2000000u|rgb));
            node.values["mode_color"]=node.values["color"];
            node.values["color_policy"]=std::to_string((node.integer("color_policy",1)&~2)|(style.grayscale?2:0));
            node.values["screen"]=real(style.plot?style.screening:0);
            node.values["linetype"]=std::to_string(style.use_object_linetype?31:style.pattern_id);
            node.values["linepattern_size"]=real(style.pattern_size);
            node.values["adaptive_linetype"]=style.adaptive_linetype?"TRUE":"FALSE";
            node.values["fill_style"]=std::to_string(style.fill_style);
            node.values["end_style"]=std::to_string(style.source_end_style==4&&style.endcap==LinecapStyle::Butt?4:int(style.endcap));
            node.values["join_style"]=std::to_string(style.source_join_style==5&&style.join==LinejoinStyle::Miter?5:int(style.join));
            node.values["lineweight"]="0";
            if(!style.use_object_lineweight) {
                double mm=style.width_mm>=0?style.width_mm:gdoc::lineweight_mm(style.lineweight);
                mm*=style.lineweight_scale;
                std::string wk=std::to_string(weights.values.size());
                while(weights.values.contains(wk)) wk=std::to_string(std::stoul(wk)+1);
                for(const auto& [k,value]:weights.values) if(std::abs(weights.number(k,0)-mm)<1e-9) { wk=k; break; }
                weights.values[wk]=real(mm); node.values["lineweight"]=wk;
            }
            root.children["plot_style"].children[key]=std::move(node);
            if(indexed) { root.children["aci_table"].values[key]=style.name; root.children["aci_table"].strings.insert(key); }
        }
        std::ostringstream out; out.imbue(std::locale::classic());
        std::function<void(const Node&)> emit=[&](const Node& n) {
            for(const auto& [key,value]:n.values) {
                const bool text=n.strings.contains(key)||key=="description"||key=="name"||key=="localized_name"||key=="color_name"||value.starts_with("Color_");
                out<<key<<"="<<(text?"\"":"")<<value<<"\n";
            }
            for(const auto& [key,node]:n.children) { out<<key<<"{\n"; emit(node); out<<"}\n"; }
        };
        emit(root); std::string text=out.str(); text.push_back(0);
        if(text.size()>4*1024*1024) throw std::runtime_error("table exceeds size limit");
        uLongf size=compressBound(text.size()); std::vector<gcm::u8> compressed(size);
        if(compress2(compressed.data(),&size,reinterpret_cast<const Bytef*>(text.data()),text.size(),Z_BEST_COMPRESSION)!=Z_OK) throw std::runtime_error("compression failed");
        compressed.resize(size);
        const std::string header=indexed?"PIAFILEVERSION_2.0,CTBVER1,compress\r\npmzlibcodec":"PIAFILEVERSION_2.0,STBVER1,compress\r\npmzlibcodec";
        std::vector<gcm::u8> bytes(header.begin(),header.end());
        const auto word=[&](std::uint32_t v){ for(int i=0;i<4;++i) bytes.push_back(gcm::u8(v>>(8*i))); };
        word(adler32(1,compressed.data(),compressed.size())); word(text.size()); word(compressed.size()); bytes.insert(bytes.end(),compressed.begin(),compressed.end());
        const auto check=read_plot_style(bytes); if(!check.ok) throw std::runtime_error(check.error);
        return bytes;
    } catch(const std::exception& e) { if(error) *error=e.what(); return {}; }
}
std::vector<f64> plot_style_dashes(int pattern,f64 size,f64 scale) {
    // Published CTB indices; lengths are multiples of the paper pattern size.
    static const std::vector<std::vector<double>> patterns={
        {},{6,3},{0,3},{6,3,0,3},{3,3},{6,3},{12,3},{3,3,3,6},{6,3,6,6},{12,3,12,6},
        {6,3,12,3},{6,3,3,3,3,3},{12,3,3,3},{12,3,0,3,0,3},{12,3,0,3},{6,3,0,3,3,3,0,3},{0,6},
        {12,3},{12,18},{24,3,0,3},{24,3,0,3,0,3},{24,3,0,3,0,3,0,3},{0,3},{24,3,6,3},{24,3,6,3,6,3},
        {12,3,0,3},{12,3,12,3,0,3},{12,3,0,3,0,3},{12,3,12,3,0,3,0,3},{12,3,0,3,0,3,0,3},{12,3,12,3,0,3,0,3,0,3}
    };
    if(pattern<1||pattern>=int(patterns.size())) return {};
    auto out=patterns[pattern]; for(auto& value:out) value=std::max(0.05,value*size*(pattern<17?scale:1)); return out;
}

}
