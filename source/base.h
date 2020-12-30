/*
**	base.h
*/
#pragma once

#include <cstdio>
#include <cstdint>
#include <cstdarg>
#include <cstdlib>
#include <cassert>

#include <string.h>
#include <memory.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <unistd.h>
#include <dirent.h>
#include <limits.h>
#include <fcntl.h>
#include <ctype.h>
#include <ftw.h>

#include <string>
#include <vector>

#include <atomic>

using namespace std;


#include <orbis/libkernel.h>
#include <orbis/UserService.h>
#include <orbis/Pad.h>



// base types

typedef uint8_t  u8,  U8;
typedef uint16_t u16, U16;
typedef uint32_t u32, U32;
typedef uint64_t u64, U64;

typedef int8_t  s8,  S8;
typedef int16_t s16, S16;
typedef int32_t s32, S32;
typedef int64_t s64, S64;

typedef float  f32, F32;
typedef double f64, F64;


typedef U64 unat, unat_t;	// 'Natural' sized base types, PS4 is x64 no check req.
typedef S64 snat, snat_t;

/*
#ifdef size_t	// getting errors about size_t being int, W T F
#undef size_t
typedef unat  size_t;
#endif
*/
typedef unat  addr_t;
typedef off_t offs_t;
// size_t is std //



typedef int32_t sce_error;


// constexpr helpers

template<typename T> constexpr T KB(const T n) { return 1024 * n;  }
template<typename T> constexpr T MB(const T n) { return 1024 * KB(n); }
template<typename T> constexpr T GB(const T n) { return 1024 * MB(n); }
template<typename T> constexpr T TB(const T n) { return 1024 * GB(n); }



#define INLINE inline // __forceinline__


// Generic Alignment for non pow2, up is abuseable since it's an integer :)

template<typename T> INLINE T Align(const T val, const T align, unat up=0)
{
	return (val / align + up) * align;
}

// Alignment for unsigned integers of any type, align must be pow2!

#define POW2_MASK (align - static_cast<T>(1))

template<typename T> INLINE T AlignUp(const T addr, const T align)
{
	return (addr + POW2_MASK) & ~POW2_MASK;
}

template<typename T> INLINE T AlignDown(const T addr, const T align)
{
	return addr & ~POW2_MASK;
}

//#define alignTo		Align
#define alignUp		AlignUp
#define alignDown	AlignDown

#define SCE_OK ORBIS_OK
#define IS_OK(expr)  (SCE_OK == (expr))
#define FAILED(expr) (SCE_OK != (expr))



void klog(const char *format, ...);

static void hexdump(u8 *pAddr, u32 psize, u32 cols = 16)
{
#if 1
	if(psize >= 0x100000)  // we're not writing a book to uart
		return;
#endif

	for (u32 n=0; n<psize; n+=cols) {
		u32 clen = ((psize - n) > cols) ? cols : (psize - n);
		klog("%p +%04X:  ", pAddr, n);
		for (u32 c=0; c<clen; c++)  klog("%02X%s", pAddr[n+c], ((3==(c&3)) && (c!=(clen-1))) ? " - ":" ");
		klog("  -   \"");
		for (u32 c=0; c< clen; c++) klog("%c", (isprint(pAddr[n+c])) ? pAddr[n + c] : ' ');
		klog("\" \n");
	}
}




inline static bool strf(std::string *str, const char *fmt, ...)
{
	int required, length;
	va_list args;

	va_start(args, fmt);
	required = std::vsnprintf(nullptr, 0, fmt, args);
	va_end(args);
	if (required < 0) {
		str->clear();
		return false;
	}
	required += 1;

	auto buf = std::make_unique<char[]>(required);
	va_start(args, fmt);
	length = std::vsnprintf(buf.get(), required, fmt, args);
	va_end(args);
	if (length < 0) {
		str->clear();
		return false;
	}

	*str = buf.get();
	return true;
}




#define _YN(b) ((b)?"Yes":"No")




struct BtnState {
	u32 _dumb	: 1;
	u32 l3		: 1;
	u32 r3		: 1;
	u32 options	: 1;

	u32 up		: 1;
	u32 right	: 1;
	u32 down	: 1;
	u32 left	: 1;

	u32 l2		: 1;
	u32 r2		: 1;
	u32 l1		: 1;
	u32 r1		: 1;

	u32 triangle: 1;
	u32 circle	: 1;
	u32 cross	: 1;
	u32 square	: 1;

	u32 _dumber	: 4;
	u32 touch	: 1;
	u32 _dumbest:10;
	u32 intercepted:1;
};

