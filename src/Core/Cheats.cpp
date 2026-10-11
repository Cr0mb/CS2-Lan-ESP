// USB-Lan-ESP - Stripped down ESP + Aimbot + RCS only

#include <string>
#include <thread>
#include <future>
#include <iostream>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <fstream>
#include <filesystem>
#include <ctime>

#include "Cheats.h"
#include "VisCheckManager.h"
#include "Render.h"
#include "Config.h"
#include "Init.h"
#include "GlobalVars.h"
#include "LiveConfig.h"
#include "../Features/ESP.h"
#include "../Features/RCS.H"

int PreviousTotalHits = 0;

void Visual(const CEntity&);
void AIM(const CEntity&, std::vector<Vec3>);

// Crash logging function
void WriteCrashLog(const std::string& errorMessage, const std::string& functionName)
{
	try {
		// Get executable directory
		char exePath[MAX_PATH];
		GetModuleFileNameA(NULL, exePath, MAX_PATH);
		std::filesystem::path exeDir = std::filesystem::path(exePath).parent_path();
		
		// Create timestamp
		auto now = std::time(nullptr);
		auto tm = *std::localtime(&now);
		
		// Create crash log filename with timestamp
		char timestamp[100];
		std::strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", &tm);
		std::string logFileName = "crash_log_" + std::string(timestamp) + ".txt";
		std::filesystem::path logPath = exeDir / logFileName;
		
		// Write crash log
		std::ofstream logFile(logPath);
		if (logFile.is_open()) {
			logFile << "=== GHAXLABS CRASH LOG ===" << std::endl;
			logFile << "Timestamp: " << std::asctime(&tm);
			logFile << "Function: " << functionName << std::endl;
			logFile << "Error: " << errorMessage << std::endl;
			logFile << std::endl;
			
			// System info
			logFile << "=== SYSTEM INFO ===" << std::endl;
			DWORD64 localPawn = 0, localController = 0;
			if (memoryManager.ReadMemory(gGame.GetLocalPawnAddress(), localPawn)) {
				logFile << "Local pawn address: 0x" << std::hex << localPawn << std::dec << std::endl;
			}
			if (memoryManager.ReadMemory(gGame.GetLocalControllerAddress(), localController)) {
				logFile << "Local controller address: 0x" << std::hex << localController << std::dec << std::endl;
			}
			
			logFile << "Entity list address: 0x" << std::hex << gGame.GetEntityListEntry() << std::dec << std::endl;
			logFile << "Matrix address: 0x" << std::hex << gGame.GetMatrixAddress() << std::dec << std::endl;
			
			logFile << std::endl;
			logFile << "=== END CRASH LOG ===" << std::endl;
			logFile.close();
			
			printf("[CRASH] Log saved to: %s\n", logPath.string().c_str());
		}
	}
	catch (...) {
		printf("[CRASH] Failed to write crash log\n");
	}
}

void Cheats::Run()
{
	try {
		// Reload config if changed (every 2 seconds)
		LiveConfig::CheckReload();

		// Apply live config to legacy settings
		auto& cfg = LiveConfig::g_settings;
		ESPConfig::ESPenabled = cfg.esp_enabled;
		ESPConfig::ShowBoxESP = cfg.esp_box;
		ESPConfig::ShowHealthBar = cfg.esp_health;
		ESPConfig::ShowBoneESP = cfg.esp_bones;
		ESPConfig::ShowPlayerName = cfg.esp_name;
		ESPConfig::ShowDistance = cfg.esp_distance;
		ESPConfig::ShowHeadBox = cfg.esp_head;
		ESPConfig::VisibleOnly = cfg.esp_visible_only;
		ESPConfig::BoxRounding = 0.0f;  // No rounded corners

		LegitBotConfig::AimBot = cfg.aim_enabled;
		AimControl::AimFov = cfg.aim_fov;
		AimControl::Smooth = cfg.aim_smooth;
		AimControl::HotKey = VK_LBUTTON;  // Hardcode LMB for testing
		// Set aim bone via HitboxList
		if (!AimControl::HitboxList.empty())
			AimControl::HitboxList[0] = cfg.aim_bone;

		LegitBotConfig::RCS = cfg.rcs_enabled;

		// Skip if game window not active
		if (!Init::Client::isGameWindowActive()) {
			std::this_thread::sleep_for(std::chrono::milliseconds(20));
			return;
		}

	// Try to read view matrix (needed for ESP 3D to 2D projection)
	if (!memoryManager.ReadMemory(gGame.GetMatrixAddress(), gGame.View.Matrix, 64))
	{
		// Matrix read failed - suppressing warning output
	}

	// Update EntityList entry and get local player data
	gGame.UpdateEntityListEntry();

	DWORD64 LocalControllerAddress = 0;
	DWORD64 LocalPawnAddress = 0;
	if (!memoryManager.ReadMemory(gGame.GetLocalControllerAddress(), LocalControllerAddress))
	{
		return;
	}
	if (!memoryManager.ReadMemory(gGame.GetLocalPawnAddress(), LocalPawnAddress))
	{
		return;
	}

	if (LocalPawnAddress == 0 || LocalControllerAddress == 0) {
        g_globalVars->UpdateGlobalvars();
        cachedResults.clear();
        return;
    }

	// LocalEntity
	CEntity LocalEntity;
	int LocalPlayerControllerIndex = 0;
	LocalEntity.UpdateClientData();
	if (!LocalEntity.UpdateController(LocalControllerAddress)) {
		cachedResults.clear();
		return;
	}
	if (!LocalEntity.UpdatePawn(LocalPawnAddress) && !MenuConfig::WorkInSpec) {
		cachedResults.clear();
		return;
	}

	// Update m_currentTick
	bool success = memoryManager.ReadMemory<DWORD>(LocalEntity.Controller.Address + Offset.PlayerController.m_nTickBase, m_currentTick);
	if (!success) {
		m_currentTick = 0;
	}

	// aimbot data
	std::vector<Vec3> AimPosList;

	// process entities
	auto entityResults = ProcessEntities(LocalEntity, LocalPlayerControllerIndex);

	// render ESP, collect aimbot data
	HandleEnts(entityResults, LocalEntity, LocalPlayerControllerIndex, AimPosList);

	Visual(LocalEntity);

	// Watermark
	{
		ImDrawList* drawList = ImGui::GetBackgroundDrawList();
		ImVec2 screenSize = ImGui::GetIO().DisplaySize;
		const char* watermark = "Made by Cr0mb";
		ImVec2 textSize = ImGui::CalcTextSize(watermark);
		ImVec2 pos = ImVec2(screenSize.x - textSize.x - 10, 10);
		drawList->AddText(pos, IM_COL32(200, 200, 200, 180), watermark);
	}

	// RCS only
	if (LegitBotConfig::RCS && LocalEntity.Pawn.ShotsFired > RCS::RCSBullet)
		RCS::RecoilControl(LocalEntity);

	// Aimbot every new tick
	if (m_currentTick != m_previousTick)
	{
		AIM(LocalEntity, AimPosList);
		m_previousTick = m_currentTick;
	}
	
	}
	catch (const std::exception& e) {
		WriteCrashLog("Exception in Cheats::Run(): " + std::string(e.what()), "Cheats::Run");
		printf("[CRASH] Exception caught in Cheats::Run: %s\n", e.what());
	}
	catch (...) {
		WriteCrashLog("Unknown exception in Cheats::Run()", "Cheats::Run");
		printf("[CRASH] Unknown exception caught in Cheats::Run\n");
	}
}

// collect entity data
std::vector<std::pair<int, CEntity>> Cheats::CollectEntityData(CEntity& localEntity, int& localPlayerControllerIndex)
{
	std::vector<EntityBatchData> batchData;
	batchData.reserve(64);

	int validEntities = 0;
	int totalEntitiesChecked = 0;

	// collect entity addresses
	// CS2 entity-chunk slots are 0x70 bytes each, 512 slots per chunk.
	// The previous 1..64 window dropped player controllers placed past
	// slot 64 (which recent CS2 builds do, after stuffing chunk 0 with
	// more game-system entities). 1..128 covers every observed spill.
	constexpr int kMaxControllerSlot = 128;
	for (int entityIndex = 0; entityIndex < kMaxControllerSlot; ++entityIndex)
	{
		totalEntitiesChecked++;
		DWORD64 entityAddress = 0;
		if (!memoryManager.ReadMemory<DWORD64>(gGame.GetEntityListEntry() + (entityIndex + 1) * 0x70, entityAddress))
		{
			continue;
		}

		if (entityAddress != 0) {
			validEntities++;
		}

		// skip local player
		if (entityAddress == localEntity.Controller.Address)
		{
			localPlayerControllerIndex = entityIndex;
			continue;
		}

		// get pawn address
		CEntity tempEntity;
		tempEntity.Controller.Address = entityAddress;
		DWORD64 pawnAddress = tempEntity.Controller.GetPlayerPawnAddress();

		if (pawnAddress != 0)
		{
			batchData.emplace_back(entityIndex, entityAddress, pawnAddress);
		}
	}

	if (batchData.empty())
	{
		// Clear cache if no entities found - may indicate round transition
		static int emptyFrameCount = 0;
		emptyFrameCount++;
		if (emptyFrameCount > 10) { // Clear cache after 10 empty frames
			cachedResults.clear();
			emptyFrameCount = 0;
		}
		return {};
	}
	else
	{
		// Reset empty frame counter when entities are found
		static int emptyFrameCount = 0;
		emptyFrameCount = 0;
	}

	// Process entities individually (more resilient than batch processing)
	std::vector<std::pair<int, CEntity>> entities;
	entities.reserve(batchData.size());
	
	// Process each entity individually and collect successful ones
	for (size_t i = 0; i < batchData.size(); i++) {
		CEntity entity;
		entity.Controller.Address = batchData[i].controllerAddress;
		
		// Try to update controller
		if (!entity.UpdateController(batchData[i].controllerAddress)) {
			continue;
		}
		
		// Try to update pawn
		if (!entity.UpdatePawn(batchData[i].pawnAddress)) {
			continue;
		}
		
		// Success - add to results
		entities.emplace_back(batchData[i].entityIndex, entity);
	}

	// update cache
	cachedResults = entities;

	return cachedResults;
}

// process, prepare results
std::vector<EntityResult> Cheats::ProcessEntities(CEntity& localEntity, int& localPlayerControllerIndex)
{
	// get batch-processed entities
	auto entities = CollectEntityData(localEntity, localPlayerControllerIndex);
	std::vector<EntityResult> results;
	results.reserve(entities.size());

	// process each entity
	for (auto& [entityIndex, entity] : entities)
	{
		EntityResult result;
		result.entityIndex = entityIndex;
		result.entity = entity;

		if (!entity.IsAlive())
			continue;

		// check if teammate
		result.isTeammate = (entity.Controller.TeamID == localEntity.Controller.TeamID);

		// check if in screen
		result.isInScreen = entity.IsInScreen();

		// calculate distance
		result.distance = static_cast<int>(entity.Pawn.Pos.DistanceTo(localEntity.Pawn.Pos) / 100);

		// calculate esp box rect
		if (ESPConfig::ESPenabled && result.isInScreen)
			result.espRect = ESP::GetBoxRect(entity, ESPConfig::BoxType);

		result.isValid = true;
		results.push_back(result);
	}
	
	return results;
}

// render ESP, collect aimbot data
void Cheats::HandleEnts(const std::vector<EntityResult>& entities, CEntity& localEntity,
	int localPlayerControllerIndex, std::vector<Vec3>& aimPosList)
{
	static std::map<DWORD64, Render::HealthBar> HealthBarMap;
	float MaxAimDistance = 100000;

	for (const auto& result : entities)
	{
		if (!result.isValid)
		{
			if (HealthBarMap.count(result.entity.Controller.Address))
				HealthBarMap.erase(result.entity.Controller.Address);
			continue;
		}

		const auto& entity = result.entity;
		const int entityIndex = result.entityIndex;

        // skip not in screen
		if (!result.isInScreen)
		{
			continue;
		}

		// process aimbot data - collect visible bone positions for aimbot
		if (!AimControl::HitboxList.empty() && !(LegitBotConfig::AimEnemiesOnly && result.isTeammate)) {
			bool doVisCheck = LegitBotConfig::VisibleCheck && g_VisCheckManager && g_VisCheckManager->IsReady();

			const auto& bones = entity.GetBone().BonePosList;
			if (bones.empty()) continue;

			for (size_t i = 0; i < AimControl::HitboxList.size(); ++i) {
				int hitboxID = AimControl::HitboxList[i];
				if (hitboxID < 0 || hitboxID >= (int)bones.size()) continue;
				Vec3 bonePos = bones[hitboxID].Pos;

				// Check visibility to the actual bone, not just entity origin
				if (doVisCheck && !g_VisCheckManager->IsVisible(localEntity.Pawn.CameraPos, bonePos))
					continue;

				// Velocity prediction: extrapolate bone position by one tick
				if (AimControl::VelocityPrediction) {
					constexpr float tickInterval = 1.0f / 64.0f;
					bonePos = bonePos + entity.Pawn.Velocity * (tickInterval * AimControl::VelocityPredictionScale);
				}

				aimPosList.push_back(bonePos);
			}
		}

		// render esp - skip teammates if enemies only
		if (ESPConfig::ESPenabled && !(ESPConfig::EnemiesOnly && result.isTeammate) && (!ESPConfig::FlashCheck || localEntity.Pawn.FlashDuration < 0.1f))
		{
			const ImVec4& Rect = result.espRect;
			const int distance = result.distance;

			if (MenuConfig::RenderDistance == 0 || (distance <= MenuConfig::RenderDistance && MenuConfig::RenderDistance > 0))
			{
				// Efficient visibility check using spotted mask (from GhaxV3)
				// Uses server-authoritative m_bSpottedByMask instead of expensive ray casting
				bool bIsVisible = entity.IsSpottedByLocal(localPlayerControllerIndex);

				// Fallback to VisCheckManager ray casting if spotted mask says not visible
				// (spotted mask can have false negatives but not false positives)
				if (!bIsVisible && g_VisCheckManager && g_VisCheckManager->IsReady())
				{
					bIsVisible = g_VisCheckManager->IsVisible(localEntity.Pawn.CameraPos, entity.Pawn.Pos);
				}

				// Skip all ESP rendering if "Visible Only" is enabled and player is not visible
				if (ESPConfig::VisibleOnly && !bIsVisible)
					continue;
				
				ESP::RenderPlayerESP(localEntity, entity, Rect, localPlayerControllerIndex, entityIndex);
				Render::DrawDistance(localEntity, entity, Rect);

				// healthbar
				if(ESPConfig::ShowHealthBar || ESPConfig::ShowHealthNum)
				{
					ImVec2 HealthBarPos = { Rect.x - 6.f, Rect.y };
					ImVec2 HealthBarSize = { 4, Rect.w };
					Render::DrawHealthBar(entity.Controller.Address, 100.0f, static_cast<float>(entity.Pawn.Health), HealthBarPos, HealthBarSize);
				}


			}
		}
	}
}

void Visual(const CEntity& LocalEntity)
{
	// Fov circle for aimbot
	if (LocalEntity.Controller.TeamID != 0)
		Render::DrawFovCircle(ImGui::GetBackgroundDrawList(), LocalEntity);
}

void AIM(const CEntity& LocalEntity, std::vector<Vec3> AimPosList)
{
	static ULONGLONG lastTick = 0;
	ULONGLONG currentTick = GetTickCount64();

	if (!LegitBotConfig::AimBot) {
		RCS::RecoilControl(LocalEntity);
		return;
	}

	bool shouldAim = LegitBotConfig::AimAlways || (GetAsyncKeyState(AimControl::HotKey) & 0x8000);
	if (shouldAim && !AimPosList.empty())
		AimControl::AimBot(LocalEntity, LocalEntity.Pawn.CameraPos, AimPosList);

	if (LegitBotConfig::AimToggleMode && (GetAsyncKeyState(AimControl::HotKey) & 0x8000) &&
		currentTick - lastTick >= 200) {
		AimControl::switchToggle();
		lastTick = currentTick;
	}
}

std::string Cheats::GetCurrentMapName() {
    if (!g_globalVars || !g_globalVars->g_cCurrentMap) {
        return "";
    }

    char currentMap[256] = { 0 };
    if (!memoryManager.ReadMemory(reinterpret_cast<DWORD64>(g_globalVars->g_cCurrentMap),
        currentMap, sizeof(currentMap) - 1)) {
        return "";
    }

    currentMap[255] = '\0';
    return std::string(currentMap);
}

void Cheats::CheckCurrentMapName() {
    static std::string lastMapName = "";
    std::string currentMapName = "";
    
    // Get matchmaking.dll base for map detection
    DWORD64 matchmakingBase = memoryManager.GetModuleBase(L"matchmaking.dll");
    
    if (matchmakingBase != 0 && Offset.dwGameTypes == 0)
        Offset.TryResolveDeferred();

    if (matchmakingBase != 0 && Offset.dwGameTypes != 0) {
        const DWORD64 DW_GAME_TYPES = Offset.dwGameTypes;
        const DWORD64 DW_GAME_TYPES_MAP_NAME = 0x120;

        DWORD64 mapNamePtrAddress = matchmakingBase + DW_GAME_TYPES + DW_GAME_TYPES_MAP_NAME;
        DWORD64 mapNamePtr = 0;
        
        if (memoryManager.ReadMemory(mapNamePtrAddress, mapNamePtr, sizeof(DWORD64))) {
            if (mapNamePtr != 0 && mapNamePtr < 0x7FFFFFFFFFFF) {
                char mapNameBuffer[128] = { 0 };
                if (memoryManager.ReadMemory(mapNamePtr, mapNameBuffer, sizeof(mapNameBuffer) - 1)) {
                    mapNameBuffer[127] = '\0';
                    std::string mapName(mapNameBuffer);
                    
                    if (!mapName.empty() && mapName.length() >= 3 && !std::all_of(mapName.begin(), mapName.end(), ::isspace)) {
                        currentMapName = mapName;
                    } else {
                        currentMapName = "<empty>";
                    }
                }
            }
        }
    }
    
    if (currentMapName.empty()) {
        currentMapName = "<unknown>";
    }
    
    // Only print when map changes
    if (currentMapName != lastMapName) {
        if (currentMapName == "<empty>") {
            printf("[MAP] Left game/In menu\n");
        } else if (currentMapName == "<unknown>") {
            printf("[MAP] Unable to detect current map\n");
        } else {
            printf("[MAP] Map changed to: %s\n", currentMapName.c_str());
        }
        
        // Notify VisCheckManager about map change
        if (g_VisCheckManager) {
            g_VisCheckManager->OnMapChange(currentMapName);
        }
        
        lastMapName = currentMapName;
    }
}