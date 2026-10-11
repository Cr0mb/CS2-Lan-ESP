#pragma once
#include "../Game/Game.h"
#include "../Game/Entity.h"
#include "../Features/Aimbot.h"
#include <vector>

struct EntityResult {
	int entityIndex = -1;
	CEntity entity;
	ImVec4 espRect;
	int distance = 0;
	bool isInScreen = false;
	bool isTeammate = false;
	bool isValid = false;
};

namespace Cheats
{
	static DWORD m_currentTick = 0;
	static DWORD m_previousTick = 0;
	static std::vector<std::pair<int, CEntity>> cachedResults;

	void Run();

	std::vector<std::pair<int, CEntity>> CollectEntityData(CEntity& localEntity, int& localPlayerControllerIndex);
	std::vector<EntityResult> ProcessEntities(CEntity& localEntity, int& localPlayerControllerIndex);
	void HandleEnts(const std::vector<EntityResult>& entities, CEntity& localEntity,
		int localPlayerControllerIndex, std::vector<Vec3>& aimPosList);

	std::string GetCurrentMapName();
	void CheckCurrentMapName();
}

struct {
	ImFont* normal15px = nullptr;
} fonts;
