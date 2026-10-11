#pragma once

#include "../Vischeck/VisCheck.h"
#include "../Game/Entity.h"
#include <memory>
#include <string>
#include <vector>

class VisCheckManager {
private:
    std::unique_ptr<VisCheck> vischeck;
    std::string currentMapName;
    bool mapLoaded;

    bool LoadMapFromFile(const std::string& mapName, std::vector<char>& outData);

public:
    VisCheckManager();
    ~VisCheckManager();

    // Load map when map changes
    void OnMapChange(const std::string& newMapName);

    // Simple visibility check
    bool IsVisible(const Vec3& pos1, const Vec3& pos2);

    // Check if current map is loaded and ready
    bool IsReady() const;

    // Get current loaded map name
    std::string GetLoadedMap() const;
};

// Global instance
extern std::unique_ptr<VisCheckManager> g_VisCheckManager;
