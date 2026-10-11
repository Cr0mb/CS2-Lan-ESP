#include "VisCheckManager.h"
#include <iostream>
#include <windows.h>
#include <shlobj.h>
#include <fstream>
#include <vector>

// Global instance
std::unique_ptr<VisCheckManager> g_VisCheckManager = nullptr;

VisCheckManager::VisCheckManager()
    : vischeck(nullptr), currentMapName(""), mapLoaded(false) {
}

VisCheckManager::~VisCheckManager() {
    if (vischeck) {
        vischeck->UnloadMap();
    }
}

static std::string TrimMapName(const std::string& mapName) {
    std::string trimmed = mapName;
    while (!trimmed.empty() && (trimmed[0] == ' ' || trimmed[0] == '\t' ||
           trimmed[0] == '\r' || trimmed[0] == '\n' ||
           (unsigned char)trimmed[0] == 0xFF)) {
        trimmed.erase(0, 1);
    }
    while (!trimmed.empty() && (trimmed.back() == ' ' || trimmed.back() == '\t' ||
           trimmed.back() == '\r' || trimmed.back() == '\n' ||
           (unsigned char)trimmed.back() == 0xFF)) {
        trimmed.pop_back();
    }
    return trimmed;
}

// Get %APPDATA%\GHaxLabs\maps\ directory
static std::string GetMapsDirectory() {
    char buf[MAX_PATH] = {0};
    if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, buf))) {
        std::string path(buf);
        return path + "\\GHaxLabs\\maps\\";
    }
    // Fallback to exe directory
    GetModuleFileNameA(NULL, buf, MAX_PATH);
    std::string path(buf);
    size_t pos = path.find_last_of("\\/");
    if (pos != std::string::npos)
        return path.substr(0, pos + 1) + "maps\\";
    return ".\\maps\\";
}

bool VisCheckManager::LoadMapFromFile(const std::string& mapName, std::vector<char>& outData) {
    std::string mapPath = GetMapsDirectory() + mapName + ".opt.z";

    std::ifstream file(mapPath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        printf("[VisCheck] Map file not found: %s\n", mapPath.c_str());
        return false;
    }

    std::streamsize fileSize = file.tellg();
    if (fileSize <= 0) {
        printf("[VisCheck] Map file is empty: %s\n", mapPath.c_str());
        return false;
    }

    file.seekg(0, std::ios::beg);
    outData.resize(static_cast<size_t>(fileSize));
    if (!file.read(outData.data(), fileSize)) {
        printf("[VisCheck] Failed to read map file: %s\n", mapPath.c_str());
        return false;
    }

    printf("[VisCheck] Loaded map file: %s (%lld KB)\n", mapPath.c_str(), fileSize / 1024);
    return true;
}

void VisCheckManager::OnMapChange(const std::string& newMapName) {
    std::string trimmedMapName = TrimMapName(newMapName);

    if (trimmedMapName == currentMapName) {
        return;
    }

    if (vischeck) {
        vischeck->UnloadMap();
    }

    currentMapName = trimmedMapName;
    mapLoaded = false;

    if (trimmedMapName == "<empty>" || trimmedMapName == "<unknown>" || trimmedMapName.empty()) {
        printf("[VisCheck] No map loaded (in menu)\n");
        return;
    }

    std::vector<char> mapFileData;
    if (!LoadMapFromFile(trimmedMapName, mapFileData)) {
        printf("[VisCheck] Cannot load map: %s (file not found in maps/ directory)\n", trimmedMapName.c_str());
        return;
    }

    try {
        vischeck = std::make_unique<VisCheck>();
        if (vischeck->LoadMapFromMemory(mapFileData.data(), mapFileData.size(), trimmedMapName)) {
            mapLoaded = true;
            printf("[VisCheck] Successfully loaded map from file: %s\n", trimmedMapName.c_str());
        } else {
            printf("[VisCheck] Failed to load map: %s\n", trimmedMapName.c_str());
            vischeck.reset();
        }
    } catch (const std::exception& e) {
        printf("[VisCheck] Exception loading map %s: %s\n", trimmedMapName.c_str(), e.what());
        vischeck.reset();
    }
}

bool VisCheckManager::IsVisible(const Vec3& pos1, const Vec3& pos2) {
    if (!mapLoaded || !vischeck) {
        return false;
    }

    try {
        Vector3 point1(pos1.x, pos1.y, pos1.z);
        Vector3 point2(pos2.x, pos2.y, pos2.z);
        return vischeck->IsPointVisible(point1, point2);
    } catch (const std::exception& e) {
        printf("[VisCheck] Exception during visibility check: %s\n", e.what());
        return false;
    }
}

bool VisCheckManager::IsReady() const {
    return mapLoaded && vischeck && vischeck->IsMapLoaded();
}

std::string VisCheckManager::GetLoadedMap() const {
    return currentMapName;
}
