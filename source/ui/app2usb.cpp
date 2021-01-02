#include "../base.h"


#include "../videoout.h"
#include "../kernel_utils.h"

#include "../imgui/imgui.h"
#include "../imgui/imgui_sw.hpp"


#include "app2usb.h"



atomic<u64> opNum{0}, progCurr{0}, progSize{0};




void App2USB::render()
{
	static pthread_t opThread;

	auto scaling = 1.0f;	// only used for spacing
	u32 resW=VideoOut::Get().Width(), resH=VideoOut::Get().Height();

	if (None!=opType) {		// will race for opType but should be ok 
		bool confirmed=false;
		ImGui::OpenPopup("Confirm");

		ImGui::SetNextWindowSize(ImVec2(600, 400));
		if (ImGui::BeginPopupModal("Confirm"))
		{
			if (PlsWait == opType)	// In thread, get operation status from atomics and display for stupid ppl to not close/unplug usb/restart/panic and hyperventilate
			{
				ImGui::Text("Please Wait");
				ImGui::Text("");	// casts are ugly but c++ needs implicit or explicit cast to get a clue what's required here
				ImGui::Text("Operation %ld:  Progress %ld%% (%s/%s)", u64(opNum), u64(100.f*double(u64(progCurr))/u64(progSize)),
							sizeStr(progCurr).c_str(), sizeStr(progSize).c_str());	//u64(progCurr), u64(progSize));

				if (mtx.try_lock()) {
					void* rv=nullptr;
					pthread_join(opThread,&rv);
					confirmed=true;
					mtx.unlock();
				}
			}
			else if (FatalError==opType)
			{
				ImGui::Text("There was an error! (In: %s)", (nullptr==opTarget?"[ALL] operation":opTarget->path.c_str()));
				if(!errorStr.empty())
					ImGui::Text("%s", errorStr.c_str());

				if (ImGui::Button("OK"))
					confirmed=true;
			}
			else {
				ImGui::Text(" "); ImGui::Text(" ");
				ImGui::SetNextWindowPosCenter();
				ImGui::Text("Are you certain?");

				ImGui::SetNextWindowPos(ImVec2(280, 350));

				if (ImGui::Button("YES")) {
#if !defined(PLZ_NO_THREAD)
					if (!!pthread_create(&opThread, nullptr, thrEntry, nullptr)) {
						klog("Error, failed to create thread!\n");
						opType=FatalError;
					}
					usleep(100);	// *FIXME* come up with better wai, split opType and uiType
					opType=PlsWait;	
					
#else
					if (!bakeMeAcake()) {
						opType=FatalError;
					else
						confirmed=true;
#endif
				}

				ImGui::SameLine(44);
				if (ImGui::Button("No... scared: lemme out of here!")) 
					confirmed=true;
			}

			ImGui::EndPopup();
		}

		if(confirmed) {
			opType=None;
			opTarget=nullptr;
			errorStr.clear();

			refreshList();
		}
	}

	ImGui::SetNextWindowPos(ImVec2(0, 0));
	ImGui::SetNextWindowSize(ImVec2(resW, resH));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);

	ImGui::Begin("##app2usb", NULL, ImGuiWindowFlags_NoDecoration);


	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(22 * scaling, 10 * scaling));		// from 8, 4
	ImGui::AlignTextToFramePadding();
	ImGui::Text("\t-*-\tps4-app2usb\t-*- Use buttons to move single items or select [X] check boxes to move multiple. -*-");
	ImGui::SameLine(ImGui::GetContentRegionAvailWidth() - 500);		ImGui::Text("Current USB Device: \"%s\" ", usbPath.c_str());
	ImGui::SameLine(ImGui::GetContentRegionAvailWidth() - 260); if (ImGui::Button("Change to Next USB Device")) {
		usbIdx  = ++usbIdx % usbList.size();
		usbPath = usbList[usbIdx];
	}


	ImGui::Text("\t-*-");
	ImGui::SameLine(140.f);	if (ImGui::Button("Select ALL"))   { for (auto& e : opList) { e.selected=true; }  }
	ImGui::SameLine(270.f);	if (ImGui::Button("Deselect ALL")) { for (auto& e : opList) { e.selected=false; } }
	ImGui::SameLine(520);	if (ImGui::Button("Move Selected to USB"))			{ opType=SelToUSB; }
	ImGui::SameLine(720);	if (ImGui::Button("Move Selected to PS4 Console"))	{ opType=SelToHDD; }
	ImGui::Text(" ");

	{
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8 * scaling, 20 * scaling));		// from 8, 4

		for (auto& e : opList)
		{
			ImGui::PushID(&e);

			bool onUsb = entOnUSB(e);

			const char* btnText = onUsb ? "Move to PS4" : "Move to USB";
			if (ImGui::Button(btnText)) {
				opType=onUsb ? ToHDD : ToUSB ;
				opTarget=&e;
			}

			ImGui::SameLine(140.f);
			if(ImGui::Checkbox("Select to Move with ALL", &e.selected)) {
				klog("entry %s is now %s\n", e.path.c_str(), e.selected?"true":"false");
			}

			ImGui::SameLine(380.f);		ImGui::Text("\"  %s  \"", e.tid.c_str());
			ImGui::SameLine(520.f);		ImGui::Text("\"  %s  \"", e.path.c_str());

			ImGui::PopID();
		}

		ImGui::PopStyleVar();
	}

	ImGui::Text("\t-*-");


	ImGui::PopStyleVar();
	ImGui::PopStyleVar();

	ImGui::End();

}


	// could spawn thread and check mtx locked var or atomic
	bool App2USB::bakeMeAcake()
	{
		vector<OPEntry*> selList;

		if ( (opType==None || opType==FatalError || PlsWait==opType) ||
			((opType==ToUSB|| opType==ToHDD) && !opTarget))
			return false;

		if (ToUSB==opType || ToHDD==opType)
			selList.push_back(opTarget);
		else {
			for (auto& e : opList)
				if (e.selected)
					selList.push_back(&e);
		}

		if (!existsDir(usbPath)) {
			klog("USB Mount point is invalid! usbPath \"%s\" \n", usbPath.c_str());
			errorStr=string("USB Mount point is invalid! Did you remove it?");
			return false;
		}

#if defined(_USE_LAME_PATHS_FOR_EXISTING_USBS)	// Don't bother, they used original package names from the json and that is 100% pointless: someone elses problem
		string	pathUsbBase		(usbPath+"/PS4"),
				pathUsbApp		(pathUsbBase+"/"),
				pathUsbPatch	(pathUsbBase+"/updates"),
				pathUsbAddCont	(pathUsbBase+"/dlc"),
				pathUsbAppMeta	(usbBasePath+"/whotfknows_check_copyMeta()"),
			;
#else
		string	pathUsbBase		(usbPath+"/app2usb"),
				pathUsbApp		(pathUsbBase+"/app"),
				pathUsbPatch	(pathUsbBase+"/patch"),
				pathUsbAddCont	(pathUsbBase+"/addcont"),
				pathUsbAppMeta	(pathUsbBase+"/appmeta")
			;
#endif
		string	pathUserApp		("/user/app"),
				pathUserPatch	("/user/patch"),
				pathUserAddCont	("/user/addcont"),
				pathUserAppMeta	("/user/appmeta")
			;

		bool toUSB = (ToUSB==opType || SelToUSB==opType);

		for (const OPEntry* ent : selList) {

			if (!ent || ent->path.empty()) return false;

			klog("opNum[%zd] path: \"%s\" \n", u64(opNum++), ent->path.c_str());

			// just remove toUSB/HOST and always switch?  people are stupid they'll prob get confused and wonder wtf is going on and keep swapping them until their brain falls out 
			const string& srcTID = ent->tid;
			const string& srcApp = ent->path;
	
			const string  dstApp = toUSB? string(pathUsbApp  + "/" + srcTID)
										: string(pathUserApp + "/" + srcTID);


			if (srcApp==dstApp || (toUSB && string::npos != srcApp.rfind("/mnt/usb"))) {
				klog("@@@@@@@@@@ *FIXME* trying to copy to same place! src: \"%s\" , dst: \"%s\" \n", srcApp.c_str(), dstApp.c_str());
				continue;
			}

			string srcPkg = srcApp + "/app.pkg";
			string dstPkg = dstApp + "/app.pkg";


			if (existsLink(srcPkg)) { klog(">>> src is symlink!\n"); continue; }	// shouldn't be in list, remove?
			if (!toUSB && existsLink(dstPkg)) unlinkFile(dstPkg);


			if (!existsFile(srcPkg)) {
				opType=FatalError;
				errorStr="Failed to find app pkg!";
				klog("Error, find app pkg: \"%s\" ent{tid: \"%s\" , path: \"%s\"}\n", srcPkg.c_str(), ent->tid.c_str(), ent->path.c_str());
				return false;
			}

			if (!existsDir(dstApp))
				mkpath(dstApp,0700);
		
			if (!moveFile(srcPkg, dstPkg,0770)) {
				opType=FatalError;
				errorStr="Failed to move app pkg!";
				klog("Error, move app pkg: \"%s\" \n", srcPkg.c_str());
				return false;
			}

			if (toUSB && !linkFile(srcPkg, dstPkg))	{
				opType=FatalError;
				errorStr="Failed to link app pkg!";
				klog("Error, link app pkg: \"%s\" \n", srcPkg.c_str());
				return false;
			}

#if _WANT_PATCH_MOVED
			{
				const string  srcPatch = toUSB	? string(pathUserPatch + "/" + srcTID)
												: string(pathUsbPatch  + "/" + srcTID);

				const string  dstPatch = toUSB	? string(pathUsbPatch  + "/" + srcTID)
												: string(pathUserPatch + "/" + srcTID);


				srcPkg = srcPatch + "/patch.pkg";
				dstPkg = dstPatch + "/patch.pkg";

				if (existsFile(srcPkg))
				{
					if (!existsDir(dstPatch))
						mkpath(dstPatch, 0700);

					if (!moveFile(srcPkg, dstPkg, 0770)) {
						opType=FatalError;
						errorStr="Failed to move patch pkg!";
						klog("Error, move patch pkg: \"%s\" \n", srcPkg.c_str());
						return false;
					}

					if (toUSB && !linkFile(srcPkg, dstPkg))	{
						opType=FatalError;
						errorStr="Failed to link patch pkg!";
						klog("Error, link patch pkg: \"%s\" \n", srcPkg.c_str());
						return false;
					}

				}
			}
#endif

#if _WANT_ADDCONT_MOVED
			{
				const string  srcAddCont = toUSB? string(pathUserAddCont + "/" + srcTID)
												: string(pathUsbAddCont	 + "/" + srcTID);

				const string  dstAddCont = toUSB? string(pathUsbAddCont  + "/" + srcTID)
												: string(pathUserAddCont + "/" + srcTID);


				srcPkg = srcAddCont + "/ac.pkg";
				dstPkg = dstAddCont + "/ac.pkg";

				if (existsFile(srcPkg))
				{
					if (!existsDir(dstAddCont))
						mkpath(dstAddCont, 0700);

					if (!moveFile(srcPkg, dstPkg, 0770)) {
						opType=FatalError;
						errorStr="Failed to move ac pkg!";
						klog("Error, move ac pkg: \"%s\" \n", srcPkg.c_str());
						return false;
					}

					if (toUSB && !linkFile(srcPkg, dstPkg))	{
						opType=FatalError;
						errorStr="Failed to link ac pkg!";
						klog("Error, link ac pkg: \"%s\" \n", srcPkg.c_str());
						return false;
					}

				}
			}
#endif

#if 1 // _WANT_META_MOVED
	// TODO check if any of this is even needed , almost sure the renamed xml isn't since i didn't use CID and it works fine...
			if (toUSB)
			{			
				const string  srcAppMeta(pathUserAppMeta + "/" + srcTID);

				StringList metaList;
				if(getEntries(srcAppMeta, metaList)) {
	
					for (auto& me : metaList) {
						string srcPath = srcAppMeta + "/" + me;
						string dstPath = dstApp + "/" + me;
	
						if (me.npos!=me.rfind(".png") ||
							me.npos!=me.rfind(".dds") ||
							me.npos!=me.rfind(".at9"))
						{
							if (!copyFile(srcPath, dstPath, 0777)) {
								opType=FatalError;
								errorStr="Failed to copy meta file!";
								klog("Error, copy meta file: \"%s\" \n", srcPath.c_str());
								return false;
							}
						}
					}
				}
				string cid=srcTID;	//getContentID(dstAppPkg);

				if (!existsFile	(srcAppMeta + "/pronunciation.xml") ||
					!copyFile	(srcAppMeta + "/pronunciation.xml", dstApp + "/" + cid + ".xml"))  {
					opType=FatalError;
					errorStr="Failed to copy meta pronunciation.xml!";
					klog("Error, copy meta pronunciation.xml!");
					return false;
				}
			}
#endif
		}	// for:
		return true;
	}



bool App2USB::getEntries(const string& dir, StringList& entries, bool wantDots)
{
	static vector<char> dentBuff(0x10000,0);
	entries.clear();

	if (dir.empty())
		return false;

#if _DEBUG
	klog("%s(\"%s\") \n", __func__, dir.c_str());
#endif
	int dfd = open(dir.c_str(), O_RDONLY | O_DIRECTORY);
	if (dfd > 0) {

		dentBuff.resize(0x10000);	// 64k ought to be enough, really... 

		int dsize = 0;
		while (0 < (dsize = sceKernelGetdents(dfd, &dentBuff[0], dentBuff.size())))
		{
			int offs = 0;
			dirent *de = (dirent *)&dentBuff[offs];

			while (offs < dsize && de) {
				de = (dirent *)&dentBuff[offs];
				string dName(de->d_name);
				if(string(".")==dName || string("..")==dName) {
					if(wantDots)
						entries.push_back(de->d_name);
				}
				else {
					entries.push_back(de->d_name);
#if _DEBUG
					u32 mode = _mode(string(dir + "/" + de->d_name).c_str());
					klog("entry fileNo: 0x%08X , mode: %jo , name: \"%s\" \n", de->d_fileno, mode, de->d_name);
#endif
				}
				offs += de->d_reclen;
			}
		}

		close(dfd);
		return true;
	}

	return false;
}




bool App2USB::copyDir(const string& src, const string& dst, u32 perms)
{
	klog("copyDir(src: \"%s\", dst \"%s\") \n", src.c_str(), dst.c_str());
	if(!existsDir(src))
		return false;

#if 0	// ofc libc is broken,  it was built with broken structs, thX OO
	int res=0;
	if (0 != (res=nftw(src.c_str(), dirEntFTW, 20, 0))) { // FTW_CHDIR|FTW_MOUNT
		klog("nftw() res: 0x%08X\n",res);
		return false;
	}
#else	// we shall get next level stupid with it instead

	StringList entList;
	if(!getEntries(src, entList))
		return false;

	if(!!mkpath(dst,perms)) {
		klog("copyDir(dst: \"%s\") mkdir() failed with 0x%08X\n", dst.c_str(), errno);
		return false;
	}
	for (const auto& e : entList)
	{
		if (string(".")==e || string("..")==e)
			continue;

		string fullSrc = src + string("/") + e;
		string fullDst = dst + string("/") + e;
		if (existsFile(fullSrc)) {
			if(!copyFile(fullSrc, fullDst, perms))
				return false;
		}
		else if (existsDir(fullSrc)) {
			if(!copyDir(fullSrc, fullDst, perms))
				return false;
		}
		else klog("copyDir(src: \"%s\") entry \"%s\" is something other than file/dir!\n",src.c_str(), e.c_str());
	}

#endif

	return true;
}



bool App2USB::copyFile(const string& src, const string& dst, u32 perms)
{
	klog("copyFile(src: \"%s\", dst \"%s\") \n", src.c_str(), dst.c_str());
	if(!existsFile(src))
		return false;

	off_t size=0;
	vector<u8> fileBuff;

	int fdr = 0;
	if ((fdr=open(src.c_str(), O_RDONLY)) <= 0) {
		klog("open(src): got fdr %d , errno 0x%08X\n", fdr, errno);
		if (EACCES==errno)
			errorStr=string("Access Denied !");

		return false;
	}
	size = lseek(fdr, 0, SEEK_END);
	klog("-src size: %ld bytes!\n", size);
	lseek(fdr, 0, SEEK_SET);
	if (size < 0)
		return false;


	int fdw=0;
	if ((fdw=open(dst.c_str(), O_WRONLY|O_CREAT, perms)) <= 0) {
		klog("open(dst): got fdw %d , errno 0x%08X\n", fdw, errno);
		if (EACCES==errno)
			errorStr=string("Access Denied !");
		close(fdr);
		return false;
	}


	progCurr = 0;
	progSize = size;

	const u64 fileCopyMax=MB(128), fileChunkSize=MB(32);

	if (size<fileCopyMax) {
		u64 actual=0;
		fileBuff.resize(size);
		if (size>0 && size!=(actual=read(fdr, &fileBuff[0], size))) {
			klog("@@ bad read , read %ld / %ld bytes : errno: 0x%08X\n", actual, size, errno);
			return false;
		}
		progCurr = u64(progSize/2);

		if (size>0 && size!=(actual=write(fdw, &fileBuff[0], size))) {
			klog("@@ bad write , wrote %ld / %ld bytes : errno: 0x%08X\n", actual, size, errno);
			return false;
		}
		progCurr = u64(progSize);
	} else {
		u64 actr=0,actw=0;
		fileBuff.resize(fileChunkSize);
		do { 
			actr=read (fdr, &fileBuff[0], fileChunkSize);
			actw=write(fdw, &fileBuff[0], actr);
			if (actr!=actw || (actw<fileChunkSize && ((progCurr+actw)!=progSize))) {
				klog("@@ bad file IO at %ld / %ld bytes (r: %ld , w: %ld) : errno: 0x%08X\n", u64(progCurr), size, actr,actw, errno);
				return false;
			}
			progCurr+=actw;
		}
		while(progCurr<progSize);
	}

	close(fdr);
	close(fdw);


	return true;
}

bool App2USB::delFile(const string& src)
{
	if(!existsFile(src))
		return false;

	return 0==remove(src.c_str());
}


bool App2USB::moveFile(const string& src, const string& dst, u32 perms)
{
	return (existsFile(src) && copyFile(src,dst,perms) && delFile(src));
}


bool App2USB::unlinkFile(const string& src)
{
	if (!existsLink(src))
		return false;

	return 0==unlink(src.c_str());
}

// these are swapped somewhat so they match the rest of the functions
// create symlink@src == file@dst
bool App2USB::linkFile(const string& src, const string& dst)
{
	string sLinkTgt = dst;
	string sLinkPath = src;

	if (!existsFile(dst) || src.empty()) {
		klog("linkFile(dst: \"%s\") doesn't exist or not target!\n", dst.c_str());
		return false;
	}

	if (existsLink(sLinkPath))
		unlinkFile(sLinkPath);

	else if (existsFile(sLinkPath)) {	// links count as file ig
		klog("linkFile(src: \"%s\") exists as a file (mode: 0x%08X)!\n", sLinkPath.c_str(), _mode(sLinkPath.c_str()));
		return false;
	}

	if (0!=symlink(sLinkTgt.c_str(), sLinkPath.c_str())) {
		klog("linkFile() failed with 0x%08X\n", errno);
		return false;
	}

	return existsLink(sLinkPath);
}





