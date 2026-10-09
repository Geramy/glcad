#pragma once
#include "cpl_conv.h"
using VSILFILE=std::FILE;
VSILFILE* VSIFOpenL(const char*,const char*);
int VSIFSeekL(VSILFILE*,vsi_l_offset,int);
vsi_l_offset VSIFTellL(VSILFILE*);
inline int VSIFCloseL(VSILFILE* f){return std::fclose(f);}
inline std::size_t VSIFReadL(void* p,std::size_t size,std::size_t n,VSILFILE* f){return std::fread(p,size,n,f);}
inline std::size_t VSIFWriteL(const void* p,std::size_t size,std::size_t n,VSILFILE* f){return std::fwrite(p,size,n,f);}
inline void VSIRewindL(VSILFILE* f){std::rewind(f);}
inline void VSIFree(void* p){CPLFree(p);}
#define VSI_CALLOC_VERBOSE(n,size) CPLCalloc(n,size)
