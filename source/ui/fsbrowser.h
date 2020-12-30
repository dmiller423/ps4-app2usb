/*
*	Filesystem Browser Scene
*/
#pragma once



class FSBrowser
	//	: public Scene
{

	string currPath;
	vector<char> dentBuff;
	vector<string> entries;

	bool m_inUI = true;
	ImGuiTextFilter filter;


	string homePath() {
		return string("/data");
	}

	string defPath() {
		return homePath();
	}

public:

	FSBrowser(const string& homePath = string())
		: currPath(homePath)
	{
		if (currPath.empty())
			currPath = defPath();

		filter.Clear();
		refresh();
	}

	bool inUI() {
		return m_inUI;
	}


	void enter() {
		refresh();
		m_inUI = true;
	}

	void leave() {
		m_inUI = false;
	}

#define _FULLSCREEN 0
	void render()
	{
		auto scaling = 1.0f;	// only used for spacing
		u32 resW=VideoOut::Get().Width(), resH=VideoOut::Get().Height();

#if _FULLSCREEN
		ImGui::SetNextWindowPos(ImVec2(0, 0));
		ImGui::SetNextWindowSize(ImVec2(resW, resH));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);

		ImGui::Begin("##main", NULL, ImGuiWindowFlags_NoDecoration);
#else

		ImGui::SetNextWindowSize(ImVec2(800,600)); //resW-400,resH-400));
		ImGui::Begin("Select Directory for Backup or Restore base."); 

#endif

		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(20 * scaling, 8 * scaling));		// from 8, 4
		ImGui::AlignTextToFramePadding();
		ImGui::Text("Current Directory: \"%s\" ", currPath.c_str());

#if _FULLSCREEN && _SETTINGS
		ImGui::SameLine(ImGui::GetContentRegionAvailWidth() - ImGui::CalcTextSize("Settings").x - ImGui::GetStyle().FramePadding.x * 2.0f /*+ ImGui::GetStyle().ItemSpacing.x*/);
		if (ImGui::Button("Settings"))//, ImVec2(0, 30 * scaling)))
			; // gui_state = Settings;
#endif
		ImGui::PopStyleVar();

		// ImGui::GetID("library")
		ImGui::BeginChild("##dirsel", ImVec2(0, ImGui::GetContentRegionAvail().y-120), true);
		{
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8 * scaling, 20 * scaling));		// from 8, 4


			for (auto ent : entries) {
				//	if (filter.IsActive() &&
				//		filter.PassFilter(game.c_str()))
				{
					//	ImGui::PushID(game.c_str());
					if (ImGui::Selectable(ent.c_str()))
					{
						klog(" SELECTED \"%s\" \n", ent.c_str());

						if (string(".") == ent) {	// how does break cause a crash here ??
							refresh();
						}
						else if (string("..") == ent) {
							cdUp();
						}

						// *FIXME* have to add a button choose dir ig, this hack is for a file
						else if (!cdTo(ent)) {


							klog("@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@ BEEEEEEEEp %s \n", ent.c_str());



						}
						klog("</SELECTED>\n");
					}
					//	ImGui::PopID();
				}
			}

			ImGui::PopStyleVar();
		}
		ImGui::EndChild();

		// drop it to the ....
		if (ImGui::Button("[OK] Select Directory"))//, ImVec2(0, 30 * scaling)))
			; // gui_state = Settings;
		// ! for both _FULLSCREEN and not
		ImGui::End();

#if _FULLSCREEN
		ImGui::PopStyleVar();
#endif
	}

	void cdUp()
	{
		if (currPath.empty()) {
			currPath = string("/");
			return;
		}

		size_t csz = currPath.size();
		size_t lsi = currPath.rfind('/');

		if ((csz - 1) == lsi)		// ends w. sep, b00000
			lsi = currPath.rfind('/', csz - 2);

		currPath.resize(lsi + ((0 == lsi) ? 1 : 0));
		refresh();
	}


	bool cdTo(string subDir)
	{
		klog(">> cdTo(\"%s\") \n", subDir.c_str());

		if (currPath.empty()) {
			klog("Error, currPath is empty on cdTo(\"%s\") \n", subDir.c_str());
			return false;
		}
		bool endsOnSep = ('/' == subDir[subDir.size() - 1]);
		string tpath = currPath;
		if (!endsOnSep) tpath += "/";
		tpath += subDir;

		if (refresh(tpath)) {
			currPath = tpath;
			return true;
		}
		klog("refresh(\"%s\") failed!\n", tpath.c_str());
		refresh(currPath);	// Make sure we get current data back if we can't open tpath !
		return false;
	}


	bool refresh(string dir = string())
	{
		entries.clear();

		if (dir.empty())
			dir = currPath;

#ifdef _DEBUG
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
#ifdef _DEBUG
					klog("entry fileNo: %X , name: \"%s\" \n", de->d_fileno, de->d_name);
#endif
					entries.push_back(de->d_name);
					offs += de->d_reclen;
				}
			}

			close(dfd);
			return true;
		}

		return false;
	}


};













