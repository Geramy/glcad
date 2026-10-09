#include "cpl_vsi.h"
#include <cstdarg>
#include <filesystem>
namespace gdgn_port {
thread_local std::string error;
thread_local std::vector<std::string> warnings;
void clear_error(){error.clear();warnings.clear();}
}
void CPLError(CPLErr severity,int,const char* format,...) {
    if(severity<CE_Warning)return;
    char message[1024];va_list args;va_start(args,format);
    std::vsnprintf(message,sizeof(message),format,args);va_end(args);
    if(severity==CE_Warning){if(gdgn_port::warnings.size()<32)gdgn_port::warnings.emplace_back(message);}
    else gdgn_port::error=message;
}
VSILFILE* VSIFOpenL(const char* path,const char* mode) {
#ifdef _WIN32
    const std::string utf8(path);const auto file=std::filesystem::path(std::u8string(utf8.begin(),utf8.end())).wstring();
    std::wstring flags;for(const auto* p=mode;*p;++p)flags+=wchar_t(*p);
    return _wfopen(file.c_str(),flags.c_str());
#else
    return std::fopen(path,mode);
#endif
}
int VSIFSeekL(VSILFILE* file,vsi_l_offset offset,int origin) {
    if(offset>std::uint64_t(std::numeric_limits<std::int64_t>::max()))return -1;
#ifdef _WIN32
    return _fseeki64(file,std::int64_t(offset),origin);
#else
    return fseeko(file,off_t(offset),origin);
#endif
}
vsi_l_offset VSIFTellL(VSILFILE* file) {
#ifdef _WIN32
    return vsi_l_offset(_ftelli64(file));
#else
    return vsi_l_offset(ftello(file));
#endif
}
