#pragma once
#include <Windows.h>
#include <string>
#include <map>
#include "../Core/MemoryMgr.h"

class Offsets
{
public:
	Offsets();
	~Offsets();
	void UpdateOffsets();

	// Resolve offsets deferred from initial dump (currently: matchmaking.dll
	// dwGameTypes, which loads only after entering a match). Cheap when the
	// value is already populated; pattern-scans matchmaking.dll otherwise.
	void TryResolveDeferred();

	DWORD EntityList;
	DWORD Matrix;
	DWORD ViewAngle;
	DWORD LocalPlayerController;
	DWORD LocalPlayerPawn;
	DWORD GlobalVars;
	DWORD PlantedC4;
	DWORD InputSystem;
	DWORD Sensitivity;
	DWORD Sensitivity_sensitivity;
	DWORD dwGameTypes;          // matchmaking.dll RVA — deferred until matchmaking.dll loads

	struct
	{
		DWORD Jump;
		DWORD Left;
		DWORD Right;
		DWORD Attack;
	}Buttons;

	struct
	{
		DWORD IsAlive;
		DWORD PlayerPawn;
		DWORD iszPlayerName;
	}Entity;

	struct
	{
		DWORD BulletServices;
		DWORD CameraServices;
		DWORD pClippingWeapon;               // 0 on build 14154 — field removed from schema

		DWORD isScoped;
		DWORD isDefusing;
		DWORD TotalHit;
		DWORD Pos;
		DWORD CurrentArmor;
		DWORD MaxHealth;
		DWORD CurrentHealth;
		DWORD GameSceneNode;
		DWORD BoneArray;
		DWORD angEyeAngles;
		DWORD vecLastClipCameraPos;          // 0 on build 14154 — field removed from schema
		DWORD iShotsFired;
		DWORD flFlashDuration;
		DWORD aimPunchAngle;                 // 0 on build 14154 — read via AimPunchServices instead
		DWORD aimPunchCache;                 // 0 on build 14154 — read via AimPunchServices instead
		DWORD iIDEntIndex;
		DWORD iTeamNum;
		DWORD iFovStart;
		DWORD fFlags;
		DWORD bSpottedByMask;
		DWORD AbsVelocity;
		DWORD m_bWaitForNoAttack;
		DWORD m_pWeaponServices;
		DWORD m_flEmitSoundTime;
		DWORD pAimPunchServices;             // C_CSPlayerPawn.m_pAimPunchServices (ptr)
		DWORD aimPunchServicesBaseAngle;     // CCSPlayer_AimPunchServices.m_predictableBaseAngle
		DWORD hActiveWeapon;                 // CPlayer_WeaponServices.m_hActiveWeapon

	} Pawn;

	struct
	{
		DWORD RealTime;
		DWORD FrameCount;
		DWORD MaxClients;
		DWORD IntervalPerTick;
		DWORD CurrentTime;
		DWORD CurrentTime2;
		DWORD TickCount;
		DWORD IntervalPerTick2;
		DWORD CurrentNetchan;
		DWORD CurrentMap;
		DWORD CurrentMapName;
	} GlobalVar;

	struct
	{
		DWORD m_steamID;
		DWORD m_hPawn;
		DWORD m_pObserverServices;
		DWORD m_hObserverTarget;
		DWORD m_hController;
		DWORD PawnArmor;
		DWORD HasDefuser;
		DWORD HasHelmet;
		DWORD m_nTickBase;
	} PlayerController;

	struct
	{
		DWORD AttributeManager;
	} EconEntity;

	struct
	{
		DWORD WeaponDataPTR;
		DWORD szName;
		DWORD Clip1;
		DWORD MaxClip;
		DWORD Item;
		DWORD ItemDefinitionIndex;
		DWORD hMyWeapons;
	} WeaponBaseData;

	struct
	{
		DWORD m_bBeingDefused;
		DWORD m_flDefuseCountDown;
		DWORD m_nBombSite;
	} C4;

	// --- Build 14154 compatibility flags -----------------------------------
	// The 14154 schema dropped m_pClippingWeapon / m_aimPunchAngle /
	// m_aimPunchCache / m_vecLastClipCameraPos. When these flags are set the
	// consumer must route reads through the services pointer / synthesize the
	// value. See Entity.cpp and Features/RCS.cpp.
	bool PClippingWeaponMissing = false;
	bool AimPunchMissing = false;
	bool CameraPosMissing = false;

private:
	void SetOffsetsFromDumper();
	DWORD GetSchemaField(const std::string& className, const std::string& fieldName);
};

inline Offsets Offset;

// Resolve the local/other player's currently-wielded weapon entity address.
// Tries the legacy direct field (m_pClippingWeapon) first; if that field is
// missing (build 14154+) falls back to CPlayer_WeaponServices -> m_hActiveWeapon
// -> entity list lookup. Returns 0 on failure.
DWORD64 ResolveActiveWeapon(DWORD64 pawnAddr);
