#pragma once
#include <string>
#include <fstream>
#include <filesystem>

// Live config - reloads from file every few seconds without restart
namespace LiveConfig {

struct Settings {
    // ESP
    bool esp_enabled = true;
    bool esp_box = true;
    bool esp_health = true;
    bool esp_bones = true;
    bool esp_name = true;
    bool esp_distance = true;
    bool esp_head = true;
    bool esp_visible_only = false;

    // Aimbot
    bool aim_enabled = true;
    float aim_fov = 5.0f;
    float aim_smooth = 3.0f;
    int aim_bone = 6; // head
    int aim_key = 0x01; // VK_LBUTTON

    // RCS
    bool rcs_enabled = true;
    float rcs_scale_x = 1.0f;
    float rcs_scale_y = 1.0f;
    int rcs_start_bullet = 1;
};

inline Settings g_settings;
inline std::string g_configPath;
inline ULONGLONG g_lastLoadTime = 0;

inline void SetConfigPath(const std::string& exePath) {
    std::filesystem::path p(exePath);
    g_configPath = p.parent_path().string() + "\\config.ini";
}

inline void CreateDefaultConfig() {
    std::ofstream f(g_configPath);
    if (!f.is_open()) return;

    f << "; USB-Lan-ESP Config - Edit while running, auto-reloads every 2 seconds\n";
    f << "\n[ESP]\n";
    f << "enabled=1\n";
    f << "box=1\n";
    f << "health=1\n";
    f << "bones=1\n";
    f << "name=1\n";
    f << "distance=1\n";
    f << "head=1\n";
    f << "visible_only=0\n";
    f << "\n[Aimbot]\n";
    f << "enabled=1\n";
    f << "fov=5.0\n";
    f << "smooth=3.0\n";
    f << "bone=6          ; 6=head, 5=neck, 4=spine\n";
    f << "key=2           ; 2=right click, 1=left click\n";
    f << "\n[RCS]\n";
    f << "enabled=1\n";
    f << "scale_x=1.0     ; horizontal compensation (0.5-1.5)\n";
    f << "scale_y=1.0     ; vertical compensation (0.5-1.5)\n";
    f << "start_bullet=1  ; start compensating after N bullets\n";
    f.close();
}

inline std::string Trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

inline void LoadConfig() {
    if (!std::filesystem::exists(g_configPath)) {
        CreateDefaultConfig();
        return;
    }

    std::ifstream f(g_configPath);
    if (!f.is_open()) return;

    std::string line, section;
    while (std::getline(f, line)) {
        line = Trim(line);
        if (line.empty() || line[0] == ';') continue;

        if (line[0] == '[') {
            size_t end = line.find(']');
            if (end != std::string::npos)
                section = line.substr(1, end - 1);
            continue;
        }

        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = Trim(line.substr(0, eq));
        std::string val = Trim(line.substr(eq + 1));
        // Remove comments from value
        size_t comment = val.find(';');
        if (comment != std::string::npos)
            val = Trim(val.substr(0, comment));

        if (section == "ESP") {
            if (key == "enabled") g_settings.esp_enabled = (val == "1");
            else if (key == "box") g_settings.esp_box = (val == "1");
            else if (key == "health") g_settings.esp_health = (val == "1");
            else if (key == "bones") g_settings.esp_bones = (val == "1");
            else if (key == "name") g_settings.esp_name = (val == "1");
            else if (key == "distance") g_settings.esp_distance = (val == "1");
            else if (key == "head") g_settings.esp_head = (val == "1");
            else if (key == "visible_only") g_settings.esp_visible_only = (val == "1");
        }
        else if (section == "Aimbot") {
            if (key == "enabled") g_settings.aim_enabled = (val == "1");
            else if (key == "fov") g_settings.aim_fov = std::stof(val);
            else if (key == "smooth") g_settings.aim_smooth = std::stof(val);
            else if (key == "bone") g_settings.aim_bone = std::stoi(val);
            else if (key == "key") g_settings.aim_key = std::stoi(val);
        }
        else if (section == "RCS") {
            if (key == "enabled") g_settings.rcs_enabled = (val == "1");
            else if (key == "scale_x") g_settings.rcs_scale_x = std::clamp(std::stof(val), 0.5f, 1.5f);
            else if (key == "scale_y") g_settings.rcs_scale_y = std::clamp(std::stof(val), 0.5f, 1.5f);
            else if (key == "start_bullet") g_settings.rcs_start_bullet = std::stoi(val);
        }
    }
    f.close();
}

inline void CheckReload() {
    ULONGLONG now = GetTickCount64();
    if (now - g_lastLoadTime > 2000) { // Reload every 2 seconds
        LoadConfig();
        g_lastLoadTime = now;
    }
}

} // namespace LiveConfig
