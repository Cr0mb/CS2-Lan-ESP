#pragma once
#include <string>
#include <vector>
#include <map>
#include "../Game/Entity.h"
#include "../Core/Config.h"
#include "../OS-ImGui/imgui/imgui.h"

namespace GrenadeHelper
{
    // Grenade lineup data structure
    struct Lineup
    {
        Vec3 standPos;           // Where to stand
        Vec2 aimAngles;          // {pitch, yaw} where to aim
        std::string label;       // Name/description (e.g., "CT", "Stairs")
        std::string throwType;   // "Throw", "Jumpthrow", "Runthrow", etc.
        int runDuration;         // Seconds to run (for runthrows)
        std::string duckStatus;  // "on", "off", or ""
        int angleOffset;         // Additional offset
        Vec3 targetPos;          // Where grenade will land (optional)
        bool isCustom;           // User-created vs imported from default file
    };

    // Map data - organized by grenade type
    struct MapData
    {
        std::string mapName;
        std::map<std::string, std::vector<Lineup>> lineups; // "molotov", "smoke", "he", "flash"
    };
    
    // Editor state
    struct EditorState
    {
        bool isCreating;
        bool isEditing;
        int editingMapIndex;
        int editingGrenadeIndex;
        int editingLineupIndex;
        
        // Lineup being edited/created
        Lineup workingLineup;
        std::string selectedMap;
        std::string selectedGrenadeType;
        char lineupName[256];
        int selectedActionIndex;
        
        // UI state
        int selectedListMapIndex;
        int selectedListGrenadeIndex;
        int selectedListLineupIndex;
    };

    // Initialize and load grenade data
    bool Initialize();
    
    // Load lineups from CSV data file
    bool LoadLineupData(const std::string& filePath);
    
    // Save custom lineups to file
    bool SaveCustomLineups(const std::string& filePath);
    
    // Get current equipped grenade type
    std::string GetEquippedGrenadeType(const CEntity& localPlayer);
    
    // Find closest lineup to player position for current grenade
    const Lineup* FindClosestLineup(const CEntity& localPlayer, const std::string& grenadeType, float& outDistance);
    
    // Render helper visuals
    void Render(const CEntity& localPlayer);
    
    // Draw circle on ground at position
    void DrawGroundCircle(const Vec3& worldPos, float radius, ImColor color, float thickness = 2.0f, bool filled = false);
    
    // Draw aim line from player to target angle
    void DrawAimLine(const CEntity& localPlayer, const Vec2& aimAngles);
    
    // Get current map name
    std::string GetCurrentMapName();
    
    // UI Functions
    void RenderEditorUI(const CEntity& localPlayer);
    void RecordCurrentPosition(const CEntity& localPlayer);
    void RecordTargetPosition(const CEntity& localPlayer);
    void SaveCurrentLineup();
    void DeleteLineup(const std::string& mapName, const std::string& grenadeType, int index);
    void StartCreatingLineup();
    void StartEditingLineup(const std::string& mapName, const std::string& grenadeType, int index);
    void CancelEditing();
    
    // Global data storage
    extern std::map<std::string, MapData> g_mapLineups;
    extern bool g_initialized;
    extern EditorState g_editorState;
    extern bool g_showEditor;
    extern bool g_editorWasOpen; // Track if editor was open before menu closed
}
