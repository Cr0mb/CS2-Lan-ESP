#define _USE_MATH_DEFINES
#include "GrenadeHelper.h"
#include "../Core/Cheats.h"
#include "../Offsets/Offsets.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <filesystem>
#include <cmath>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace GrenadeHelper
{
    std::map<std::string, MapData> g_mapLineups;
    bool g_initialized = false;
    EditorState g_editorState = {};
    bool g_showEditor = false;
    bool g_editorWasOpen = false;
    
    // Distance threshold to show aim line (in units)
    constexpr float PROXIMITY_THRESHOLD = 50.0f;
    
    // Circle radius for stand position
    constexpr float STAND_CIRCLE_RADIUS = 30.0f;
    
    // Action type options for dropdown
    const char* ACTION_TYPES[] = {
        "Throw",
        "Jumpthrow",
        "Crouch + Throw",
        "Run + Throw",
        "Crouch + Jumpthrow",
        "Run + Jumpthrow",
        "Walk + Throw"
    };

    bool Initialize()
    {
        if (g_initialized)
            return true;
        
        // Get exe directory
        char exePath[MAX_PATH];
        GetModuleFileNameA(NULL, exePath, MAX_PATH);
        std::filesystem::path exeDir = std::filesystem::path(exePath).parent_path();
        
        // Try to load GrenadeHelperNEW.data from exe directory
        std::string dataPath = (exeDir / "GrenadeHelperNEW.data").string();
        
        if (!std::filesystem::exists(dataPath))
        {
            printf("[GrenadeHelper] Data file not found: %s\n", dataPath.c_str());
            return false;
        }
        
        if (LoadLineupData(dataPath))
        {
            g_initialized = true;
            printf("[GrenadeHelper] Loaded lineup data from: %s\n", dataPath.c_str());
            
            // Also try to load custom lineups
            std::string customPath = (exeDir / "GrenadeHelperCustom.data").string();
            if (std::filesystem::exists(customPath))
            {
                LoadLineupData(customPath);
                printf("[GrenadeHelper] Loaded custom lineups from: %s\n", customPath.c_str());
            }
            
            return true;
        }
        
        return false;
    }

    // Helper function to split CSV line
    std::vector<std::string> SplitCSV(const std::string& line)
    {
        std::vector<std::string> tokens;
        std::string token;
        bool inQuotes = false;
        
        for (size_t i = 0; i < line.length(); i++)
        {
            char c = line[i];
            
            if (c == '"')
            {
                inQuotes = !inQuotes;
            }
            else if (c == ',' && !inQuotes)
            {
                tokens.push_back(token);
                token.clear();
            }
            else
            {
                token += c;
            }
        }
        
        if (!token.empty())
            tokens.push_back(token);
        
        return tokens;
    }
    
    // Helper function to extract grenade type from action string
    std::string ExtractGrenadeType(const std::string& action)
    {
        std::string lowerAction = action;
        std::transform(lowerAction.begin(), lowerAction.end(), lowerAction.begin(), ::tolower);
        
        // Check for grenade type keywords in action string
        if (lowerAction.find("smoke") != std::string::npos)
            return "smoke";
        if (lowerAction.find("flash") != std::string::npos)
            return "flash";
        if (lowerAction.find("molo") != std::string::npos || lowerAction.find("molotov") != std::string::npos || 
            lowerAction.find("fire") != std::string::npos || lowerAction.find("incendiary") != std::string::npos)
            return "molotov";
        if (lowerAction.find("he") != std::string::npos || lowerAction.find("grenade") != std::string::npos)
            return "he";
        
        // Default to smoke if no specific type found
        return "smoke";
    }
    
    // CSV parser for the new data format
    // Format: StartX,StartY,StartZ,StartName,EndX,EndY,EndZ,Action,Map,Value
    bool LoadLineupData(const std::string& filePath)
    {
        std::ifstream file(filePath);
        if (!file.is_open())
        {
            printf("[GrenadeHelper] Failed to open: %s\n", filePath.c_str());
            return false;
        }
        
        // Check if this is a custom file
        bool isCustomFile = filePath.find("GrenadeHelperCustom") != std::string::npos;
        
        std::string line;
        int lineNumber = 0;
        int lineupsFound = 0;
        
        while (std::getline(file, line))
        {
            lineNumber++;
            
            // Trim whitespace
            size_t start = line.find_first_not_of(" \t\r\n");
            if (start == std::string::npos)
                continue;
            line = line.substr(start);
            
            size_t end = line.find_last_not_of(" \t\r\n");
            if (end != std::string::npos)
                line = line.substr(0, end + 1);
            
            if (line.empty())
                continue;
            
            try
            {
                // Split CSV line
                std::vector<std::string> tokens = SplitCSV(line);
                
                // Validate we have all required fields
                // Format: StartX,StartY,StartZ,StartName,EndX,EndY,EndZ,Action,Map,Value
                if (tokens.size() < 10)
                {
                    printf("[GrenadeHelper] Line %d: Insufficient fields (%zu), skipping\n", lineNumber, tokens.size());
                    continue;
                }
                
                Lineup lineup;
                
                // Parse start position (where to stand)
                lineup.standPos.x = std::stof(tokens[0]);
                lineup.standPos.y = std::stof(tokens[1]);
                lineup.standPos.z = std::stof(tokens[2]);
                
                // Parse label (StartName)
                lineup.label = tokens[3];
                
                // Parse target position (where grenade lands)
                lineup.targetPos.x = std::stof(tokens[4]);
                lineup.targetPos.y = std::stof(tokens[5]);
                lineup.targetPos.z = std::stof(tokens[6]);
                
                // Parse action (throw type)
                lineup.throwType = tokens[7];
                
                // Parse map name
                std::string mapName = tokens[8];
                
                // Parse value (used for various purposes, we'll use as run duration)
                int value = std::stoi(tokens[9]);
                lineup.runDuration = 0;
                lineup.angleOffset = 0;
                lineup.duckStatus = "";
                
                // Determine throw type details from action string
                std::string lowerAction = lineup.throwType;
                std::transform(lowerAction.begin(), lowerAction.end(), lowerAction.begin(), ::tolower);
                
                if (lowerAction.find("crouch") != std::string::npos || lowerAction.find("ctrl") != std::string::npos)
                {
                    lineup.duckStatus = "on";
                }
                
                if (lowerAction.find("run") != std::string::npos || lowerAction.find("w+") != std::string::npos)
                {
                    lineup.runDuration = value > 0 ? value : 1;
                }
                
                // Calculate aim angles from start position to target position
                // This is a simple calculation - aim toward the target
                float dx = lineup.targetPos.x - lineup.standPos.x;
                float dy = lineup.targetPos.y - lineup.standPos.y;
                float dz = lineup.targetPos.z - lineup.standPos.z;
                
                // Calculate yaw (horizontal angle)
                float yaw = atan2f(dy, dx) * 180.0f / (float)M_PI;
                
                // Calculate pitch (vertical angle)
                float horizontalDistance = sqrtf(dx * dx + dy * dy);
                float pitch = -atan2f(dz, horizontalDistance) * 180.0f / (float)M_PI;
                
                lineup.aimAngles = Vec2(pitch, yaw);
                
                // Mark as custom or default
                lineup.isCustom = isCustomFile;
                
                // Extract grenade type from action string
                std::string grenadeType = ExtractGrenadeType(lineup.throwType);
                
                // Add to map data
                g_mapLineups[mapName].mapName = mapName;
                g_mapLineups[mapName].lineups[grenadeType].push_back(lineup);
                
                lineupsFound++;
            }
            catch (const std::exception& e)
            {
                printf("[GrenadeHelper] Line %d: Parse error: %s\n", lineNumber, e.what());
                continue;
            }
        }
        
        file.close();
        
        printf("[GrenadeHelper] Loaded %zu maps, %d total lineups\n", g_mapLineups.size(), lineupsFound);
        for (const auto& mapPair : g_mapLineups) {
            printf("[GrenadeHelper]   %s: ", mapPair.first.c_str());
            for (const auto& grenadePair : mapPair.second.lineups) {
                printf("%s(%zu) ", grenadePair.first.c_str(), grenadePair.second.size());
            }
            printf("\n");
        }
        return !g_mapLineups.empty();
    }

    std::string GetCurrentMapName()
    {
        // Use the same map detection method as CheckCurrentMapName in Cheats.cpp
        DWORD64 matchmakingBase = memoryManager.GetModuleBase(L"matchmaking.dll");
        
        if (matchmakingBase != 0) {
            if (Offset.dwGameTypes == 0) Offset.TryResolveDeferred();
            if (Offset.dwGameTypes == 0) return "<unknown>";
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
                        
                        // More aggressive trim - remove all non-printable and whitespace from start/end
                        // Find first alphanumeric character
                        size_t start = 0;
                        while (start < mapName.length() && !std::isalnum(static_cast<unsigned char>(mapName[start]))) {
                            start++;
                        }
                        
                        if (start < mapName.length()) {
                            mapName = mapName.substr(start);
                            
                            // Find last alphanumeric or underscore character
                            size_t end = mapName.length() - 1;
                            while (end > 0 && !std::isalnum(static_cast<unsigned char>(mapName[end])) && mapName[end] != '_') {
                                end--;
                            }
                            mapName = mapName.substr(0, end + 1);
                        }
                        
                        if (!mapName.empty() && mapName.length() >= 3) {
                            return mapName;
                        }
                    }
                }
            }
        }
        
        return "";
    }

    std::string GetEquippedGrenadeType(const CEntity& localPlayer)
    {
        // Read active weapon (handles 14154 schema without m_pClippingWeapon).
        DWORD64 activeWeaponAddress = ResolveActiveWeapon(localPlayer.Pawn.Address);
        if (!activeWeaponAddress)
            return "";
        
        // Calculate weapon index address using the correct offsets
        DWORD64 weaponIndexAddress = activeWeaponAddress + Offset.EconEntity.AttributeManager +
            Offset.WeaponBaseData.Item + Offset.WeaponBaseData.ItemDefinitionIndex;
        
        // Read weapon index (short)
        short weaponIndex;
        if (!memoryManager.ReadMemory(weaponIndexAddress, weaponIndex) || weaponIndex == -1)
            return "";
        
        // Get weapon name from the map (same as Entity.cpp)
        auto it = CEntity::weaponNames.find(weaponIndex);
        std::string weapon = (it != CEntity::weaponNames.end()) ? it->second : "";
        
        // Map weapon names to grenade types
        if (weapon == "smokegrenade")
            return "smoke";
        if (weapon == "molotov" || weapon == "incgrenade")
            return "molotov";
        if (weapon == "hegrenade")
            return "he";
        if (weapon == "flashbang")
            return "flash";
        
        return "";
    }

    const Lineup* FindClosestLineup(const CEntity& localPlayer, const std::string& grenadeType, float& outDistance)
    {
        std::string mapName = GetCurrentMapName();
        if (mapName.empty() || grenadeType.empty())
            return nullptr;
        
        auto mapIt = g_mapLineups.find(mapName);
        if (mapIt == g_mapLineups.end())
            return nullptr;
        
        auto grenadeIt = mapIt->second.lineups.find(grenadeType);
        if (grenadeIt == mapIt->second.lineups.end() || grenadeIt->second.empty())
            return nullptr;
        
        const Lineup* closest = nullptr;
        float minDistance = FLT_MAX;
        Vec3 playerPos = localPlayer.Pawn.Pos;
        
        for (const auto& lineup : grenadeIt->second)
        {
            float distance = playerPos.DistanceTo(lineup.standPos);
            if (distance < minDistance)
            {
                minDistance = distance;
                closest = &lineup;
            }
        }
        
        outDistance = minDistance;
        return closest;
    }

    void DrawGroundCircle(const Vec3& worldPos, float radius, ImColor color, float thickness, bool filled)
    {
        constexpr int segments = 48; // Increased for smoother circles
        constexpr float angleStep = 2.0f * (float)M_PI / segments;
        
        ImVec2 screenPoints[segments];
        int validPoints = 0;
        
        for (int i = 0; i < segments; i++)
        {
            float angle = i * angleStep;
            Vec3 point(
                worldPos.x + radius * cosf(angle),
                worldPos.y + radius * sinf(angle),
                worldPos.z
            );
            
            Vec2 screen;
            if (gGame.View.WorldToScreen(point, screen))
            {
                screenPoints[validPoints++] = ImVec2(screen.x, screen.y);
            }
        }
        
        // Draw circle with enhanced styling
        if (validPoints > 2)
        {
            // Draw filled circle if requested
            if (filled && validPoints >= 3)
            {
                // Create semi-transparent fill
                ImColor fillColor = ImColor(color.Value.x, color.Value.y, color.Value.z, color.Value.w * 0.2f);
                
                // Draw filled polygon
                for (int i = 1; i < validPoints - 1; i++)
                {
                    ImGui::GetBackgroundDrawList()->AddTriangleFilled(
                        screenPoints[0],
                        screenPoints[i],
                        screenPoints[i + 1],
                        fillColor
                    );
                }
            }
            
            // Draw black outline for visibility
            for (int i = 0; i < validPoints - 1; i++)
            {
                ImGui::GetBackgroundDrawList()->AddLine(
                    screenPoints[i],
                    screenPoints[i + 1],
                    ImColor(0, 0, 0, 180),
                    thickness + 2.0f
                );
            }
            // Close the circle outline
            ImGui::GetBackgroundDrawList()->AddLine(
                screenPoints[validPoints - 1],
                screenPoints[0],
                ImColor(0, 0, 0, 180),
                thickness + 2.0f
            );
            
            // Draw colored circle
            for (int i = 0; i < validPoints - 1; i++)
            {
                ImGui::GetBackgroundDrawList()->AddLine(
                    screenPoints[i],
                    screenPoints[i + 1],
                    color,
                    thickness
                );
            }
            // Close the circle
            ImGui::GetBackgroundDrawList()->AddLine(
                screenPoints[validPoints - 1],
                screenPoints[0],
                color,
                thickness
            );
        }
    }

    void DrawAimLine(const CEntity& localPlayer, const Vec2& aimAngles)
    {
        // Calculate aim direction from angles
        float pitch = aimAngles.x * ((float)M_PI / 180.0f);
        float yaw = aimAngles.y * ((float)M_PI / 180.0f);
        
        Vec3 forward;
        forward.x = cosf(pitch) * cosf(yaw);
        forward.y = cosf(pitch) * sinf(yaw);
        forward.z = -sinf(pitch);
        
        // Create aim line from camera position
        Vec3 start = localPlayer.Pawn.CameraPos;
        Vec3 end = start + forward * 2000.0f;
        
        Vec2 screenEnd;
        if (gGame.View.WorldToScreen(end, screenEnd))
        {
            // Draw from screen center to aim point
            ImVec2 screenCenter(Gui.Window.Size.x / 2.0f, Gui.Window.Size.y / 2.0f);
            
            // GHaxLabs themed colors - cyan/teal gradient
            ImColor lineColorStart = ImColor(0, 255, 200, 255);
            ImColor lineColorEnd = ImColor(0, 200, 255, 255);
            
            // Draw black outline for visibility
            ImGui::GetBackgroundDrawList()->AddLine(
                screenCenter,
                ImVec2(screenEnd.x, screenEnd.y),
                ImColor(0, 0, 0, 200),
                6.0f
            );
            
            // Draw gradient effect with multiple lines
            for (int i = 0; i < 3; i++)
            {
                float t = i / 3.0f;
                ImColor gradientColor = ImColor(
                    (int)(lineColorStart.Value.x * 255 * (1 - t) + lineColorEnd.Value.x * 255 * t),
                    (int)(lineColorStart.Value.y * 255 * (1 - t) + lineColorEnd.Value.y * 255 * t),
                    (int)(lineColorStart.Value.z * 255 * (1 - t) + lineColorEnd.Value.z * 255 * t),
                    255
                );
                
                ImGui::GetBackgroundDrawList()->AddLine(
                    screenCenter,
                    ImVec2(screenEnd.x, screenEnd.y),
                    gradientColor,
                    3.5f - i * 0.5f
                );
            }
            
            // Draw enhanced crosshair at end with pulsing effect
            static float pulseTime = 0.0f;
            pulseTime += 0.05f;
            float pulse = 0.8f + 0.2f * sinf(pulseTime);
            
            float crossSize = 12.0f * pulse;
            float innerSize = 6.0f;
            
            // Draw outer cross (black outline)
            ImGui::GetBackgroundDrawList()->AddLine(
                ImVec2(screenEnd.x - crossSize, screenEnd.y),
                ImVec2(screenEnd.x + crossSize, screenEnd.y),
                ImColor(0, 0, 0, 220),
                5.0f
            );
            ImGui::GetBackgroundDrawList()->AddLine(
                ImVec2(screenEnd.x, screenEnd.y - crossSize),
                ImVec2(screenEnd.x, screenEnd.y + crossSize),
                ImColor(0, 0, 0, 220),
                5.0f
            );
            
            // Draw colored cross with gradient
            ImGui::GetBackgroundDrawList()->AddLine(
                ImVec2(screenEnd.x - crossSize, screenEnd.y),
                ImVec2(screenEnd.x + crossSize, screenEnd.y),
                lineColorStart,
                3.0f
            );
            ImGui::GetBackgroundDrawList()->AddLine(
                ImVec2(screenEnd.x, screenEnd.y - crossSize),
                ImVec2(screenEnd.x, screenEnd.y + crossSize),
                lineColorEnd,
                3.0f
            );
            
            // Draw center dot
            ImGui::GetBackgroundDrawList()->AddCircleFilled(
                ImVec2(screenEnd.x, screenEnd.y),
                3.0f,
                ImColor(255, 255, 255, 255)
            );
            ImGui::GetBackgroundDrawList()->AddCircle(
                ImVec2(screenEnd.x, screenEnd.y),
                innerSize,
                lineColorStart,
                16,
                2.0f
            );
        }
    }

    void Render(const CEntity& localPlayer)
    {
        if (!MiscCFG::GrenadeHelper || !g_initialized)
            return;
        
        if (!localPlayer.IsAlive())
            return;
        
        // Get equipped grenade type
        std::string grenadeType = GetEquippedGrenadeType(localPlayer);
        
        if (grenadeType.empty())
            return;
        
        std::string mapName = GetCurrentMapName();
        if (mapName.empty())
            return;
        
        auto mapIt = g_mapLineups.find(mapName);
        if (mapIt == g_mapLineups.end())
            return;
        
        auto grenadeIt = mapIt->second.lineups.find(grenadeType);
        if (grenadeIt == mapIt->second.lineups.end())
            return;
        
        Vec3 playerPos = localPlayer.Pawn.Pos;
        const Lineup* closestLineup = nullptr;
        float closestDistance = FLT_MAX;
        
        // Animation time for pulsing effects
        static float animTime = 0.0f;
        animTime += 0.03f;
        
        // Draw all lineup circles for this grenade type
        for (const auto& lineup : grenadeIt->second)
        {
            // Use 2D distance (ignore Z) so player doesn't need to jump
            float dx = playerPos.x - lineup.standPos.x;
            float dy = playerPos.y - lineup.standPos.y;
            float distance = sqrt(dx * dx + dy * dy);
            
            // Track closest
            if (distance < closestDistance)
            {
                closestDistance = distance;
                closestLineup = &lineup;
            }
            
            // Themed colors based on grenade type
            ImColor baseColor;
            if (grenadeType == "smoke") baseColor = ImColor(200, 200, 200, 255);
            else if (grenadeType == "flash") baseColor = ImColor(255, 255, 100, 255);
            else if (grenadeType == "he") baseColor = ImColor(255, 100, 100, 255);
            else if (grenadeType == "molotov") baseColor = ImColor(255, 150, 0, 255);
            else baseColor = ImColor(0, 255, 200, 255); // Default cyan
            
            // Calculate alpha based on distance with smooth falloff
            float alpha = 1.0f;
            bool isClose = distance < PROXIMITY_THRESHOLD;
            
            if (isClose)
            {
                // Pulsing effect when close
                float pulse = 0.7f + 0.3f * sinf(animTime * 2.0f);
                alpha = pulse;
                
                // Draw filled circle for closest lineup
                if (&lineup == closestLineup)
                {
                    ImColor glowColor = ImColor(
                        (int)(baseColor.Value.x * 255),
                        (int)(baseColor.Value.y * 255),
                        (int)(baseColor.Value.z * 255),
                        (int)(200 * alpha)
                    );
                    DrawGroundCircle(lineup.standPos, STAND_CIRCLE_RADIUS, glowColor, 3.5f, true);
                }
                else
                {
                    ImColor circleColor = ImColor(
                        (int)(baseColor.Value.x * 255),
                        (int)(baseColor.Value.y * 255),
                        (int)(baseColor.Value.z * 255),
                        (int)(180 * alpha)
                    );
                    DrawGroundCircle(lineup.standPos, STAND_CIRCLE_RADIUS, circleColor, 2.5f);
                }
            }
            else
            {
                // Dimmer when far
                ImColor distantColor = ImColor(
                    (int)(baseColor.Value.x * 255 * 0.6f),
                    (int)(baseColor.Value.y * 255 * 0.6f),
                    (int)(baseColor.Value.z * 255 * 0.6f),
                    120
                );
                DrawGroundCircle(lineup.standPos, STAND_CIRCLE_RADIUS, distantColor, 2.0f);
            }
        }
        
        // If player is close enough to any lineup, show aim line and text
        if (closestLineup && closestDistance < PROXIMITY_THRESHOLD)
        {
            // Draw aim line
            DrawAimLine(localPlayer, closestLineup->aimAngles);
            
            // Draw enhanced text instructions
            ImVec2 screenCenter(Gui.Window.Size.x / 2.0f, Gui.Window.Size.y / 2.0f);
            
            // Build instruction text
            std::string throwInstruction = closestLineup->throwType;
            std::string additionalInfo = "";
            
            if (closestLineup->duckStatus == "on")
                additionalInfo += " [CROUCH]";
            if (closestLineup->runDuration > 0)
                additionalInfo += " [RUN " + std::to_string(closestLineup->runDuration) + "s]";
            
            // Title text (lineup name)
            std::string titleText = closestLineup->label;
            std::string actionText = throwInstruction + additionalInfo;
            
            // Calculate text sizes and position
            ImFont* font = ImGui::GetFont();
            float titleFontSize = 20.0f;
            float actionFontSize = 16.0f;
            
            ImVec2 titleSize = font->CalcTextSizeA(titleFontSize, FLT_MAX, 0.0f, titleText.c_str());
            ImVec2 actionSize = font->CalcTextSizeA(actionFontSize, FLT_MAX, 0.0f, actionText.c_str());
            
            float maxWidth = (titleSize.x > actionSize.x) ? titleSize.x : actionSize.x;
            float totalHeight = titleSize.y + actionSize.y + 15;
            
            ImVec2 boxPos(screenCenter.x - maxWidth / 2.0f - 15, screenCenter.y + 280);
            ImVec2 boxSize(maxWidth + 30, totalHeight + 20);
            
            // Draw styled background with gradient
            ImGui::GetBackgroundDrawList()->AddRectFilled(
                boxPos,
                ImVec2(boxPos.x + boxSize.x, boxPos.y + boxSize.y),
                ImColor(10, 15, 25, 230),
                8.0f
            );
            
            // Draw border with grenade type color
            ImColor borderColor;
            if (grenadeType == "smoke") borderColor = ImColor(200, 200, 200, 255);
            else if (grenadeType == "flash") borderColor = ImColor(255, 255, 100, 255);
            else if (grenadeType == "he") borderColor = ImColor(255, 100, 100, 255);
            else if (grenadeType == "molotov") borderColor = ImColor(255, 150, 0, 255);
            else borderColor = ImColor(0, 255, 200, 255);
            
            ImGui::GetBackgroundDrawList()->AddRect(
                boxPos,
                ImVec2(boxPos.x + boxSize.x, boxPos.y + boxSize.y),
                borderColor,
                8.0f,
                0,
                3.0f
            );
            
            // Draw title text with shadow
            ImVec2 titlePos(boxPos.x + 15, boxPos.y + 10);
            ImGui::GetBackgroundDrawList()->AddText(
                font,
                titleFontSize,
                ImVec2(titlePos.x + 2, titlePos.y + 2),
                ImColor(0, 0, 0, 180),
                titleText.c_str()
            );
            ImGui::GetBackgroundDrawList()->AddText(
                font,
                titleFontSize,
                titlePos,
                ImColor(255, 255, 255, 255),
                titleText.c_str()
            );
            
            // Draw action text with shadow
            ImVec2 actionPos(boxPos.x + 15, boxPos.y + 15 + titleSize.y + 5);
            ImGui::GetBackgroundDrawList()->AddText(
                font,
                actionFontSize,
                ImVec2(actionPos.x + 2, actionPos.y + 2),
                ImColor(0, 0, 0, 180),
                actionText.c_str()
            );
            ImGui::GetBackgroundDrawList()->AddText(
                font,
                actionFontSize,
                actionPos,
                borderColor,
                actionText.c_str()
            );
            
            // Draw distance indicator
            char distanceText[32];
            snprintf(distanceText, sizeof(distanceText), "%.1fm", closestDistance / 100.0f);
            ImVec2 distSize = font->CalcTextSizeA(14.0f, FLT_MAX, 0.0f, distanceText);
            ImVec2 distPos(boxPos.x + boxSize.x - distSize.x - 10, boxPos.y + 8);
            
            ImGui::GetBackgroundDrawList()->AddText(
                font,
                14.0f,
                distPos,
                ImColor(150, 150, 150, 255),
                distanceText
            );
        }
    }
    
    // Save custom lineups to CSV file
    bool SaveCustomLineups(const std::string& filePath)
    {
        std::ofstream file(filePath);
        if (!file.is_open())
        {
            printf("[GrenadeHelper] Failed to save custom lineups to: %s\n", filePath.c_str());
            return false;
        }
        
        int savedCount = 0;
        
        // Write custom lineups in CSV format
        for (const auto& mapPair : g_mapLineups)
        {
            const std::string& mapName = mapPair.first;
            const MapData& mapData = mapPair.second;
            
            for (const auto& grenadePair : mapData.lineups)
            {
                const std::string& grenadeType = grenadePair.first;
                
                for (const auto& lineup : grenadePair.second)
                {
                    // Only save custom lineups
                    if (!lineup.isCustom)
                        continue;
                    
                    // Format: StartX,StartY,StartZ,StartName,EndX,EndY,EndZ,Action,Map,Value
                    file << lineup.standPos.x << ","
                         << lineup.standPos.y << ","
                         << lineup.standPos.z << ","
                         << lineup.label << ","
                         << lineup.targetPos.x << ","
                         << lineup.targetPos.y << ","
                         << lineup.targetPos.z << ","
                         << lineup.throwType << ","
                         << mapName << ","
                         << 10 << "\n";
                    
                    savedCount++;
                }
            }
        }
        
        file.close();
        printf("[GrenadeHelper] Saved %d custom lineups to: %s\n", savedCount, filePath.c_str());
        return true;
    }
    
    void StartCreatingLineup()
    {
        g_editorState = {}; // Reset state
        g_editorState.isCreating = true;
        g_editorState.selectedActionIndex = 0;
        g_editorState.workingLineup = {};
        g_editorState.workingLineup.isCustom = true;
        strcpy_s(g_editorState.lineupName, "New Lineup");
        g_editorState.selectedMap = GetCurrentMapName();
        g_editorState.selectedGrenadeType = "";
    }
    
    void StartEditingLineup(const std::string& mapName, const std::string& grenadeType, int index)
    {
        auto mapIt = g_mapLineups.find(mapName);
        if (mapIt == g_mapLineups.end())
            return;
        
        auto grenadeIt = mapIt->second.lineups.find(grenadeType);
        if (grenadeIt == mapIt->second.lineups.end())
            return;
        
        if (index < 0 || index >= grenadeIt->second.size())
            return;
        
        g_editorState = {}; // Reset state
        g_editorState.isEditing = true;
        g_editorState.workingLineup = grenadeIt->second[index];
        g_editorState.selectedMap = mapName;
        g_editorState.selectedGrenadeType = grenadeType;
        strcpy_s(g_editorState.lineupName, g_editorState.workingLineup.label.c_str());
        
        // Find action index
        g_editorState.selectedActionIndex = 0;
        for (int i = 0; i < IM_ARRAYSIZE(ACTION_TYPES); i++)
        {
            if (g_editorState.workingLineup.throwType == ACTION_TYPES[i])
            {
                g_editorState.selectedActionIndex = i;
                break;
            }
        }
        
        // Store indices for deletion/update
        g_editorState.editingMapIndex = index;
    }
    
    void CancelEditing()
    {
        g_editorState = {};
    }
    
    void RecordCurrentPosition(const CEntity& localPlayer)
    {
        if (!localPlayer.IsAlive())
            return;
        
        Vec3 pos = localPlayer.Pawn.Pos;
        g_editorState.workingLineup.standPos = pos;
    }
    
    void RecordTargetPosition(const CEntity& localPlayer)
    {
        if (!localPlayer.IsAlive())
            return;
        
        // Get view angles
        Vec2 viewAngles = localPlayer.Pawn.ViewAngle;
        
        // Convert angles to direction vector and project forward
        float pitch = viewAngles.x * (float)M_PI / 180.0f;
        float yaw = viewAngles.y * (float)M_PI / 180.0f;
        
        // Calculate direction
        float cosPitch = cosf(pitch);
        Vec3 forward;
        forward.x = cosf(yaw) * cosPitch;
        forward.y = sinf(yaw) * cosPitch;
        forward.z = -sinf(pitch);
        
        // Project forward from player position (simulate grenade landing ~1000 units away)
        Vec3 playerPos = localPlayer.Pawn.Pos;
        float distance = 1000.0f;
        
        g_editorState.workingLineup.targetPos.x = playerPos.x + forward.x * distance;
        g_editorState.workingLineup.targetPos.y = playerPos.y + forward.y * distance;
        g_editorState.workingLineup.targetPos.z = playerPos.z + forward.z * distance;
        
        // Also store aim angles
        g_editorState.workingLineup.aimAngles = viewAngles;
    }
    
    void SaveCurrentLineup()
    {
        // Validate data
        if (strlen(g_editorState.lineupName) == 0)
        {
            printf("[GrenadeHelper] Cannot save lineup without a name!\n");
            return;
        }
        
        if (g_editorState.selectedMap.empty())
        {
            printf("[GrenadeHelper] Cannot save lineup without a map!\n");
            return;
        }
        
        if (g_editorState.selectedGrenadeType.empty())
        {
            printf("[GrenadeHelper] Cannot save lineup without a grenade type!\n");
            return;
        }
        
        // Update lineup data
        g_editorState.workingLineup.label = g_editorState.lineupName;
        g_editorState.workingLineup.throwType = ACTION_TYPES[g_editorState.selectedActionIndex];
        g_editorState.workingLineup.isCustom = true;
        
        // Calculate aim angles if not set
        if (g_editorState.workingLineup.aimAngles.x == 0 && g_editorState.workingLineup.aimAngles.y == 0)
        {
            float dx = g_editorState.workingLineup.targetPos.x - g_editorState.workingLineup.standPos.x;
            float dy = g_editorState.workingLineup.targetPos.y - g_editorState.workingLineup.standPos.y;
            float dz = g_editorState.workingLineup.targetPos.z - g_editorState.workingLineup.standPos.z;
            
            float yaw = atan2f(dy, dx) * 180.0f / (float)M_PI;
            float horizontalDistance = sqrtf(dx * dx + dy * dy);
            float pitch = -atan2f(dz, horizontalDistance) * 180.0f / (float)M_PI;
            
            g_editorState.workingLineup.aimAngles = Vec2(pitch, yaw);
        }
        
        // If editing, remove old lineup
        if (g_editorState.isEditing)
        {
            auto& lineups = g_mapLineups[g_editorState.selectedMap].lineups[g_editorState.selectedGrenadeType];
            for (auto it = lineups.begin(); it != lineups.end(); ++it)
            {
                if (it->label == g_editorState.lineupName && it->standPos.x == g_editorState.workingLineup.standPos.x)
                {
                    lineups.erase(it);
                    break;
                }
            }
        }
        
        // Add to map data
        g_mapLineups[g_editorState.selectedMap].mapName = g_editorState.selectedMap;
        g_mapLineups[g_editorState.selectedMap].lineups[g_editorState.selectedGrenadeType].push_back(g_editorState.workingLineup);
        
        // Save to file
        char exePath[MAX_PATH];
        GetModuleFileNameA(NULL, exePath, MAX_PATH);
        std::filesystem::path exeDir = std::filesystem::path(exePath).parent_path();
        std::string customPath = (exeDir / "GrenadeHelperCustom.data").string();
        SaveCustomLineups(customPath);
        
        printf("[GrenadeHelper] Saved lineup: %s (%s - %s)\n", 
               g_editorState.lineupName, 
               g_editorState.selectedMap.c_str(),
               g_editorState.selectedGrenadeType.c_str());
        
        // Reset editor state
        CancelEditing();
    }
    
    void DeleteLineup(const std::string& mapName, const std::string& grenadeType, int index)
    {
        auto mapIt = g_mapLineups.find(mapName);
        if (mapIt == g_mapLineups.end())
            return;
        
        auto grenadeIt = mapIt->second.lineups.find(grenadeType);
        if (grenadeIt == mapIt->second.lineups.end())
            return;
        
        if (index < 0 || index >= grenadeIt->second.size())
            return;
        
        grenadeIt->second.erase(grenadeIt->second.begin() + index);
        
        // Save to file
        char exePath[MAX_PATH];
        GetModuleFileNameA(NULL, exePath, MAX_PATH);
        std::filesystem::path exeDir = std::filesystem::path(exePath).parent_path();
        std::string customPath = (exeDir / "GrenadeHelperCustom.data").string();
        SaveCustomLineups(customPath);
        
        printf("[GrenadeHelper] Deleted lineup from %s - %s\n", mapName.c_str(), grenadeType.c_str());
    }
    
    void RenderEditorUI(const CEntity& localPlayer)
    {
        // Track editor state and sync with main menu
        static bool lastMenuState = false;
        
        if (!MenuConfig::ShowMenu)
        {
            // Menu just closed - remember if editor was open
            if (lastMenuState && g_showEditor)
            {
                g_editorWasOpen = true;
            }
            lastMenuState = false;
            return;
        }
        else
        {
            // Menu just opened - restore editor if it was open before
            if (!lastMenuState && g_editorWasOpen)
            {
                g_showEditor = true;
                g_editorWasOpen = false;
            }
            lastMenuState = true;
        }
        
        if (!g_showEditor)
            return;
        
        // Get current map for auto-fill
        std::string currentMap = GetCurrentMapName();
        std::string currentGrenadeType = GetEquippedGrenadeType(localPlayer);
        
        // GHaxLabs theme colors
        ImColor accentColor = ImColor(ImGui::GetStyleColorVec4(ImGuiCol_Border));
        ImColor headerColor = ImColor(59, 71, 148, 180);
        ImColor successColor = ImColor(0, 255, 100, 255);
        ImColor warningColor = ImColor(255, 200, 0, 255);
        ImColor errorColor = ImColor(255, 100, 100, 255);
        ImColor customTagColor = ImColor(32, 178, 170, 255);
        
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(15, 15));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 8));
        ImGui::SetNextWindowSize(ImVec2(450, 650), ImGuiCond_FirstUseEver);
        
        if (ImGui::Begin("Grenade Helper Editor", &g_showEditor, ImGuiWindowFlags_NoCollapse))
        {
            // Check if creating or editing
            if (g_editorState.isCreating || g_editorState.isEditing)
            {
                ImGui::GradientText(g_editorState.isCreating ? "Creating New Lineup" : "Editing Lineup");
                ImGui::Separator();
                ImGui::Spacing();
                
                // Lineup Name with styled label
                ImGui::TextColored(accentColor, "Lineup Name:");
                ImGui::SetNextItemWidth(-1);
                ImGui::InputText("##lineupname", g_editorState.lineupName, IM_ARRAYSIZE(g_editorState.lineupName));
                
                // Map Name (read-only, auto-detected) with styled display
                ImGui::Spacing();
                ImGui::TextColored(accentColor, "Map:");
                ImGui::SameLine();
                if (currentMap.empty())
                    ImGui::TextColored(warningColor, "Unknown - Join a game!");
                else
                    ImGui::TextColored(successColor, "%s", currentMap.c_str());
                g_editorState.selectedMap = currentMap;
                
                // Grenade Type Dropdown
                ImGui::Spacing();
                ImGui::TextColored(accentColor, "Grenade Type:");
                const char* grenadeTypes[] = { "smoke", "flash", "he", "molotov" };
                
                // Determine current selection index
                int selectedGrenadeIdx = 0;
                if (!g_editorState.selectedGrenadeType.empty())
                {
                    // Use stored selection
                    for (int i = 0; i < IM_ARRAYSIZE(grenadeTypes); i++)
                    {
                        if (g_editorState.selectedGrenadeType == grenadeTypes[i])
                        {
                            selectedGrenadeIdx = i;
                            break;
                        }
                    }
                }
                else if (!currentGrenadeType.empty())
                {
                    // Auto-select if holding grenade and no selection yet
                    for (int i = 0; i < IM_ARRAYSIZE(grenadeTypes); i++)
                    {
                        if (currentGrenadeType == grenadeTypes[i])
                        {
                            selectedGrenadeIdx = i;
                            g_editorState.selectedGrenadeType = grenadeTypes[i];
                            break;
                        }
                    }
                }
                
                ImGui::SetNextItemWidth(-1);
                if (ImGui::Combo("##grenadetype", &selectedGrenadeIdx, grenadeTypes, IM_ARRAYSIZE(grenadeTypes)))
                {
                    g_editorState.selectedGrenadeType = grenadeTypes[selectedGrenadeIdx];
                }
                
                // Action/Throw Type Dropdown
                ImGui::Spacing();
                ImGui::TextColored(accentColor, "Throw Type:");
                ImGui::SetNextItemWidth(-1);
                ImGui::Combo("##actiontype", &g_editorState.selectedActionIndex, ACTION_TYPES, IM_ARRAYSIZE(ACTION_TYPES));
                
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();
                
                // Position Recording section with gradient header
                ImGui::GradientText("Position Recording");
                ImGui::Spacing();
                
                // Stand Position with themed display
                ImGui::TextColored(accentColor, "Stand Position:");
                ImGui::Indent(10);
                ImGui::Text("X: %.2f  Y: %.2f  Z: %.2f", 
                           g_editorState.workingLineup.standPos.x,
                           g_editorState.workingLineup.standPos.y,
                           g_editorState.workingLineup.standPos.z);
                ImGui::Unindent(10);
                
                if (ImGui::Button("Record Current Position", ImVec2(-1, 32)))
                {
                    RecordCurrentPosition(localPlayer);
                }
                
                ImGui::Spacing();
                
                // Target Position with themed display
                ImGui::TextColored(accentColor, "Target Position:");
                ImGui::Indent(10);
                ImGui::Text("X: %.2f  Y: %.2f  Z: %.2f", 
                           g_editorState.workingLineup.targetPos.x,
                           g_editorState.workingLineup.targetPos.y,
                           g_editorState.workingLineup.targetPos.z);
                ImGui::Unindent(10);
                
                if (ImGui::Button("Record Aim Target", ImVec2(-1, 32)))
                {
                    RecordTargetPosition(localPlayer);
                }
                
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();
                
                // Save/Cancel Buttons with themed styling
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.1f, 0.5f, 0.2f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.2f, 0.6f, 0.3f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.05f, 0.4f, 0.15f, 1.0f));
                if (ImGui::Button("Save Lineup", ImVec2(-1, 35)))
                {
                    SaveCurrentLineup();
                }
                ImGui::PopStyleColor(3);
                
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.1f, 0.1f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.6f, 0.2f, 0.2f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.4f, 0.05f, 0.05f, 1.0f));
                if (ImGui::Button("Cancel", ImVec2(-1, 35)))
                {
                    CancelEditing();
                }
                ImGui::PopStyleColor(3);
            }
            else
            {
                // Browse mode with gradient header
                ImGui::GradientText("Browse Lineups");
                ImGui::Separator();
                ImGui::Spacing();
                
                // Create new button with themed styling
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.4f, 0.55f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.5f, 0.65f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.1f, 0.35f, 0.5f, 1.0f));
                if (ImGui::Button("+ Create New Lineup", ImVec2(-1, 35)))
                {
                    StartCreatingLineup();
                }
                ImGui::PopStyleColor(3);
                
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();
                
                // Filter by current map with styled display
                ImGui::TextColored(accentColor, "Current Map:");
                ImGui::SameLine();
                if (currentMap.empty())
                    ImGui::TextColored(warningColor, "Join a game to see lineups");
                else
                    ImGui::TextColored(successColor, "%s", currentMap.c_str());
                ImGui::Spacing();
                
                if (!currentMap.empty())
                {
                    auto mapIt = g_mapLineups.find(currentMap);
                    if (mapIt != g_mapLineups.end())
                    {
                        // Show lineups organized by grenade type with themed headers
                        for (auto& grenadePair : mapIt->second.lineups)
                        {
                            const std::string& grenadeType = grenadePair.first;
                            auto& lineups = grenadePair.second;
                            
                            if (lineups.empty())
                                continue;
                            
                            // Styled collapsing header with grenade type color coding
                            ImColor grenadeColor = accentColor;
                            if (grenadeType == "smoke") grenadeColor = ImColor(200, 200, 200, 255);
                            else if (grenadeType == "flash") grenadeColor = ImColor(255, 255, 100, 255);
                            else if (grenadeType == "he") grenadeColor = ImColor(255, 100, 100, 255);
                            else if (grenadeType == "molotov") grenadeColor = ImColor(255, 150, 0, 255);
                            
                            ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.15f, 0.25f, 0.35f, 0.9f));
                            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.2f, 0.3f, 0.4f, 1.0f));
                            ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.25f, 0.35f, 0.45f, 1.0f));
                            
                            std::string headerText = grenadeType + " (" + std::to_string(lineups.size()) + ")";
                            if (ImGui::CollapsingHeader(headerText.c_str()))
                            {
                                ImGui::Indent(10);
                                
                                for (int i = 0; i < lineups.size(); i++)
                                {
                                    const Lineup& lineup = lineups[i];
                                    
                                    ImGui::PushID(i);
                                    
                                    // Lineup name with custom indicator and distance info
                                    std::string displayName = lineup.label;
                                    
                                    if (lineup.isCustom)
                                        ImGui::TextColored(customTagColor, "[Custom]");
                                    else
                                        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "[Default]");
                                    ImGui::SameLine();
                                    ImGui::Text("%s", displayName.c_str());
                                    
                                    // Buttons on same line for custom lineups
                                    if (lineup.isCustom)
                                    {
                                        ImGui::SameLine();
                                        ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 150);
                                        
                                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.4f, 0.5f, 0.8f));
                                        if (ImGui::SmallButton("Edit"))
                                        {
                                            StartEditingLineup(currentMap, grenadeType, i);
                                        }
                                        ImGui::PopStyleColor();
                                        
                                        ImGui::SameLine();
                                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.2f, 0.2f, 0.8f));
                                        if (ImGui::SmallButton("Delete"))
                                        {
                                            DeleteLineup(currentMap, grenadeType, i);
                                        }
                                        ImGui::PopStyleColor();
                                    }
                                    
                                    ImGui::PopID();
                                }
                                
                                ImGui::Unindent(10);
                            }
                            
                            ImGui::PopStyleColor(3);
                        }
                    }
                    else
                    {
                        ImGui::Spacing();
                        ImGui::TextColored(errorColor, "No lineups for this map yet.");
                        ImGui::TextDisabled("Create your first lineup above!");
                    }
                }
            }
        }
        ImGui::End();
        ImGui::PopStyleVar(2);
    }
}
