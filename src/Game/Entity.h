#pragma once
#include "Game.h"
#include "View.h"
#include "Bone.h"
#include "../Core/Globals.h"
#include <map>
#include <tuple>
#include <type_traits>

// Batch field descriptor for compile-time automation
template<typename T>
struct BatchField {
	DWORD64 offset;
	T* target;
	SIZE_T size = sizeof(T);

	constexpr BatchField(DWORD64 off, T* tgt) : offset(off), target(tgt) {}
};

// Conditional field for dependent reads
template<typename T>
struct ConditionalField {
	DWORD64 baseAddr;
	DWORD64 offset;
	T* target;
	SIZE_T size = sizeof(T);
	T defaultValue;

	ConditionalField(DWORD64 base, DWORD64 off, T* tgt, T def = T{})
		: baseAddr(base), offset(off), target(tgt), defaultValue(def) {
	}
};

struct C_UTL_VECTOR
{
	DWORD64 Count = 0;
	DWORD64 Data = 0;
};

class PlayerController
{
public:
	DWORD64 Address = 0;
	int Money = 0;
	int CashSpent = 0;
	int CashSpentTotal = 0;
	int TeamID = 0;
	int Health = 0;
	int AliveStatus = 0;
	INT64 SteamID = 0;
	int m_nTickBase = 0;
	DWORD Pawn = 0;
	std::string PlayerName;
	std::vector<std::string> spectators = {};

	DWORD64 cachedEntityListEntry = 0;
	DWORD lastCachedPawn = 0;

public:
	bool GetTeamID();
	bool GetHealth();
	bool GetIsAlive();
	bool GetPlayerName();
	bool GetPlayerSteamID();
	DWORD64 GetPlayerPawnAddress();
};

class PlayerPawn
{
public:
	enum class Flags
	{
		NONE,
		ON_GROUND = 1 << 0,
		IN_CROUCH = 1 << 1
	};

	// Defaults matter: UpdatePawn now treats most field reads as best-effort
	// (mirroring GFusion's read_data) so transient state on one player doesn't
	// drop them from ESP. If a best-effort read fails we keep the prior value
	// instead of dropping the entity, so primitives MUST start from a known
	// safe value rather than stack garbage.
	DWORD64 Address = 0;
	CBone BoneData;
	Vec2 ViewAngle{};
	Vec3 Pos{};
	Vec2 ScreenPos{};
	Vec3 CameraPos{};
	float Speed = 0.f;
	Vec3 Velocity{};
	std::string WeaponName;
	DWORD ShotsFired = 0;
	DWORD GameSceneNode = 0;
	Vec2 AimPunchAngle{};
	C_UTL_VECTOR AimPunchCache{};
	int Health = 0;
	int Ammo = 0;
	//int MaxAmmo;
	int Armor = 0;
	int TeamID = 0;
	int Fov = 90;
	DWORD64 bSpottedByMask = 0;
	int fFlags = 0;
	float FlashDuration = 0.f;
	//bool isDefusing;

public:
	bool GetPos();
	bool GetViewAngle();
	bool GetCameraPos();
	bool GetWeaponName();
	bool GetShotsFired();
	bool GetAimPunchAngle();
	bool GetHealth();
	bool GetTeamID();
	bool GetFov();
	bool GetSpotted();
	bool GetFFlags();
	bool GetAimPunchCache();
	bool GetAmmo();
	//bool GetMaxAmmo();
	bool GetArmor();
	//bool GetDefusing();
	bool GetFlashDuration();
	bool GetVelocity();

	std::vector<short> GetWeaponInventory(DWORD64 entityList) const;

	bool HasFlag(const Flags Flag) const noexcept
	{
		return fFlags & (int)Flag;
	}
};

class Client
{
public:
	float Sensitivity;

public:
	bool GetSensitivity();
};

class CEntity
{
private:

	// Pre-calculated field descriptors (computed once, reused)
	static inline auto GetPawnFieldDescriptors(DWORD64 pawnAddr, PlayerPawn& pawn) {
		return std::make_tuple(
			BatchField{ Offset.Pawn.angEyeAngles, &pawn.ViewAngle },
			BatchField{ Offset.Pawn.vecLastClipCameraPos, &pawn.CameraPos },
			BatchField{ Offset.Pawn.Pos, &pawn.Pos },
			BatchField{ Offset.Pawn.bSpottedByMask, &pawn.bSpottedByMask },
			BatchField{ Offset.Pawn.fFlags, &pawn.fFlags },
			BatchField{ Offset.Pawn.iShotsFired, &pawn.ShotsFired },
			BatchField{ Offset.Pawn.aimPunchAngle, &pawn.AimPunchAngle },
			BatchField{ Offset.Pawn.iTeamNum, &pawn.TeamID },
			BatchField{ Offset.Pawn.CurrentHealth, &pawn.Health },
			BatchField{ Offset.Pawn.CurrentArmor, &pawn.Armor },
			BatchField{ Offset.Pawn.flFlashDuration, &pawn.FlashDuration },
			//BatchField{ Offset.C4.m_bBeingDefused, &pawn.isDefusing },
			BatchField{ Offset.Pawn.aimPunchCache, &pawn.AimPunchCache },
			BatchField{ Offset.Pawn.GameSceneNode, &pawn.GameSceneNode }
		);
	}

	// Template function to build requests from field descriptors
	template<typename... Fields>
	static void BuildRequests(DWORD64 baseAddr, std::vector<std::pair<DWORD64, SIZE_T>>& requests,
		const std::tuple<Fields...>& fields) {
		std::apply([&](const auto&... field) {
			((requests.emplace_back(baseAddr + field.offset, field.size)), ...);
			}, fields);
	}

	// Template function to extract data from buffer
	template<typename... Fields>
	static void ExtractData(const BYTE* buffer, SIZE_T& offset, const std::tuple<Fields...>& fields) {
		std::apply([&](const auto&... field) {
			((memcpy(field.target, buffer + offset, field.size), offset += field.size), ...);
			}, fields);
	}

public:
	PlayerController Controller;
	PlayerPawn Pawn;
	Client Client;

	bool UpdateController(const DWORD64& PlayerControllerAddress);
	bool UpdatePawn(const DWORD64& PlayerPawnAddress);
	bool UpdateClientData();
	bool IsAlive() const;
	bool IsInScreen();
	CBone GetBone() const;

	// Efficient visibility check using spotted mask (from GhaxV3)
	bool IsSpottedByLocal(int localPlayerSlot) const;

	static DWORD64 ResolveEntityHandle(uint32_t handle);

	static std::unordered_map<int, std::string> weaponNames;
	static inline std::string GetWeaponName(int weaponID) {
		auto it = weaponNames.find(weaponID);
		if (it != weaponNames.end()) {
			return it->second;
		}
		return "Weapon_None";
	}

};

struct EntityBatchData {
	int entityIndex;
	DWORD64 controllerAddress;
	DWORD64 pawnAddress;

	EntityBatchData(int idx, DWORD64 ctrlAddr, DWORD64 pawnAddr)
		: entityIndex(idx), controllerAddress(ctrlAddr), pawnAddress(pawnAddr) {
	}
};

class EntityBatchProcessor {
private:
	std::vector<EntityBatchData> entityBatchData;
	std::vector<std::pair<DWORD64, SIZE_T>> allRequests;
	std::vector<BYTE> masterBuffer;

	// Phase 1: Controller + Core Pawn data
	bool ProcessCoreEntityData(std::vector<std::pair<int, CEntity>>& entities,
		std::vector<DWORD64>& weaponAddresses,
		std::vector<DWORD64>& cameraAddresses);

	// Phase 2: Weapon data
	bool ProcessWeaponData(std::vector<std::pair<int, CEntity>>& entities,
		const std::vector<DWORD64>& weaponAddresses,
		std::vector<DWORD64>& weaponDataAddresses);

	// Phase 3: Final dependent data
	bool ProcessDependenciesData(std::vector<std::pair<int, CEntity>>& entities,
		const std::vector<DWORD64>& weaponDataAddresses,
		const std::vector<DWORD64>& cameraAddresses);

public:

	bool ProcessAllEntities(std::vector<std::pair<int, CEntity>>& entities, const std::vector<EntityBatchData>& batchData);
};