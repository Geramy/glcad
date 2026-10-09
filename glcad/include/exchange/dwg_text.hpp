#pragma once
#include <string>
#include <string_view>
#include <stdexcept>
#include <cstdio>

namespace gdwg::detail {
// R2000 stores byte strings; Unicode escapes carry UTF-16 outside the codepage.
inline std::string encode_text(std::string_view source) {
    std::string out;
    for(std::size_t i=0;i<source.size();) {
        unsigned char ch=source[i++];
        if(ch<128) { out.push_back(char(ch)); continue; }
        unsigned cp=0,need=0,minimum=0;
        if((ch&0xe0)==0xc0) { cp=ch&31; need=1; minimum=128; }
        else if((ch&0xf0)==0xe0) { cp=ch&15; need=2; minimum=2048; }
        else if((ch&0xf8)==0xf0) { cp=ch&7; need=3; minimum=65536; }
        else throw std::runtime_error("invalid UTF-8");
        for(unsigned j=0;j<need;++j) { if(i>=source.size()||(static_cast<unsigned char>(source[i])&0xc0)!=0x80) throw std::runtime_error("invalid UTF-8"); cp=(cp<<6)|(source[i++]&63); }
        if(cp<minimum||cp>0x10ffff||(cp>=0xd800&&cp<=0xdfff)) throw std::runtime_error("invalid UTF-8");
        const auto append=[&](unsigned unit) { char buffer[8]; std::snprintf(buffer,sizeof(buffer),"\\U+%04X",unit); out+=buffer; };
        if(cp<=0xffff) append(cp); else { cp-=0x10000; append(0xd800+(cp>>10)); append(0xdc00+(cp&1023)); }
    }
    if(out.size()>32767||out.find('\0')!=std::string::npos) throw std::runtime_error("DWG text exceeds format limits");
    return out;
}
inline std::string decode_text(std::string_view source) {
    std::string out;
    const auto unit=[&](std::size_t at)->int {
        if(at+7>source.size()||source.substr(at,3)!="\\U+") return -1;
        int value=0; for(int k=3;k<7;++k) { char c=source[at+k]; int d=c>='0'&&c<='9'?c-'0':c>='A'&&c<='F'?c-'A'+10:c>='a'&&c<='f'?c-'a'+10:-1; if(d<0) return -1; value=value*16+d; } return value;
    };
    for(std::size_t i=0;i<source.size();) {
        int cp=unit(i); if(cp<0) { out+=source[i++]; continue; }
        if(cp>=0xd800&&cp<=0xdbff) { int low=unit(i+7); if(low<0xdc00||low>0xdfff) { out+=source[i++]; continue; } cp=0x10000+((cp-0xd800)<<10)+low-0xdc00; i+=7; }
        else if(cp>=0xdc00&&cp<=0xdfff) { out+=source[i++]; continue; }
        i+=7;
        if(cp<128) out+=char(cp);
        else if(cp<2048) { out+=char(0xc0|(cp>>6)); out+=char(0x80|(cp&63)); }
        else if(cp<65536) { out+=char(0xe0|(cp>>12)); out+=char(0x80|((cp>>6)&63)); out+=char(0x80|(cp&63)); }
        else { out+=char(0xf0|(cp>>18)); out+=char(0x80|((cp>>12)&63)); out+=char(0x80|((cp>>6)&63)); out+=char(0x80|(cp&63)); }
    }
    return out;
}
}
