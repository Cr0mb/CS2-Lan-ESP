#include "Offsets.h"
#include "../Core/Dumper.h"
#include "../Game/Game.h"
#include "../xorstr.h"
#include <cstdio>

Offsets::Offsets() {}
Offsets::~Offsets() {}

DWORD Offsets::GetSchemaField(const std::string& className, const std::string& fieldName)
{
    auto& schema = g_Dumper.GetSchemaFields();
    auto classIt = schema.find(className);
    if (classIt == schema.end())
    {
        printf("[Offsets] WARNING: Class '%s' not found in schema\n", className.c_str());
        return 0;
    }
    auto fieldIt = classIt->second.find(fieldName);
    if (fieldIt == classIt->second.end())
    {
        printf("[Offsets] WARNING: Field '%s.%s' not found in schema\n", className.c_str(), fieldName.c_str());
        return 0;
    }
    return fieldIt->second;
}

void Offsets::UpdateOffsets()
{
    DWORD pid = memoryManager.GetProcessID(L"cs2.exe");
    if (pid == 0)
        throw std::runtime_error("cs2.exe not found");

    if (!g_Dumper.Run(pid))
        throw std::runtime_error("Dumper failed");

    SetOffsetsFromDumper();
}

void Offsets::SetOffsetsFromDumper()
{
    auto& offsets = g_Dumper.GetOffsets();
    auto& buttons = g_Dumper.GetButtons();

    // Pattern-scanned offsets (client.dll)
    auto& cl = offsets["client.dll"];
    this->EntityList = cl["dwEntityList"];
    this->Matrix = cl["dwViewMatrix"];
    this->ViewAngle = cl["dwViewAngles"];
    this->LocalPlayerController = cl["dwLocalPlayerController"];
    this->LocalPlayerPawn = cl["dwLocalPlayerPawn"];
    this->GlobalVars = cl["dwGlobalVars"];
    this->PlantedC4 = cl["dwPlantedC4"];
    this->Sensitivity = cl["dwSensitivity"];
    this->Sensitivity_sensitivity = cl["dwSensitivity_sensitivity"];

    // Pattern-scanned offsets (inputsystem.dll)
    auto& is = offsets["inputsystem.dll"];
    this->InputSystem = is["dwInputSystem"];

    // Pattern-scanned offsets (matchmaking.dll). Only present if matchmaking
    // was loaded at dumper time; resolved lazily on first use otherwise.
    auto mmIt = offsets.find("matchmaking.dll");
    this->dwGameTypes = (mmIt != offsets.end()) ? mmIt->second["dwGameTypes"] : 0;

    printf("[Offsets] EntityList=0x%X Matrix=0x%X ViewAngle=0x%X\n", EntityList, Matrix, ViewAngle);
    printf("[Offsets] LocalPlayerController=0x%X LocalPlayerPawn=0x%X GlobalVars=0x%X\n", LocalPlayerController, LocalPlayerPawn, GlobalVars);
    printf("[Offsets] PlantedC4=0x%X InputSystem=0x%X Sensitivity=0x%X\n", PlantedC4, InputSystem, Sensitivity);

    // Buttons
    auto getBtnOrWarn = [&](const char* name) -> DWORD {
        auto it = buttons.find(name);
        if (it != buttons.end()) return it->second;
        printf("[Offsets] WARNING: Button '%s' not found\n", name);
        return 0;
    };
    this->Buttons.Attack = getBtnOrWarn("attack");
    this->Buttons.Jump = getBtnOrWarn("jump");
    this->Buttons.Right = getBtnOrWarn("right");
    this->Buttons.Left = getBtnOrWarn("left");

    printf("[Offsets] Buttons: Attack=0x%X Jump=0x%X Right=0x%X Left=0x%X\n",
        Buttons.Attack, Buttons.Jump, Buttons.Right, Buttons.Left);

    // Schema fields
    this->Entity.IsAlive = GetSchemaField("CCSPlayerController", "m_bPawnIsAlive");
    this->Entity.PlayerPawn = GetSchemaField("CCSPlayerController", "m_hPlayerPawn");
    this->Entity.iszPlayerName = GetSchemaField("CBasePlayerController", "m_iszPlayerName");

    this->Pawn.BulletServices = GetSchemaField("C_CSPlayerPawn", "m_pBulletServices");
    this->Pawn.CameraServices = GetSchemaField("C_BasePlayerPawn", "m_pCameraServices");
    this->Pawn.pClippingWeapon = GetSchemaField("C_CSPlayerPawn", "m_pClippingWeapon");  // 0 on 14154
    this->Pawn.isScoped = GetSchemaField("C_CSPlayerPawn", "m_bIsScoped");
    this->Pawn.isDefusing = GetSchemaField("C_CSPlayerPawn", "m_bIsDefusing");
    this->Pawn.TotalHit = GetSchemaField("CCSPlayer_BulletServices", "m_totalHitsOnServer");
    this->Pawn.Pos = GetSchemaField("C_BasePlayerPawn", "m_vOldOrigin");
    this->Pawn.CurrentArmor = GetSchemaField("C_CSPlayerPawn", "m_ArmorValue");
    this->Pawn.MaxHealth = GetSchemaField("C_BaseEntity", "m_iMaxHealth");
    this->Pawn.CurrentHealth = GetSchemaField("C_BaseEntity", "m_iHealth");
    this->Pawn.GameSceneNode = GetSchemaField("C_BaseEntity", "m_pGameSceneNode");
    this->Pawn.BoneArray = GetSchemaField("CSkeletonInstance", "m_modelState") + 0x80;
    this->Pawn.angEyeAngles = GetSchemaField("C_CSPlayerPawn", "m_angEyeAngles");
    this->Pawn.vecLastClipCameraPos = GetSchemaField("C_CSPlayerPawn", "m_vecLastClipCameraPos");  // 0 on 14154
    this->Pawn.iShotsFired = GetSchemaField("C_CSPlayerPawn", "m_iShotsFired");
    this->Pawn.flFlashDuration = GetSchemaField("C_CSPlayerPawnBase", "m_flFlashDuration");
    this->Pawn.aimPunchAngle = GetSchemaField("C_CSPlayerPawn", "m_aimPunchAngle");  // 0 on 14154
    this->Pawn.aimPunchCache = GetSchemaField("C_CSPlayerPawn", "m_aimPunchCache");
    if (this->Pawn.aimPunchCache == 0)
        this->Pawn.aimPunchCache = GetSchemaField("C_CSPlayerPawn", "m_aimPunchTickData");
    this->Pawn.iIDEntIndex = GetSchemaField("C_CSPlayerPawn", "m_iIDEntIndex");
    this->Pawn.iTeamNum = GetSchemaField("C_BaseEntity", "m_iTeamNum");
    this->Pawn.iFovStart = GetSchemaField("CCSPlayerBase_CameraServices", "m_iFOVStart");
    this->Pawn.fFlags = GetSchemaField("C_BaseEntity", "m_fFlags");
    this->Pawn.bSpottedByMask = GetSchemaField("C_CSPlayerPawn", "m_entitySpottedState")
        + GetSchemaField("EntitySpottedState_t", "m_bSpottedByMask");
    this->Pawn.AbsVelocity = GetSchemaField("C_BaseEntity", "m_vecAbsVelocity");
    this->Pawn.m_bWaitForNoAttack = GetSchemaField("C_CSPlayerPawn", "m_bWaitForNoAttack");
    this->Pawn.m_pWeaponServices = GetSchemaField("C_BasePlayerPawn", "m_pWeaponServices");
    this->Pawn.m_flEmitSoundTime = GetSchemaField("C_CSPlayerPawn", "m_flEmitSoundTime");

    // Build 14154+: m_aimPunchAngle / m_aimPunchCache / m_pClippingWeapon were
    // removed from the schema. Use AimPunchServices instead.
    this->Pawn.pAimPunchServices = GetSchemaField("C_CSPlayerPawn", "m_pAimPunchServices");
    if (this->Pawn.pAimPunchServices == 0)
        this->Pawn.pAimPunchServices = 0x14B8;  // Hardcoded fallback from GhaxV3
    this->Pawn.aimPunchServicesBaseAngle = GetSchemaField("CCSPlayer_AimPunchServices", "m_predictableBaseAngle");
    if (this->Pawn.aimPunchServicesBaseAngle == 0)
        this->Pawn.aimPunchServicesBaseAngle = 0x50;  // Hardcoded fallback
    this->Pawn.hActiveWeapon = GetSchemaField("CPlayer_WeaponServices", "m_hActiveWeapon");

    // Record which fields are missing; consumers branch on these flags so the
    // rest of the codebase can keep using Offset.Pawn.* as before.
    this->PClippingWeaponMissing = (this->Pawn.pClippingWeapon == 0);
    this->AimPunchMissing = (this->Pawn.aimPunchAngle == 0);
    this->CameraPosMissing = (this->Pawn.vecLastClipCameraPos == 0);

    // Provide a non-zero fallback offset for the batch reader: it reads whatever
    // bytes live at pawn+offset and we post-process them. Point the stale
    // offsets at Pos so the reads land on valid memory; the buffered values are
    // then overwritten in Entity.cpp post-batch.
    if (this->Pawn.pClippingWeapon == 0)       this->Pawn.pClippingWeapon = this->Pawn.Pos;
    if (this->Pawn.aimPunchAngle == 0)         this->Pawn.aimPunchAngle = this->Pawn.Pos;
    if (this->Pawn.aimPunchCache == 0)         this->Pawn.aimPunchCache = this->Pawn.Pos;
    if (this->Pawn.vecLastClipCameraPos == 0)  this->Pawn.vecLastClipCameraPos = this->Pawn.Pos;

    // GlobalVar sub-offsets (hardcoded, part of engine struct layout)
    this->GlobalVar.RealTime = 0x00;
    this->GlobalVar.FrameCount = 0x04;
    this->GlobalVar.MaxClients = 0x10;
    this->GlobalVar.IntervalPerTick = 0x14;
    this->GlobalVar.CurrentTime = 0x30;
    this->GlobalVar.CurrentTime2 = 0x38;
    this->GlobalVar.TickCount = 0x48;
    this->GlobalVar.IntervalPerTick2 = 0x44;
    this->GlobalVar.CurrentNetchan = 0x0048;
    this->GlobalVar.CurrentMap = 0x0180;
    this->GlobalVar.CurrentMapName = 0x0188;

    this->PlayerController.m_nTickBase = GetSchemaField("CBasePlayerController", "m_nTickBase");
    this->PlayerController.m_steamID = GetSchemaField("CBasePlayerController", "m_steamID");
    this->PlayerController.m_hPawn = GetSchemaField("CBasePlayerController", "m_hPawn");
    this->PlayerController.m_pObserverServices = GetSchemaField("C_BasePlayerPawn", "m_pObserverServices");
    this->PlayerController.m_hObserverTarget = GetSchemaField("CPlayer_ObserverServices", "m_hObserverTarget");
    this->PlayerController.m_hController = GetSchemaField("C_BasePlayerPawn", "m_hController");
    this->PlayerController.PawnArmor = GetSchemaField("CCSPlayerController", "m_iPawnArmor");
    this->PlayerController.HasDefuser = GetSchemaField("CCSPlayerController", "m_bPawnHasDefuser");
    this->PlayerController.HasHelmet = GetSchemaField("CCSPlayerController", "m_bPawnHasHelmet");

    this->EconEntity.AttributeManager = GetSchemaField("C_EconEntity", "m_AttributeManager");

    this->WeaponBaseData.WeaponDataPTR = GetSchemaField("C_BaseEntity", "m_nSubclassID") + 0x08;
    this->WeaponBaseData.szName = GetSchemaField("CCSWeaponBaseVData", "m_szName");
    this->WeaponBaseData.Clip1 = GetSchemaField("C_BasePlayerWeapon", "m_iClip1");
    this->WeaponBaseData.MaxClip = GetSchemaField("CBasePlayerWeaponVData", "m_iMaxClip1");
    this->WeaponBaseData.Item = GetSchemaField("C_AttributeContainer", "m_Item");
    this->WeaponBaseData.ItemDefinitionIndex = GetSchemaField("C_EconItemView", "m_iItemDefinitionIndex");
    this->WeaponBaseData.hMyWeapons = GetSchemaField("CPlayer_WeaponServices", "m_hMyWeapons");

    this->C4.m_bBeingDefused = GetSchemaField("C_PlantedC4", "m_bBeingDefused");
    this->C4.m_flDefuseCountDown = GetSchemaField("C_PlantedC4", "m_flDefuseCountDown");
    this->C4.m_nBombSite = GetSchemaField("C_PlantedC4", "m_nBombSite");

    printf("[Offsets] All offsets set from dumper successfully\n");
    if (this->PClippingWeaponMissing || this->AimPunchMissing || this->CameraPosMissing)
    {
        printf("[Offsets] 14154 compat: pClippingWeapon=%s aimPunch=%s cameraPos=%s\n",
               this->PClippingWeaponMissing ? "services" : "direct",
               this->AimPunchMissing ? "services" : "direct",
               this->CameraPosMissing ? "synthesized" : "direct");
    }
}

void Offsets::TryResolveDeferred()
{
    if (this->dwGameTypes != 0) return;
    if (!g_Dumper.RescanMatchmaking()) return;
    auto& offsets = g_Dumper.GetOffsets();
    auto mmIt = offsets.find("matchmaking.dll");
    if (mmIt == offsets.end()) return;
    this->dwGameTypes = mmIt->second["dwGameTypes"];
    if (this->dwGameTypes != 0)
        printf("[Offsets] dwGameTypes resolved late: 0x%X\n", this->dwGameTypes);
}

DWORD64 ResolveActiveWeapon(DWORD64 pawnAddr)
{
    if (pawnAddr == 0) return 0;

    // Legacy direct path (pre-14154)
    if (!Offset.PClippingWeaponMissing && Offset.Pawn.pClippingWeapon != 0)
    {
        DWORD64 weapon = 0;
        if (memoryManager.ReadMemory<DWORD64>(pawnAddr + Offset.Pawn.pClippingWeapon, weapon) && weapon)
            return weapon;
    }

    // 14154+: resolve via CPlayer_WeaponServices -> m_hActiveWeapon -> entity list
    if (Offset.Pawn.m_pWeaponServices == 0 || Offset.Pawn.hActiveWeapon == 0)
        return 0;

    DWORD64 services = 0;
    if (!memoryManager.ReadMemory<DWORD64>(pawnAddr + Offset.Pawn.m_pWeaponServices, services) || !services)
        return 0;

    uint32_t handle = 0;
    if (!memoryManager.ReadMemory<uint32_t>(services + Offset.Pawn.hActiveWeapon, handle))
        return 0;

    handle &= 0x7FFF;
    if (handle == 0 || handle == 0x7FFF)
        return 0;

    DWORD64 entListBase = 0;
    if (!memoryManager.ReadMemory<DWORD64>(gGame.GetEntityListAddress(), entListBase) || !entListBase)
        return 0;

    DWORD64 listEntry = 0;
    if (!memoryManager.ReadMemory<DWORD64>(entListBase + 0x10 + 8 * (handle >> 9), listEntry) || !listEntry)
        return 0;

    DWORD64 weapon = 0;
    if (!memoryManager.ReadMemory<DWORD64>(listEntry + 0x70 * (handle & 0x1FF), weapon))
        return 0;
    return weapon;
}
