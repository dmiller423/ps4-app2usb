/*
*	Backup Scene
*/
#pragma once

//#define XXH_VECTOR 0 // XXH_SCALAR
//#define XXH_INLINE_ALL 1
//#include "xxhash.h"

class App2USB
{
	using StringList = vector<string>;

	enum OPType {
		None=0,
		ToUSB,
		ToHDD,
		SelToUSB,
		SelToHDD,

		PlsWait,
		FatalError,

		OPType_Count
	};

	struct OPEntry {
		bool selected;
		const string tid;	// Overrides UI display string
		const string path;
		u32 flags;			// bool symlink;
	};

	enum EntFlags :u32
	{
		F_None =0,
		F_OnHDD=0,
		F_OnUSB=1,
		F_Symlink=8,

	};
	
	vector<OPEntry> opList ;

	OPType opType=None;
	OPEntry* opTarget=nullptr;

	size_t usbIdx  = 0;
	string usbPath = string();
	string errorStr= string("no error");
	StringList usbList;

	App2USB() {
		opList.reserve(1024);
		if (!refreshList()) {
			opType=FatalError;
		}
	}

public:

	static App2USB& Get()
	{
		static App2USB ui;
		return ui;
	}

	static void ResetOp() {
		Get().opType=None;
	}

	void render();



private:

	inline static u32 _m(const struct stat* s) { return ((u32*)s)[2]; }	// *HACK* OO mighty broken
	inline static u32 _mode(const char* sPath) { struct stat sb; if(0==lstat(sPath, &sb)) { return _m(&sb); } return 0; }

	inline static bool existsDir (const char* sPath)  { return !!S_ISDIR(_mode(sPath)); }
	inline static bool existsFile(const char* sPath)  { return !!S_ISREG(_mode(sPath)); }
	inline static bool existsLink(const char* sPath)  { return !!S_ISLNK(_mode(sPath)); }

	inline static bool existsDir (const string& path) { return existsDir (path.c_str()); }
	inline static bool existsFile(const string& path) { return existsFile(path.c_str()); }
	inline static bool existsLink(const string& path) { return existsLink(path.c_str()); }

	bool delFile (const string& src);
	bool linkFile(const string& src, const string& dst);
	bool moveFile(const string& src, const string& dst, u32 perms=0777);
	bool copyFile(const string& src, const string& dst, u32 perms=0777);
	bool copyDir (const string& src, const string& dst, u32 perms=0777);


	static bool unlinkFile(const string& src);


	static bool getEntries(const string& dir, StringList& entries, bool wantDots=false);


	inline static string sHx64(uint64_t v) {
		thread_local static char b[17];
		b[16]=0;
		sprintf(b,"%016lX",v);
		return string(b);	//ltoa(v,b,16));	// no ltoa ...
	}

	inline static bool entOnUSB(const OPEntry& e) { return bool(e.flags & F_OnUSB); }
	inline static bool entOnHDD(const OPEntry& e) { return !entOnUSB(e); }


	bool refreshList()
	{
		klog("%s() \n", __func__);

		bool gotUSB = getUSBs();

		StringList sl { string("/user") };
		sl.insert(sl.end(), usbList.begin(), usbList.end());


		opList.clear();
		for (const auto& se : sl)
		{
			string s(se);

#if defined(_USE_LAME_PATHS_FOR_EXISTING_USBS)	// Don't bother, they used original package names from the json and that is 100% pointless: someone elses problem
			string	usbRoot("PS4");
#else
			string	usbRoot("app2usb");
#endif
			bool isOnUSB = (se!=sl[0]);
			if (isOnUSB)			// if()
				s+=string("/")+usbRoot;

			s+=string("/app");

			StringList entList;
			if(!getEntries(s, entList)) {
				klog("refreshList(): failed to get dir entries! \"%s\"\n", se.c_str());
				errorStr="refreshList(): failed to get dir entries!";
				return false;
			}


			for (const auto& e : entList) {

				string tPath(s+"/"+e);

				if (!existsDir(tPath)) {
					klog("refreshList(): path is not dir: \"%s\"\n", tPath.c_str());
					continue;
				}

				bool pkgLink = existsLink(tPath+"/app.pkg");
				bool pkgFile = existsFile(tPath+"/app.pkg");

#if 1// _DEBUG
				if (pkgLink)
					klog("refreshList(): path IS PKG LINK / app.pkg: \"%s\"\n", tPath.c_str());

				if (!pkgLink && !pkgFile) {
					klog("refreshList(): path doesn't contain app.pkg: \"%s\"\n", tPath.c_str());
					continue;
				}
#endif
				if (pkgFile) {	// limit to actual container
					OPEntry ope {
						false, e, tPath, 
						(isOnUSB?F_OnUSB:F_OnHDD) | 
						(pkgLink?F_Symlink:F_None)
					};
					opList.push_back(ope);
				}
			}
		}

		if (!gotUSB) {
			opType=FatalError;
			errorStr="There are no USB drives attached, or aren't formatted properly.";
		}

		return true;
	}

	bool getUSBs()
	{
		usbList.clear();

		StringList mntList, tstList;
		if(getEntries("/mnt", mntList)) {
			u32 bxo=0;
			for (auto& me : mntList) {
				string tmpPath = string("/mnt/") + me;

				if (0==me.rfind("usb",0) && isdigit(me[3]) && existsDir(tmpPath))
				{
					tstList.clear();
					if (getEntries(tmpPath, tstList) && tstList.size() > 2)
					{
						usbList.push_back(tmpPath);
					}
				}
			}
		}
		if(!usbList.size())
			return false;
		
		if (usbPath.empty())
			usbPath=usbList[0];

		return true;
	}

	// could spawn thread and check mtx locked var or atomic
	bool bakeMeAcake()
	{
		vector<OPEntry*> selList;

		if ((None==opType || FatalError==opType || PlsWait==opType) ||
			((ToUSB==opType || ToHDD==opType) && !opTarget))
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

		u32 entN=0;
		for (const OPEntry* ent : selList) {

			if (!ent || ent->path.empty()) return false;

			klog("pathList[%d] path: \"%s\" \n", entN++, ent->path.c_str());

			// just remove toUSB/HOST and always switch?  people are stupid they'll prob get confused and wonder wtf is going on and keep swapping them until their brain falls out 
			const string& srcTID = ent->tid;
			const string& srcApp = ent->path;
	
			const string  dstApp = toUSB? string(pathUsbApp  + "/" + srcTID)
										: string(pathUserApp + "/" + srcTID);


			if (srcApp==dstApp || (toUSB && string::npos != srcApp.rfind("/mnt/usb"))) {
				klog("@@@@@@@@@@ *FIXME* trying to copy to same place! src: \"%s\" , dst: \"%s\" \n", srcApp.c_str(), dstApp.c_str());
				continue;
			}

#if 1	// remove this after testing *FIXME*
			StringList sl;
			getEntries(srcApp,sl);
#endif
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
#if 0
				string cid=srcTID;	//getContentID(dstAppPkg);

				if (!existsFile(srcAppMeta + "/pronunciation.xml") ||
					!copyFile(srcAppMeta + "/pronunciation.xml", dstApp + "/" + cid + ".xml"))  {
					opType=FatalError;
					errorStr="Failed to copy meta pronunciation.xml!";
					klog("Error, copy meta pronunciation.xml!");
					return false;
				}
#endif
			}

		}	// for:
		return true;
	}




#if 0	// someday
	static int dirEntFTW(const char *fpath, const struct stat *sb, int tflag, struct FTW *ftwbuf)
	{
		klog("%s(fpath: \"%s\", stat.mode: 0x%08X)\n",__func__,fpath,_m(sb));
		return 0;
	}
#endif





	static int mkpath(const std::string& s, u32 p=0774)
	{
		size_t pos=0;
		std::string dir, tmp=s;

		if(tmp[tmp.size()-1]!='/'){
			tmp+=string("/");	// force trailing / so we can handle everything in loop
		}

		while((pos=tmp.find_first_of('/',pos))!=std::string::npos){
			dir=tmp.substr(0,pos++);
			if(dir.size()==0) continue; // if leading / first time is 0 length
			if(-1==mkdir(dir.c_str(),mode_t(p)) && errno!=EEXIST){
				return errno;
			}
		}
		return 0;
	}


	static bool mkpath(const char *dir, u32 p=0774)
	{
		return mkpath(string(dir), p);
	}



};























