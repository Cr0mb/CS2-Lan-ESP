#pragma once
#include <Windows.h>
#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include "MemoryMgr.h"

using OffsetMap = std::map<std::string, std::map<std::string, DWORD>>;
using ButtonMap = std::map<std::string, DWORD>;
using SchemaFieldMap = std::map<std::string, std::map<std::string, DWORD>>;

class Dumper
{
public:
    bool Run(DWORD processID);

    OffsetMap& GetOffsets() { return m_offsets; }
    ButtonMap& GetButtons() { return m_buttons; }
    SchemaFieldMap& GetSchemaFields() { return m_schemaFields; }

    // Re-run only the matchmaking.dll scan. matchmaking.dll loads on first
    // matchmaking action (joining a queue / lobby) so it is often missing
    // when the dumper first runs. Returns true on success.
    bool RescanMatchmaking();

private:
    DWORD m_processID = 0;

    OffsetMap m_offsets;
    ButtonMap m_buttons;
    SchemaFieldMap m_schemaFields;

    struct ModuleInfo {
        DWORD64 base = 0;
        DWORD size = 0;
        std::vector<BYTE> data;
    };

    bool LoadModule(const wchar_t* name, ModuleInfo& info);
    bool ReadModuleMemory(DWORD64 base, DWORD size, std::vector<BYTE>& out);

    struct PatternByte { BYTE value; bool wildcard; };
    static std::vector<PatternByte> ParsePattern(const std::string& pattern);
    int FindPattern(const BYTE* data, size_t dataSize, const std::vector<PatternByte>& pattern, size_t startOffset = 0);

    DWORD ResolveRIPRelative(const ModuleInfo& mod, int matchOffset, int ripOffset, int instrLen);

    bool ScanOffsets();
    bool ScanClientOffsets(ModuleInfo& client);
    bool ScanInputSystemOffsets(ModuleInfo& inputsystem);
    bool ScanMatchmakingOffsets(ModuleInfo& matchmaking);
    bool ScanButtons(ModuleInfo& client);
    bool ScanSchemas();
    bool ReadSchemaSystem(ModuleInfo& schemasystem);
};

inline Dumper g_Dumper;
