#include "base.h"

#include "videoout.h"
#include "kernel_utils.h"

#include "imgui/imgui.h"
#include "imgui/imgui_sw.hpp"

#include "ui/app2usb.h"
#include "ui/fsbrowser.h"


//#ifndef _OO_
unsigned int sceLibcHeapExtendedAlloc = 1;
size_t       sceLibcHeapSize = ~0;	//SCE_LIBC_HEAP_SIZE_EXTENDED_ALLOC_NO_LIMIT;
size_t       sceLibcHeapInitialSize = MB(256);
unsigned int sceLibcHeapDebugFlags = 4; //SCE_LIBC_HEAP_DEBUG_SHORTAGE;
//#endif


int main()
{
	jailbreak(get_fw_version());

	OrbisUserServiceUserId userId=0;
	if (ORBIS_OK != sceUserServiceInitialize(nullptr) ||
		ORBIS_OK != sceUserServiceGetInitialUser(&userId)) {
		klog("Error, sceUserService{Init,GetUser}(): Failed! \n");
		return -1;
	}
	klog("-- got userId: 0x%08X\n", userId);


	int pad_h = 0;
	if(!!scePadInit() ||
	   !(pad_h = scePadOpen(userId, 0 /*std*/, 0, nullptr))) {
		klog("Error, scePad{Init,Open}(): Failed! \n");
		return -1;
	}
	klog("-- got pad with handle: 0x%08X\n",pad_h);

	VideoOut& videoOut = VideoOut::Get();
	if (!videoOut.Init()) {
		klog("Error, VideoOut::Init(): Failed! \n");
		return 0;
	}

	klog("[VideoOut] Initialized...\n");
#if _DEBUG
	for (auto _i = 0; _i < 3; _i++)	{
		videoOut.ClearBuffer();
		videoOut.SubmitFlip();
		videoOut.WaitOnFlip();
	}
#endif

	u32 resW=videoOut.Width(), resH=videoOut.Height();

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.DisplaySize = ImVec2(resW, resH);
#if 0	// this look bad on soft.rast, DisplayFramebufferScale not usable here either
	io.FontGlobalScale = Align(resH/720.f, 0.25f, 0);	// DisplayFramebufferScale=ImVec2(resW/1280.f, resH/720.f)
#endif


	klog("calling imgui_sw::bind_imgui_painting()...\n");
	imgui_sw::SwOptions sw_options;
	imgui_sw::bind_imgui_painting();

	App2USB& ui = App2USB::Get();

	while(1) {
		videoOut.ClearBuffer();

		if(!io.Fonts->IsBuilt())
			klog("Error, No Font is set! \n");

		ImGui::NewFrame();


		ui.render();


		ImGui::Render();

		paint_imgui((uint32_t*)videoOut.CurrentBuffer(), videoOut.Width(), videoOut.Height(), sw_options);


		videoOut.WaitOnFlip();
		videoOut.SubmitFlip();


		OrbisPadData pad;
		memset(&pad,0,sizeof(pad));
		if(!!scePadReadState(pad_h, &pad))
			klog("Warning, failed to read data for pad_h: 0x%08X\n", pad_h);
		else {
			for (uint32_t i = 0; i < 22; i++)
				io.NavInputs[i] = 0.f;

			BtnState* bs = (BtnState*)&pad.buttons;

			if (bs->cross)	io.NavInputs[ImGuiNavInput_Activate]	= 1.f;
			if (bs->circle)	io.NavInputs[ImGuiNavInput_Cancel]		= 1.f;

			if (bs->up)		io.NavInputs[ImGuiNavInput_DpadUp]		= 1.f;
			if (bs->down)	io.NavInputs[ImGuiNavInput_DpadDown]	= 1.f;
			if (bs->left)	io.NavInputs[ImGuiNavInput_DpadLeft]	= 1.f;
			if (bs->right)	io.NavInputs[ImGuiNavInput_DpadRight]	= 1.f;
#if 0
#define MAP_BUTTON(NAV_NO, BUTTON_NO)       { io.NavInputs[NAV_NO] = (SDL_JoystickGetButton(joy, BUTTON_NO) != 0) ? 1.0f : 0.0f; }

			MAP_BUTTON(ImGuiNavInput_Activate, SDL_CONTROLLER_BUTTON_A);           // 0, A / Cross
			MAP_BUTTON(ImGuiNavInput_Cancel, SDL_CONTROLLER_BUTTON_B);             // 1, B / Circle
			MAP_BUTTON(ImGuiNavInput_Menu, SDL_CONTROLLER_BUTTON_X);               // 2, X / Square
			MAP_BUTTON(ImGuiNavInput_Input, SDL_CONTROLLER_BUTTON_Y);              // 3, Y / Triangle

			float thumb_dead_zone = 3000;
			Sint16 ls = SDL_JoystickGetAxis(joy, 0);
			Sint16 rs = SDL_JoystickGetAxis(joy, 1);

			if (abs(ls) > thumb_dead_zone) printf("LStick Analogue: %d\n", ls);
			if (abs(rs) > thumb_dead_zone) printf("RStick Analogue: %d\n", rs);

#define MAP_ANALOG(NAV_NO, AXIS_V, V0, V1) { float vn = (float)(AXIS_V - V0) / (float)(V1 - V0); if (vn > 1.0f) vn = 1.0f; if (vn > 0.0f && io.NavInputs[NAV_NO] < vn) io.NavInputs[NAV_NO] = vn; }
			MAP_ANALOG(ImGuiNavInput_LStickLeft, ls, -thumb_dead_zone, -32768);
			MAP_ANALOG(ImGuiNavInput_LStickRight, ls, +thumb_dead_zone, +32767);
			MAP_ANALOG(ImGuiNavInput_LStickUp, rs, -thumb_dead_zone, -32767);
			MAP_ANALOG(ImGuiNavInput_LStickDown, rs, +thumb_dead_zone, +32767);
#endif

		}

	}

neveruary:
	scePadClose(pad_h);
	videoOut.Term();
	return 0;
}











void klog(const char *format, ...)
{
	char buff[1024];
	memset(buff, 0, 1024);

	va_list args;
	va_start(args, format);
	vsprintf(buff, format, args);
	va_end(args);

	int fd = open("/dev/ttyu0", 1 /*O_WRONLY*/, 0600);             // /dev/klog? O_DIRECT | Open the device with read/write access
	if (fd < 0) {
		perror("Failed to open the device...");        // idk if we have perror, doesn't matter we'll find out 
		return;
	}
	char* t = buff;
	while (0 != *t) write(fd, t++, 1);
	close(fd);
}


