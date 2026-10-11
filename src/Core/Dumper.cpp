#include "Dumper.h"
#include <TlHelp32.h>
#include <cstdio>
#include <cstring>

// ============================================================
// Pattern scanning + RIP-relative resolution
// ============================================================

std::vector<Dumper::PatternByte> Dumper::ParsePattern(const std::string& pattern)
{
    std::vector<PatternByte> result;
    for (size_t i = 0; i < pattern.size(); i += 2)
    {
        while (i < pattern.size() && pattern[i] == ' ') i++;
        if (i + 1 >= pattern.size()) break;

        if (pattern[i] == '?' && pattern[i + 1] == '?')
            result.push_back({ 0, true });
        else
        {
            BYTE val = (BYTE)strtol(pattern.substr(i, 2).c_str(), nullptr, 16);
            result.push_back({ val, false });
        }
    }
    return result;
}

int Dumper::FindPattern(const BYTE* data, size_t dataSize, const std::vector<PatternByte>& pattern, size_t startOffset)
{
    if (pattern.empty() || dataSize < pattern.size()) return -1;

    for (size_t i = startOffset; i <= dataSize - pattern.size(); i++)
    {
        bool found = true;
        for (size_t j = 0; j < pattern.size(); j++)
        {
            if (!pattern[j].wildcard && data[i + j] != pattern[j].value)
            {
                found = false;
                break;
            }
        }
        if (found) return (int)i;
    }
    return -1;
}

DWORD Dumper::ResolveRIPRelative(const ModuleInfo& mod, int matchOffset, int ripOffset, int instrLen)
{
    // Read the 4-byte signed displacement at matchOffset + ripOffset
    if (matchOffset + ripOffset + 4 > (int)mod.data.size()) return 0;

    int32_t disp = *(int32_t*)&mod.data[matchOffset + ripOffset];
    // RIP = matchOffset + instrLen (address of next instruction, relative to module start)
    DWORD target = (DWORD)(matchOffset + instrLen + disp);
    return target;
}

// ============================================================
// Module loading via Toolhelp + kernel driver reads
// ============================================================

bool Dumper::ReadModuleMemory(DWORD64 base, DWORD size, std::vector<BYTE>& out)
{
    out.resize(size, 0);
    const DWORD chunkSize = 0x1000;

    for (DWORD offset = 0; offset < size; offset += chunkSize)
    {
        DWORD toRead = min(chunkSize, size - offset);
        memoryManager.ReadMemory<BYTE>(base + offset, out[offset], toRead);
    }
    return true;
}

bool Dumper::LoadModule(const wchar_t* name, ModuleInfo& info)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, m_processID);
    if (snap == INVALID_HANDLE_VALUE) return false;

    MODULEENTRY32W me = {};
    me.dwSize = sizeof(me);
    bool found = false;

    if (Module32FirstW(snap, &me))
    {
        do
        {
            if (_wcsicmp(me.szModule, name) == 0)
            {
                info.base = (DWORD64)me.modBaseAddr;
                info.size = me.modBaseSize;
                found = true;
                break;
            }
        } while (Module32NextW(snap, &me));
    }
    CloseHandle(snap);

    if (!found) return false;

    return ReadModuleMemory(info.base, info.size, info.data);
}

// ============================================================
// Main entry
// ============================================================

bool Dumper::Run(DWORD processID)
{
    m_processID = processID;
    m_offsets.clear();
    m_buttons.clear();
    m_schemaFields.clear();

    printf("[Dumper] Phase 1: Pattern scanning offsets...\n");
    if (!ScanOffsets())
    {
        printf("[Dumper] ERROR: Pattern scan failed\n");
        return false;
    }

    printf("[Dumper] Phase 2: Scanning buttons...\n");
    ModuleInfo client;
    if (!LoadModule(L"client.dll", client))
    {
        printf("[Dumper] ERROR: Failed to load client.dll for button scan\n");
        return false;
    }
    if (!ScanButtons(client))
        printf("[Dumper] WARNING: Button scan had issues\n");

    printf("[Dumper] Phase 3: Schema system traversal...\n");
    if (!ScanSchemas())
        printf("[Dumper] WARNING: Schema scan had issues\n");

    return true;
}

// ============================================================
// Phase 1: Pattern-based offset scanning
// ============================================================

bool Dumper::ScanOffsets()
{
    ModuleInfo client;
    if (!LoadModule(L"client.dll", client))
    {
        printf("[Dumper] ERROR: Failed to load client.dll\n");
        return false;
    }

    ModuleInfo inputsystem;
    if (!LoadModule(L"inputsystem.dll", inputsystem))
    {
        printf("[Dumper] ERROR: Failed to load inputsystem.dll\n");
        return false;
    }

    ScanClientOffsets(client);
    ScanInputSystemOffsets(inputsystem);

    // matchmaking.dll is optional — only loaded once a match is joined. Scan it
    // best-effort so dwGameTypes (used for map detection / vischeck) is
    // populated when available.
    ModuleInfo matchmaking;
    if (LoadModule(L"matchmaking.dll", matchmaking))
        ScanMatchmakingOffsets(matchmaking);
    else
        printf("[Dumper] matchmaking.dll not loaded yet — dwGameTypes deferred\n");

    return true;
}

bool Dumper::RescanMatchmaking()
{
    if (m_processID == 0) return false;
    ModuleInfo mod;
    if (!LoadModule(L"matchmaking.dll", mod)) return false;
    return ScanMatchmakingOffsets(mod);
}

bool Dumper::ScanMatchmakingOffsets(ModuleInfo& mod)
{
    auto& mmMap = m_offsets["matchmaking.dll"];
    const BYTE* data = mod.data.data();
    size_t sz = mod.data.size();

    // dwGameTypes: 48 8D 0D ${'} FF 90  (LEA RCX,[RIP+disp32]; CALL [RAX+...])
    auto p = ParsePattern("48 8D 0D ?? ?? ?? ?? FF 90");
    int match = FindPattern(data, sz, p);
    if (match < 0) { printf("[Dumper] MISS: dwGameTypes\n"); return false; }
    DWORD rva = ResolveRIPRelative(mod, match, 3, 7);
    mmMap["dwGameTypes"] = rva;
    printf("[Dumper] dwGameTypes = 0x%X\n", rva);
    return true;
}

bool Dumper::ScanClientOffsets(ModuleInfo& mod)
{
    auto& clientMap = m_offsets["client.dll"];
    const BYTE* data = mod.data.data();
    size_t sz = mod.data.size();

    // Helper lambda for scan + RIP resolve
    auto scanRIP = [&](const char* name, const char* pat, int ripOff, int instrLen) -> DWORD {
        auto p = ParsePattern(pat);
        int match = FindPattern(data, sz, p);
        if (match < 0) { printf("[Dumper] MISS: %s\n", name); return 0; }
        DWORD rva = ResolveRIPRelative(mod, match, ripOff, instrLen);
        clientMap[name] = rva;
        printf("[Dumper] %s = 0x%X\n", name, rva);
        return rva;
    };

    // dwCSGOInput: 48 89 05 ${'} 0F 57 C0 0F 11 05
    // pattern!("488905${'} 0f57c0 0f1105")
    // RIP disp at offset 3, instruction ends at offset 7
    DWORD csgoInput = scanRIP("dwCSGOInput", "48 89 05 ?? ?? ?? ?? 0F 57 C0 0F 11 05", 3, 7);

    // dwViewAngles: derived from dwCSGOInput
    // pattern!("f2420f108428u4") -> F2 42 0F 10 84 28 XX XX XX XX
    // u4 at offset 6 = raw 4-byte value added to dwCSGOInput
    {
        auto p = ParsePattern("F2 42 0F 10 84 28 ?? ?? ?? ??");
        int match = FindPattern(data, sz, p);
        if (match >= 0 && csgoInput != 0)
        {
            DWORD subOffset = *(DWORD*)&data[match + 6];
            clientMap["dwViewAngles"] = csgoInput + subOffset;
            printf("[Dumper] dwViewAngles = 0x%X (CSGOInput + 0x%X)\n", csgoInput + subOffset, subOffset);
        }
        else printf("[Dumper] MISS: dwViewAngles\n");
    }

    // dwEntityList: 48 89 0D ${'} E9 ${} CC
    scanRIP("dwEntityList", "48 89 0D ?? ?? ?? ?? E9 ?? ?? ?? ?? CC", 3, 7);

    // dwGlobalVars: 48 89 15 ${'} 48 89 42
    scanRIP("dwGlobalVars", "48 89 15 ?? ?? ?? ?? 48 89 42", 3, 7);

    // dwLocalPlayerController: 48 8B 05 ${'} 41 89 BE
    scanRIP("dwLocalPlayerController", "48 8B 05 ?? ?? ?? ?? 41 89 BE", 3, 7);

    // dwViewMatrix: 48 8D 0D ${'} 48 C1 E0 06
    scanRIP("dwViewMatrix", "48 8D 0D ?? ?? ?? ?? 48 C1 E0 06", 3, 7);

    // dwPlantedC4: 48 8B 15 ${'} 41 FF C0 48 8D 4C 24 ? 44 89 05
    scanRIP("dwPlantedC4", "48 8B 15 ?? ?? ?? ?? 41 FF C0 48 8D 4C 24 ?? 44 89 05", 3, 7);

    // dwPrediction: 48 8D 05 ${'} C3 CC CC CC CC CC CC CC CC CC CC CC CC 40 53 56 41 54
    // Try the full pattern first, then shorter fallbacks
    DWORD prediction = 0;
    {
        auto p1 = ParsePattern("48 8D 05 ?? ?? ?? ?? C3 CC CC CC CC CC CC CC CC CC 40 53 56 41 54");
        int m = FindPattern(data, sz, p1);
        if (m >= 0)
        {
            prediction = ResolveRIPRelative(mod, m, 3, 7);
        }
        else
        {
            // Fallback: shorter pattern
            auto p2 = ParsePattern("48 8D 05 ?? ?? ?? ?? C3 CC CC CC CC CC CC CC CC 40 53 56 41 54");
            m = FindPattern(data, sz, p2);
            if (m >= 0)
                prediction = ResolveRIPRelative(mod, m, 3, 7);
            else
            {
                // Another fallback without the trailing bytes
                auto p3 = ParsePattern("48 8D 05 ?? ?? ?? ?? C3 CC CC CC CC CC CC CC CC CC 40 53");
                m = FindPattern(data, sz, p3);
                if (m >= 0)
                    prediction = ResolveRIPRelative(mod, m, 3, 7);
            }
        }
        if (prediction != 0)
        {
            clientMap["dwPrediction"] = prediction;
            printf("[Dumper] dwPrediction = 0x%X\n", prediction);
        }
        else
            printf("[Dumper] MISS: dwPrediction\n");
    }

    // dwLocalPlayerPawn: derived from dwPrediction
    // pattern!("4c39b6u4 74? 4488be")
    // u4 at offset 3 added to dwPrediction
    {
        auto p = ParsePattern("4C 39 B6 ?? ?? ?? ?? 74 ?? 44 88 BE");
        int match = FindPattern(data, sz, p);
        if (match >= 0 && prediction != 0)
        {
            DWORD subOffset = *(DWORD*)&data[match + 3];
            clientMap["dwLocalPlayerPawn"] = prediction + subOffset;
            printf("[Dumper] dwLocalPlayerPawn = 0x%X (Prediction + 0x%X)\n", prediction + subOffset, subOffset);
        }
        else printf("[Dumper] MISS: dwLocalPlayerPawn\n");
    }

    // dwSensitivity: pattern!("488d0d${[8]'} 660f6ecd")
    // ${[8]'} = RIP-resolve 4 bytes, then skip 8 bytes of wildcards
    // Search for: 48 8D 0D [disp32] [8 wildcards] 66 0F 6E CD
    {
        auto p = ParsePattern("48 8D 0D ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? 66 0F 6E CD");
        int match = FindPattern(data, sz, p);
        if (match >= 0)
        {
            DWORD rva = ResolveRIPRelative(mod, match, 3, 7);
            clientMap["dwSensitivity"] = rva;
            printf("[Dumper] dwSensitivity = 0x%X\n", rva);
        }
        else
        {
            // Shorter fallback
            auto p2 = ParsePattern("48 8D 0D ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? 66 0F 6E");
            match = FindPattern(data, sz, p2);
            if (match >= 0)
            {
                DWORD rva = ResolveRIPRelative(mod, match, 3, 7);
                clientMap["dwSensitivity"] = rva;
                printf("[Dumper] dwSensitivity = 0x%X (fallback)\n", rva);
            }
            else
                printf("[Dumper] MISS: dwSensitivity\n");
        }
    }

    // dwSensitivity_sensitivity: 48 8D 7E u1 48 0F BA E0 ? 72 ? 85 D2 49 0F 4F FF
    // u1 at offset 3 = single byte value
    {
        auto p = ParsePattern("48 8D 7E ?? 48 0F BA E0 ?? 72 ?? 85 D2 49 0F 4F FF");
        int match = FindPattern(data, sz, p);
        if (match >= 0)
        {
            DWORD val = (DWORD)data[match + 3];
            clientMap["dwSensitivity_sensitivity"] = val;
            printf("[Dumper] dwSensitivity_sensitivity = 0x%X\n", val);
        }
        else printf("[Dumper] MISS: dwSensitivity_sensitivity\n");
    }

    return true;
}

bool Dumper::ScanInputSystemOffsets(ModuleInfo& mod)
{
    auto& isMap = m_offsets["inputsystem.dll"];
    const BYTE* data = mod.data.data();
    size_t sz = mod.data.size();

    // dwInputSystem: 48 89 05 ${'} 33 C0
    auto p = ParsePattern("48 89 05 ?? ?? ?? ?? 33 C0");
    int match = FindPattern(data, sz, p);
    if (match >= 0)
    {
        DWORD rva = ResolveRIPRelative(mod, match, 3, 7);
        isMap["dwInputSystem"] = rva;
        printf("[Dumper] dwInputSystem = 0x%X\n", rva);
    }
    else printf("[Dumper] MISS: dwInputSystem\n");

    return true;
}

// ============================================================
// Phase 2: Button linked list walking
// ============================================================

bool Dumper::ScanButtons(ModuleInfo& client)
{
    auto& btnMap = m_buttons;
    const BYTE* data = client.data.data();
    size_t sz = client.data.size();

    // Button list head: 48 8B 15 ${'} 48 85 D2 74 ? 48 8B 02 48 85 C0
    auto p = ParsePattern("48 8B 15 ?? ?? ?? ?? 48 85 D2 74 ?? 48 8B 02 48 85 C0");
    int match = FindPattern(data, sz, p);
    if (match < 0)
    {
        printf("[Dumper] MISS: button list head pattern\n");
        return false;
    }

    DWORD listHeadRva = ResolveRIPRelative(client, match, 3, 7);
    printf("[Dumper] Button list head RVA = 0x%X\n", listHeadRva);

    // Read the pointer at client.dll + listHeadRva
    DWORD64 listHeadPtr = 0;
    memoryManager.ReadMemory(client.base + listHeadRva, listHeadPtr);

    if (listHeadPtr == 0)
    {
        printf("[Dumper] Button list head is null\n");
        return false;
    }

    // Walk the linked list
    // KeyButton layout: name@0x08 (ptr to string), state@0x30 (u32), next@0x88 (ptr to next)
    DWORD64 current = listHeadPtr;
    int count = 0;
    while (current != 0 && count < 200)
    {
        // Read name pointer
        DWORD64 namePtr = 0;
        memoryManager.ReadMemory(current + 0x08, namePtr);

        if (namePtr != 0)
        {
            char nameBuf[64] = {};
            memoryManager.ReadMemory(current + 0x08, namePtr);
            // Read string from namePtr
            for (int i = 0; i < 63; i++)
            {
                BYTE ch = 0;
                memoryManager.ReadMemory(namePtr + i, ch);
                nameBuf[i] = (char)ch;
                if (ch == 0) break;
            }

            if (nameBuf[0] != '\0')
            {
                // State RVA = (current + 0x30) - client.base
                DWORD stateRva = (DWORD)((current + 0x30) - client.base);
                btnMap[nameBuf] = stateRva;
                printf("[Dumper] Button '%s' state RVA = 0x%X\n", nameBuf, stateRva);
            }
        }

        // Read next pointer
        DWORD64 next = 0;
        memoryManager.ReadMemory(current + 0x88, next);
        current = next;
        count++;
    }

    printf("[Dumper] Found %d buttons\n", count);
    return !btnMap.empty();
}

// ============================================================
// Phase 3: Schema system traversal
// ============================================================

static const char* g_neededClasses[] = {
    "CCSPlayerController",
    "CBasePlayerController",
    "C_CSPlayerPawn",
    "C_BasePlayerPawn",
    "C_BaseEntity",
    "CSkeletonInstance",
    "CCSPlayerBase_CameraServices",
    "CCSPlayer_BulletServices",
    "C_CSPlayerPawnBase",
    "EntitySpottedState_t",
    "CPlayer_ObserverServices",
    "C_EconEntity",
    "C_BasePlayerWeapon",
    "CCSWeaponBaseVData",
    "CBasePlayerWeaponVData",
    "C_AttributeContainer",
    "C_EconItemView",
    "CPlayer_WeaponServices",
    "C_PlantedC4",
    "CCSPlayer_AimPunchServices",
    nullptr
};

static bool IsNeededClass(const char* name)
{
    for (int i = 0; g_neededClasses[i]; i++)
        if (strcmp(name, g_neededClasses[i]) == 0) return true;
    return false;
}

bool Dumper::ScanSchemas()
{
    ModuleInfo schemasystem;
    if (!LoadModule(L"schemasystem.dll", schemasystem))
    {
        printf("[Dumper] ERROR: Failed to load schemasystem.dll\n");
        return false;
    }

    return ReadSchemaSystem(schemasystem);
}

bool Dumper::ReadSchemaSystem(ModuleInfo& schemasystem)
{
    const BYTE* data = schemasystem.data.data();
    size_t sz = schemasystem.data.size();

    // SchemaSystem pattern: 4C 8D 35 ${'} 0F 28 45
    auto p = ParsePattern("4C 8D 35 ?? ?? ?? ?? 0F 28 45");
    int match = FindPattern(data, sz, p);
    if (match < 0)
    {
        printf("[Dumper] MISS: SchemaSystem pattern\n");
        return false;
    }

    DWORD schemaRva = ResolveRIPRelative(schemasystem, match, 3, 7);
    DWORD64 schemaAddr = schemasystem.base + schemaRva;
    printf("[Dumper] SchemaSystem at 0x%llX (RVA 0x%X)\n", schemaAddr, schemaRva);

    // SchemaSystem layout:
    // 0x0190: UtlVector<Pointer64<TypeScope>> type_scopes
    //   UtlVector: count@0x00 (i32), data@0x08 (ptr to array)
    // 0x0280: registration_count (i32)

    int32_t regCount = 0;
    memoryManager.ReadMemory(schemaAddr + 0x0280, regCount);
    printf("[Dumper] Schema registration count: %d\n", regCount);

    if (regCount == 0)
    {
        printf("[Dumper] No schema registrations\n");
        return false;
    }

    int32_t scopeCount = 0;
    memoryManager.ReadMemory(schemaAddr + 0x0190, scopeCount);

    DWORD64 scopeDataPtr = 0;
    memoryManager.ReadMemory(schemaAddr + 0x0198, scopeDataPtr);

    printf("[Dumper] TypeScope count: %d, data ptr: 0x%llX\n", scopeCount, scopeDataPtr);

    if (scopeCount <= 0 || scopeDataPtr == 0) return false;

    for (int i = 0; i < scopeCount && i < 32; i++)
    {
        DWORD64 typeScopePtr = 0;
        memoryManager.ReadMemory(scopeDataPtr + i * 8, typeScopePtr);
        if (typeScopePtr == 0) continue;

        // TypeScope layout:
        // 0x0008: name (char[256])
        // 0x0560: class_bindings (UtlTsHash<SchemaClassBinding>)
        char scopeName[256] = {};
        for (int c = 0; c < 255; c++)
        {
            BYTE ch = 0;
            memoryManager.ReadMemory(typeScopePtr + 0x08 + c, ch);
            scopeName[c] = (char)ch;
            if (ch == 0) break;
        }
        printf("[Dumper] TypeScope[%d]: '%s'\n", i, scopeName);

        // UtlTsHash at offset 0x0560:
        // 0x0000: UtlMemoryPool entry_mem (0x60 bytes)
        //   0x000C: blocks_allocated (i32)
        //   0x0010: peak_allocated (i32)
        //   0x0020: free_blocks (TsListBase -> head -> next ptr)
        // 0x0060: buckets[256] (each 0x18: add_lock@0, first@0x08, first_uncommitted@0x10)

        DWORD64 hashBase = typeScopePtr + 0x0560;

        int32_t blocksAllocated = 0;
        memoryManager.ReadMemory(hashBase + 0x0C, blocksAllocated);

        int32_t peakAllocated = 0;
        memoryManager.ReadMemory(hashBase + 0x10, peakAllocated);

        printf("[Dumper]   blocks_allocated=%d, peak_allocated=%d\n", blocksAllocated, peakAllocated);

        // Walk bucket linked lists (first_uncommitted chains)
        // Each bucket at hashBase + 0x60 + i*0x18
        // UtlTsHashFixedData: ui_key@0x00, next@0x08, data@0x10

        std::vector<DWORD64> classPtrs;

        for (int b = 0; b < 256; b++)
        {
            DWORD64 bucketBase = hashBase + 0x60 + b * 0x18;
            DWORD64 nodePtr = 0;
            memoryManager.ReadMemory(bucketBase + 0x10, nodePtr); // first_uncommitted

            int safety = 0;
            while (nodePtr != 0 && safety < 512)
            {
                DWORD64 dataPtr = 0;
                memoryManager.ReadMemory(nodePtr + 0x10, dataPtr);

                if (dataPtr != 0)
                    classPtrs.push_back(dataPtr);

                DWORD64 nextNode = 0;
                memoryManager.ReadMemory(nodePtr + 0x08, nextNode);
                nodePtr = nextNode;
                safety++;

                if ((int)classPtrs.size() >= blocksAllocated) break;
            }
            if ((int)classPtrs.size() >= blocksAllocated) break;
        }

        // Also walk free list: UtlTsHashAllocatedBlob
        // free_blocks at hashBase + 0x20 -> TsListBase -> head -> next (ptr)
        // TsListBase: head@0x00 -> TsListHead: next@0x00
        {
            DWORD64 freePtr = 0;
            memoryManager.ReadMemory(hashBase + 0x20, freePtr);

            int safety = 0;
            while (freePtr != 0 && safety < 4096)
            {
                // UtlTsHashAllocatedBlob: next@0x00, data@0x10
                DWORD64 blobData = 0;
                memoryManager.ReadMemory(freePtr + 0x10, blobData);

                if (blobData != 0)
                    classPtrs.push_back(blobData);

                DWORD64 nextBlob = 0;
                memoryManager.ReadMemory(freePtr + 0x00, nextBlob);
                freePtr = nextBlob;
                safety++;

                if ((int)classPtrs.size() >= blocksAllocated + peakAllocated) break;
            }
        }

        printf("[Dumper]   Found %zu class binding pointers\n", classPtrs.size());

        // Read each class binding
        // SchemaClassInfoData layout (build 14154):
        // 0x0008: name (ptr to string)
        // 0x0024: field_count (i16)     // was 0x1C pre-14154
        // 0x0030: fields (ptr to SchemaClassFieldData array)  // was 0x28
        for (DWORD64 classPtr : classPtrs)
        {
            DWORD64 namePtr = 0;
            memoryManager.ReadMemory(classPtr + 0x08, namePtr);
            if (namePtr == 0) continue;

            char className[128] = {};
            for (int c = 0; c < 127; c++)
            {
                BYTE ch = 0;
                memoryManager.ReadMemory(namePtr + c, ch);
                className[c] = (char)ch;
                if (ch == 0) break;
            }

            if (className[0] == '\0') continue;
            if (!IsNeededClass(className)) continue;

            int16_t fieldCount = 0;
            memoryManager.ReadMemory(classPtr + 0x24, fieldCount);

            DWORD64 fieldsPtr = 0;
            memoryManager.ReadMemory(classPtr + 0x30, fieldsPtr);

            if (fieldCount <= 0 || fieldsPtr == 0) continue;

            printf("[Dumper]   Class '%s' (%d fields)\n", className, fieldCount);

            // SchemaClassFieldData: name@0x00 (ptr), offset@0x10 (i32), size=0x20 per entry
            auto& fieldMap = m_schemaFields[className];

            for (int f = 0; f < fieldCount && f < 512; f++)
            {
                DWORD64 fieldBase = fieldsPtr + f * 0x20;

                DWORD64 fieldNamePtr = 0;
                memoryManager.ReadMemory(fieldBase + 0x00, fieldNamePtr);
                if (fieldNamePtr == 0) continue;

                char fieldName[128] = {};
                for (int c = 0; c < 127; c++)
                {
                    BYTE ch = 0;
                    memoryManager.ReadMemory(fieldNamePtr + c, ch);
                    fieldName[c] = (char)ch;
                    if (ch == 0) break;
                }

                int32_t fieldOffset = 0;
                memoryManager.ReadMemory(fieldBase + 0x10, fieldOffset);

                fieldMap[fieldName] = (DWORD)fieldOffset;
                printf("[Dumper]     %s.%s = 0x%X\n", className, fieldName, fieldOffset);
            }
        }
    }

    return true;
}
