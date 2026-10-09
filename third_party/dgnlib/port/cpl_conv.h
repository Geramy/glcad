#pragma once
#include "cpl_port.h"
#include <new>
#include <string>
#include <vector>
#define CPLAssert assert
enum CPLErr {CE_None,CE_Debug,CE_Warning,CE_Failure,CE_Fatal};
enum {CPLE_AppDefined=1,CPLE_OpenFailed=4,CPLE_AssertionFailed=7};
namespace gdgn_port {
extern thread_local std::string error;
extern thread_local std::vector<std::string> warnings;
void clear_error();
}
void CPLError(CPLErr,int,const char*,...);
inline void* CPLMalloc(std::size_t n) {
    if(n>64*1024*1024)throw std::bad_alloc();
    void* p=std::malloc(n?n:1);if(!p)throw std::bad_alloc();return p;
}
inline void* CPLCalloc(std::size_t n,std::size_t size) {
    if(size&&n>64*1024*1024/size)throw std::bad_alloc();
    void* p=std::calloc(n?n:1,size?size:1);if(!p)throw std::bad_alloc();return p;
}
inline void* CPLRealloc(void* old,std::size_t n) {
    if(n>64*1024*1024)throw std::bad_alloc();
    void* p=std::realloc(old,n?n:1);if(!p)throw std::bad_alloc();return p;
}
inline void CPLFree(void* p){std::free(p);}
inline char* CPLStrdup(const char* s){auto n=std::strlen(s)+1;auto* p=static_cast<char*>(CPLMalloc(n));std::memcpy(p,s,n);return p;}
inline double CPLAtof(const char* s){return std::strtod(s,nullptr);}
