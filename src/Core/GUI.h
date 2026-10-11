#pragma once
#include "..\Core\Config.h"
#include "..\Core\Render.h"
#include "..\Features\Aimbot.h"
#include "..\Features\Radar.h"
#include "..\Features\Misc.h"
#include "..\Features\GrenadeHelper.h"
#include "..\Features\TriggerBot.h"
#include "..\Config\ConfigMenu.h"
#include "..\Config\ConfigSaver.h"

#include "..\Resources\Language.h"
#include "..\Resources\Images.h"
#include "../Helpers/KeyManager.h"

#include "../Features/ESP.h"
#include <filesystem>
#include <string>
#include <vector>
#include <thread>
#include <fstream>

inline ID3D11ShaderResourceView* Logo = NULL;
inline int LogoW = 0, LogoH = 0;

inline ID3D11ShaderResourceView* HeaderBg = NULL;
inline int HeaderBgW = 0, HeaderBgH = 0;

inline bool checkbox1 = true;
inline bool checkbox2 = false;
inline bool checkbox3 = false;
inline bool checkbox4 = false;
inline bool checkbox5 = false;

namespace GUI
{
	void LoadDefaultConfig()
	{
		if (!MenuConfig::defaultConfig)
			return;

		MyConfigSaver::LoadConfig("default.cfg");
		MenuConfig::defaultConfig = false;
	}

	inline void InitHitboxList()
	{
		if (LegitBotConfig::HitboxUpdated)
			return;
		auto HitboxList = AimControl::HitboxList;

		checkbox1 = (std::find(HitboxList.begin(), HitboxList.end(), BONEINDEX::head) != HitboxList.end());
		checkbox2 = (std::find(HitboxList.begin(), HitboxList.end(), BONEINDEX::neck_0) != HitboxList.end());
		checkbox3 = (std::find(HitboxList.begin(), HitboxList.end(), BONEINDEX::spine_1) != HitboxList.end());
		checkbox4 = (std::find(HitboxList.begin(), HitboxList.end(), BONEINDEX::spine_2) != HitboxList.end());
		checkbox5 = (std::find(HitboxList.begin(), HitboxList.end(), BONEINDEX::pelvis) != HitboxList.end());

		LegitBotConfig::HitboxUpdated = true;
	}
	void addHitbox(int BoneIndex)
	{
		AimControl::HitboxList.push_back(BoneIndex);
	}
	void removeHitbox(int BoneIndex)
	{
		for (auto it = AimControl::HitboxList.begin(); it != AimControl::HitboxList.end(); ++it) {
			if (*it == BoneIndex) {
				AimControl::HitboxList.erase(it);
				break;
			}
		}
	}

	void LoadImages()
	{
		if (Logo == NULL)
		{
			Gui.LoadTextureFromMemory(Images::Logo, sizeof Images::Logo, &Logo, &LogoW, &LogoH);
			MenuConfig::MarkWinPos = ImVec2(ImGui::GetIO().DisplaySize.x - 300.0f, 100.f);
			MenuConfig::RadarWinPos = ImVec2(25.f, 25.f);
			MenuConfig::SpecWinPos = ImVec2(10.0f, ImGui::GetIO().DisplaySize.y / 2 - 200);
			MenuConfig::BombWinPos = ImVec2((ImGui::GetIO().DisplaySize.x - 200.0f) / 2.0f, 80.0f);
		}
		if (HeaderBg == NULL)
		{
			std::ifstream file("ghaxmodmenu.png", std::ios::binary | std::ios::ate);
			if (!file.is_open())
				file.open("..\\ghaxmodmenu.png", std::ios::binary | std::ios::ate);
			
			if (file.is_open())
			{
				std::streamsize size = file.tellg();
				file.seekg(0, std::ios::beg);
				std::vector<unsigned char> buffer(size);
				if (file.read((char*)buffer.data(), size))
				{
					Gui.LoadTextureFromMemory(buffer.data(), size, &HeaderBg, &HeaderBgW, &HeaderBgH);
				}
			}
		}
	}

	// =========================================================================
	//  Modern overhaul (v14): sidebar + content panel, mouse-driven, real
	//  interactive sliders. Original console-style arrow-key mod menu retired
	//  along with its pseudo-sliders (text-only "value" with ± nudge via the
	//  arrow keys — no fill bar, no drag).
	// =========================================================================
	void DrawGui()
	{
		LoadImages();
		InitHitboxList();

		// Menu toggle hotkey. Default INSERT; users with custom MenuConfig::HotKey
		// can rebind. Kept identical to the legacy behaviour so existing players
		// don't have to relearn.
		static bool insert_down = false;
		if (GetAsyncKeyState(VK_INSERT) & 0x8000) {
			if (!insert_down) { MenuConfig::ShowMenu = !MenuConfig::ShowMenu; insert_down = true; }
		} else { insert_down = false; }

		if (!MenuConfig::ShowMenu) return;

		// Push a clean style stack so other windows aren't affected by our
		// frame metrics. The old menu mutated globals (WindowPadding,
		// ItemSpacing, WindowBorderSize) which leaked into ESP/Radar windows
		// drawn later in the same frame.
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,   ImVec2(8, 6));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding,  3.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);

		// --- Theme colours (red/dark, matches the rest of the GHax suite) ---
		const ImU32 col_bg          = IM_COL32(18, 18, 22, 245);
		const ImU32 col_sidebar     = IM_COL32(14, 14, 18, 255);
		const ImU32 col_header      = IM_COL32(24, 24, 30, 255);
		const ImU32 col_accent      = IM_COL32(235, 60, 60, 255);
		const ImU32 col_divider     = IM_COL32(40, 40, 48, 255);
		const ImU32 col_text        = IM_COL32(212, 212, 220, 255);
		const ImU32 col_text_dim    = IM_COL32(140, 140, 150, 255);
		const ImU32 col_text_bright = IM_COL32(248, 248, 252, 255);
		const ImU32 col_tab_hover   = IM_COL32(28, 28, 36, 255);

		const float win_w     = 560.0f;
		const float win_h     = 420.0f;
		const float header_h  = 56.0f;
		const float footer_h  = 26.0f;
		const float sidebar_w = 150.0f;

		ImGui::SetNextWindowPos(ImVec2(80.0f, 80.0f), ImGuiCond_Once);
		ImGui::SetNextWindowSize(ImVec2(win_w, win_h));

		ImGui::Begin("GHaxFree", &MenuConfig::ShowMenu,
			ImGuiWindowFlags_NoDecoration |
			ImGuiWindowFlags_NoScrollbar  |
			ImGuiWindowFlags_NoBackground);

		auto dl   = ImGui::GetWindowDrawList();
		ImVec2 wp = ImGui::GetWindowPos();

		// ── Window backdrop ────────────────────────────────────────────────
		dl->AddRectFilled(wp, ImVec2(wp.x + win_w, wp.y + win_h), col_bg, 6.0f);
		dl->AddRect      (wp, ImVec2(wp.x + win_w, wp.y + win_h), col_divider, 6.0f, 0, 1.0f);

		// ── Header ─────────────────────────────────────────────────────────
		dl->AddRectFilled(wp,
			ImVec2(wp.x + win_w, wp.y + header_h),
			col_header, 6.0f, ImDrawFlags_RoundCornersTop);
		dl->AddLine(
			ImVec2(wp.x, wp.y + header_h),
			ImVec2(wp.x + win_w, wp.y + header_h),
			col_accent, 1.5f);

		if (Logo) {
			float lsz = header_h - 16.0f;
			dl->AddImage((ImTextureID)Logo,
				ImVec2(wp.x + 14.0f, wp.y + 8.0f),
				ImVec2(wp.x + 14.0f + lsz, wp.y + 8.0f + lsz));
		}
		dl->AddText(ImVec2(wp.x + 14.0f + (Logo ? header_h - 16.0f : 0.0f) + 12.0f, wp.y + 12.0f),
			col_text_bright, "GHax Free");
		dl->AddText(ImVec2(wp.x + 14.0f + (Logo ? header_h - 16.0f : 0.0f) + 12.0f, wp.y + 30.0f),
			col_text_dim, "Counter-Strike 2");

		// Driver Status indicator (header right side). The label intentionally
		// omits the driver's name — only the connection state is shown so a
		// screenshot doesn't leak product internals.
		{
			const bool ok = memoryManager.IsDriverReady();
			const char* statusTxt = ok ? "Connected" : "Offline";
			const ImU32 statusCol = ok ? IM_COL32(60, 220, 100, 255)
			                           : IM_COL32(235, 70, 70, 255);

			const char* lbl = "Driver Status:";
			ImVec2 lblSz = ImGui::CalcTextSize(lbl);
			ImVec2 valSz = ImGui::CalcTextSize(statusTxt);
			float padR = 16.0f;
			float dotR = 5.0f;
			float totalW = lblSz.x + 6.0f + dotR * 2.0f + 6.0f + valSz.x;
			float xRight = wp.x + win_w - padR;
			float xLbl   = xRight - totalW;
			float yMid   = wp.y + header_h * 0.5f;

			dl->AddText(ImVec2(xLbl, yMid - lblSz.y * 0.5f), col_text_dim, lbl);
			dl->AddCircleFilled(
				ImVec2(xLbl + lblSz.x + 6.0f + dotR, yMid),
				dotR, statusCol);
			dl->AddText(
				ImVec2(xLbl + lblSz.x + 6.0f + dotR * 2.0f + 6.0f, yMid - valSz.y * 0.5f),
				statusCol, statusTxt);
		}

		// ── Sidebar ────────────────────────────────────────────────────────
		dl->AddRectFilled(
			ImVec2(wp.x, wp.y + header_h),
			ImVec2(wp.x + sidebar_w, wp.y + win_h - footer_h),
			col_sidebar);
		dl->AddLine(
			ImVec2(wp.x + sidebar_w, wp.y + header_h),
			ImVec2(wp.x + sidebar_w, wp.y + win_h - footer_h),
			col_divider, 1.0f);

		struct Tab { const char* label; };
		static const Tab kTabs[] = {
			{ "Aimbot"    },
			{ "Trigger"   },
			{ "ESP"       },
			{ "Misc"      },
			{ "Config"    },
		};
		const int kTabCount = (int)(sizeof(kTabs) / sizeof(kTabs[0]));
		static int s_Tab = 0;

		const float tab_h = 34.0f;
		const float tab_y0 = header_h + 10.0f;
		for (int i = 0; i < kTabCount; ++i) {
			float ty = tab_y0 + i * (tab_h + 2.0f);
			ImVec2 tabMin(wp.x + 6.0f, wp.y + ty);
			ImVec2 tabMax(wp.x + sidebar_w - 6.0f, wp.y + ty + tab_h);

			ImGui::SetCursorScreenPos(tabMin);
			ImGui::PushID(i);
			bool clicked = ImGui::InvisibleButton("##tab",
				ImVec2(tabMax.x - tabMin.x, tabMax.y - tabMin.y));
			bool hovered = ImGui::IsItemHovered();
			ImGui::PopID();
			if (clicked) s_Tab = i;

			bool selected = (s_Tab == i);
			ImU32 fill = selected ? IM_COL32(34, 34, 42, 255)
			                      : (hovered ? col_tab_hover : 0);
			if (fill) dl->AddRectFilled(tabMin, tabMax, fill, 3.0f);
			if (selected)
				dl->AddRectFilled(tabMin,
					ImVec2(tabMin.x + 3.0f, tabMax.y),
					col_accent, 3.0f, ImDrawFlags_RoundCornersLeft);

			ImU32 tcol = selected ? col_text_bright : col_text;
			dl->AddText(ImVec2(tabMin.x + 14.0f,
				tabMin.y + (tab_h - ImGui::GetFontSize()) * 0.5f),
				tcol, kTabs[i].label);
		}

		// Sidebar bottom: Unload button
		{
			float by = win_h - footer_h - 40.0f;
			ImVec2 bMin(wp.x + 8.0f, wp.y + by);
			ImVec2 bMax(wp.x + sidebar_w - 8.0f, wp.y + by + 28.0f);
			ImGui::SetCursorScreenPos(bMin);
			bool clicked = ImGui::InvisibleButton("##unload",
				ImVec2(bMax.x - bMin.x, bMax.y - bMin.y));
			bool hovered = ImGui::IsItemHovered();
			ImU32 fill = hovered ? IM_COL32(180, 40, 40, 255)
			                     : IM_COL32(140, 32, 32, 255);
			dl->AddRectFilled(bMin, bMax, fill, 3.0f);
			ImVec2 sz = ImGui::CalcTextSize("Unload");
			dl->AddText(ImVec2(bMin.x + (bMax.x - bMin.x - sz.x) * 0.5f,
				bMin.y + (bMax.y - bMin.y - sz.y) * 0.5f),
				col_text_bright, "Unload");
			if (clicked) Init::Client::Exit();
		}

		// ── Content panel ─────────────────────────────────────────────────
		const float content_x  = sidebar_w + 14.0f;
		const float content_y  = header_h + 12.0f;
		const float content_w  = win_w - sidebar_w - 28.0f;
		const float content_h  = win_h - header_h - footer_h - 24.0f;

		ImGui::SetCursorPos(ImVec2(content_x, content_y));
		ImGui::PushStyleColor(ImGuiCol_FrameBg,        IM_COL32(28, 28, 36, 255));
		ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(36, 36, 46, 255));
		ImGui::PushStyleColor(ImGuiCol_FrameBgActive,  IM_COL32(40, 40, 52, 255));
		ImGui::PushStyleColor(ImGuiCol_SliderGrab,        IM_COL32(235, 60, 60, 255));
		ImGui::PushStyleColor(ImGuiCol_SliderGrabActive,  IM_COL32(255, 80, 80, 255));
		ImGui::PushStyleColor(ImGuiCol_CheckMark,         IM_COL32(235, 60, 60, 255));
		ImGui::PushStyleColor(ImGuiCol_Text,              col_text);
		ImGui::PushStyleColor(ImGuiCol_TextDisabled,      col_text_dim);
		ImGui::PushStyleColor(ImGuiCol_Button,        IM_COL32(40, 40, 50, 255));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(54, 54, 66, 255));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive,  IM_COL32(60, 60, 74, 255));
		ImGui::PushStyleColor(ImGuiCol_ChildBg,       IM_COL32(0, 0, 0, 0));

		ImGui::BeginChild("##content", ImVec2(content_w, content_h), false,
			ImGuiWindowFlags_NoScrollbar);

		// Section header (small accent bar + label)
		auto Section = [&](const char* title) {
			ImVec2 p = ImGui::GetCursorScreenPos();
			float w = ImGui::GetContentRegionAvail().x;
			float h = ImGui::GetFontSize() + 8.0f;
			dl->AddRectFilled(p,
				ImVec2(p.x + 3.0f, p.y + h),
				col_accent);
			dl->AddText(ImVec2(p.x + 10.0f, p.y + (h - ImGui::GetFontSize()) * 0.5f),
				col_text_bright, title);
			ImGui::Dummy(ImVec2(w, h + 2.0f));
		};

		auto HotkeyRow = [&](const char* label, int& vk, std::string& displayName) {
			char btnTxt[80];
			snprintf(btnTxt, sizeof(btnTxt), "%s", displayName.empty() ? "[None]" : displayName.c_str());
			ImGui::TextUnformatted(label);
			ImGui::SameLine(content_w * 0.55f);
			ImGui::PushID(label);
			ImGui::PushItemWidth(content_w * 0.40f);
			if (ImGui::Button(btnTxt, ImVec2(content_w * 0.40f, 0))) {
				std::thread([&vk, &displayName]() {
					KeyMgr::GetPressedKey(vk, &displayName);
				}).detach();
			}
			ImGui::PopItemWidth();
			ImGui::PopID();
		};

		const float sliderW = content_w - 12.0f;
		(void)sliderW;

		// ── AIMBOT ────────────────────────────────────────────────────────
		if (s_Tab == 0) {
			Section("AIMBOT");
			ImGui::Checkbox(Text::Aimbot::Enable.c_str(), &LegitBotConfig::AimBot);
			if (LegitBotConfig::AimBot) {
				HotkeyRow("Aimbot Key", AimControl::HotKey, Text::Aimbot::HotKey);
				ImGui::SliderFloat(Text::Aimbot::FovSlider.c_str(),    &AimControl::AimFov, 0.f, 30.f, "%.1f");
				ImGui::SliderFloat(Text::Aimbot::SmoothSlider.c_str(), &AimControl::Smooth, 0.f, 10.f, "%.1f");
				ImGui::Checkbox(Text::Aimbot::DrawFov.c_str(),  &ESPConfig::DrawFov);
				ImGui::Checkbox("Enemies Only##aim",            &LegitBotConfig::AimEnemiesOnly);
				ImGui::Checkbox(Text::Aimbot::VisCheck.c_str(), &LegitBotConfig::VisibleCheck);

				ImGui::Dummy(ImVec2(0, 4.0f));
				Section("RECOIL");
				ImGui::Checkbox(Text::RCS::Toggle.c_str(), &LegitBotConfig::RCS);
				if (LegitBotConfig::RCS) {
					ImGui::SliderFloat(Text::RCS::Yaw.c_str(),   &RCS::RCSScale.x, 0.f, 2.f, "%.2f");
					ImGui::SliderFloat(Text::RCS::Pitch.c_str(), &RCS::RCSScale.y, 0.f, 2.f, "%.2f");
				}

				ImGui::Dummy(ImVec2(0, 4.0f));
				Section("HITBOXES");
				auto HB = [&](const char* lbl, bool& flag, int bone) {
					bool prev = flag;
					ImGui::Checkbox(lbl, &flag);
					if (flag != prev) { if (flag) addHitbox(bone); else removeHitbox(bone); }
				};
				HB("Head",    checkbox1, BONEINDEX::head);
				HB("Neck",    checkbox2, BONEINDEX::neck_0);
				HB("Chest",   checkbox3, BONEINDEX::spine_1);
				HB("Stomach", checkbox4, BONEINDEX::spine_2);
				HB("Pelvis",  checkbox5, BONEINDEX::pelvis);
			}
		}
		// ── TRIGGERBOT ────────────────────────────────────────────────────
		else if (s_Tab == 1) {
			Section("TRIGGERBOT");
			ImGui::Checkbox(Text::Trigger::Enable.c_str(), &LegitBotConfig::TriggerBot);
			if (LegitBotConfig::TriggerBot) {
				HotkeyRow("Trigger Key", TriggerBot::HotKey, Text::Trigger::HotKey);
				ImGui::Checkbox(Text::Trigger::Toggle.c_str(), &LegitBotConfig::TriggerAlways);
				ImGui::SliderInt(Text::Trigger::DelaySlider.c_str(),    &TriggerBot::TriggerDelay, 0, 300, "%d ms");
				ImGui::SliderInt(Text::Trigger::FakeShotSlider.c_str(), &TriggerBot::ShotDuration, 0, 1000, "%d ms");
				ImGui::Checkbox("Enemies Only##tb",            &LegitBotConfig::TriggerEnemiesOnly);
				ImGui::Checkbox(Text::Trigger::ScopeOnly.c_str(), &TriggerBot::ScopeOnly);
			}
		}
		// ── ESP ───────────────────────────────────────────────────────────
		else if (s_Tab == 2) {
			Section("OVERLAY");
			ImGui::Checkbox(Text::ESP::Enable.c_str(), &ESPConfig::ESPenabled);
			if (ESPConfig::ESPenabled) {
				ImGui::Columns(2, "##espcols", false);
				ImGui::SetColumnWidth(0, content_w * 0.55f);
				ImGui::Checkbox(Text::ESP::Box.c_str(),        &ESPConfig::ShowBoxESP);
				ImGui::Checkbox(Text::ESP::Skeleton.c_str(),   &ESPConfig::ShowBoneESP);
				ImGui::Checkbox(Text::ESP::HealthBar.c_str(),  &ESPConfig::ShowHealthBar);
				ImGui::Checkbox(Text::ESP::HealthNum.c_str(),  &ESPConfig::ShowHealthNum);
				ImGui::NextColumn();
				ImGui::Checkbox(Text::ESP::Weapon.c_str(),     &ESPConfig::ShowWeaponESP);
				ImGui::Checkbox(Text::ESP::ScopedESP.c_str(),  &ESPConfig::ShowIsScoped);
				ImGui::Checkbox(Text::ESP::FlashedESP.c_str(), &ESPConfig::ShowIsBlind);
				ImGui::Columns(1);
				ImGui::Dummy(ImVec2(0, 2.0f));
				ImGui::Checkbox("Enemies Only##esp",           &ESPConfig::EnemiesOnly);
				ImGui::Checkbox(Text::ESP::VisCheck.c_str(),   &ESPConfig::VisibleCheck);
			}
		}
		// ── MISC ──────────────────────────────────────────────────────────
		else if (s_Tab == 3) {
			Section("GENERAL");
			HotkeyRow("Menu Key", MenuConfig::HotKey, Text::Misc::HotKey);
			ImGui::Checkbox(Text::Misc::Watermark.c_str(), &MiscCFG::WaterMark);
			ImGui::Checkbox("Bunny Hop",      &MiscCFG::BunnyHop);
			ImGui::Checkbox("Spectator List", &MiscCFG::SpecList);
			ImGui::Checkbox("OBS Bypass",     &MenuConfig::BypassOBS);
		}
		// ── CONFIG ────────────────────────────────────────────────────────
		else if (s_Tab == 4) {
			Section("PROFILE");
			float bw = content_w - 12.0f;
			if (ImGui::Button("Save Default Config", ImVec2(bw, 28.0f)))
				MyConfigSaver::SaveConfig("default.cfg", "GHax");
			if (ImGui::Button("Load Default Config", ImVec2(bw, 28.0f)))
				MyConfigSaver::LoadConfig("default.cfg");
			if (ImGui::Button("Reset to Default",    ImVec2(bw, 28.0f)))
				ConfigMenu::ResetToDefault();
		}

		ImGui::EndChild();
		ImGui::PopStyleColor(12);

		// ── Footer ────────────────────────────────────────────────────────
		ImVec2 fp(wp.x, wp.y + win_h - footer_h);
		dl->AddRectFilled(fp, ImVec2(fp.x + win_w, fp.y + footer_h),
			IM_COL32(14, 14, 18, 255), 6.0f, ImDrawFlags_RoundCornersBottom);
		dl->AddLine(fp, ImVec2(fp.x + win_w, fp.y), col_divider, 1.0f);
		dl->AddText(ImVec2(fp.x + 12.0f,
			fp.y + (footer_h - ImGui::GetFontSize()) * 0.5f),
			col_text_dim, "INSERT to toggle menu");

		char buildStamp[32];
		snprintf(buildStamp, sizeof(buildStamp), "v14");
		ImVec2 vsz = ImGui::CalcTextSize(buildStamp);
		dl->AddText(ImVec2(fp.x + win_w - vsz.x - 12.0f,
			fp.y + (footer_h - ImGui::GetFontSize()) * 0.5f),
			col_text_dim, buildStamp);

		ImGui::End();
		ImGui::PopStyleVar(6);

		LoadDefaultConfig();
	}
}