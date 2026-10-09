#pragma once
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#define CPL_DLL
#define CPL_UNUSED
#define CPL_C_START extern "C" {
#define CPL_C_END }
#define TRUE 1
#define FALSE 0
#ifndef M_PI
#define M_PI 3.141592653589793238462643383279502884
#endif
using GByte = unsigned char;
using GInt32 = std::int32_t;
using GUInt32 = std::uint32_t;
using vsi_l_offset=std::uint64_t;
#if defined(_WIN32) || __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define CPL_LSB
#define CPL_LSBPTR16(p) ((void)(p))
#define CPL_LSBPTR32(p) ((void)(p))
#define CPL_MSBPTR32(p) gdgn_swap32(p)
#else
#define CPL_LSBPTR16(p) gdgn_swap16(p)
#define CPL_LSBPTR32(p) gdgn_swap32(p)
#define CPL_MSBPTR32(p) ((void)(p))
#endif
inline void gdgn_swap16(void* p) {auto* b=static_cast<unsigned char*>(p);std::swap(b[0],b[1]);}
inline void gdgn_swap32(void* p) {auto* b=static_cast<unsigned char*>(p);std::swap(b[0],b[3]);std::swap(b[1],b[2]);}
