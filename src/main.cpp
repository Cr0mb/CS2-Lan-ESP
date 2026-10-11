// USB-Lan-ESP - CS2 External ESP + Aimbot + RCS
// LAN/USB edition - No auth, no menu, NeacSafe64 driver only

#include "Core/Cheats.h"
#include "Core/VisCheckManager.h"
#include "Core/GlobalVars.h"
#include "Core/LiveConfig.h"
#include "Offsets/Offsets.h"
#include "Core/Init.h"
#include "Features/RCS.h"

#include <filesystem>
#include <KnownFolders.h>
#include <ShlObj.h>
#include <iostream>
#include <string>
#include <thread>
#include <atomic>
#include <TlHelp32.h>
#include <d3d11.h>

namespace fs = std::filesystem;

HANDLE g_hNeacController = INVALID_HANDLE_VALUE;
std::atomic<bool> g_shouldExit{false};
bool g_monitoredMode = false;

// Stub logo symbols (no logo needed)
ID3D11ShaderResourceView* Logo = nullptr;
int LogoW = 0, LogoH = 0;
namespace Images { unsigned char Logo[1] = {0}; }

void StartCheat();
void ExitKeyThread();

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    // Check for monitored mode (launched by Monitor process)
    g_monitoredMode = (strstr(lpCmdLine, "--monitored") != nullptr);

    // No console - completely silent operation
    if (!IsUserAnAdmin()) {
        return -1;
    }

    // Start exit key monitoring thread
    std::thread exitThread(ExitKeyThread);
    exitThread.detach();

    StartCheat();

    g_shouldExit = true;
    return 0;
}

void ExitKeyThread() {
    while (!g_shouldExit) {
        if (GetAsyncKeyState(VK_END) & 0x8000) {
            g_shouldExit = true;
            Sleep(200);
            ExitProcess(0);
        }
        Sleep(50);
    }
}

void StartCheat()
{
    char exePath[MAX_PATH];
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    LiveConfig::SetConfigPath(exePath);
    LiveConfig::LoadConfig();

    char documentsPath[MAX_PATH];
    if (SHGetFolderPathA(NULL, CSIDL_PERSONAL, NULL, 0, documentsPath) == S_OK) {
        MenuConfig::docPath = documentsPath;
        MenuConfig::path = MenuConfig::docPath + "\\USB-Lan-ESP";
        try { fs::create_directories(MenuConfig::path + "\\Data"); } catch (...) {}
    }

    MenuConfig::ShowMenu = false;
    MiscCFG::WaterMark = false;

    int retries = g_monitoredMode ? 10 : 5;
    bool connected = false;

    for (int i = 0; i < retries && !connected && !g_shouldExit; i++) {
        if (i > 0) Sleep(1000);
        connected = memoryManager.ConnectDriver(L"NeacSafe64");
    }

    if (!connected) return;

    bool preStart = false;
    while (memoryManager.GetProcessID(L"cs2.exe") == 0 && !g_shouldExit) {
        preStart = true;
        Sleep(500);
    }
    if (g_shouldExit) return;

    if (preStart) Sleep(12000);
    if (g_shouldExit) return;

    if (!memoryManager.Attach(memoryManager.GetProcessID(L"cs2.exe")))
        return;

    try {
        Offset.UpdateOffsets();
    } catch (...) {
        return;
    }
    if (g_shouldExit) return;

    if (!gGame.InitAddress())
        return;

    g_globalVars = std::make_unique<globalvars>();
    g_VisCheckManager = std::make_unique<VisCheckManager>();

    try {
        Gui.AttachAnotherWindow("Counter-Strike 2", "SDL_app", Cheats::Run);
    } catch (...) {}
}
