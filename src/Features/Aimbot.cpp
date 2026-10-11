#include "Aimbot.h"
#undef max
#undef min

void AimControl::switchToggle()
{
    LegitBotConfig::AimAlways = !LegitBotConfig::AimAlways;
}

std::pair<float, float> AimControl::Humanize(float TargetX, float TargetY) {

    static float HumanizationAmount = HumanizationStrength*2.0f/100.0f;

    if (HumanizationAmount <= 0.0f)
    {
        PrevTargetX = TargetX;
        PrevTargetY = TargetY;
        return { TargetX, TargetY };
    }
    
    // random distributions for different types of jitter
    std::uniform_real_distribution<float> jitterDist(-10.f, 10.f);
    std::uniform_real_distribution<float> microDist(-10.f, 10.f);
    std::uniform_real_distribution<float> smoothnessDist(0.4f, 10.f);
    
    // calculate movement distance for dynamic adjustments
    float MovementDistance = std::sqrt(TargetX * TargetX + TargetY * TargetY);
    
    // add micro-movements (scaled by strength)
    float MicroJitterX = microDist(gen) * std::min(MovementDistance * 0.25f, 8.0f) * HumanizationAmount;
    float MicroJitterY = microDist(gen) * std::min(MovementDistance * 0.25f, 8.0f) * HumanizationAmount;
    
    // add larger jitter for longer movements (scaled by strength)
    float JitterScale = std::min(MovementDistance * 0.15f, 12.0f) * HumanizationAmount;
    float JitterX = jitterDist(gen) * JitterScale;
    float JitterY = jitterDist(gen) * JitterScale;
    
    // create slightly curved path (scaled by strength)
    float PerpX = -TargetY * 0.35f * jitterDist(gen) * HumanizationAmount;
    float PerpY = TargetX * 0.35f * jitterDist(gen) * HumanizationAmount;
    
    // apply smoothing with strength-controlled factor (more aggressive smoothing variation)
    // at strength=0, no smoothing (immediate response)
    // at strength=1, full smoothing range with more noticeable lag
    float baseSmoothFactor = smoothnessDist(gen);
    float SmoothFactor = 1.0f - ((1.0f - baseSmoothFactor) * HumanizationAmount);
    float SmoothedX = TargetX * SmoothFactor + PrevTargetX * (1.0f - SmoothFactor);
    float SmoothedY = TargetY * SmoothFactor + PrevTargetY * (1.0f - SmoothFactor);
    
    // combine
    float HumanizedX = SmoothedX + MicroJitterX + JitterX + PerpX;
    float HumanizedY = SmoothedY + MicroJitterY + JitterY + PerpY;
    
    // store current targets for next frame smoothing
    PrevTargetX = TargetX;
    PrevTargetY = TargetY;
    
    return { HumanizedX, HumanizedY };
}

void AimControl::AimBot(const CEntity& Local, Vec3 LocalPos, std::vector<Vec3>& AimPosList)
{
    if (MenuConfig::ShowMenu)
        return;

    std::string curWeapon = TriggerBot::GetWeapon(Local);
    if (!TriggerBot::CheckWeapon(curWeapon))
        return;

    if (onlyAuto && !CheckAutoMode(curWeapon))
        return;

    if (static_cast<int>(Local.Pawn.ShotsFired) <= AimBullet - 1 && AimBullet != 0)
    {
        HasTarget = false;
        return;
    }

    if (AimControl::ScopeOnly)
    {
        bool isScoped;
        memoryManager.ReadMemory<bool>(Local.Pawn.Address + Offset.Pawn.isScoped, isScoped);
        if (!isScoped && TriggerBot::CheckScopeWeapon(curWeapon))
        {
            HasTarget = false;
            return;
        }
    }

    if (!IgnoreFlash && Local.Pawn.FlashDuration > 0.f)
        return;

    const int ListSize = static_cast<int>(AimPosList.size());
    if (ListSize == 0) {
        HasTarget = false;
        return;
    }

    // --- Angle-based target selection ---
    float BestNorm = MAXV;
    int BestTargetIndex = -1;

    for (int i = 0; i < ListSize; i++)
    {
        Vec3 OppPos = AimPosList[i] - LocalPos;
        const float Distance = sqrtf(OppPos.x * OppPos.x + OppPos.y * OppPos.y);
        if (Distance < 0.001f) continue;

        float Yaw = atan2f(OppPos.y, OppPos.x) * 57.295779513f - Local.Pawn.ViewAngle.y;
        const float Pitch = -atan2f(OppPos.z, Distance) * 57.295779513f - Local.Pawn.ViewAngle.x;

        // Normalize yaw delta to [-180, 180]
        while (Yaw > 180.f) Yaw -= 360.f;
        while (Yaw < -180.f) Yaw += 360.f;

        const float Norm = sqrtf(Yaw * Yaw + Pitch * Pitch);

        if (Norm < BestNorm) {
            BestNorm = Norm;
            BestTargetIndex = i;
        }
    }

    if (BestNorm >= AimFov || BestNorm <= AimFovMin || BestTargetIndex == -1) {
        HasTarget = false;
        RemainderX = 0.0f;
        RemainderY = 0.0f;
        return;
    }

    HasTarget = true;

    // --- Calculate angle delta to best target ---
    Vec3 OppPos = AimPosList[BestTargetIndex] - LocalPos;
    const float Distance = sqrtf(OppPos.x * OppPos.x + OppPos.y * OppPos.y);

    float TargetYaw = atan2f(OppPos.y, OppPos.x) * 57.295779513f;
    float TargetPitch = -atan2f(OppPos.z, Distance) * 57.295779513f;

    float DeltaYaw = TargetYaw - Local.Pawn.ViewAngle.y;
    float DeltaPitch = TargetPitch - Local.Pawn.ViewAngle.x;

    // Normalize yaw delta
    while (DeltaYaw > 180.f) DeltaYaw -= 360.f;
    while (DeltaYaw < -180.f) DeltaYaw += 360.f;

    // --- Integrated RCS compensation ---
    if (LegitBotConfig::RCS && Local.Pawn.ShotsFired > 1) {
        float burst = fminf((float)Local.Pawn.ShotsFired / 3.0f, 1.0f);
        DeltaPitch -= Local.Pawn.AimPunchAngle.x * 2.0f * RCS::RCSScale.x * burst;
        DeltaYaw -= Local.Pawn.AimPunchAngle.y * 2.0f * RCS::RCSScale.y * burst;
    }

    // --- Smoothing (exponential interpolation) ---
    if (Smooth > 0.0f)
    {
        // Exponential factor: larger Smooth = slower approach, never overshoots
        // t ranges from ~0.05 (very smooth) to ~0.5 (snappy) based on Smooth value
        float t = 1.0f / (Smooth * 2.0f);
        t = fmaxf(0.02f, fminf(t, 0.5f));

        // Scale down further when far from target (prevents long-distance snaps)
        const float DistanceRatio = BestNorm / AimFov;
        t *= (0.5f + 0.5f * (1.0f - DistanceRatio));

        DeltaPitch *= t;
        DeltaYaw *= t;
    }

    // --- Convert angle deltas to mouse pixel counts ---
    // CS2: mouse_count * m_yaw * sensitivity = degrees of rotation
    // So: mouse_count = degrees / (m_yaw * sensitivity)
    constexpr float M_YAW = 0.022f;
    float sens = Local.Client.Sensitivity;
    if (sens < 0.001f) sens = 1.0f; // safety

    float TargetX = -DeltaYaw / (M_YAW * sens);
    float TargetY = DeltaPitch / (M_YAW * sens);

    // --- Humanization ---
    if (HumanizeVar)
    {
        auto [HumanizedX, HumanizedY] = Humanize(TargetX, TargetY);
        TargetX = HumanizedX;
        TargetY = HumanizedY;
    }

    // --- Send mouse input with remainder accumulation ---
    static ULONGLONG lastAimTime = GetTickCount64();
    ULONGLONG currentTick = GetTickCount64();

    if (currentTick - lastAimTime >= static_cast<ULONGLONG>(MenuConfig::AimDelay))
    {
        // Accumulate sub-pixel remainders from previous frames
        TargetX += RemainderX;
        TargetY += RemainderY;

        // Cap per-frame mouse movement to prevent large instant snaps
        constexpr float MaxMousePerFrame = 40.0f;
        TargetX = fmaxf(-MaxMousePerFrame, fminf(TargetX, MaxMousePerFrame));
        TargetY = fmaxf(-MaxMousePerFrame, fminf(TargetY, MaxMousePerFrame));

        int mX = (int)TargetX;
        int mY = (int)TargetY;

        // Store fractional remainder for next frame
        RemainderX = TargetX - (float)mX;
        RemainderY = TargetY - (float)mY;

        if (mX != 0 || mY != 0) {
            char clampX = (char)(mX > 127 ? 127 : (mX < -127 ? -127 : mX));
            char clampY = (char)(mY > 127 ? 127 : (mY < -127 ? -127 : mY));
            char btnState = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) ? 1 : 0;
            mouse_move(btnState, clampX, clampY, 0);
        }

        lastAimTime = currentTick;
    }
}

bool AimControl::CheckAutoMode(const std::string& WeaponName)
{
    if (WeaponName == "deagle" || WeaponName == "elite" || WeaponName == "fiveseven" || WeaponName == "glock" || WeaponName == "awp" || WeaponName == "xm1014" || WeaponName == "mag7" || WeaponName == "sawedoff" || WeaponName == "tec9" || WeaponName == "zeus" || WeaponName == "p2000" || WeaponName == "nova" || WeaponName == "p250" || WeaponName == "ssg08" || WeaponName == "usp" || WeaponName == "revolver")
        return false;
    else
        return true;
}