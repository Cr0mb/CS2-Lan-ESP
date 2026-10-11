#include "RCS.h"
#include "../Core/LiveConfig.h"
extern "C" {
#include "../Helpers/Mouse.h"
}
#include <cmath>
#include <algorithm>

namespace RCS {

// Static state
static Vec2 s_oldPunch = { 0, 0 };
static Vec2 s_unaccounted = { 0, 0 };
static int s_lastShotsFired = 0;

// Hardcoded offsets from GhaxV3 (latest CS2)
constexpr std::ptrdiff_t OFF_AIM_PUNCH_SERVICES = 0x14B8;
constexpr std::ptrdiff_t OFF_PREDICTABLE_ANGLE = 0x50;
constexpr std::ptrdiff_t OFF_UNPREDICTABLE_ANGLE = 0xA4;

Vec2 ReadAimPunch(uint64_t pawnAddr) {
    // Read AimPunchServices pointer
    uint64_t services = 0;
    if (!memoryManager.ReadMemory(pawnAddr + OFF_AIM_PUNCH_SERVICES, services) || !services)
        return Vec2{0, 0};

    // Try predictable angle first
    Vec2 punch{0, 0};
    memoryManager.ReadMemory(services + OFF_PREDICTABLE_ANGLE, punch);
    if (punch.x != 0.0f || punch.y != 0.0f)
        return punch;

    // Fallback to unpredictable
    memoryManager.ReadMemory(services + OFF_UNPREDICTABLE_ANGLE, punch);
    return punch;
}

void RecoilControl(CEntity LocalPlayer)
{
    auto& cfg = LiveConfig::g_settings;
    if (!cfg.rcs_enabled)
        return;

    int shotsFired = LocalPlayer.Pawn.ShotsFired;

    // Detect spray end/reset
    if (shotsFired <= 0 || shotsFired < s_lastShotsFired) {
        // Read current punch to avoid huge delta on next spray
        s_oldPunch = ReadAimPunch(LocalPlayer.Pawn.Address);
        s_unaccounted.x = 0; s_unaccounted.y = 0;
        s_lastShotsFired = 0;
        return;
    }

    // Check if we should apply RCS
    bool shouldApply = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    if (!shouldApply || shotsFired < cfg.rcs_start_bullet) {
        s_oldPunch = ReadAimPunch(LocalPlayer.Pawn.Address);
        s_unaccounted.x = 0; s_unaccounted.y = 0;
        s_lastShotsFired = shotsFired;
        return;
    }

    s_lastShotsFired = shotsFired;

    // Read current punch
    Vec2 aimPunch = ReadAimPunch(LocalPlayer.Pawn.Address);

    // Calculate delta from previous
    Vec2 punchDelta = {
        aimPunch.x - s_oldPunch.x,
        aimPunch.y - s_oldPunch.y
    };

    // Skip if no significant change
    if (std::abs(punchDelta.x) < 0.001f && std::abs(punchDelta.y) < 0.001f) {
        s_oldPunch = aimPunch;
        return;
    }

    // Get sensitivity
    float sensitivity = LocalPlayer.Client.Sensitivity;
    if (sensitivity <= 0.0f) sensitivity = 1.0f;

    // CS2: m_yaw = 0.022
    constexpr float M_YAW = 0.022f;
    float degPerPixel = sensitivity * M_YAW;

    if (degPerPixel <= 0.0f) {
        s_oldPunch = aimPunch;
        return;
    }

    // Calculate mouse compensation with config scales
    // punch.x = pitch (vertical), punch.y = yaw (horizontal)
    // Negate to counter the recoil
    float compX = -(punchDelta.y * cfg.rcs_scale_x * 2.0f) / degPerPixel;
    float compY = -(punchDelta.x * cfg.rcs_scale_y * 2.0f) / degPerPixel;

    // Add fractional pixels from last frame
    compX += s_unaccounted.x;
    compY += s_unaccounted.y;

    // Round to integer
    int dx = static_cast<int>(std::roundf(compX));
    int dy = static_cast<int>(std::roundf(compY));

    // Track unaccounted fractions for next frame
    s_unaccounted.x = compX - static_cast<float>(dx);
    s_unaccounted.y = compY - static_cast<float>(dy);

    // Inject mouse movement
    if (dx != 0 || dy != 0) {
        // Split into multiple moves if > 127 (mouse_move limit)
        int remainX = dx, remainY = dy;
        while (remainX != 0 || remainY != 0) {
            char mX = static_cast<char>(std::clamp(remainX, -127, 127));
            char mY = static_cast<char>(std::clamp(remainY, -127, 127));
            mouse_move(1, mX, mY, 0);
            remainX -= mX;
            remainY -= mY;
        }
    }

    s_oldPunch = aimPunch;
}

} // namespace RCS
