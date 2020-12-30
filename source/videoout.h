/*
**	VideoOut.h,	Initial VideoOut impl. - Add GpuAlloc 
*/
#pragma once

#include <orbis/VideoOut.h>

class VideoOut
{
//	typedef int32_t Handle;

	int32_t handle;
	int32_t flipMode, flipRate;
	bool tripleBuffer, tile;

	OrbisKernelEqueue flipQueue;
	OrbisVideoOutBufferAttribute attr;


	off_t phyAddr;

	addr_t mapAddr;
	size_t memSize;			// Size total of all framebuffer, aligned total
	size_t bufSize;			// Size of one framebuffer (w.o alignment)
	size_t alignTo;

	addr_t addrList[16];	// 16=max buffer slots

	unat currBuffer;

public:

	INLINE static VideoOut& Get() {
		static VideoOut videoOut(1920,1080,true,true);	
		return videoOut;
	}

	VideoOut(u32 defWidth=1920, u32 defHeight=1080, bool defTile=false, bool defTripleBuffer=false)
		: handle(0)
		, flipRate(1)	// 30fps auto
		, flipMode(1)	// vsync
		, tile(defTile)
		, tripleBuffer(defTripleBuffer)
		, alignTo(MB<unat>(2))
		, mapAddr(0)
		, phyAddr(0)
		, currBuffer(0)

	{
		memset(addrList, 0, sizeof(addr_t) * 16);

		memset(&attr, 0, sizeof(attr));
		attr.width  = defWidth;
		attr.height = defHeight;
		attr.aspect = 0;			// ORBIS_VIDEO_OUT_ASPECT_RATIO_16_9=0
		attr.format = 0x80002200;	// ORBIS_VIDEO_OUT_PIXEL_FORMAT_A8R8G8B8_SRGB
		attr.tmode  = 1;			// ORBIS_VIDEO_OUT_TILING_MODE_LINEAR; // u32(!tile);
		attr.pixelPitch = defWidth;	// so it changes every other member and leaves this one camel case ... 
	}

	bool Init()
	{
		u32 rx=attr.width, ry=attr.height;

		if (!Open()) {
			klog("Error, Failed to obtain handle!\n");
			return false;
		}
		if (!!sceKernelCreateEqueue(&flipQueue, "flipQueue") ||
			!!sceVideoOutAddFlipEvent(flipQueue, handle, NULL)) {
			klog("Error, Failed to create/add flip queue\n");
			goto fail;
		}
		if (!GetRes(rx, ry)) {
			attr.width =rx;
			attr.height=ry;
		}
		if (!AllocFramebuffers()) {
			klog("Error, Failed to AllocFramebuffers!\n");
			goto fail;
		}
		sceVideoOutSetFlipRate(handle, flipRate);

		return true;

fail:
		Close();
		return false;
	}

	void Term() {
		FreeFramebuffers();
		Close();
	}

	INLINE int32_t Handle() { return handle; }

	INLINE unat Width() { return attr.width; }
	INLINE unat Height() { return attr.height; }

	INLINE size_t MemSize() { return memSize;  }
	INLINE size_t BufferSize() { return bufSize; }
	INLINE size_t BufferCount() { return (tripleBuffer ? 3 : 2); }

	INLINE addr_t GetBuffer(unat n) { return addrList[n & 15]; }
	INLINE addr_t CurrentBuffer() { return GetBuffer(currBuffer); }

	INLINE void ClearBuffer(u32 b = ~0)	{
		addr_t buffer = (~0 == b || b >= BufferCount()) ? CurrentBuffer() : GetBuffer(b) ;
		memset((void*)buffer, 0, BufferSize());
	}

	INLINE bool IsFlipPending() {
		return sceVideoOutIsFlipPending(handle) != 0;
	}

	INLINE void WaitOnFlip() {
		int out = 0;
		OrbisKernelEvent ev;
		while (IsFlipPending()) {
			assert(ORBIS_OK == sceKernelWaitEqueue(flipQueue, &ev, 1, &out, 0));
		}
	}

	INLINE bool GetFlipStatus(OrbisVideoOutFlipStatus *status) {
		return ORBIS_OK == sceVideoOutGetFlipStatus(handle, status);
	}

	// sceVideoOutGetResolutionStatus

	INLINE void WaitOnFlip(u64 arg) {
		int out = 0;
		OrbisKernelEvent ev;
		OrbisVideoOutFlipStatus status;

		while (1) {
			GetFlipStatus(&status);
			if (status.flipArg >= arg)
				return;
			assert(ORBIS_OK == sceKernelWaitEqueue(flipQueue, &ev, 1, &out, 0));
		}
	}

	INLINE void SubmitFlip(s64 buffer = -1, u64 arg = 0)
	{
	//	sce::Gnm::flushGarlic();		// no flush4u

		currBuffer = (buffer == -1) ? currBuffer : buffer;
		sceVideoOutSubmitFlip(handle, currBuffer, flipMode, arg);
		//printf("SubmitFlip() Buffer[%d] %p \n", currBuffer, (u8*)CurrentBuffer());

		const static unat bufferCount = BufferCount();
		currBuffer = ((currBuffer + 1) % bufferCount);
	}

	INLINE int GetRes(u32& resX, u32& resY)
	{
		OrbisVideoOutResolutionStatus rs;
		memset(&rs, 0, sizeof(rs));

		int32_t rsu = sceVideoOutGetResolutionStatus(handle, &rs);
		if (ORBIS_OK==rsu) {
			resX = rs.width;
			resY = rs.height;
		}
		return rsu;
	}


private:

	bool Open() {
		handle = sceVideoOutOpen(ORBIS_VIDEO_USER_MAIN, ORBIS_VIDEO_OUT_BUS_MAIN, 0, 0);
		return (handle > 0);
	}

	void Close() {
		sceVideoOutClose(handle);
		handle = 0;
	}



	// 8294400 = 1x 1080p buffer @32bpp w.o padding/align

	bool AllocFramebuffers(u32 width=0, u32 height=0)
	{
		if (width  > 0) attr.width  = width;
		if (height > 0) attr.height = height;

		size_t pixelCount = attr.width * attr.height, Bpp = 4; // 32bpp
		size_t alignedSize = AlignUp(pixelCount * Bpp, alignTo);

		bufSize = pixelCount * Bpp;
		memSize = alignedSize * BufferCount();

		int32_t res = ORBIS_OK;

		if (ORBIS_OK != (res = sceKernelAllocateDirectMemory(0, ORBIS_KERNEL_MAIN_DMEM_SIZE, memSize, alignTo, ORBIS_KERNEL_WB_ONION, &phyAddr))) {
			klog("Error, sceKernelAllocateDirectMemory() Failed with 0x%08X\n",(u32)res);
			return false;
		}
		if (ORBIS_OK != (res = sceKernelMapDirectMemory((void**)&mapAddr, memSize, ORBIS_KERNEL_PROT_CPU_RW | ORBIS_KERNEL_PROT_GPU_RW, 0, phyAddr, alignTo))) {
			klog("Error, sceKernelMapDirectMemory() Failed with 0x%08X\n", (u32)res);
			return false;
		}

		for (unat i = 0; i < BufferCount(); i++) {
			addrList[i] = mapAddr + (i * alignedSize);	// fbAlloc(memSize, alignTo)
			klog("Buffer[%lX] %p \n", i, (void*)addrList[i]);
		}

		if (ORBIS_OK != (res = sceVideoOutRegisterBuffers(handle, 0, (void* const*)addrList, BufferCount(), &attr))) {
			klog("Error, sceVideoOutRegisterBuffers() Failed with 0x%08X\n", (u32)res);
			FreeFramebuffers();
			return false;
		}

		return true;

	}

	void FreeFramebuffers()
	{
		for (unat i=0; i<BufferCount(); i++)
			sceVideoOutUnregisterBuffers(handle, i);

		sceKernelMunmap((void*)mapAddr, memSize);
		sceKernelReleaseDirectMemory(phyAddr, memSize);
	}







};


