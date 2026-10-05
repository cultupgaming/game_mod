#include <main.h>
#include <natives.h>
#include <types.h>

#include <windows.h>
#include <Xinput.h>

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <string>

#include "keyboard.h"
#include "script.h"
#include "trace.h"

namespace
{
    constexpr int WORLD_ACTOR_CAPACITY = 70;
    constexpr float RDK_TARGETING_RADIUS_METRES = 20.0f;
    constexpr ULONGLONG NEUTRAL_TARGET_REFRESH_MS = 100;

    // Verified eWeaponEnum value from EvilBlunt/RDR-Strings-and-Enums:
    // WEAPON_NONE=-1, WEAPON_PISTOL_Volcanic=0, ...,
    // WEAPON_THROWN_ThrowingKnife=25.
    constexpr int THROWING_KNIFE_WEAPON = 25;

    // Present the existing throwing-knife slot as Knifemare's signature weapon.
    // "Assassin Knife" intentionally matches "Throwing Knife" at 14 characters,
    // so the proven Possession localization-buffer technique can reuse the
    // existing display/radial buffers without enlarging them.
    constexpr const char* ASSASSIN_KNIFE_LABEL = "Assassin Knife";
    constexpr std::size_t ASSASSIN_KNIFE_GXT_KEY_CAPACITY = 96;

    // #93 manual execution harness. F8 aligns John into the base game's
    // close-range front execution slot, enables execution eligibility on the
    // target, then sends the base game's normal primary attack. We deliberately
    // do not invent an action-node or animation name:
    // runtime acceptance requires the game itself to enter linked execution
    // state and naturally deliver the lethal outcome.
    // Release default: keep the F6/F7/F8 execution regression harness off.
    constexpr bool EXECUTION_DEBUG_CONTROLS_ENABLED = false;
    constexpr DWORD MANUAL_EXECUTION_KEY = VK_F8;
    constexpr DWORD TEST_NPC_STANDING_KEY = VK_F6;
    constexpr DWORD TEST_NPC_WALKING_KEY = VK_F7;
    // #118 temporary visual-inspection key. F8 remains reserved for the
    // execution harness; F9 switches only between the two planned outfit
    // candidates so runtime testing can settle the final recipe.
    constexpr DWORD OUTFIT_INSPECTION_KEY = VK_F9;
    constexpr ULONGLONG ASSASSIN_OUTFIT_SWITCH_TIMEOUT_MS = 750;
    constexpr int ASSASSIN_OUTFIT_DEADLY_ASSASSIN = 18;
    constexpr int PLAYER_MESH_LONGARM_HOLSTER = 25;
    constexpr int PLAYER_ACCESSORY_HAT = 0;
    constexpr int PLAYER_ACCESSORY_BANDANA = 1;
    constexpr int PLAYER_MESH_BANDANA = 26;
    constexpr int PLAYER_MESH_LEFT_GLOVE = 27;
    constexpr int PLAYER_MESH_RIGHT_GLOVE = 28;
    constexpr int PLAYER_MESH_LEFT_NO_GLOVE = 29;
    constexpr int PLAYER_MESH_RIGHT_NO_GLOVE = 30;
    // PC actor-enum list: ACTOR_CAUCASIAN_MALE_TownFolk02.
    constexpr int TEST_NPC_ACTOR_ENUM = 202;
    constexpr ULONGLONG TEST_NPC_STREAM_TIMEOUT_MS = 3000;
    constexpr float TEST_NPC_SPAWN_FORWARD_METRES = 4.0f;
    constexpr float TEST_NPC_GROUND_SNAP_DISTANCE = 10.0f;
    constexpr int TEST_NPC_GROUND_SNAP_MODE = 1092616192;
    constexpr float TEST_NPC_STAND_STILL_SECONDS = 600.0f;
    constexpr float TEST_NPC_HELP_SECONDS = 3.0f;
    constexpr float PI = 3.14159265358979323846f;
    constexpr float FRONT_EXECUTION_OFFSET_METRES = 0.85f;
    constexpr float ALIGNMENT_POSITION_TOLERANCE_METRES = 0.35f;
    constexpr float TARGET_DRIFT_TOLERANCE_METRES = 0.35f;
    constexpr float TARGET_HEADING_TOLERANCE_DEGREES = 15.0f;
    // Only moving targets receive this short-lived task. It expires on its own;
    // stationary targets retain the exact e074812 execution timing.
    constexpr float MOVING_TARGET_STAND_STILL_SECONDS = 0.15f;
    // After an aimed throwing-knife action is captured, keep that exact target
    // stationary while John's throw finishes and execution is prepared.
    // The task self-expires; manual F8 #93 timing remains unchanged.
    constexpr ULONGLONG AUTOMATIC_THROW_RELEASE_WAIT_MS = 1000;
    constexpr ULONGLONG AUTOMATIC_FIRE_RELEASE_WAIT_MS = 1000;
    // After early reposition, keep the captured target proof-protected until
    // its own hit metadata proves John's original knife arrived. If the knife
    // misses, use a distance-scaled bounded clear window instead of waiting for
    // an impact that will never occur.
    constexpr ULONGLONG PROJECTILE_CLEAR_MIN_MS = 250;
    constexpr float PROJECTILE_CLEAR_PER_METRE_MS = 40.0f;
    constexpr ULONGLONG PROJECTILE_CLEAR_MAX_MS = 900;
    constexpr float AUTOMATIC_TARGET_STAND_STILL_SECONDS = 2.0f;
    // Exact-target execution staging is refreshed every frame only until the
    // contextual linked execution takes ownership. Keep each individual task
    // short so stopping the refresh immediately gives RDR control back.
    constexpr float AUTOMATIC_TARGET_STAGING_STAND_STILL_SECONDS = 0.15f;
    // Runtime comparison: the successful contextual execution reached FIRE
    // with the target freshly idle (gait 0, action time about 0.025s), while
    // the failed attempt still had gait 1 and an old action node. After the
    // universal Neutral reset, actively settle the target into that idle state.
    constexpr ULONGLONG AUTOMATIC_TARGET_IDLE_SETTLE_MS = 125;
    constexpr float AUTOMATIC_TARGET_IDLE_ACTION_TIME_MAX_SECONDS = 0.125f;
    // Release at the first game input/action acknowledgement, with a hard cap
    // if SendInput reaches Windows but never reaches the game's input sampler.
    constexpr ULONGLONG AUTOMATIC_INPUT_ACK_TIMEOUT_MS = 100;
    constexpr ULONGLONG ALIGNMENT_TIMEOUT_MS = 1500;
    // Runtime traces show successful contextual executions enter linked state
    // within roughly 16-50 ms. Do not make a real miss look like a 1.5 second
    // pause before the bounded retry.
    constexpr ULONGLONG EXECUTION_FIRST_ATTEMPT_TIMEOUT_MS = 250;
    constexpr ULONGLONG EXECUTION_RETRY_TIMEOUT_MS = 1500;
    constexpr ULONGLONG AUTOMATIC_EXECUTION_DIAGNOSTIC_INTERVAL_MS = 250;
    constexpr ULONGLONG EXECUTION_TOTAL_TIMEOUT_MS = 7000;
    constexpr ULONGLONG CONTROL_RETURN_TIMEOUT_MS = 750;
    // Diagnostic handoff: use the documented low-hash SET_PLAYER_CONTROL
    // clear-tasks flag after the physical knife has already left John's hand.
    // Keep this verification short so a failed task clear cannot turn back
    // into the old wait-for-impact race.
    constexpr int PLAYER_CONTROL_CLEAR_TASKS_FLAG = 4;
    constexpr ULONGLONG AUTOMATIC_THROW_TASK_CLEAR_WAIT_MS = 100;

    // #137 Assassin-style blink transition. Fade to black quickly, hold a
    // clearly visible full-black beat that actually masks the warp, then reveal
    // the arrival with the proven screen shock and direct XInput vibration.
    // FOV manipulation was rejected in runtime testing and is intentionally
    // removed.
    constexpr float TELEPORT_VISUAL_MIN_TRAVEL_METRES = 0.15f;
    constexpr float TELEPORT_BLINK_FADE_OUT_SECONDS = 0.055f;
    constexpr ULONGLONG TELEPORT_BLINK_FADE_OUT_WAIT_MS = 55;
    constexpr ULONGLONG TELEPORT_BLINK_BLACK_HOLD_MS = 120;
    constexpr float TELEPORT_BLINK_FADE_IN_SECONDS = 0.090f;
    constexpr ULONGLONG TELEPORT_RUMBLE_DURATION_MS = 120;
    constexpr WORD TELEPORT_RUMBLE_LEFT_MOTOR = 65535;
    constexpr WORD TELEPORT_RUMBLE_RIGHT_MOTOR = 65535;
    constexpr float TELEPORT_SHOCK_AMPLITUDE = 1.0f;
    constexpr float TELEPORT_SHOCK_SPEED = 9.0f;
    constexpr float TELEPORT_SHOCK_STRENGTH = 1.0f;

    constexpr unsigned long long IS_GAME_PAUSED_HASH = 0x57246C02;
    constexpr unsigned long long CUTSCENE_MANAGER_IS_CUTSCENE_PLAYING_HASH = 0xA61FA36B;
    constexpr unsigned long long GET_POSITION_HASH = 0x99BD9D6F;
    constexpr unsigned long long IS_ACTOR_RIDING_HASH = 0xA6BBE769;
    constexpr unsigned long long IS_ACTOR_INSIDE_VEHICLE_HASH = 0x12325AE7;
    constexpr unsigned long long IS_ACTOR_PLAYER_HASH = 0xB27E91E7;
    constexpr unsigned long long IS_ACTOR_LOCAL_PLAYER_HASH = 0x6542CF26;
    constexpr unsigned long long GET_ACTOR_PROOF_HASH = 0x147EA072;
    constexpr unsigned long long SET_ACTOR_PROOF_HASH = 0xA5875DC8;
    constexpr unsigned long long CLEAR_ACTOR_PROOF_HASH = 0xF5B74E20;
    constexpr unsigned long long GET_LAST_ATTACK_TARGET_HASH = 0xEB40C2FC;
    constexpr unsigned long long GET_LAST_ATTACK_TIME_HASH = 0x69FA5315;
    constexpr unsigned long long GET_LAST_ATTACKER_HASH = 0x2C0F211D;
    constexpr unsigned long long GET_LAST_HIT_TIME_HASH = 0x3A207AF2;
    constexpr unsigned long long GET_LAST_HIT_WEAPON_HASH = 0x07B7AA6B;
    // K3rhos/RDR-PC-Natives-DB and TheRouletteBoi/rdr-nativedb-data:
    // BOOL (Actor), BOOL (Actor), BOOL (Actor), float (Actor), respectively.
    constexpr unsigned long long IS_ACTOR_THROWING_HASH = 0x886BD8AD;
    constexpr unsigned long long IS_ACTOR_REACTING_HASH = 0xBFD6AE3D;
    constexpr unsigned long long IS_ACTOR_READY_FOR_ACTION_HASH = 0xFB2B0CCF;
    constexpr unsigned long long GET_CURR_ACTION_NODE_PLAY_TIME_HASH = 0x5E84F53E;
    // PC input strings / hashes from the native DB; (action, 1, 0) is also
    // used by Red-Mods/RDRMP-Server-Resources/resources/noclip/client.lua.
    constexpr unsigned long long IS_DIGITAL_ACTION_DOWN_HASH = 0x062C5047;
    constexpr unsigned long long SET_WEAPONENUM_LOCKED_HASH = 0x0E4B7A33;
    constexpr unsigned long long IS_WEAPONENUM_LOCKED_HASH = 0xCCE4A339;
    constexpr unsigned long long GET_AMMO_ENUM_HASH = 0xD3E16075;
    // Same enum-identity diagnostics used to prove the Possessor WTD target.
    constexpr unsigned long long GET_WEAPON_ENUM_STRING_FROM_ENUM_HASH = 0x6A9CFA2A;
    constexpr unsigned long long GET_WEAPON_INTERNAL_NAME_HASH = 0x87C5471F;
    constexpr unsigned long long GET_WEAPON_ICON_NAME_HASH = 0xBE06C265;
    constexpr unsigned long long GET_WEAPON_FRAGMENT_NAME_HASH = 0xE8739A48;
    constexpr unsigned long long GIVE_WEAPON_TO_ACTOR_HASH = 0x6AA0EAF2;
    constexpr unsigned long long ACTOR_HAS_WEAPON_HASH = 0x0D47CFBD;
    constexpr unsigned long long ACTOR_SET_INV_AMMO_HASH = 0x4372593E;
    constexpr unsigned long long ACTOR_GET_INV_AMMO_HASH = 0xE224AC6F;
    constexpr unsigned long long ACTOR_GET_INV_AMMO_MAX_AMOUNT_HASH = 0x7AB368CF;
    constexpr unsigned long long GET_TARGET_ACTOR_HASH = 0x0EF7427B;
    constexpr unsigned long long GET_ACTOR_UNDER_RETICLE_HASH = 0x86BAAC6C;
    constexpr unsigned long long IS_PLAYER_TARGETTING_ACTOR_HASH = 0x87DDCA96;
    constexpr unsigned long long SET_CAN_ACTOR_HARDLOCK_NEUTRALS_HASH = 0x1EEE7494;
    constexpr unsigned long long SET_ACTOR_CAN_BE_HARDLOCKED_HASH = 0xF4429710;
    constexpr unsigned long long SET_ACTOR_ONLY_HARDLOCK_IF_HOSTILE_HASH = 0x5CC16A49;
    constexpr unsigned long long SET_ALLOW_EXECUTE_HASH = 0x5896817B;
    constexpr unsigned long long SET_ACTOR_ONE_SHOT_DEATH_HASH = 0xCDC686B2;
    constexpr unsigned long long GET_ACTOR_ONE_SHOT_DEATH_STATUS_HASH = 0x0912622D;
    constexpr unsigned long long SET_ACTOR_HEALTH_HASH = 0xFA090024;
    constexpr unsigned long long GET_ACTOR_HEALTH_HASH = 0xF246F15D;
    constexpr unsigned long long GET_ACTOR_MAX_HEALTH_HASH = 0xB69A84AF;
    constexpr unsigned long long AI_IS_HOSTILE_OR_ENEMY_HASH = 0x9AB964F4;
    constexpr unsigned long long MEMORY_CONSIDER_AS_ENEMY_HASH = 0x745A1BA3;
    constexpr unsigned long long MEMORY_CONSIDER_ACCORDING_TO_FACTION_HASH = 0xACD4084D;
    constexpr unsigned long long MEMORY_CLEAR_ALL_HASH = 0x4485B246;
    constexpr unsigned long long GET_ACTOR_FACTION_HASH = 0x52E2A611;
    constexpr unsigned long long SET_ACTOR_FACTION_HASH = 0xCC63951A;
    constexpr unsigned long long TASK_CLEAR_HASH = 0x16876A25;
    constexpr int FACTION_NEUTRAL = 1;
    constexpr unsigned long long GET_HEADING_HASH = 0x42DE39F0;
    constexpr unsigned long long SET_ACTOR_HEADING_HASH = 0xECE8520B;
    constexpr unsigned long long TASK_WANDER_HASH = 0x17BCA08E;
    // TheRouletteBoi/rdr-nativedb-data: void (Actor, Vector3*, int axis).
    constexpr unsigned long long GET_ACTOR_AXIS_HASH = 0x294A5549;
    constexpr unsigned long long GET_ACTOR_GAIT_TYPE_HASH = 0xAC232F6E;
    constexpr unsigned long long GET_ACTOR_POSTURE_HASH = 0xDB993A4F;
    constexpr unsigned long long TASK_STAND_STILL_HASH = 0x6F80965D;
    constexpr unsigned long long TELEPORT_ACTOR_WITH_HEADING_HASH = 0xE4DE507C;
    // #137 blink transition natives. Particle/smoke code remains deliberately
    // excluded; fade state and actor draw are the only RDR visual natives kept
    // here. Controller vibration is handled directly through XInput below.
    constexpr unsigned long long HUD_SET_FADE_COLOR_HASH = 0x4DA5F502;
    constexpr unsigned long long HUD_FADE_OUT_HASH = 0x52963366;
    constexpr unsigned long long HUD_FADE_IN_HASH = 0xF90F6C51;
    constexpr unsigned long long GET_DRAW_ACTOR_HASH = 0x085A9CA6;
    constexpr unsigned long long SET_DRAW_ACTOR_HASH = 0xE6644CE5;
    constexpr unsigned long long FIRE_SHOCK_HASH = 0xFA43DCC5;
    constexpr unsigned long long SET_SHOCK_SPEED_HASH = 0xEC906A7A;
    constexpr unsigned long long SET_SHOCK_AMPLITUDE_HASH = 0xC9FCD3EC;
    constexpr unsigned long long IS_ACTOR_ON_GROUND_HASH = 0x709EC06C;
    constexpr unsigned long long GET_ACTOR_INCAPACITATED_HASH = 0xEE4E2461;
    constexpr unsigned long long RESET_REACT_NODE_FOR_ACTOR_HASH = 0x7B17C5C3;
    constexpr unsigned long long RESET_ACTIONTREE_FOR_ACTOR_HASH = 0x07EC142B;
    constexpr unsigned long long SET_LINKED_ANIM_TARGET_HASH = 0x0A192D09;
    constexpr unsigned long long GET_LINKED_ANIM_TARGET_HASH = 0xA4E9E7EE;
    constexpr unsigned long long CLEAR_LINKED_ANIM_TARGET_HASH = 0xAC54E120;
    constexpr unsigned long long IS_ACTOR_PERFORMING_LINKED_ANIMATION_HASH = 0xCA9364C5;
    constexpr unsigned long long IS_ACTOR_ANIM_PHASE_LOCKED_HASH = 0xE0AC4B86;
    constexpr unsigned long long RELEASE_ACTOR_ANIM_PHASE_LOCK_HASH = 0xAEBAE989;
    constexpr unsigned long long IS_PLAYER_CONTROLLABLE_HASH = 0x9613C2D0;
    constexpr unsigned long long SET_PLAYER_CONTROL_HASH = 0xD17AFCD8;
    constexpr unsigned long long SWITCH_ACTOR_ENUM_VARIATION_HASH = 0x7AB17813;
    constexpr unsigned long long GET_CURRENT_ACTOR_ENUM_VARIATION_HASH = 0xB54567B9;
    constexpr unsigned long long ACTOR_ENABLE_VARIABLE_MESH_HASH = 0xDA2F6203;
    constexpr unsigned long long ACTOR_IS_VARIABLE_MESH_ENABLED_HASH = 0x5DE31288;
    constexpr unsigned long long ACTOR_HAS_VARIABLE_MESH_HASH = 0xA091179F;
    constexpr unsigned long long EQUIP_ACCESSORY_HASH = 0x5A80659D;
    constexpr unsigned long long DEEQUIP_ACCESSORY_HASH = 0xF7696B8B;
    constexpr unsigned long long IS_ACCESSORY_EQUIPPED_HASH = 0xE094DB31;

    constexpr unsigned long long SET_ACTOR_ALLOW_WEAPON_REACTIONS_HASH = 0x003D7C2F;

    struct alignas(8) NativeVector3
    {
        float x = 0.0f;
        DWORD paddingX = 0;
        float y = 0.0f;
        DWORD paddingY = 0;
        float z = 0.0f;
        DWORD paddingZ = 0;
    };

    static_assert(sizeof(NativeVector3) == 24);
    static_assert(offsetof(NativeVector3, x) == 0);
    static_assert(offsetof(NativeVector3, y) == 8);
    static_assert(offsetof(NativeVector3, z) == 16);

    struct AimedThrowRequest
    {
        Actor shooter = 0;
        Actor target = 0;
        unsigned long long throwId = 0;
        int originalProof = 0;
        bool proofProtected = false;
        bool weaponReactionsSuppressed = false;
        bool targetStagingActive = false;
        float targetHitBaseline = 0.0f;
        ULONGLONG projectileReleasedAt = 0;
        ULONGLONG projectileClearWindowMs = PROJECTILE_CLEAR_MIN_MS;
    };

    struct DetectorState
    {
        Actor player = 0;
        bool starterKnivesGranted = false;
        ULONGLONG nextStarterGrantAttempt = 0;
        bool armed = false;
        bool wasThrowing = false;
        Actor cachedAimedTarget = 0;
        bool cachedAimDropoutRetained = false;
        unsigned long long nextThrowId = 1;
        bool hasRequest = false;
        AimedThrowRequest request;
    };

    enum class ThrowExecutionFlowState
    {
        Idle,
        ThrowCaptured,
        Executing,
        Recovering
    };

    struct ThrowExecutionFlow
    {
        ThrowExecutionFlowState state = ThrowExecutionFlowState::Idle;
        AimedThrowRequest request;
        ULONGLONG transitionedAt = 0;
    };

    struct OutfitState
    {
        Actor player = 0;
        int originalVariation = -1;
        bool originalCaptured = false;
        bool glovesEnabled = true;
        bool applied = false;
        bool failed = false;
    };

    DetectorState g_detector;
    OutfitState g_outfit;
    ThrowExecutionFlow g_throwExecutionFlow;
    bool g_executionRunning = false;
    bool g_primaryAttackDown = false;
    Actor g_neutralHardlockPlayer = 0;
    bool g_neutralHardlockEnabled = false;
    Actor g_relaxedNeutralTargets[WORLD_ACTOR_CAPACITY] = {};
    int g_relaxedNeutralTargetCount = 0;
    ULONGLONG g_nextNeutralTargetRefresh = 0;
    Layout g_testNpcLayout = 0;
    Actor g_testNpc = 0;
    unsigned int g_testNpcSpawnSequence = 0;

    struct AssassinKnifeUiState
    {
        bool keysResolved = false;
        bool labelsCaptured = false;
        bool displayApplied = false;
        bool radialApplied = false;
        char displayKey[ASSASSIN_KNIFE_GXT_KEY_CAPACITY] = {};
        char radialKey[ASSASSIN_KNIFE_GXT_KEY_CAPACITY] = {};
        char* displayBuffer = nullptr;
        char* radialBuffer = nullptr;
        std::size_t displayCapacity = 0;
        std::size_t radialCapacity = 0;
    };

    AssassinKnifeUiState g_assassinKnifeUi;

    bool MaintainAimedThrowTargetStaging(
        AimedThrowRequest& request);

    Actor CurrentPlayer()
    {
        return ACTOR::GET_PLAYER_ACTOR(ACTOR::GET_LOCAL_SLOT());
    }

    bool IsValidAssassinKnifeUiString(const char* value)
    {
        return value != nullptr
            && value[0] != '\0'
            && STRING::IS_STRING_VALID(value);
    }

    bool ResolveAssassinKnifeUiKeys()
    {
        if (g_assassinKnifeUi.keysResolved)
            return true;

        const char* displayKey =
            WEAPON::GET_WEAPON_DISPLAY_NAME(THROWING_KNIFE_WEAPON);
        if (!IsValidAssassinKnifeUiString(displayKey))
            return false;

        const int displayWritten = std::snprintf(
            g_assassinKnifeUi.displayKey,
            sizeof(g_assassinKnifeUi.displayKey),
            "%s",
            displayKey);
        const int radialWritten = std::snprintf(
            g_assassinKnifeUi.radialKey,
            sizeof(g_assassinKnifeUi.radialKey),
            "%s_RAD",
            displayKey);
        if (displayWritten <= 0
            || static_cast<std::size_t>(displayWritten)
                >= sizeof(g_assassinKnifeUi.displayKey)
            || radialWritten <= 0
            || static_cast<std::size_t>(radialWritten)
                >= sizeof(g_assassinKnifeUi.radialKey))
        {
            return false;
        }

        g_assassinKnifeUi.keysResolved = true;
        return true;
    }

    bool WriteAssassinKnifeLabelBuffer(
        const char* key,
        char* buffer,
        std::size_t capacity)
    {
        if (key == nullptr || buffer == nullptr || capacity == 0)
            return false;

        const std::size_t desiredBytes =
            std::strlen(ASSASSIN_KNIFE_LABEL) + 1;
        if (desiredBytes > capacity)
            return false;

        DWORD oldProtect = 0;
        if (!VirtualProtect(
                buffer,
                capacity,
                PAGE_READWRITE,
                &oldProtect))
        {
            return false;
        }

        std::memcpy(buffer, ASSASSIN_KNIFE_LABEL, desiredBytes);

        DWORD ignoredProtect = 0;
        VirtualProtect(buffer, capacity, oldProtect, &ignoredProtect);
        FlushInstructionCache(GetCurrentProcess(), buffer, desiredBytes);

        const char* observed = UI::UI_GET_STRING(key);
        return observed == buffer
            && IsValidAssassinKnifeUiString(observed)
            && std::strcmp(observed, ASSASSIN_KNIFE_LABEL) == 0;
    }

    bool CaptureAssassinKnifeLabelBuffers()
    {
        if (!ResolveAssassinKnifeUiKeys())
            return false;

        const char* display =
            UI::UI_GET_STRING(g_assassinKnifeUi.displayKey);
        const char* radial =
            UI::UI_GET_STRING(g_assassinKnifeUi.radialKey);
        if (!IsValidAssassinKnifeUiString(display)
            || !IsValidAssassinKnifeUiString(radial))
        {
            return false;
        }

        char* displayBuffer = const_cast<char*>(display);
        char* radialBuffer = const_cast<char*>(radial);
        if (displayBuffer != g_assassinKnifeUi.displayBuffer)
        {
            g_assassinKnifeUi.displayBuffer = displayBuffer;
            g_assassinKnifeUi.displayCapacity = std::strlen(display) + 1;
            g_assassinKnifeUi.displayApplied =
                std::strcmp(display, ASSASSIN_KNIFE_LABEL) == 0;
        }
        if (radialBuffer != g_assassinKnifeUi.radialBuffer)
        {
            g_assassinKnifeUi.radialBuffer = radialBuffer;
            g_assassinKnifeUi.radialCapacity = std::strlen(radial) + 1;
            g_assassinKnifeUi.radialApplied =
                std::strcmp(radial, ASSASSIN_KNIFE_LABEL) == 0;
        }

        const std::size_t desiredBytes =
            std::strlen(ASSASSIN_KNIFE_LABEL) + 1;
        g_assassinKnifeUi.labelsCaptured =
            desiredBytes <= g_assassinKnifeUi.displayCapacity
            && desiredBytes <= g_assassinKnifeUi.radialCapacity;
        return g_assassinKnifeUi.labelsCaptured;
    }

    void UpdateAssassinKnifeUiIdentity()
    {
        if (!CaptureAssassinKnifeLabelBuffers())
            return;

        if (!g_assassinKnifeUi.displayApplied)
        {
            g_assassinKnifeUi.displayApplied =
                WriteAssassinKnifeLabelBuffer(
                    g_assassinKnifeUi.displayKey,
                    g_assassinKnifeUi.displayBuffer,
                    g_assassinKnifeUi.displayCapacity);
        }
        if (!g_assassinKnifeUi.radialApplied)
        {
            g_assassinKnifeUi.radialApplied =
                WriteAssassinKnifeLabelBuffer(
                    g_assassinKnifeUi.radialKey,
                    g_assassinKnifeUi.radialBuffer,
                    g_assassinKnifeUi.radialCapacity);
        }
    }

    void TraceAssassinKnifeFileLoaderDiagnostics()
    {
        static bool traced = false;
        if (traced)
            return;
        traced = true;

        const auto toUtf8 =
            [](const wchar_t* source, char* destination, std::size_t capacity)
            {
                if (destination == nullptr || capacity == 0)
                    return;

                destination[0] = '\0';
                if (source == nullptr || source[0] == L'\0')
                    return;

                const int converted =
                    WideCharToMultiByte(
                        CP_UTF8,
                        0,
                        source,
                        -1,
                        destination,
                        static_cast<int>(capacity),
                        nullptr,
                        nullptr);
                if (converted == 0)
                    destination[0] = '\0';
            };

        wchar_t exePath[1024] = {};
        const DWORD exeLength = GetModuleFileNameW(nullptr, exePath, 1024);
        if (exeLength == 0 || exeLength >= 1024)
        {
            Trace("RDK Assassin Knife FILELOADER cannot resolve RDR.exe path");
            return;
        }

        const std::wstring executable(exePath);
        const auto separator = executable.find_last_of(L"\\/");
        if (separator == std::wstring::npos)
        {
            Trace("RDK Assassin Knife FILELOADER cannot resolve game root");
            return;
        }

        const std::wstring gameRoot = executable.substr(0, separator + 1);
        const std::wstring stockMapres = gameRoot + L"game\\mapres.rpf";
        const std::wstring updateMapres = gameRoot + L"update\\game\\mapres.rpf";

        const char* enumName =
            invoke<const char*>(
                GET_WEAPON_ENUM_STRING_FROM_ENUM_HASH,
                THROWING_KNIFE_WEAPON);
        const char* internalName =
            invoke<const char*>(
                GET_WEAPON_INTERNAL_NAME_HASH,
                THROWING_KNIFE_WEAPON);
        const char* iconName =
            invoke<const char*>(
                GET_WEAPON_ICON_NAME_HASH,
                THROWING_KNIFE_WEAPON);
        const char* fragmentName =
            invoke<const char*>(
                GET_WEAPON_FRAGMENT_NAME_HASH,
                THROWING_KNIFE_WEAPON);
        char identityLine[768] = {};
        std::snprintf(
            identityLine,
            sizeof(identityLine),
            "RDK Assassin Knife enum-25 metadata: enum=%s internal=%s icon=%s fragment=%s",
            enumName != nullptr ? enumName : "<null>",
            internalName != nullptr ? internalName : "<null>",
            iconName != nullptr ? iconName : "<null>",
            fragmentName != nullptr ? fragmentName : "<null>");
        WriteTrace(identityLine);

        const DWORD updateAttributes = GetFileAttributesW(updateMapres.c_str());
        Trace(
            "RDK Assassin Knife FILELOADER physical update\\game\\mapres.rpf present",
            updateAttributes != INVALID_FILE_ATTRIBUTES
                && !(updateAttributes & FILE_ATTRIBUTE_DIRECTORY) ? 1LL : 0LL);

        using IsUltimateAsiLoaderFn = bool(WINAPI*)();
        using GetOverloadPathWFn = bool(WINAPI*)(wchar_t*, std::size_t);
        using GetOverloadedFilePathWFn =
            bool(WINAPI*)(const wchar_t*, wchar_t*, std::size_t);

        const wchar_t* moduleNames[] =
        {
            L"dinput8.dll",
            L"wininet.dll"
        };

        for (const wchar_t* moduleName : moduleNames)
        {
            HMODULE module = GetModuleHandleW(moduleName);

            wchar_t modulePath[1024] = {};
            if (module != nullptr)
            {
                GetModuleFileNameW(
                    module,
                    modulePath,
                    static_cast<DWORD>(
                        sizeof(modulePath) / sizeof(modulePath[0])));
            }

            auto isUltimateAsiLoader =
                module != nullptr
                ? reinterpret_cast<IsUltimateAsiLoaderFn>(
                    GetProcAddress(module, "IsUltimateASILoader"))
                : nullptr;
            auto getOverloadPath =
                module != nullptr
                ? reinterpret_cast<GetOverloadPathWFn>(
                    GetProcAddress(module, "GetOverloadPathW"))
                : nullptr;
            auto getOverloadedFilePath =
                module != nullptr
                ? reinterpret_cast<GetOverloadedFilePathWFn>(
                    GetProcAddress(module, "GetOverloadedFilePathW"))
                : nullptr;

            const bool isUltimate =
                isUltimateAsiLoader != nullptr
                && isUltimateAsiLoader();

            wchar_t overloadRoot[1024] = {};
            const bool overloadActive =
                getOverloadPath != nullptr
                && getOverloadPath(
                    overloadRoot,
                    sizeof(overloadRoot) / sizeof(overloadRoot[0]));

            wchar_t mappedMapres[1024] = {};
            const bool mapresMapped =
                getOverloadedFilePath != nullptr
                && getOverloadedFilePath(
                    stockMapres.c_str(),
                    mappedMapres,
                    (sizeof(mappedMapres) / sizeof(mappedMapres[0])) - 1);

            char moduleNameUtf8[64] = {};
            char modulePathUtf8[1024] = {};
            char overloadRootUtf8[1024] = {};
            char mappedMapresUtf8[1024] = {};
            toUtf8(moduleName, moduleNameUtf8, sizeof(moduleNameUtf8));
            toUtf8(modulePath, modulePathUtf8, sizeof(modulePathUtf8));
            toUtf8(overloadRoot, overloadRootUtf8, sizeof(overloadRootUtf8));
            toUtf8(mappedMapres, mappedMapresUtf8, sizeof(mappedMapresUtf8));

            char line[1900] = {};
            std::snprintf(
                line,
                sizeof(line),
                "RDK Assassin Knife FILELOADER module=%s loaded=%d path=%s ual-export=%d ual=%d overload-api=%d overload-root-status=%s overload-root=%s mapres-api=%d mapres-mapped=%d mapres-path=%s",
                moduleNameUtf8[0] != '\0' ? moduleNameUtf8 : "<unknown>",
                module != nullptr ? 1 : 0,
                modulePathUtf8[0] != '\0' ? modulePathUtf8 : "<not-loaded>",
                isUltimateAsiLoader != nullptr ? 1 : 0,
                isUltimate ? 1 : 0,
                getOverloadPath != nullptr ? 1 : 0,
                getOverloadPath == nullptr ? "api-unavailable"
                    : (overloadActive ? "available" : "not-reported"),
                overloadRootUtf8[0] != '\0'
                    ? overloadRootUtf8 : "<none>",
                getOverloadedFilePath != nullptr ? 1 : 0,
                mapresMapped ? 1 : 0,
                mappedMapresUtf8[0] != '\0'
                    ? mappedMapresUtf8 : "<none>");
            WriteTrace(line);
        }

        HANDLE mapres = CreateFileW(
            stockMapres.c_str(),
            GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
        const DWORD openError =
            mapres == INVALID_HANDLE_VALUE ? GetLastError() : 0;

        wchar_t openedPath[1024] = {};
        DWORD pathLength = 0;
        DWORD pathError = 0;
        if (mapres != INVALID_HANDLE_VALUE)
        {
            pathLength = GetFinalPathNameByHandleW(
                mapres,
                openedPath,
                1024,
                FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
            pathError = pathLength == 0 ? GetLastError()
                : (pathLength >= 1024 ? ERROR_INSUFFICIENT_BUFFER : 0);
            CloseHandle(mapres);
        }

        if (pathError != 0)
            openedPath[0] = L'\0';

        const wchar_t* finalPath = openedPath;
        if (std::wcsncmp(finalPath, L"\\\\?\\", 4) == 0)
            finalPath += 4;

        const bool openedUpdate =
            pathError == 0
            && pathLength != 0
            && _wcsicmp(finalPath, updateMapres.c_str()) == 0;

        char openedPathUtf8[1024] = {};
        toUtf8(finalPath, openedPathUtf8, sizeof(openedPathUtf8));

        char line[1400] = {};
        std::snprintf(
            line,
            sizeof(line),
            "RDK Assassin Knife FILELOADER mapres-open-update=%d open-error=%lu path-error=%lu mapres-open-path=%s (read-only probe; archive mount not verified)",
            openedUpdate ? 1 : 0,
            static_cast<unsigned long>(openError),
            static_cast<unsigned long>(pathError),
            openedPathUtf8[0] != '\0'
                ? openedPathUtf8 : "<unavailable>");
        WriteTrace(line);
    }

    float ReadKnifeAmmo(Actor player)
    {
        const int ammoEnum =
            invoke<int>(GET_AMMO_ENUM_HASH, THROWING_KNIFE_WEAPON);
        return ammoEnum >= 0
            ? invoke<float>(ACTOR_GET_INV_AMMO_HASH, player, ammoEnum, TRUE)
            : -1.0f;
    }

    bool HasLinkedAction(Actor actor)
    {
        return invoke<int>(GET_LINKED_ANIM_TARGET_HASH, actor) != 0
            || invoke<BOOL>(IS_ACTOR_PERFORMING_LINKED_ANIMATION_HASH, actor)
                != FALSE;
    }

    bool IsGameActionDown(const char* action)
    {
        return invoke<BOOL>(IS_DIGITAL_ACTION_DOWN_HASH, action, 1, 0) != FALSE;
    }

    void TraceCombatState(
        const char* event,
        Actor player,
        Actor target,
        int attempt)
    {
        if (player == 0 || !ENTITY::IS_ACTOR_VALID(player))
            return;

        const bool valid =
            target != 0 && ENTITY::IS_ACTOR_VALID(target);
        char line[1700] = {};
        std::snprintf(
            line,
            sizeof(line),
            "RDK #94 combat sample: event=%s throw=%llu attempt=%d flow=%d running=%d player=%lld target=%lld target-valid=%d health=%.3f max-health=%.3f alive=%d proof=%d one-shot=%d weapon=%d ammo=%.1f attack-time=%.3f attack-target=%lld player-ready=%d target-ready=%d player-throwing=%d player-reacting=%d target-reacting=%d player-action-time=%.3f target-action-time=%.3f LMB=%d RMB=%d game-fire=%d game-target=%d player-linked=%d target-linked=%d player-animation=%d target-animation=%d target-ground=%d target-incap=%d target-gait=%d target-posture=%d target-heading=%.2f",
            event,
            g_throwExecutionFlow.request.throwId,
            attempt,
            static_cast<int>(g_throwExecutionFlow.state),
            g_executionRunning ? 1 : 0,
            static_cast<long long>(player),
            static_cast<long long>(target),
            valid ? 1 : 0,
            valid
                ? static_cast<double>(
                    invoke<float>(GET_ACTOR_HEALTH_HASH, target))
                : -1.0,
            valid
                ? static_cast<double>(
                    invoke<float>(GET_ACTOR_MAX_HEALTH_HASH, target))
                : -1.0,
            valid && HEALTH::IS_ACTOR_ALIVE(target) ? 1 : 0,
            valid
                ? invoke<int>(
                    GET_ACTOR_PROOF_HASH,
                    target)
                : -1,
            valid
                ? invoke<int>(
                    GET_ACTOR_ONE_SHOT_DEATH_STATUS_HASH,
                    target)
                : -1,
            INVENTORY::GET_WEAPON_IN_HAND(player),
            static_cast<double>(ReadKnifeAmmo(player)),
            static_cast<double>(
                invoke<float>(GET_LAST_ATTACK_TIME_HASH, player)),
            static_cast<long long>(
                invoke<Actor>(GET_LAST_ATTACK_TARGET_HASH, player)),
            invoke<BOOL>(
                IS_ACTOR_READY_FOR_ACTION_HASH,
                player) != FALSE ? 1 : 0,
            valid
                && invoke<BOOL>(
                    IS_ACTOR_READY_FOR_ACTION_HASH,
                    target) != FALSE ? 1 : 0,
            invoke<BOOL>(IS_ACTOR_THROWING_HASH, player) != FALSE
                ? 1 : 0,
            invoke<BOOL>(IS_ACTOR_REACTING_HASH, player) != FALSE
                ? 1 : 0,
            valid
                && invoke<BOOL>(
                    IS_ACTOR_REACTING_HASH,
                    target) != FALSE ? 1 : 0,
            static_cast<double>(
                invoke<float>(
                    GET_CURR_ACTION_NODE_PLAY_TIME_HASH,
                    player)),
            valid
                ? static_cast<double>(
                    invoke<float>(
                        GET_CURR_ACTION_NODE_PLAY_TIME_HASH,
                        target))
                : -1.0,
            (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0 ? 1 : 0,
            (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0 ? 1 : 0,
            IsGameActionDown("@GENERIC.FIRE") ? 1 : 0,
            IsGameActionDown("@GENERIC.TARGET") ? 1 : 0,
            invoke<int>(GET_LINKED_ANIM_TARGET_HASH, player),
            valid
                ? invoke<int>(GET_LINKED_ANIM_TARGET_HASH, target)
                : 0,
            invoke<BOOL>(
                IS_ACTOR_PERFORMING_LINKED_ANIMATION_HASH,
                player) != FALSE ? 1 : 0,
            valid
                && invoke<BOOL>(
                    IS_ACTOR_PERFORMING_LINKED_ANIMATION_HASH,
                    target) != FALSE ? 1 : 0,
            valid
                && invoke<BOOL>(
                    IS_ACTOR_ON_GROUND_HASH,
                    target) != FALSE ? 1 : 0,
            valid
                && invoke<BOOL>(
                    GET_ACTOR_INCAPACITATED_HASH,
                    target) != FALSE ? 1 : 0,
            valid ? invoke<int>(GET_ACTOR_GAIT_TYPE_HASH, target) : -1,
            valid ? invoke<int>(GET_ACTOR_POSTURE_HASH, target) : -1,
            valid
                ? static_cast<double>(
                    invoke<float>(GET_HEADING_HASH, target))
                : 0.0);
        WriteTrace(line);
    }

    bool IsLivingActor(Actor actor)
    {
        return actor != 0
            && ENTITY::IS_ACTOR_VALID(actor)
            && HEALTH::IS_ACTOR_ALIVE(actor);
    }

    void GrantMaxThrowingKnives(Actor player)
    {
        if (g_detector.starterKnivesGranted || !IsLivingActor(player))
            return;

        const ULONGLONG now = GetTickCount64();
        if (now < g_detector.nextStarterGrantAttempt)
            return;
        g_detector.nextStarterGrantAttempt = now + 1000;

        // Mirror RDR's own weapon-25 grant path: unlock the weapon enum first,
        // then give the weapon. Throwing knives use inventory ammo type 3,
        // so the reserve must be filled through the inventory-ammo natives
        // rather than ACTOR_SET_WEAPON_AMMO (which is the wrong ammo pool).
        invoke<void>(
            SET_WEAPONENUM_LOCKED_HASH,
            THROWING_KNIFE_WEAPON,
            false);

        invoke<void>(
            GIVE_WEAPON_TO_ACTOR_HASH,
            player,
            THROWING_KNIFE_WEAPON,
            1.0f,
            false,
            true);

        const int ammoEnum =
            invoke<int>(GET_AMMO_ENUM_HASH, THROWING_KNIFE_WEAPON);
        const float maxInventoryAmmo =
            invoke<float>(
                ACTOR_GET_INV_AMMO_MAX_AMOUNT_HASH,
                player,
                ammoEnum);

        if (ammoEnum >= 0
            && std::isfinite(maxInventoryAmmo)
            && maxInventoryAmmo > 0.0f)
        {
            invoke<void>(
                ACTOR_SET_INV_AMMO_HASH,
                player,
                ammoEnum,
                maxInventoryAmmo,
                false);
        }

        const bool locked =
            invoke<BOOL>(
                IS_WEAPONENUM_LOCKED_HASH,
                THROWING_KNIFE_WEAPON) != 0;
        const bool hasWeapon =
            invoke<BOOL>(
                ACTOR_HAS_WEAPON_HASH,
                player,
                THROWING_KNIFE_WEAPON) != 0;
        const float actualInventoryAmmo =
            ammoEnum >= 0
                ? invoke<float>(
                    ACTOR_GET_INV_AMMO_HASH,
                    player,
                    ammoEnum,
                    true)
                : -1.0f;

        char line[512] = {};
        std::snprintf(
            line,
            sizeof(line),
            "RDK starter throwing-knife grant attempt: player=%lld locked=%d has-weapon=%d ammo-enum=%d max-inventory=%.1f actual-inventory=%.1f",
            static_cast<long long>(player),
            locked ? 1 : 0,
            hasWeapon ? 1 : 0,
            ammoEnum,
            static_cast<double>(maxInventoryAmmo),
            static_cast<double>(actualInventoryAmmo));
        WriteTrace(line);

        if (!locked
            && hasWeapon
            && ammoEnum >= 0
            && std::isfinite(actualInventoryAmmo)
            && actualInventoryAmmo > 0.0f)
        {
            g_detector.starterKnivesGranted = true;
            Trace("RDK starter throwing knives ready");
        }
    }

    Vector3 ReadPosition(Actor actor)
    {
        NativeVector3 nativePosition;
        invoke<void>(GET_POSITION_HASH, actor, &nativePosition);

        Vector3 position{};
        position.x = nativePosition.x;
        position.y = nativePosition.y;
        position.z = nativePosition.z;
        return position;
    }

    bool IsFinitePosition(const Vector3& position)
    {
        return std::isfinite(position.x)
            && std::isfinite(position.y)
            && std::isfinite(position.z);
    }

    float DistanceSquared(const Vector3& a, const Vector3& b)
    {
        const float dx = a.x - b.x;
        const float dy = a.y - b.y;
        const float dz = a.z - b.z;
        return dx * dx + dy * dy + dz * dz;
    }

    float GroundPlaneDistanceSquared(const Vector3& a, const Vector3& b)
    {
        // RDR1 uses X/Z as the ground plane and Y as elevation. Teleporting
        // John to an execution slot can legitimately ground-snap Y while X/Z
        // remain exactly correct, so execution placement must not reject that.
        const float dx = a.x - b.x;
        const float dz = a.z - b.z;
        return dx * dx + dz * dz;
    }

    bool ComputeDistanceOnlyExecutionPosition(
        const Vector3& playerPosition,
        const Vector3& targetPosition,
        Vector3& desired)
    {
        if (!IsFinitePosition(playerPosition)
            || !IsFinitePosition(targetPosition))
        {
            return false;
        }

        // Preserve the original target->John ground-plane direction. Automatic
        // repositioning may only shorten that distance; it must not move John
        // around the target into a forced front/rear slot.
        const float dx = playerPosition.x - targetPosition.x;
        const float dz = playerPosition.z - targetPosition.z;
        const float distanceSquared = dx * dx + dz * dz;
        if (!std::isfinite(distanceSquared)
            || distanceSquared <= 0.0001f)
        {
            return false;
        }

        const float distance = std::sqrt(distanceSquared);
        const float desiredDistance =
            distance > FRONT_EXECUTION_OFFSET_METRES
                ? FRONT_EXECUTION_OFFSET_METRES
                : distance;
        const float scale = desiredDistance / distance;

        desired.x = targetPosition.x + dx * scale;
        desired.y =
            distance > FRONT_EXECUTION_OFFSET_METRES
                ? targetPosition.y
                : playerPosition.y;
        desired.z = targetPosition.z + dz * scale;
        return IsFinitePosition(desired);
    }

    bool IsGameplayInterrupted()
    {
        return invoke<BOOL>(IS_GAME_PAUSED_HASH) != 0
            || invoke<BOOL>(CUTSCENE_MANAGER_IS_CUTSCENE_PLAYING_HASH) != 0;
    }

    void PlayTeleportArrivalShock()
    {
        invoke<void>(
            SET_SHOCK_AMPLITUDE_HASH,
            TELEPORT_SHOCK_AMPLITUDE);
        invoke<void>(
            SET_SHOCK_SPEED_HASH,
            TELEPORT_SHOCK_SPEED);
        invoke<void>(
            FIRE_SHOCK_HASH,
            TELEPORT_SHOCK_STRENGTH);
        Trace(
            "RDK #137 teleport arrival shock fired: amplitude=1.0 speed=9.0 strength=1.0; smoke deferred");
    }

    void ResetAssassinOutfitForPlayer(Actor player)
    {
        g_outfit = {};
        g_outfit.player = player;
        g_outfit.glovesEnabled = true;

        char line[384] = {};
        std::snprintf(
            line,
            sizeof(line),
            "RDK #118 outfit context reset: player=%lld variation=%d preset=bandana+gloves",
            static_cast<long long>(player),
            ASSASSIN_OUTFIT_DEADLY_ASSASSIN);
        WriteTrace(line);
    }

    void TraceOutfitMeshState(
        Actor player,
        int meshId,
        const char* label)
    {
        const bool hasMesh =
            invoke<BOOL>(
                ACTOR_HAS_VARIABLE_MESH_HASH,
                player,
                meshId) != FALSE;
        const bool enabled =
            hasMesh
                && invoke<BOOL>(
                    ACTOR_IS_VARIABLE_MESH_ENABLED_HASH,
                    player,
                    meshId) != FALSE;

        char line[448] = {};
        std::snprintf(
            line,
            sizeof(line),
            "RDK #118 mesh probe: label=%s id=%d present=%d enabled=%d",
            label,
            meshId,
            hasMesh ? 1 : 0,
            enabled ? 1 : 0);
        WriteTrace(line);
    }

    bool SetOptionalOutfitMesh(
        Actor player,
        int meshId,
        const char* label,
        bool enable)
    {
        const bool hasMesh =
            invoke<BOOL>(
                ACTOR_HAS_VARIABLE_MESH_HASH,
                player,
                meshId) != FALSE;
        if (!hasMesh)
        {
            char line[448] = {};
            std::snprintf(
                line,
                sizeof(line),
                "RDK #118 mesh skipped: label=%s id=%d present=0 requested=%d",
                label,
                meshId,
                enable ? 1 : 0);
            WriteTrace(line);
            return true;
        }

        const bool before =
            invoke<BOOL>(
                ACTOR_IS_VARIABLE_MESH_ENABLED_HASH,
                player,
                meshId) != FALSE;

        invoke<void>(
            ACTOR_ENABLE_VARIABLE_MESH_HASH,
            player,
            meshId,
            enable ? TRUE : FALSE);

        const bool after =
            invoke<BOOL>(
                ACTOR_IS_VARIABLE_MESH_ENABLED_HASH,
                player,
                meshId) != FALSE;

        char line[512] = {};
        std::snprintf(
            line,
            sizeof(line),
            "RDK #118 mesh toggle: label=%s id=%d present=1 before=%d requested=%d after=%d result=%s",
            label,
            meshId,
            before ? 1 : 0,
            enable ? 1 : 0,
            after ? 1 : 0,
            after == enable ? "ok" : "warning");
        WriteTrace(line);

        // Some player models expose a variable-mesh id but the game can still
        // force its render state. Treat that as a runtime warning, not a reason
        // to fight the engine every frame.
        return after == enable;
    }

    void ReassertAssassinHatOff(Actor player, bool traceAlways)
    {
        if (!IsLivingActor(player))
            return;

        const bool before =
            invoke<BOOL>(
                IS_ACCESSORY_EQUIPPED_HASH,
                player,
                PLAYER_ACCESSORY_HAT) != FALSE;

        // AccessoryModel from the RDR PC native DB: hat=0, bandana=1. Keep the
        // Assassin preset intentionally hatless even if gameplay restores or
        // equips John's hat after the variation was applied.
        invoke<void>(
            DEEQUIP_ACCESSORY_HASH,
            player,
            PLAYER_ACCESSORY_HAT);

        const bool after =
            invoke<BOOL>(
                IS_ACCESSORY_EQUIPPED_HASH,
                player,
                PLAYER_ACCESSORY_HAT) != FALSE;

        if (traceAlways || before || after)
        {
            char line[384] = {};
            std::snprintf(
                line,
                sizeof(line),
                "RDK #118 assassin hat-off policy: accessory-slot=%d before=%d after=%d result=%s",
                PLAYER_ACCESSORY_HAT,
                before ? 1 : 0,
                after ? 1 : 0,
                !after ? "hat-off" : "warning-still-equipped");
            WriteTrace(line);
        }
    }

    bool ApplyAssassinOutfit(Actor player)
    {
        if (!IsLivingActor(player))
            return false;

        const int beforeVariation =
            invoke<int>(GET_CURRENT_ACTOR_ENUM_VARIATION_HASH, player);

        if (!g_outfit.originalCaptured)
        {
            g_outfit.originalVariation = beforeVariation;
            g_outfit.originalCaptured = true;

            char captureLine[384] = {};
            std::snprintf(
                captureLine,
                sizeof(captureLine),
                "RDK #118 original outfit captured: player=%lld variation=%d",
                static_cast<long long>(player),
                g_outfit.originalVariation);
            WriteTrace(captureLine);
        }

        int activeVariation = beforeVariation;
        bool variationSwitched = false;
        if (beforeVariation != ASSASSIN_OUTFIT_DEADLY_ASSASSIN)
        {
            invoke<int>(
                SWITCH_ACTOR_ENUM_VARIATION_HASH,
                player,
                ASSASSIN_OUTFIT_DEADLY_ASSASSIN);

            const ULONGLONG switchStarted = GetTickCount64();
            int switchProbes = 0;
            do
            {
                activeVariation =
                    invoke<int>(
                        GET_CURRENT_ACTOR_ENUM_VARIATION_HASH,
                        player);
                if (activeVariation == ASSASSIN_OUTFIT_DEADLY_ASSASSIN)
                    break;

                if (CurrentPlayer() != player
                    || !IsLivingActor(player)
                    || IsGameplayInterrupted())
                {
                    Trace(
                        "RDK #118 outfit switch aborted while waiting for asynchronous variation change");
                    return false;
                }

                ++switchProbes;
                scriptWait(0);
            }
            while (
                GetTickCount64() - switchStarted
                < ASSASSIN_OUTFIT_SWITCH_TIMEOUT_MS);

            variationSwitched =
                activeVariation == ASSASSIN_OUTFIT_DEADLY_ASSASSIN;

            char switchLine[576] = {};
            std::snprintf(
                switchLine,
                sizeof(switchLine),
                "RDK #118 outfit switch settled: player=%lld before=%d requested=%d after=%d elapsed-ms=%llu probes=%d preset=%s",
                static_cast<long long>(player),
                beforeVariation,
                ASSASSIN_OUTFIT_DEADLY_ASSASSIN,
                activeVariation,
                static_cast<unsigned long long>(
                    GetTickCount64() - switchStarted),
                switchProbes,
                g_outfit.glovesEnabled ? "bandana+gloves" : "bandana-only");
            WriteTrace(switchLine);

            if (!variationSwitched)
            {
                g_outfit.failed = true;
                Trace(
                    "RDK #118 outfit apply failed: requested variation did not settle before timeout");
                return false;
            }
        }
        else
        {
            char activeLine[448] = {};
            std::snprintf(
                activeLine,
                sizeof(activeLine),
                "RDK #118 outfit switch skipped: variation=%d already active preset=%s",
                beforeVariation,
                g_outfit.glovesEnabled ? "bandana+gloves" : "bandana-only");
            WriteTrace(activeLine);
        }

        // Keep the normal long-arm holster untouched. Probe it only so the
        // runtime log records whether this outfit exposes that mesh.
        TraceOutfitMeshState(
            player,
            PLAYER_MESH_LONGARM_HOLSTER,
            "longarm-holster");

        // Player mesh documentation has gaps at 22 and 24. Probe those IDs
        // without toggling them so runtime testing can tell us whether this PC
        // build exposes any undocumented headwear-related mesh.
        TraceOutfitMeshState(player, 22, "undocumented-22");
        TraceOutfitMeshState(player, 24, "undocumented-24");

        // Make the Assassin silhouette explicitly hatless rather than relying
        // on the current variation/gameplay state. Do this before forcing the
        // bandana so the two accessory policies are independent.
        ReassertAssassinHatOff(player, true);

        // Rockstar's own bandana.c directly toggles player mesh 26; it
        // does not gate that call behind ACTOR_HAS_VARIABLE_MESH. The bounded
        // variation-settle loop above also ensures the outfit is actually
        // active before this render state is applied.
        invoke<void>(
            EQUIP_ACCESSORY_HASH,
            player,
            PLAYER_ACCESSORY_BANDANA,
            TRUE);
        invoke<void>(
            ACTOR_ENABLE_VARIABLE_MESH_HASH,
            player,
            PLAYER_MESH_BANDANA,
            TRUE);

        const bool bandanaAccessoryEquipped =
            invoke<BOOL>(
                IS_ACCESSORY_EQUIPPED_HASH,
                player,
                PLAYER_ACCESSORY_BANDANA) != FALSE;
        const bool bandanaMeshEnabled =
            invoke<BOOL>(
                ACTOR_IS_VARIABLE_MESH_ENABLED_HASH,
                player,
                PLAYER_MESH_BANDANA) != FALSE;
        const bool bandanaMeshReportedPresent =
            invoke<BOOL>(
                ACTOR_HAS_VARIABLE_MESH_HASH,
                player,
                PLAYER_MESH_BANDANA) != FALSE;

        char bandanaLine[512] = {};
        std::snprintf(
            bandanaLine,
            sizeof(bandanaLine),
            "RDK #118 bandana forced: accessory-slot=%d equipped=%d mesh=%d reported-present=%d enabled=%d variation=%d",
            PLAYER_ACCESSORY_BANDANA,
            bandanaAccessoryEquipped ? 1 : 0,
            PLAYER_MESH_BANDANA,
            bandanaMeshReportedPresent ? 1 : 0,
            bandanaMeshEnabled ? 1 : 0,
            activeVariation);
        WriteTrace(bandanaLine);

        // Treat the actual enabled state as authoritative for this known John
        // mesh; ACTOR_HAS_VARIABLE_MESH is logged only as a diagnostic.
        bool meshSetupClean = bandanaMeshEnabled;

        if (g_outfit.glovesEnabled)
        {
            // These arm meshes are mutually exclusive. Remove the bare-arm
            // meshes first so the game never has to resolve both variants as
            // enabled while the glove preset is being restored.
            meshSetupClean &=
                SetOptionalOutfitMesh(
                    player,
                    PLAYER_MESH_LEFT_NO_GLOVE,
                    "left-no-glove",
                    false);
            meshSetupClean &=
                SetOptionalOutfitMesh(
                    player,
                    PLAYER_MESH_RIGHT_NO_GLOVE,
                    "right-no-glove",
                    false);
            meshSetupClean &=
                SetOptionalOutfitMesh(
                    player,
                    PLAYER_MESH_LEFT_GLOVE,
                    "left-glove",
                    true);
            meshSetupClean &=
                SetOptionalOutfitMesh(
                    player,
                    PLAYER_MESH_RIGHT_GLOVE,
                    "right-glove",
                    true);
        }
        else
        {
            meshSetupClean &=
                SetOptionalOutfitMesh(
                    player,
                    PLAYER_MESH_LEFT_GLOVE,
                    "left-glove",
                    false);
            meshSetupClean &=
                SetOptionalOutfitMesh(
                    player,
                    PLAYER_MESH_RIGHT_GLOVE,
                    "right-glove",
                    false);
            meshSetupClean &=
                SetOptionalOutfitMesh(
                    player,
                    PLAYER_MESH_LEFT_NO_GLOVE,
                    "left-no-glove",
                    true);
            meshSetupClean &=
                SetOptionalOutfitMesh(
                    player,
                    PLAYER_MESH_RIGHT_NO_GLOVE,
                    "right-no-glove",
                    true);
        }

        g_outfit.applied = true;
        g_outfit.failed = false;

        char appliedLine[512] = {};
        std::snprintf(
            appliedLine,
            sizeof(appliedLine),
            "RDK #118 outfit applied: player=%lld variation=%d original=%d preset=%s mesh-result=%s",
            static_cast<long long>(player),
            ASSASSIN_OUTFIT_DEADLY_ASSASSIN,
            g_outfit.originalVariation,
            g_outfit.glovesEnabled ? "bandana+gloves" : "bandana-only",
            meshSetupClean ? "clean" : "warnings");
        WriteTrace(appliedLine);
        return true;
    }

    void HandleOutfitInspectionTrigger()
    {
        if (!IsKeyJustUp(OUTFIT_INSPECTION_KEY))
            return;

        Trace("RDK #118 outfit inspection trigger received: F9");

        if (g_executionRunning)
        {
            Trace(
                "RDK #118 outfit inspection rejected: execution is running");
            return;
        }

        const Actor player = CurrentPlayer();
        if (!IsLivingActor(player))
        {
            Trace(
                "RDK #118 outfit inspection rejected: local player invalid/dead");
            return;
        }
        if (IsGameplayInterrupted())
        {
            Trace(
                "RDK #118 outfit inspection rejected: pause/cutscene active");
            return;
        }

        if (g_outfit.player != player)
            ResetAssassinOutfitForPlayer(player);

        g_outfit.glovesEnabled = !g_outfit.glovesEnabled;
        g_outfit.applied = false;
        g_outfit.failed = false;

        char line[448] = {};
        std::snprintf(
            line,
            sizeof(line),
            "RDK #118 outfit preset selected: variation=%d preset=%s",
            ASSASSIN_OUTFIT_DEADLY_ASSASSIN,
            g_outfit.glovesEnabled ? "bandana+gloves" : "bandana-only");
        WriteTrace(line);
    }

    void UpdateAssassinOutfit()
    {
        const Actor player = CurrentPlayer();
        if (player != g_outfit.player)
            ResetAssassinOutfitForPlayer(player);

        if (!IsLivingActor(player))
        {
            // The same actor handle can occasionally survive a death/respawn
            // transition. Clearing the one-shot flags here allows one clean
            // reapply when that actor becomes living again.
            g_outfit.applied = false;
            g_outfit.failed = false;
            return;
        }

        if (g_executionRunning || IsGameplayInterrupted())
            return;

        if (g_outfit.applied)
        {
            ReassertAssassinHatOff(player, false);
            return;
        }
        if (g_outfit.failed)
            return;

        ApplyAssassinOutfit(player);
    }

    bool IsEligibleNpc(Actor actor, Actor player)
    {
        return IsLivingActor(actor)
            && actor != player
            && ENTITY::IS_ACTOR_HUMAN(actor)
            && !invoke<BOOL>(IS_ACTOR_PLAYER_HASH, actor)
            && !invoke<BOOL>(IS_ACTOR_LOCAL_PLAYER_HASH, actor)
            && !invoke<BOOL>(IS_ACTOR_RIDING_HASH, actor)
            && !invoke<BOOL>(IS_ACTOR_INSIDE_VEHICLE_HASH, actor);
    }

    float NormalizeHeading(float heading)
    {
        if (!std::isfinite(heading))
            return heading;

        heading = std::fmod(heading, 360.0f);
        if (heading < 0.0f)
            heading += 360.0f;
        return heading;
    }

    float HeadingDifference(float a, float b)
    {
        a = NormalizeHeading(a);
        b = NormalizeHeading(b);
        if (!std::isfinite(a) || !std::isfinite(b))
            return 360.0f;

        float difference = std::fabs(a - b);
        if (difference > 180.0f)
            difference = 360.0f - difference;
        return difference;
    }

    float ReadHeading(Actor actor)
    {
        return invoke<float>(GET_HEADING_HASH, actor);
    }

    void TraceAutomaticFacing(
        const char* stage,
        Actor player,
        Actor target,
        int sample)
    {
        // Observe only the active captured request; never change aim, facing
        // or target ownership. Manual F8 does not enter this diagnostic.
        if (g_throwExecutionFlow.state != ThrowExecutionFlowState::Executing
            || g_throwExecutionFlow.request.shooter != player
            || g_throwExecutionFlow.request.target != target
            || !IsLivingActor(player)
            || target == 0 || !ENTITY::IS_ACTOR_VALID(target))
            return;

        const Vector3 playerPosition = ReadPosition(player);
        const Vector3 targetPosition = ReadPosition(target);
        const float playerHeading = ReadHeading(player);
        const float targetHeading = ReadHeading(target);
        if (!IsFinitePosition(playerPosition)
            || !IsFinitePosition(targetPosition)
            || !std::isfinite(playerHeading)
            || !std::isfinite(targetHeading))
        {
            Trace("RDK #94 automatic facing sample unavailable: invalid transform");
            return;
        }

        const float dx = targetPosition.x - playerPosition.x;
        const float dz = targetPosition.z - playerPosition.z;
        const float distance = std::sqrt(dx * dx + dz * dz);
        const bool bearingValid = distance > 0.001f;
        const float radians = playerHeading * PI / 180.0f;
        // Native-axis trace 20260928-112151: axis 2=(sin heading, cos heading)
        // in X/Z, with the aimed NPC along its negative direction. Forward is
        // (-sin heading, -cos heading); right is (cos heading, -sin heading).
        // Positive signed angle means left; positive right distance means right.
        const float forward = -dx * std::sin(radians) - dz * std::cos(radians);
        const float right = dx * std::cos(radians) - dz * std::sin(radians);
        const float bearing = bearingValid
            ? NormalizeHeading(std::atan2(-dx, -dz) * 180.0f / PI) : 0.0f;
        const float signedAngle = bearingValid
            ? std::remainder(bearing - playerHeading, 360.0f) : 0.0f;
        const char* side = !bearingValid ? "overlapping"
            : right < -0.01f ? "left" : right > 0.01f ? "right" : "center";

        // Retain raw axis 2 as an independent check on the corrected heading
        // formula, using the same padded native-vector ABI as ReadPosition.
        NativeVector3 playerAxis;
        NativeVector3 targetAxis;
        invoke<void>(GET_ACTOR_AXIS_HASH, player, &playerAxis, 2);
        invoke<void>(GET_ACTOR_AXIS_HASH, target, &targetAxis, 2);
        const float playerAxisLength = std::sqrt(
            playerAxis.x * playerAxis.x + playerAxis.z * playerAxis.z);
        const float targetAxisLength = std::sqrt(
            targetAxis.x * targetAxis.x + targetAxis.z * targetAxis.z);
        const bool playerAxisValid = std::isfinite(playerAxisLength)
            && std::isfinite(playerAxis.y) && playerAxisLength > 0.001f;
        const bool targetAxisValid = std::isfinite(targetAxisLength)
            && std::isfinite(targetAxis.y) && targetAxisLength > 0.001f;
        // Signed horizontal distances along each normalized native axis 2;
        // positive means along that axis, not a presumed front/back label.
        // Zero is a placeholder when the corresponding axis-valid flag is 0.
        const float targetOnPlayerAxis = playerAxisValid
            ? (dx * playerAxis.x + dz * playerAxis.z) / playerAxisLength : 0.0f;
        const float playerOnTargetAxis = targetAxisValid
            ? (-dx * targetAxis.x - dz * targetAxis.z) / targetAxisLength : 0.0f;

        char line[1536] = {};
        std::snprintf(line, sizeof(line),
            "RDK #94 automatic facing: stage=%s throw=%llu sample=%d sample-tick=%llu player=%lld captured-target=%lld player=(%.3f,%.3f,%.3f) target=(%.3f,%.3f,%.3f) player-heading=%.2f target-heading=%.2f slot-heading=%.2f heading-basis=rdr-forward-negative-axis2 bearing-valid=%d target-bearing=%.2f signed-facing-angle=%.2f target-forward-m=%.3f target-right-m=%.3f target-side=%s ground-distance=%.3f height-delta=%.3f player-axis2=(%.4f,%.4f,%.4f) target-axis2=(%.4f,%.4f,%.4f) player-axis2-xz-length=%.4f target-axis2-xz-length=%.4f player-axis2-valid=%d target-axis2-valid=%d target-on-player-axis2-m=%.3f player-on-target-axis2-m=%.3f",
            stage,
            g_throwExecutionFlow.request.throwId,
            sample,
            static_cast<unsigned long long>(GetTickCount64()),
            static_cast<long long>(player),
            static_cast<long long>(target),
            static_cast<double>(playerPosition.x),
            static_cast<double>(playerPosition.y),
            static_cast<double>(playerPosition.z),
            static_cast<double>(targetPosition.x),
            static_cast<double>(targetPosition.y),
            static_cast<double>(targetPosition.z),
            static_cast<double>(playerHeading),
            static_cast<double>(targetHeading),
            static_cast<double>(NormalizeHeading(targetHeading + 180.0f)),
            bearingValid ? 1 : 0,
            static_cast<double>(bearing),
            static_cast<double>(signedAngle),
            static_cast<double>(forward),
            static_cast<double>(right),
            side,
            static_cast<double>(distance),
            static_cast<double>(targetPosition.y - playerPosition.y),
            static_cast<double>(playerAxis.x),
            static_cast<double>(playerAxis.y),
            static_cast<double>(playerAxis.z),
            static_cast<double>(targetAxis.x),
            static_cast<double>(targetAxis.y),
            static_cast<double>(targetAxis.z),
            static_cast<double>(playerAxisLength),
            static_cast<double>(targetAxisLength),
            playerAxisValid ? 1 : 0,
            targetAxisValid ? 1 : 0,
            static_cast<double>(targetOnPlayerAxis),
            static_cast<double>(playerOnTargetAxis));
        WriteTrace(line);
    }

    struct XInputRumbleApi
    {
        using GetStateFunction = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);
        using SetStateFunction = DWORD(WINAPI*)(DWORD, XINPUT_VIBRATION*);
        GetStateFunction getState = nullptr;
        SetStateFunction setState = nullptr;
    };

    const XInputRumbleApi& GetXInputRumbleApi()
    {
        static const XInputRumbleApi api = []() -> XInputRumbleApi
        {
            const wchar_t* libraries[] = {
                L"xinput1_4.dll",
                L"xinput9_1_0.dll"};
            for (const wchar_t* library : libraries)
            {
                const HMODULE module = LoadLibraryExW(
                    library,
                    nullptr,
                    LOAD_LIBRARY_SEARCH_SYSTEM32);
                if (module == nullptr)
                    continue;

                XInputRumbleApi candidate{};
                candidate.getState =
                    reinterpret_cast<XInputRumbleApi::GetStateFunction>(
                        GetProcAddress(module, "XInputGetState"));
                candidate.setState =
                    reinterpret_cast<XInputRumbleApi::SetStateFunction>(
                        GetProcAddress(module, "XInputSetState"));
                if (candidate.getState != nullptr
                    && candidate.setState != nullptr)
                {
                    return candidate; // Retain module for script lifetime.
                }

                FreeLibrary(module);
            }

            return {};
        }();

        return api;
    }

    bool StartTeleportControllerRumble(DWORD& slot)
    {
        slot = XUSER_MAX_COUNT;
        const XInputRumbleApi& api = GetXInputRumbleApi();
        if (api.getState == nullptr || api.setState == nullptr)
        {
            Trace("RDK #137 XInput rumble unavailable: XInputGetState/XInputSetState not resolved");
            return false;
        }

        for (DWORD candidate = 0; candidate < XUSER_MAX_COUNT; ++candidate)
        {
            XINPUT_STATE state{};
            if (api.getState(candidate, &state) != ERROR_SUCCESS)
                continue;

            XINPUT_VIBRATION vibration{};
            vibration.wLeftMotorSpeed = TELEPORT_RUMBLE_LEFT_MOTOR;
            vibration.wRightMotorSpeed = TELEPORT_RUMBLE_RIGHT_MOTOR;
            const DWORD result = api.setState(candidate, &vibration);
            if (result == ERROR_SUCCESS)
            {
                slot = candidate;
                return true;
            }
        }

        Trace("RDK #137 XInput rumble start failed: no connected XInput pad accepted vibration");
        return false;
    }

    bool RefreshTeleportControllerRumble(DWORD slot)
    {
        if (slot >= XUSER_MAX_COUNT)
            return false;

        const XInputRumbleApi& api = GetXInputRumbleApi();
        if (api.setState == nullptr)
            return false;

        XINPUT_VIBRATION vibration{};
        vibration.wLeftMotorSpeed = TELEPORT_RUMBLE_LEFT_MOTOR;
        vibration.wRightMotorSpeed = TELEPORT_RUMBLE_RIGHT_MOTOR;
        return api.setState(slot, &vibration) == ERROR_SUCCESS;
    }

    void StopTeleportControllerRumble(DWORD slot)
    {
        if (slot >= XUSER_MAX_COUNT)
            return;

        const XInputRumbleApi& api = GetXInputRumbleApi();
        if (api.setState == nullptr)
            return;

        XINPUT_VIBRATION vibration{};
        api.setState(slot, &vibration);
    }

    void ReadControllerInput(char* output, std::size_t capacity)
    {
        // Read-only XInput diagnostics. Resolve at startup without adding a
        // linker dependency or changing the game's input-device selection.
        using GetStateFunction = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);
        static const GetStateFunction getState = []() -> GetStateFunction
        {
            const wchar_t* libraries[] = {L"xinput1_4.dll", L"xinput9_1_0.dll"};
            for (const wchar_t* library : libraries)
            {
                const HMODULE module = LoadLibraryExW(
                    library, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
                if (module == nullptr)
                    continue;
                const auto function = reinterpret_cast<GetStateFunction>(
                    GetProcAddress(module, "XInputGetState"));
                if (function != nullptr)
                    return function; // Retain the module for the script lifetime.
                FreeLibrary(module);
            }
            return nullptr;
        }();

        if (getState == nullptr)
        {
            std::snprintf(output, capacity, "xinput=unavailable");
            return;
        }

        // Do not repeatedly poll disconnected slots in the alignment loop.
        // Every connected slot is identified explicitly; an XInput slot is
        // not assumed to be the same as RDR's local player slot.
        static ULONGLONG nextProbe[XUSER_MAX_COUNT] = {};
        static DWORD lastError[XUSER_MAX_COUNT] = {};
        char pads[XUSER_MAX_COUNT][160] = {};
        const ULONGLONG now = GetTickCount64();
        for (DWORD slot = 0; slot < XUSER_MAX_COUNT; ++slot)
        {
            if (now < nextProbe[slot])
            {
                std::snprintf(pads[slot], sizeof(pads[slot]),
                    "pad%lu=unavailable(error=%lu,retry-pending)",
                    static_cast<unsigned long>(slot),
                    static_cast<unsigned long>(lastError[slot]));
                continue;
            }

            XINPUT_STATE state{};
            const DWORD result = getState(slot, &state);
            lastError[slot] = result;
            if (result != ERROR_SUCCESS)
            {
                nextProbe[slot] = now + 1000;
                std::snprintf(pads[slot], sizeof(pads[slot]),
                    "pad%lu=unavailable(error=%lu)",
                    static_cast<unsigned long>(slot),
                    static_cast<unsigned long>(result));
                continue;
            }

            nextProbe[slot] = 0;
            const XINPUT_GAMEPAD& pad = state.Gamepad;
            std::snprintf(pads[slot], sizeof(pads[slot]),
                "pad%lu=(packet=%lu,LT=%u,RT=%u,LX=%d,LY=%d,RX=%d,RY=%d,buttons=0x%04X)",
                static_cast<unsigned long>(slot),
                static_cast<unsigned long>(state.dwPacketNumber),
                static_cast<unsigned int>(pad.bLeftTrigger),
                static_cast<unsigned int>(pad.bRightTrigger),
                static_cast<int>(pad.sThumbLX),
                static_cast<int>(pad.sThumbLY),
                static_cast<int>(pad.sThumbRX),
                static_cast<int>(pad.sThumbRY),
                static_cast<unsigned int>(pad.wButtons));
        }
        std::snprintf(output, capacity, "xinput=available %s %s %s %s",
            pads[0], pads[1], pads[2], pads[3]);
    }

    void TraceControllerInput(
        const char* stage,
        Actor player,
        Actor target,
        int sample)
    {
        if (player == 0 || !ENTITY::IS_ACTOR_VALID(player))
            return;

        const bool targetValid = target != 0 && ENTITY::IS_ACTOR_VALID(target);
        const int playerSlot = static_cast<int>(ACTOR::GET_LOCAL_SLOT());
        const ULONGLONG sampledAt = GetTickCount64();
        char controllers[704] = {};
        ReadControllerInput(controllers, sizeof(controllers));
        const float playerHeading = ReadHeading(player);
        const float targetHeading = targetValid ? ReadHeading(target) : 0.0f;
        char line[1800] = {};
        std::snprintf(line, sizeof(line),
            "RDK #94 controller context: stage=%s mode=%s throw=%llu sample=%d sample-tick=%llu player=%lld captured-target=%lld player-heading=%.2f target-heading=%.2f heading-error=%.2f player-ground=%d player-controllable=%d player-ready=%d player-throwing=%d player-action-time=%.3f game-target=%d game-fire=%d LMB=%d RMB=%d exact-targeted=%d target-actor=%lld reticle=%lld %s",
            stage,
            g_throwExecutionFlow.state == ThrowExecutionFlowState::Idle
                ? "manual-f8" : "automatic",
            g_throwExecutionFlow.request.throwId,
            sample,
            static_cast<unsigned long long>(sampledAt),
            static_cast<long long>(player),
            static_cast<long long>(target),
            static_cast<double>(playerHeading),
            static_cast<double>(targetHeading),
            targetValid ? static_cast<double>(HeadingDifference(
                playerHeading, NormalizeHeading(targetHeading + 180.0f))) : -1.0,
            invoke<BOOL>(IS_ACTOR_ON_GROUND_HASH, player) != FALSE ? 1 : 0,
            invoke<int>(IS_PLAYER_CONTROLLABLE_HASH, playerSlot),
            invoke<BOOL>(IS_ACTOR_READY_FOR_ACTION_HASH, player) != FALSE ? 1 : 0,
            invoke<BOOL>(IS_ACTOR_THROWING_HASH, player) != FALSE ? 1 : 0,
            static_cast<double>(invoke<float>(GET_CURR_ACTION_NODE_PLAY_TIME_HASH, player)),
            IsGameActionDown("@GENERIC.TARGET") ? 1 : 0,
            IsGameActionDown("@GENERIC.FIRE") ? 1 : 0,
            (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0 ? 1 : 0,
            (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0 ? 1 : 0,
            targetValid && invoke<BOOL>(IS_PLAYER_TARGETTING_ACTOR_HASH,
                playerSlot, target, TRUE) != FALSE ? 1 : 0,
            static_cast<long long>(invoke<Actor>(GET_TARGET_ACTOR_HASH)),
            static_cast<long long>(invoke<Actor>(GET_ACTOR_UNDER_RETICLE_HASH, player, 0)),
            controllers);
        WriteTrace(line);
        if (g_throwExecutionFlow.state == ThrowExecutionFlowState::Executing)
            TraceAutomaticFacing(stage, player, target, sample);
    }

    const char* ExecutionTargetRejectionReason(Actor target, Actor player)
    {
        if (!IsEligibleNpc(target, player))
            return "target is not a living on-foot human NPC";
        if (invoke<BOOL>(IS_ACTOR_ON_GROUND_HASH, target) == FALSE)
            return "target is not standing on ground";
        if (invoke<BOOL>(GET_ACTOR_INCAPACITATED_HASH, target) != FALSE)
            return "target is incapacitated";
        if (invoke<int>(GET_LINKED_ANIM_TARGET_HASH, target) != 0
            || invoke<BOOL>(IS_ACTOR_PERFORMING_LINKED_ANIMATION_HASH, target)
                != FALSE)
        {
            return "target already has linked-animation state";
        }
        return nullptr;
    }

    const char* AutomaticThrowTargetRejectionReason(
        Actor target,
        Actor player)
    {
        if (!IsEligibleNpc(target, player))
            return "target is not a living on-foot human NPC";

        // Automatic #94 owns the exact captured actor after throw start and
        // hard-locks/repositions it during the deterministic handoff. RDR's
        // IS_ACTOR_ON_GROUND can transiently report false on an otherwise
        // valid standing NPC, so ground state is diagnostic only here.
        if (invoke<BOOL>(GET_ACTOR_INCAPACITATED_HASH, target) != FALSE)
            return "target is incapacitated";
        if (invoke<int>(GET_LINKED_ANIM_TARGET_HASH, target) != 0
            || invoke<BOOL>(
                IS_ACTOR_PERFORMING_LINKED_ANIMATION_HASH,
                target) != FALSE)
        {
            return "target already has linked-animation state";
        }
        return nullptr;
    }

    bool WaitForAutomaticThrowRelease(
        Actor player,
        Actor target)
    {
        if (!IsEligibleNpc(target, player)
            || g_throwExecutionFlow.state
                != ThrowExecutionFlowState::ThrowCaptured
            || g_throwExecutionFlow.request.shooter != player
            || g_throwExecutionFlow.request.target != target)
        {
            return false;
        }

        const float startingAmmo = ReadKnifeAmmo(player);
        const float startingAttackTime =
            invoke<float>(
                GET_LAST_ATTACK_TIME_HASH,
                player);

        char startLine[640] = {};
        std::snprintf(
            startLine,
            sizeof(startLine),
            "RDK #94 throw release wait started: throw=%llu target=%lld timeout-ms=%llu starting-ammo=%.1f starting-attack-time=%.3f",
            g_throwExecutionFlow.request.throwId,
            static_cast<long long>(target),
            static_cast<unsigned long long>(
                AUTOMATIC_THROW_RELEASE_WAIT_MS),
            static_cast<double>(startingAmmo),
            static_cast<double>(startingAttackTime));
        WriteTrace(startLine);

        const ULONGLONG started = GetTickCount64();
        while (GetTickCount64() - started
            < AUTOMATIC_THROW_RELEASE_WAIT_MS)
        {
            scriptWait(0);

            if (IsGameplayInterrupted()
                || CurrentPlayer() != player
                || !IsLivingActor(player)
                || !IsEligibleNpc(target, player)
                || INVENTORY::GET_WEAPON_IN_HAND(player)
                    != THROWING_KNIFE_WEAPON)
            {
                Trace(
                    "RDK #94 throw release aborted: gameplay/player/target context changed");
                return false;
            }

            if (!MaintainAimedThrowTargetStaging(
                    g_throwExecutionFlow.request))
            {
                Trace(
                    "RDK #94 throw release aborted: exact target staging could not be maintained");
                return false;
            }

            TraceCombatState(
                "automatic throw release wait",
                player,
                target,
                0);

            if (HasLinkedAction(target)
                || HasLinkedAction(player))
            {
                Trace(
                    "RDK #94 throw release aborted: linked action already active");
                return false;
            }

            const float ammo = ReadKnifeAmmo(player);
            const float attackTime =
                invoke<float>(
                    GET_LAST_ATTACK_TIME_HASH,
                    player);
            const bool ammoReleased =
                startingAmmo >= 0.0f
                && ammo >= 0.0f
                && ammo < startingAmmo - 0.5f;
            const bool attackReleased =
                std::fabs(
                    attackTime - startingAttackTime)
                    > 0.0001f;

            if (ammoReleased || attackReleased)
            {
                const ULONGLONG releasedAt =
                    GetTickCount64();
                const Vector3 playerPosition =
                    ReadPosition(player);
                const Vector3 targetPosition =
                    ReadPosition(target);
                float releaseDistance = 0.0f;
                if (IsFinitePosition(playerPosition)
                    && IsFinitePosition(targetPosition))
                {
                    releaseDistance =
                        std::sqrt(
                            DistanceSquared(
                                playerPosition,
                                targetPosition));
                }

                ULONGLONG clearWindow =
                    static_cast<ULONGLONG>(
                        static_cast<double>(
                            PROJECTILE_CLEAR_MIN_MS)
                        + static_cast<double>(
                            releaseDistance)
                            * static_cast<double>(
                                PROJECTILE_CLEAR_PER_METRE_MS));
                if (clearWindow < PROJECTILE_CLEAR_MIN_MS)
                    clearWindow = PROJECTILE_CLEAR_MIN_MS;
                if (clearWindow > PROJECTILE_CLEAR_MAX_MS)
                    clearWindow = PROJECTILE_CLEAR_MAX_MS;

                g_throwExecutionFlow.request.projectileReleasedAt =
                    releasedAt;
                g_throwExecutionFlow.request.projectileClearWindowMs =
                    clearWindow;

                char releaseLine[768] = {};
                std::snprintf(
                    releaseLine,
                    sizeof(releaseLine),
                    "RDK #94 throw projectile released: throw=%llu target=%lld elapsed-ms=%llu ammo=%.1f attack-time=%.3f ammo-signal=%d attack-signal=%d player-throwing=%d target-reacting=%d release-distance=%.3f clear-window-ms=%llu hit-baseline=%.3f",
                    g_throwExecutionFlow.request.throwId,
                    static_cast<long long>(target),
                    static_cast<unsigned long long>(
                        releasedAt - started),
                    static_cast<double>(ammo),
                    static_cast<double>(attackTime),
                    ammoReleased ? 1 : 0,
                    attackReleased ? 1 : 0,
                    invoke<BOOL>(
                        IS_ACTOR_THROWING_HASH,
                        player) != FALSE ? 1 : 0,
                    invoke<BOOL>(
                        IS_ACTOR_REACTING_HASH,
                        target) != FALSE ? 1 : 0,
                    static_cast<double>(releaseDistance),
                    static_cast<unsigned long long>(
                        clearWindow),
                    static_cast<double>(
                        g_throwExecutionFlow.request.targetHitBaseline));
                WriteTrace(releaseLine);
                return true;
            }
        }

        TraceCombatState(
            "automatic throw release timeout",
            player,
            target,
            0);
        return false;
    }

    bool WaitForCapturedKnifeToClearStagedTarget(
        Actor player,
        Actor target)
    {
        const ULONGLONG started = GetTickCount64();
        bool exactKnifeImpactObserved = false;
        bool missWindowLogged = false;

        Trace(
            "RDK #94 staged target projectile wait started: keep only a short stand-still on the exact NPC until John's original knife hits that target or its bounded miss window expires; target reaction is diagnostic only and never gates handoff");

        while (GetTickCount64() - started
            < AUTOMATIC_THROW_RELEASE_WAIT_MS)
        {
            scriptWait(0);

            if (IsGameplayInterrupted()
                || CurrentPlayer() != player
                || !IsLivingActor(player)
                || target == 0
                || !ENTITY::IS_ACTOR_VALID(target)
                || !HEALTH::IS_ACTOR_ALIVE(target))
            {
                Trace(
                    "RDK #94 staged target projectile wait aborted: gameplay/player/target context changed");
                return false;
            }

            if (!MaintainAimedThrowTargetStaging(
                    g_throwExecutionFlow.request))
            {
                Trace(
                    "RDK #94 staged target projectile wait aborted: staging maintenance failed");
                return false;
            }

            const float currentHitTime =
                invoke<float>(
                    GET_LAST_HIT_TIME_HASH,
                    target);
            if (std::fabs(
                    currentHitTime
                    - g_throwExecutionFlow.request.targetHitBaseline)
                > 0.0001f)
            {
                const Actor attacker =
                    invoke<Actor>(
                        GET_LAST_ATTACKER_HASH,
                        target);
                const int hitWeapon =
                    invoke<int>(
                        GET_LAST_HIT_WEAPON_HASH,
                        target);

                g_throwExecutionFlow.request.targetHitBaseline =
                    currentHitTime;

                if (attacker == player
                    && hitWeapon == THROWING_KNIFE_WEAPON)
                {
                    exactKnifeImpactObserved = true;

                    // One final best-effort reaction reset after the exact
                    // original knife impact. The trace proved target-reacting
                    // may remain true, so never wait for that flag to clear.
                    invoke<void>(
                        RESET_REACT_NODE_FOR_ACTOR_HASH,
                        target);
                    invoke<void>(
                        TASK_STAND_STILL_HASH,
                        target,
                        AUTOMATIC_TARGET_STAGING_STAND_STILL_SECONDS,
                        0,
                        0);
                }

                char hitLine[832] = {};
                std::snprintf(
                    hitLine,
                    sizeof(hitLine),
                    "RDK #94 staged target hit observed: throw=%llu target=%lld hit-time=%.3f attacker=%lld attacker-is-john=%d weapon=%d knife=%d reacting=%d gait=%d ground=%d",
                    g_throwExecutionFlow.request.throwId,
                    static_cast<long long>(target),
                    static_cast<double>(currentHitTime),
                    static_cast<long long>(attacker),
                    attacker == player ? 1 : 0,
                    hitWeapon,
                    hitWeapon == THROWING_KNIFE_WEAPON ? 1 : 0,
                    invoke<BOOL>(
                        IS_ACTOR_REACTING_HASH,
                        target) != FALSE ? 1 : 0,
                    invoke<int>(
                        GET_ACTOR_GAIT_TYPE_HASH,
                        target),
                    invoke<BOOL>(
                        IS_ACTOR_ON_GROUND_HASH,
                        target) != FALSE ? 1 : 0);
                WriteTrace(hitLine);
            }

            const ULONGLONG releasedAt =
                g_throwExecutionFlow.request.projectileReleasedAt;
            const bool missWindowElapsed =
                releasedAt != 0
                && GetTickCount64() - releasedAt
                    >= g_throwExecutionFlow.request.projectileClearWindowMs;
            if (missWindowElapsed
                && !exactKnifeImpactObserved
                && !missWindowLogged)
            {
                missWindowLogged = true;
                Trace(
                    "RDK #94 staged target projectile miss window elapsed: original knife treated as clear while stand-still remains active");
            }

            if (exactKnifeImpactObserved || missWindowElapsed)
            {
                char readyLine[704] = {};
                std::snprintf(
                    readyLine,
                    sizeof(readyLine),
                    "RDK #94 staged target projectile clear: throw=%llu target=%lld exact-impact=%d miss-window=%d reacting=%d gait=%d ground=%d elapsed-ms=%llu",
                    g_throwExecutionFlow.request.throwId,
                    static_cast<long long>(target),
                    exactKnifeImpactObserved ? 1 : 0,
                    missWindowElapsed ? 1 : 0,
                    invoke<BOOL>(
                        IS_ACTOR_REACTING_HASH,
                        target) != FALSE ? 1 : 0,
                    invoke<int>(
                        GET_ACTOR_GAIT_TYPE_HASH,
                        target),
                    invoke<BOOL>(
                        IS_ACTOR_ON_GROUND_HASH,
                        target) != FALSE ? 1 : 0,
                    static_cast<unsigned long long>(
                        GetTickCount64() - started));
                WriteTrace(readyLine);
                return true;
            }
        }

        TraceCombatState(
            "staged target projectile wait timeout",
            player,
            target,
            0);
        return false;
    }

    bool WaitForAutomaticExecutionTriggerReady(
        Actor player,
        Actor target)
    {
        const ULONGLONG started = GetTickCount64();
        int reactionResetCount = 0;
        bool standStillRefreshed = false;
        bool capturedKnifeImpactObserved = false;
        bool missWindowLogged = false;

        char startLine[768] = {};
        std::snprintf(
            startLine,
            sizeof(startLine),
            "RDK #94 execution trigger-ready wait started: suppress captured-target knife-hit reaction while John finishes throw; projectile-release=%llu clear-window-ms=%llu hit-baseline=%.3f",
            static_cast<unsigned long long>(
                g_throwExecutionFlow.request.projectileReleasedAt),
            static_cast<unsigned long long>(
                g_throwExecutionFlow.request.projectileClearWindowMs),
            static_cast<double>(
                g_throwExecutionFlow.request.targetHitBaseline));
        WriteTrace(startLine);

        while (GetTickCount64() - started
            < AUTOMATIC_THROW_RELEASE_WAIT_MS)
        {
            if (IsGameplayInterrupted()
                || CurrentPlayer() != player
                || !IsLivingActor(player)
                || !IsEligibleNpc(target, player)
                || INVENTORY::GET_WEAPON_IN_HAND(player)
                    != THROWING_KNIFE_WEAPON)
            {
                Trace(
                    "RDK #94 execution trigger-ready wait aborted: gameplay/player/target context changed");
                return false;
            }

            if (HasLinkedAction(player)
                || HasLinkedAction(target))
            {
                Trace(
                    "RDK #94 execution trigger-ready wait aborted: linked action already active");
                return false;
            }

            const float currentHitTime =
                invoke<float>(
                    GET_LAST_HIT_TIME_HASH,
                    target);
            if (std::fabs(
                    currentHitTime
                    - g_throwExecutionFlow.request.targetHitBaseline)
                > 0.0001f)
            {
                const Actor attacker =
                    invoke<Actor>(
                        GET_LAST_ATTACKER_HASH,
                        target);
                const int hitWeapon =
                    invoke<int>(
                        GET_LAST_HIT_WEAPON_HASH,
                        target);

                char hitLine[704] = {};
                std::snprintf(
                    hitLine,
                    sizeof(hitLine),
                    "RDK #94 captured target hit metadata changed: throw=%llu target=%lld old-hit=%.3f new-hit=%.3f attacker=%lld attacker-match=%d weapon=%d weapon-match=%d proof=%d",
                    g_throwExecutionFlow.request.throwId,
                    static_cast<long long>(target),
                    static_cast<double>(
                        g_throwExecutionFlow.request.targetHitBaseline),
                    static_cast<double>(currentHitTime),
                    static_cast<long long>(attacker),
                    attacker == player ? 1 : 0,
                    hitWeapon,
                    hitWeapon == THROWING_KNIFE_WEAPON ? 1 : 0,
                    invoke<int>(
                        GET_ACTOR_PROOF_HASH,
                        target));
                WriteTrace(hitLine);

                g_throwExecutionFlow.request.targetHitBaseline =
                    currentHitTime;

                if (attacker == player
                    && hitWeapon == THROWING_KNIFE_WEAPON)
                {
                    capturedKnifeImpactObserved = true;
                    invoke<void>(
                        RESET_REACT_NODE_FOR_ACTOR_HASH,
                        target);
                    ++reactionResetCount;
                    invoke<void>(
                        TASK_STAND_STILL_HASH,
                        target,
                        AUTOMATIC_TARGET_STAND_STILL_SECONDS,
                        0,
                        0);
                    standStillRefreshed = true;
                    Trace(
                        "RDK #94 original captured-target knife impact absorbed under proof; reaction reset and projectile considered spent");
                }
            }

            const bool targetReacting =
                invoke<BOOL>(
                    IS_ACTOR_REACTING_HASH,
                    target) != FALSE;
            if (targetReacting)
            {
                invoke<void>(
                    RESET_REACT_NODE_FOR_ACTOR_HASH,
                    target);
                ++reactionResetCount;
                invoke<void>(
                    TASK_STAND_STILL_HASH,
                    target,
                    AUTOMATIC_TARGET_STAND_STILL_SECONDS,
                    0,
                    0);
                standStillRefreshed = true;
            }

            const ULONGLONG now =
                GetTickCount64();
            const ULONGLONG releasedAt =
                g_throwExecutionFlow.request.projectileReleasedAt;
            const bool projectileMissWindowElapsed =
                releasedAt != 0
                && now - releasedAt
                    >= g_throwExecutionFlow.request.projectileClearWindowMs;

            if (projectileMissWindowElapsed
                && !capturedKnifeImpactObserved
                && !missWindowLogged)
            {
                missWindowLogged = true;
                char line[512] = {};
                std::snprintf(
                    line,
                    sizeof(line),
                    "RDK #94 captured-target projectile clear window elapsed with no exact hit: throw=%llu target=%lld elapsed-since-release-ms=%llu clear-window-ms=%llu; treating original knife as missed/cleared target",
                    g_throwExecutionFlow.request.throwId,
                    static_cast<long long>(target),
                    static_cast<unsigned long long>(
                        now - releasedAt),
                    static_cast<unsigned long long>(
                        g_throwExecutionFlow.request.projectileClearWindowMs));
                WriteTrace(line);
            }

            const bool projectileClear =
                capturedKnifeImpactObserved
                || projectileMissWindowElapsed;

            const bool throwing =
                invoke<BOOL>(
                    IS_ACTOR_THROWING_HASH,
                    player) != FALSE;
            const bool playerReady =
                invoke<BOOL>(
                    IS_ACTOR_READY_FOR_ACTION_HASH,
                    player) != FALSE;
            const bool playerReacting =
                invoke<BOOL>(
                    IS_ACTOR_REACTING_HASH,
                    player) != FALSE;
            const bool fireReleased =
                (GetAsyncKeyState(VK_LBUTTON) & 0x8000) == 0
                && !IsGameActionDown("@GENERIC.FIRE");

            if (projectileClear
                && !throwing
                && playerReady
                && !playerReacting
                && fireReleased)
            {
                const bool finalTargetReacting =
                    invoke<BOOL>(
                        IS_ACTOR_REACTING_HASH,
                        target) != FALSE;
                if (finalTargetReacting)
                {
                    invoke<void>(
                        RESET_REACT_NODE_FOR_ACTOR_HASH,
                        target);
                    ++reactionResetCount;
                    invoke<void>(
                        TASK_STAND_STILL_HASH,
                        target,
                        AUTOMATIC_TARGET_STAND_STILL_SECONDS,
                        0,
                        0);
                    standStillRefreshed = true;
                    scriptWait(0);
                }

                char readyLine[768] = {};
                std::snprintf(
                    readyLine,
                    sizeof(readyLine),
                    "RDK #94 execution trigger ready: throw=%llu target=%lld elapsed-ms=%llu projectile-clear=%d exact-impact=%d clear-window-elapsed=%d reaction-resets=%d standstill-refreshed=%d target-reacting=%d target-ground=%d target-incap=%d",
                    g_throwExecutionFlow.request.throwId,
                    static_cast<long long>(target),
                    static_cast<unsigned long long>(
                        GetTickCount64() - started),
                    projectileClear ? 1 : 0,
                    capturedKnifeImpactObserved ? 1 : 0,
                    projectileMissWindowElapsed ? 1 : 0,
                    reactionResetCount,
                    standStillRefreshed ? 1 : 0,
                    invoke<BOOL>(
                        IS_ACTOR_REACTING_HASH,
                        target) != FALSE ? 1 : 0,
                    invoke<BOOL>(
                        IS_ACTOR_ON_GROUND_HASH,
                        target) != FALSE ? 1 : 0,
                    invoke<BOOL>(
                        GET_ACTOR_INCAPACITATED_HASH,
                        target) != FALSE ? 1 : 0);
                WriteTrace(readyLine);
                return true;
            }

            scriptWait(0);
        }

        TraceCombatState(
            "automatic execution trigger-ready timeout",
            player,
            target,
            0);
        return false;
    }

    struct ExecutionLinkState
    {
        int playerLinkedTarget = 0;
        int targetLinkedTarget = 0;
        bool playerPerforming = false;
        bool targetPerforming = false;
        bool playerPhaseLocked = false;
        bool targetPhaseLocked = false;
    };

    ExecutionLinkState ReadExecutionLinkState(Actor player, Actor target)
    {
        ExecutionLinkState state{};
        if (player != 0 && ENTITY::IS_ACTOR_VALID(player))
        {
            state.playerLinkedTarget =
                invoke<int>(GET_LINKED_ANIM_TARGET_HASH, player);
            state.playerPerforming =
                invoke<BOOL>(
                    IS_ACTOR_PERFORMING_LINKED_ANIMATION_HASH,
                    player) != FALSE;
            state.playerPhaseLocked =
                invoke<BOOL>(IS_ACTOR_ANIM_PHASE_LOCKED_HASH, player)
                    != FALSE;
        }
        if (target != 0 && ENTITY::IS_ACTOR_VALID(target))
        {
            state.targetLinkedTarget =
                invoke<int>(GET_LINKED_ANIM_TARGET_HASH, target);
            state.targetPerforming =
                invoke<BOOL>(
                    IS_ACTOR_PERFORMING_LINKED_ANIMATION_HASH,
                    target) != FALSE;
            state.targetPhaseLocked =
                invoke<BOOL>(IS_ACTOR_ANIM_PHASE_LOCKED_HASH, target)
                    != FALSE;
        }
        return state;
    }

    bool HasSynchronizedExecutionEvidence(
        const ExecutionLinkState& state,
        Actor player,
        Actor target)
    {
        // Animation flags alone do not establish who either actor is linked
        // to. A foreign partner must never qualify as exact-target evidence.
        return (state.playerLinkedTarget == static_cast<int>(target)
                || state.targetLinkedTarget == static_cast<int>(player))
            && (state.playerLinkedTarget == 0
                || state.playerLinkedTarget == static_cast<int>(target))
            && (state.targetLinkedTarget == 0
                || state.targetLinkedTarget == static_cast<int>(player))
            && (state.playerPerforming || state.targetPerforming);
    }

    void TraceExecutionLinkState(
        const char* event,
        Actor player,
        Actor target,
        const ExecutionLinkState& state)
    {
        char line[640] = {};
        std::snprintf(
            line,
            sizeof(line),
            "RDK #93 %s: player=%lld target=%lld player-linked=%d target-linked=%d player-linked-animation=%d target-linked-animation=%d player-phase-locked=%d target-phase-locked=%d",
            event,
            static_cast<long long>(player),
            static_cast<long long>(target),
            state.playerLinkedTarget,
            state.targetLinkedTarget,
            state.playerPerforming ? 1 : 0,
            state.targetPerforming ? 1 : 0,
            state.playerPhaseLocked ? 1 : 0,
            state.targetPhaseLocked ? 1 : 0);
        WriteTrace(line);
    }

    void TraceExecutionGeometry(
        const char* event,
        Actor player,
        Actor target,
        int attackAttempt)
    {
        if (player == 0
            || target == 0
            || !ENTITY::IS_ACTOR_VALID(player)
            || !ENTITY::IS_ACTOR_VALID(target))
        {
            char invalidLine[384] = {};
            std::snprintf(
                invalidLine,
                sizeof(invalidLine),
                "RDK #93 %s: attack-attempt=%d player-valid=%d target-valid=%d",
                event,
                attackAttempt,
                player != 0 && ENTITY::IS_ACTOR_VALID(player) ? 1 : 0,
                target != 0 && ENTITY::IS_ACTOR_VALID(target) ? 1 : 0);
            WriteTrace(invalidLine);
            return;
        }

        const Vector3 playerPosition = ReadPosition(player);
        const Vector3 targetPosition = ReadPosition(target);
        const float playerHeading = ReadHeading(player);
        const float targetHeading = ReadHeading(target);
        const int gaitType =
            invoke<int>(GET_ACTOR_GAIT_TYPE_HASH, target);
        const int posture =
            invoke<int>(GET_ACTOR_POSTURE_HASH, target);
        const int onGround =
            invoke<BOOL>(IS_ACTOR_ON_GROUND_HASH, target) != FALSE
                ? 1
                : 0;
        const int incapacitated =
            invoke<BOOL>(GET_ACTOR_INCAPACITATED_HASH, target) != FALSE
                ? 1
                : 0;
        const float targetHealth =
            invoke<float>(GET_ACTOR_HEALTH_HASH, target);
        const float targetMaxHealth =
            invoke<float>(GET_ACTOR_MAX_HEALTH_HASH, target);
        const float targetHealthPct =
            targetMaxHealth > 0.001f
                ? (targetHealth / targetMaxHealth) * 100.0f
                : -1.0f;
        const int hostile =
            invoke<BOOL>(
                AI_IS_HOSTILE_OR_ENEMY_HASH,
                target,
                player) != FALSE
                ? 1
                : 0;
        const int targeted =
            invoke<BOOL>(
                IS_PLAYER_TARGETTING_ACTOR_HASH,
                static_cast<int>(ACTOR::GET_LOCAL_SLOT()),
                target,
                TRUE) != FALSE
                ? 1
                : 0;
        const Actor targetActor =
            invoke<Actor>(GET_TARGET_ACTOR_HASH);
        const Actor reticleTarget =
            invoke<Actor>(GET_ACTOR_UNDER_RETICLE_HASH, player, 0);
        const int gameTargetDown =
            IsGameActionDown("@GENERIC.TARGET") ? 1 : 0;
        const int gameFireDown =
            IsGameActionDown("@GENERIC.FIRE") ? 1 : 0;
        const int controllable =
            invoke<int>(
                IS_PLAYER_CONTROLLABLE_HASH,
                static_cast<int>(ACTOR::GET_LOCAL_SLOT()));

        float slotError = -1.0f;
        float headingError = -1.0f;
        if (IsFinitePosition(playerPosition)
            && IsFinitePosition(targetPosition)
            && std::isfinite(playerHeading)
            && std::isfinite(targetHeading))
        {
            const float radians = targetHeading * PI / 180.0f;
            Vector3 desired{};
            desired.x =
                targetPosition.x
                - std::sin(radians) * FRONT_EXECUTION_OFFSET_METRES;
            desired.y = targetPosition.y;
            desired.z =
                targetPosition.z
                + std::cos(radians) * FRONT_EXECUTION_OFFSET_METRES;
            // Match the corrected automatic slot while preserving F8 geometry.
            if (g_throwExecutionFlow.state == ThrowExecutionFlowState::Executing
                && g_throwExecutionFlow.request.shooter == player
                && g_throwExecutionFlow.request.target == target)
            {
                desired.z = targetPosition.z
                    - std::cos(radians) * FRONT_EXECUTION_OFFSET_METRES;
            }
            slotError =
                std::sqrt(
                    GroundPlaneDistanceSquared(
                        playerPosition,
                        desired));
            headingError =
                HeadingDifference(
                    playerHeading,
                    NormalizeHeading(targetHeading + 180.0f));
        }

        const ExecutionLinkState linkState =
            ReadExecutionLinkState(player, target);

        char line[1800] = {};
        std::snprintf(
            line,
            sizeof(line),
            "RDK #93 %s: attack-attempt=%d player=(%.3f,%.3f,%.3f) player-heading=%.2f target=(%.3f,%.3f,%.3f) target-heading=%.2f slot-error=%.3f heading-error=%.2f gait=%d posture=%d on-ground=%d incapacitated=%d health=%.2f max-health=%.2f health-pct=%.1f hostile=%d targeted=%d target-actor=%lld reticle-target=%lld game-target=%d game-fire=%d controllable=%d player-linked=%d target-linked=%d player-linked-animation=%d target-linked-animation=%d",
            event,
            attackAttempt,
            static_cast<double>(playerPosition.x),
            static_cast<double>(playerPosition.y),
            static_cast<double>(playerPosition.z),
            static_cast<double>(playerHeading),
            static_cast<double>(targetPosition.x),
            static_cast<double>(targetPosition.y),
            static_cast<double>(targetPosition.z),
            static_cast<double>(targetHeading),
            static_cast<double>(slotError),
            static_cast<double>(headingError),
            gaitType,
            posture,
            onGround,
            incapacitated,
            static_cast<double>(targetHealth),
            static_cast<double>(targetMaxHealth),
            static_cast<double>(targetHealthPct),
            hostile,
            targeted,
            static_cast<long long>(targetActor),
            static_cast<long long>(reticleTarget),
            gameTargetDown,
            gameFireDown,
            controllable,
            linkState.playerLinkedTarget,
            linkState.targetLinkedTarget,
            linkState.playerPerforming ? 1 : 0,
            linkState.targetPerforming ? 1 : 0);
        WriteTrace(line);
        TraceCombatState(event, player, target, attackAttempt);
        TraceControllerInput(event, player, target, attackAttempt);
    }

    bool StabilizeMovingTargetBriefly(
        Actor player,
        Actor target,
        int attackAttempt,
        bool automaticThrowFlow,
        bool& standStillIssued)
    {
        standStillIssued = false;

        if (IsGameplayInterrupted()
            || CurrentPlayer() != player
            || !IsLivingActor(player)
            || !IsEligibleNpc(target, player))
        {
            Trace(
                "RDK #93 moving-target stabilization aborted: invalid gameplay context");
            return false;
        }

        const int gaitBefore =
            invoke<int>(GET_ACTOR_GAIT_TYPE_HASH, target);
        if (gaitBefore == 0 && !automaticThrowFlow)
        {
            // Critical regression guard: no wait, no task mutation, and no
            // execution timing change for stationary manual F8 targets.
            return true;
        }

        const float standStillSeconds =
            automaticThrowFlow
                ? AUTOMATIC_TARGET_STAND_STILL_SECONDS
                : MOVING_TARGET_STAND_STILL_SECONDS;

        standStillIssued = true;
        TraceExecutionGeometry(
            automaticThrowFlow
                ? "automatic target stabilization pre-standstill"
                : "moving-target pre-standstill",
            player,
            target,
            attackAttempt);

        invoke<void>(
            TASK_STAND_STILL_HASH,
            target,
            standStillSeconds,
            0,
            0);

        // Give the short task exactly one frame to replace locomotion before
        // alignment. It is intentionally not TASK_CLEAR and self-expires.
        scriptWait(0);

        if (IsGameplayInterrupted()
            || CurrentPlayer() != player
            || !IsLivingActor(player)
            || !IsEligibleNpc(target, player))
        {
            Trace(
                "RDK #93 moving-target stabilization aborted after one frame");
            return false;
        }

        TraceExecutionGeometry(
            automaticThrowFlow
                ? "automatic target stabilization post-standstill frame"
                : "moving-target post-standstill frame",
            player,
            target,
            attackAttempt);

        const int onGround =
            invoke<BOOL>(IS_ACTOR_ON_GROUND_HASH, target) != FALSE
                ? 1
                : 0;
        const int gaitAfter =
            invoke<int>(GET_ACTOR_GAIT_TYPE_HASH, target);

        char line[448] = {};
        std::snprintf(
            line,
            sizeof(line),
            "RDK #93 target standstill result: attack-attempt=%d automatic=%d gait-before=%d gait-after=%d on-ground=%d duration=%.2f",
            attackAttempt,
            automaticThrowFlow ? 1 : 0,
            gaitBefore,
            gaitAfter,
            onGround,
            static_cast<double>(standStillSeconds));
        WriteTrace(line);
        // Alignment already checks actual placement, target drift and heading.
        // A separate 2 cm / 2 degree stationary gate prevented throw 5 in 131832
        // from ever reaching that alignment, even with all action flags ready.
        return true;
    }

    bool SetPlayerControlForAlignment(
        Actor player,
        bool enabled,
        bool& ownership)
    {
        const int playerSlot = static_cast<int>(ACTOR::GET_LOCAL_SLOT());
        if (CurrentPlayer() != player || !IsLivingActor(player))
            return false;

        if (!enabled)
        {
            if (invoke<int>(
                    IS_PLAYER_CONTROLLABLE_HASH,
                    playerSlot) == 0)
            {
                Trace(
                    "RDK #93 alignment input-lock rejected: game already disabled player control");
                return false;
            }

            invoke<void>(
                SET_PLAYER_CONTROL_HASH,
                playerSlot,
                FALSE,
                0,
                0);
            ownership =
                invoke<int>(
                    IS_PLAYER_CONTROLLABLE_HASH,
                    playerSlot) == 0;
            Trace(
                ownership
                    ? "RDK #93 alignment input-lock acquired"
                    : "RDK #93 alignment input-lock failed");
            return ownership;
        }

        if (!ownership)
            return true;

        // This lock is intentionally same-frame only around the teleport call,
        // so #93 never carries a player-control mutation across a pause/cutscene
        // or a yielding wait.
        invoke<void>(
            SET_PLAYER_CONTROL_HASH,
            playerSlot,
            TRUE,
            0,
            0);

        // SET_PLAYER_CONTROL is not reflected by IS_PLAYER_CONTROLLABLE until
        // a later game frame on this build. Treat this as a release request,
        // then let the bounded alignment verification below wait for the game
        // to report control restored before we inject any attack input.
        Trace("RDK #93 alignment input-lock release requested");
        ownership = false;
        return true;
    }

    bool RepositionPlayerForThrowHandoff(
        Actor player,
        Actor target,
        bool& playerRepositioned,
        bool playTeleportVisual)
    {
        if (IsGameplayInterrupted()
            || CurrentPlayer() != player
            || !IsLivingActor(player))
        {
            Trace(
                "RDK #94 early handoff reposition aborted: player context interrupted");
            return false;
        }

        const char* targetReason =
            ExecutionTargetRejectionReason(
                target,
                player);
        if (targetReason != nullptr)
        {
            char line[448] = {};
            std::snprintf(
                line,
                sizeof(line),
                "RDK #94 early handoff reposition aborted: %s",
                targetReason);
            WriteTrace(line);
            return false;
        }

        const Vector3 playerPosition =
            ReadPosition(player);
        const Vector3 targetPosition =
            ReadPosition(target);
        const float playerHeading =
            ReadHeading(player);
        const float targetHeading =
            ReadHeading(target);
        Vector3 desired{};
        if (!IsFinitePosition(playerPosition)
            || !IsFinitePosition(targetPosition)
            || !std::isfinite(playerHeading)
            || !std::isfinite(targetHeading)
            || !ComputeDistanceOnlyExecutionPosition(
                playerPosition,
                targetPosition,
                desired))
        {
            Trace(
                "RDK #94 early handoff reposition failed: invalid/overlapping player-target transform");
            return false;
        }

        // Preserve John's current heading. The NPC is never rotated here.
        const float desiredHeading = playerHeading;
        const float originalDistance =
            std::sqrt(
                GroundPlaneDistanceSquared(
                    playerPosition,
                    targetPosition));
        const float desiredDistance =
            std::sqrt(
                GroundPlaneDistanceSquared(
                    desired,
                    targetPosition));

        char line[832] = {};
        std::snprintf(
            line,
            sizeof(line),
            "RDK #94 early handoff distance-only reposition requested: target=%lld player=(%.3f,%.3f,%.3f) target=(%.3f,%.3f,%.3f) distance-before=%.3f distance-after=%.3f player-heading-preserved=%.2f target-heading-untouched=%.2f player-destination=(%.3f,%.3f,%.3f) player-throwing=%d",
            static_cast<long long>(target),
            static_cast<double>(playerPosition.x),
            static_cast<double>(playerPosition.y),
            static_cast<double>(playerPosition.z),
            static_cast<double>(targetPosition.x),
            static_cast<double>(targetPosition.y),
            static_cast<double>(targetPosition.z),
            static_cast<double>(originalDistance),
            static_cast<double>(desiredDistance),
            static_cast<double>(desiredHeading),
            static_cast<double>(targetHeading),
            static_cast<double>(desired.x),
            static_cast<double>(desired.y),
            static_cast<double>(desired.z),
            invoke<BOOL>(
                IS_ACTOR_THROWING_HASH,
                player) != FALSE ? 1 : 0);
        WriteTrace(line);

        const float teleportTravel =
            std::sqrt(
                GroundPlaneDistanceSquared(
                    playerPosition,
                    desired));
        const bool useTeleportVisual =
            playTeleportVisual
            && std::isfinite(teleportTravel)
            && teleportTravel >= TELEPORT_VISUAL_MIN_TRAVEL_METRES;

        BOOL originalDrawState = TRUE;
        DWORD rumbleSlot = XUSER_MAX_COUNT;

        if (useTeleportVisual)
        {
            originalDrawState =
                invoke<BOOL>(
                    GET_DRAW_ACTOR_HASH,
                    player);

            char visualLine[640] = {};
            std::snprintf(
                visualLine,
                sizeof(visualLine),
                "RDK #137 assassin blink armed: travel=%.3f threshold=%.3f fade-out-wait-ms=%llu black-hold-ms=%llu fade-in-ms=90 rumble-duration-ms=%llu rumble-left=%u rumble-right=%u fov=removed",
                static_cast<double>(teleportTravel),
                static_cast<double>(TELEPORT_VISUAL_MIN_TRAVEL_METRES),
                static_cast<unsigned long long>(TELEPORT_BLINK_FADE_OUT_WAIT_MS),
                static_cast<unsigned long long>(TELEPORT_BLINK_BLACK_HOLD_MS),
                static_cast<unsigned long long>(TELEPORT_RUMBLE_DURATION_MS),
                static_cast<unsigned int>(TELEPORT_RUMBLE_LEFT_MOTOR),
                static_cast<unsigned int>(TELEPORT_RUMBLE_RIGHT_MOTOR));
            WriteTrace(visualLine);

            // The visual adds a short yield before the teleport. Refresh the
            // same short exact-target staging task already used by #94 so this
            // presentation delay cannot introduce target drift.
            invoke<void>(
                TASK_STAND_STILL_HASH,
                target,
                AUTOMATIC_TARGET_STAGING_STAND_STILL_SECONDS,
                0,
                0);

            invoke<void>(
                HUD_SET_FADE_COLOR_HASH,
                0.0f,
                0.0f,
                0.0f,
                1.0f);
            invoke<void>(
                HUD_FADE_OUT_HASH,
                TELEPORT_BLINK_FADE_OUT_SECONDS,
                1.0f,
                1);

            scriptWait(
                static_cast<DWORD>(
                    TELEPORT_BLINK_FADE_OUT_WAIT_MS));

            if (IsGameplayInterrupted()
                || CurrentPlayer() != player
                || !IsLivingActor(player)
                || target == 0
                || !ENTITY::IS_ACTOR_VALID(target)
                || !HEALTH::IS_ACTOR_ALIVE(target))
            {
                invoke<void>(
                    HUD_FADE_IN_HASH,
                    0.0f,
                    1.0f);
                Trace(
                    "RDK #137 assassin blink aborted before teleport; fade restored");
                return false;
            }

            // Hold the fully black state long enough to read as an intentional
            // blink instead of immediately reversing the fade. Refresh
            // exact-target staging
            // for the bounded hold so presentation does not introduce drift.
            invoke<void>(
                TASK_STAND_STILL_HASH,
                target,
                AUTOMATIC_TARGET_STAGING_STAND_STILL_SECONDS,
                0,
                0);
            scriptWait(
                static_cast<DWORD>(
                    TELEPORT_BLINK_BLACK_HOLD_MS));

            if (IsGameplayInterrupted()
                || CurrentPlayer() != player
                || !IsLivingActor(player)
                || target == 0
                || !ENTITY::IS_ACTOR_VALID(target)
                || !HEALTH::IS_ACTOR_ALIVE(target))
            {
                invoke<void>(
                    HUD_FADE_IN_HASH,
                    0.0f,
                    1.0f);
                Trace(
                    "RDK #137 assassin blink aborted during full-black hold; fade restored");
                return false;
            }

            invoke<void>(
                TASK_STAND_STILL_HASH,
                target,
                AUTOMATIC_TARGET_STAGING_STAND_STILL_SECONDS,
                0,
                0);

            // Hide John only for the actual warp frame, then restore his exact
            // prior draw state immediately at the destination.
            invoke<void>(
                SET_DRAW_ACTOR_HASH,
                player,
                FALSE);
        }

        // Do not toggle SET_PLAYER_CONTROL here. The automatic caller already
        // owns the short control lock used to cancel the captured throw.
        const Vector2 desiredXY{
            desired.x,
            desired.y};
        invoke<void>(
            TELEPORT_ACTOR_WITH_HEADING_HASH,
            player,
            desiredXY,
            desired.z,
            desiredHeading,
            FALSE,
            FALSE,
            FALSE);
        playerRepositioned = true;

        if (useTeleportVisual)
        {
            invoke<void>(
                SET_DRAW_ACTOR_HASH,
                player,
                originalDrawState);

            // Refresh once more after arrival so the reveal/rumble pulse stays
            // fully inside the existing exact-target staging window.
            if (target != 0
                && ENTITY::IS_ACTOR_VALID(target)
                && HEALTH::IS_ACTOR_ALIVE(target))
            {
                invoke<void>(
                    TASK_STAND_STILL_HASH,
                    target,
                    AUTOMATIC_TARGET_STAGING_STAND_STILL_SECONDS,
                    0,
                    0);
            }

            invoke<void>(
                HUD_FADE_IN_HASH,
                TELEPORT_BLINK_FADE_IN_SECONDS,
                1.0f);

            PlayTeleportArrivalShock();
            const bool rumbleStarted =
                StartTeleportControllerRumble(
                    rumbleSlot);

            if (rumbleStarted)
            {
                const ULONGLONG rumbleStartedAt = GetTickCount64();
                while (GetTickCount64() - rumbleStartedAt
                    < TELEPORT_RUMBLE_DURATION_MS)
                {
                    if (!RefreshTeleportControllerRumble(rumbleSlot))
                        break;
                    scriptWait(0);
                }

                StopTeleportControllerRumble(rumbleSlot);
            }
            else
            {
                // Keep the reveal timing stable even if no controller accepts
                // vibration so visual timing does not depend on pad state.
                scriptWait(
                    static_cast<DWORD>(
                        TELEPORT_RUMBLE_DURATION_MS));
            }

            // Restore all state we explicitly own before execution setup
            // continues. The fade is asynchronous and should have completed by
            // this point; an immediate fade-in below also protects interruption.
            if (CurrentPlayer() == player
                && ENTITY::IS_ACTOR_VALID(player))
            {
                invoke<void>(
                    SET_DRAW_ACTOR_HASH,
                    player,
                    originalDrawState);
            }
            if (IsGameplayInterrupted()
                || CurrentPlayer() != player
                || !IsLivingActor(player)
                || target == 0
                || !ENTITY::IS_ACTOR_VALID(target)
                || !HEALTH::IS_ACTOR_ALIVE(target))
            {
                invoke<void>(
                    HUD_FADE_IN_HASH,
                    0.0f,
                    1.0f);
                Trace(
                    "RDK #137 assassin blink aborted after teleport; draw/fade/XInput-rumble restored");
                return false;
            }

            Trace(
                "RDK #137 assassin blink completed: John revealed; FOV code absent; XInput rumble window completed");
        }

        TraceExecutionGeometry(
            "early handoff reposition issued",
            player,
            target,
            1);
        return true;
    }

    bool AlignPlayerInFrontOfTarget(
        Actor player,
        Actor target,
        bool automaticThrowFlow,
        bool& playerRepositioned)
    {
        for (int attempt = 0; attempt < 2; ++attempt)
        {
            if (IsGameplayInterrupted()
                || CurrentPlayer() != player
                || !IsLivingActor(player))
            {
                Trace("RDK #93 alignment aborted: player context interrupted");
                return false;
            }

            const char* targetReason =
                ExecutionTargetRejectionReason(target, player);
            if (targetReason != nullptr)
            {
                char line[384] = {};
                std::snprintf(
                    line,
                    sizeof(line),
                    "RDK #93 alignment aborted: %s",
                    targetReason);
                WriteTrace(line);
                return false;
            }

            const Vector3 targetPosition = ReadPosition(target);
            const float targetHeading = ReadHeading(target);
            if (!IsFinitePosition(targetPosition)
                || !std::isfinite(targetHeading))
            {
                Trace("RDK #93 alignment failed: invalid target transform");
                return false;
            }

            Vector3 desired{};
            float desiredHeading = 0.0f;
            if (automaticThrowFlow)
            {
                const Vector3 playerPosition = ReadPosition(player);
                const float playerHeading = ReadHeading(player);
                if (!ComputeDistanceOnlyExecutionPosition(
                        playerPosition,
                        targetPosition,
                        desired)
                    || !std::isfinite(playerHeading))
                {
                    Trace(
                        "RDK #94 automatic alignment failed: invalid/overlapping player-target transform");
                    return false;
                }

                // Automatic execution preserves the approach side and both
                // actors' headings. Only John's distance to the target changes.
                desiredHeading = playerHeading;
            }
            else
            {
                // Manual F8 keeps its existing front-slot placement exactly.
                const float radians = targetHeading * PI / 180.0f;
                desired.x =
                    targetPosition.x
                    - std::sin(radians) * FRONT_EXECUTION_OFFSET_METRES;
                desired.y = targetPosition.y;
                desired.z =
                    targetPosition.z
                    + std::cos(radians) * FRONT_EXECUTION_OFFSET_METRES;
                desiredHeading =
                    NormalizeHeading(targetHeading + 180.0f);
            }

            if (!IsFinitePosition(desired)
                || !std::isfinite(desiredHeading))
            {
                Trace("RDK #93 alignment failed: invalid execution slot transform");
                return false;
            }

            char requestLine[640] = {};
            std::snprintf(
                requestLine,
                sizeof(requestLine),
                "RDK #93 alignment requested: attempt=%d target=(%.3f,%.3f,%.3f) target-heading=%.2f player-destination=(%.3f,%.3f,%.3f) player-heading=%.2f offset=%.2f",
                attempt + 1,
                static_cast<double>(targetPosition.x),
                static_cast<double>(targetPosition.y),
                static_cast<double>(targetPosition.z),
                static_cast<double>(targetHeading),
                static_cast<double>(desired.x),
                static_cast<double>(desired.y),
                static_cast<double>(desired.z),
                static_cast<double>(desiredHeading),
                static_cast<double>(FRONT_EXECUTION_OFFSET_METRES));
            WriteTrace(requestLine);
            TraceControllerInput("alignment-before-teleport", player, target, attempt + 1);

            bool inputLockOwned = false;
            if (!SetPlayerControlForAlignment(
                    player,
                    false,
                    inputLockOwned))
            {
                return false;
            }

            const Vector2 desiredXY{desired.x, desired.y};
            invoke<void>(
                TELEPORT_ACTOR_WITH_HEADING_HASH,
                player,
                desiredXY,
                desired.z,
                desiredHeading,
                FALSE,
                FALSE,
                FALSE);
            playerRepositioned = true;

            if (automaticThrowFlow)
                TraceControllerInput("alignment-teleport-issued", player, target, attempt + 1);

            if (!SetPlayerControlForAlignment(
                    player,
                    true,
                    inputLockOwned))
            {
                return false;
            }

            const ULONGLONG started = GetTickCount64();
            const int playerSlot =
                static_cast<int>(ACTOR::GET_LOCAL_SLOT());
            int matchingFrames = 0;
            bool controlRestored = false;
            bool alignmentConfirmed = false;
            Vector3 observed = ReadPosition(player);
            while (GetTickCount64() - started < ALIGNMENT_TIMEOUT_MS)
            {
                scriptWait(0);

                if (IsGameplayInterrupted()
                    || CurrentPlayer() != player
                    || !IsLivingActor(player)
                    || !IsEligibleNpc(target, player))
                {
                    Trace(
                        "RDK #93 alignment aborted while verifying arrival");
                    return false;
                }

                observed = ReadPosition(player);
                const bool playerOnGround =
                    invoke<BOOL>(IS_ACTOR_ON_GROUND_HASH, player) != FALSE;
                if (IsFinitePosition(observed)
                    && playerOnGround
                    && GroundPlaneDistanceSquared(observed, desired)
                        <= ALIGNMENT_POSITION_TOLERANCE_METRES
                            * ALIGNMENT_POSITION_TOLERANCE_METRES)
                {
                    ++matchingFrames;
                }
                else
                {
                    matchingFrames = 0;
                }

                controlRestored =
                    invoke<int>(
                        IS_PLAYER_CONTROLLABLE_HASH,
                        playerSlot) != 0;
                TraceControllerInput("alignment-frame", player, target, attempt + 1);
                if (automaticThrowFlow)
                {
                    const Vector3 currentTargetPosition = ReadPosition(target);
                    const float currentTargetHeading = ReadHeading(target);
                    char alignmentLine[960] = {};
                    std::snprintf(
                        alignmentLine,
                        sizeof(alignmentLine),
                        "RDK #94 automatic player alignment sample: attempt=%d elapsed-ms=%llu matching-frames=%d required-frames=3 player-ground=%d player-controllable=%d player=(%.3f,%.3f,%.3f) desired=(%.3f,%.3f,%.3f) player-heading=%.2f target-heading=%.2f heading-error=%.2f target-y=%.3f player-action-time=%.3f",
                        attempt + 1,
                        static_cast<unsigned long long>(GetTickCount64() - started),
                        matchingFrames,
                        playerOnGround ? 1 : 0,
                        controlRestored ? 1 : 0,
                        static_cast<double>(observed.x),
                        static_cast<double>(observed.y),
                        static_cast<double>(observed.z),
                        static_cast<double>(desired.x),
                        static_cast<double>(desired.y),
                        static_cast<double>(desired.z),
                        static_cast<double>(ReadHeading(player)),
                        static_cast<double>(currentTargetHeading),
                        static_cast<double>(HeadingDifference(
                            ReadHeading(player),
                            NormalizeHeading(currentTargetHeading + 180.0f))),
                        static_cast<double>(currentTargetPosition.y),
                        static_cast<double>(invoke<float>(
                            GET_CURR_ACTION_NODE_PLAY_TIME_HASH, player)));
                    WriteTrace(alignmentLine);
                }

                if (matchingFrames >= 3 && controlRestored)
                {
                    alignmentConfirmed = true;
                    break;
                }
            }

            if (!alignmentConfirmed)
            {
                const float horizontalError =
                    IsFinitePosition(observed)
                        ? std::sqrt(
                            GroundPlaneDistanceSquared(
                                observed,
                                desired))
                        : -1.0f;
                const float elevationDelta =
                    IsFinitePosition(observed)
                        ? observed.y - desired.y
                        : 0.0f;
                char line[720] = {};
                std::snprintf(
                    line,
                    sizeof(line),
                    "RDK #93 alignment failed: verification timeout attempt=%d observed=(%.3f,%.3f,%.3f) horizontal-error=%.3f elevation-delta=%.3f stable-frames=%d control-restored=%d",
                    attempt + 1,
                    static_cast<double>(observed.x),
                    static_cast<double>(observed.y),
                    static_cast<double>(observed.z),
                    static_cast<double>(horizontalError),
                    static_cast<double>(elevationDelta),
                    matchingFrames,
                    controlRestored ? 1 : 0);
                WriteTrace(line);

                // Best-effort restoration of the control state that #93
                // temporarily changed. Never leave John disabled after a
                // failed manual attempt.
                invoke<void>(
                    SET_PLAYER_CONTROL_HASH,
                    playerSlot,
                    TRUE,
                    0,
                    0);
                return false;
            }

            const Vector3 finalTargetPosition = ReadPosition(target);
            const float finalTargetHeading = ReadHeading(target);
            if (!IsFinitePosition(finalTargetPosition)
                || !std::isfinite(finalTargetHeading))
            {
                Trace(
                    "RDK #93 alignment failed: target transform invalid after arrival");
                return false;
            }

            const float targetDrift =
                std::sqrt(
                    GroundPlaneDistanceSquared(
                        finalTargetPosition,
                        targetPosition));
            const float headingDrift =
                HeadingDifference(
                    finalTargetHeading,
                    targetHeading);
            const float playerHeadingError = HeadingDifference(
                ReadHeading(player), NormalizeHeading(finalTargetHeading + 180.0f));

            char observedLine[640] = {};
            std::snprintf(
                observedLine,
                sizeof(observedLine),
                "RDK #93 alignment observed: attempt=%d player=(%.3f,%.3f,%.3f) target-drift=%.3f target-heading-drift=%.2f player-heading-error=%.2f",
                attempt + 1,
                static_cast<double>(observed.x),
                static_cast<double>(observed.y),
                static_cast<double>(observed.z),
                static_cast<double>(targetDrift),
                static_cast<double>(headingDrift),
                static_cast<double>(playerHeadingError));
            WriteTrace(observedLine);

            // Match F8's acceptance. John's facing remains diagnostic; the
            // automatic caller must verify captured ownership and reject live
            // aim conflicts before its pulse. Never relax target drift tolerances.
            if (targetDrift <= TARGET_DRIFT_TOLERANCE_METRES
                && headingDrift <= TARGET_HEADING_TOLERANCE_DEGREES)
            {
                if (automaticThrowFlow)
                    Trace("RDK #94 automatic alignment accepted using F8 checks; player facing diagnostic only; exact-target FIRE gate still required");
                Trace("RDK #93 alignment confirmed");
                return true;
            }

            if (attempt == 0)
            {
                Trace(
                    "RDK #93 target drifted during alignment; trying one bounded realignment");

                // This branch is reached only after the original alignment
                // tolerances already rejected the first slot. A target can be
                // rotating in place with gait=0 (especially after hostility
                // reacts to John's teleport), so gait alone is not sufficient.
                // Request a short self-expiring stand-still before the already
                // existing second alignment. Do not TASK_CLEAR or freeze mover.
                TraceExecutionGeometry(
                    "alignment-drift rescue pre-standstill",
                    player,
                    target,
                    0);
                invoke<void>(
                    TASK_STAND_STILL_HASH,
                    target,
                    automaticThrowFlow
                        ? AUTOMATIC_TARGET_STAND_STILL_SECONDS
                        : MOVING_TARGET_STAND_STILL_SECONDS,
                    0,
                    0);
                scriptWait(0);

                if (IsGameplayInterrupted()
                    || CurrentPlayer() != player
                    || !IsLivingActor(player)
                    || !IsEligibleNpc(target, player))
                {
                    Trace(
                        "RDK #93 alignment-drift rescue aborted after standstill frame");
                    return false;
                }

                TraceExecutionGeometry(
                    "alignment-drift rescue post-standstill",
                    player,
                    target,
                    0);
            }
        }

        Trace("RDK #93 alignment failed after bounded realignment");
        return false;
    }

    bool InjectPrimaryAttack(
        Actor player,
        Actor target,
        int attackAttempt,
        bool automaticThrowFlow)
    {
        TraceControllerInput("before-synthetic-fire", player, target, attackAttempt);
        const float attackTimeBefore = invoke<float>(GET_LAST_ATTACK_TIME_HASH, player);
        bool gameFireSeen = false;
        bool actionSeen = false;
        bool contextLost = false;
        INPUT down = {};
        down.type = INPUT_MOUSE;
        down.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;

        INPUT up = {};
        up.type = INPUT_MOUSE;
        up.mi.dwFlags = MOUSEEVENTF_LEFTUP;

        SetLastError(ERROR_SUCCESS);
        const UINT downSent =
            SendInput(1, &down, sizeof(INPUT));
        const DWORD downError = GetLastError();
        g_primaryAttackDown = downSent == 1;
        const ULONGLONG downAt = GetTickCount64();
        char edgeLine[384] = {};
        std::snprintf(edgeLine, sizeof(edgeLine),
            "RDK #94 injected mouse edge: attempt=%d edge=down sent=%u error=%lu tick=%llu",
            attackAttempt, downSent, static_cast<unsigned long>(downError),
            static_cast<unsigned long long>(downAt));
        WriteTrace(edgeLine);

        if (downSent == 1)
        {
            do
            {
                scriptWait(0);
                if (!automaticThrowFlow)
                    break; // Preserve F8's one-script-frame pulse.

                contextLost = IsGameplayInterrupted()
                    || CurrentPlayer() != player
                    || !IsLivingActor(player)
                    || !IsLivingActor(target)
                    || INVENTORY::GET_WEAPON_IN_HAND(player) != THROWING_KNIFE_WEAPON;
                if (contextLost)
                    break;

                gameFireSeen = IsGameActionDown("@GENERIC.FIRE");
                actionSeen = HasLinkedAction(player) || HasLinkedAction(target)
                    || invoke<BOOL>(IS_ACTOR_THROWING_HASH, player) != FALSE
                    || invoke<float>(GET_LAST_ATTACK_TIME_HASH, player) != attackTimeBefore;
                TraceCombatState("injected down awaiting game input/action", player, target, attackAttempt);
                if (gameFireSeen || actionSeen)
                    break;
            }
            while (GetTickCount64() - downAt < AUTOMATIC_INPUT_ACK_TIMEOUT_MS);
            TraceExecutionGeometry(
                "after LMB-down processing frame",
                player,
                target,
                attackAttempt);
        }

        // Always release, including timeout, interrupted gameplay or actor loss.
        // A successful Windows down/up pair alone is not game acknowledgement.
        SetLastError(ERROR_SUCCESS);
        const UINT upSent =
            SendInput(1, &up, sizeof(INPUT));
        const DWORD upError = GetLastError();
        if (upSent == 1)
            g_primaryAttackDown = false;
        std::snprintf(edgeLine, sizeof(edgeLine),
            "RDK #94 injected mouse edge: attempt=%d edge=up sent=%u error=%lu tick=%llu held-ms=%llu outstanding-down=%d",
            attackAttempt, upSent, static_cast<unsigned long>(upError),
            static_cast<unsigned long long>(GetTickCount64()),
            static_cast<unsigned long long>(GetTickCount64() - downAt),
            g_primaryAttackDown ? 1 : 0);
        WriteTrace(edgeLine);

        TraceCombatState(
            "after primary input pulse and release",
            player,
            target,
            attackAttempt);
        TraceControllerInput("after-synthetic-fire-release", player, target, attackAttempt);
        char line[512] = {};
        std::snprintf(
            line,
            sizeof(line),
            "RDK #93 primary attack injected: button=LMB down-sent=%u down-error=%lu up-sent=%u up-error=%lu automatic=%d game-fire-seen=%d action-seen=%d context-lost=%d ack-cap-ms=%llu",
            downSent,
            static_cast<unsigned long>(downError),
            upSent,
            static_cast<unsigned long>(upError),
            automaticThrowFlow ? 1 : 0,
            gameFireSeen ? 1 : 0,
            actionSeen ? 1 : 0,
            contextLost ? 1 : 0,
            static_cast<unsigned long long>(AUTOMATIC_INPUT_ACK_TIMEOUT_MS));
        WriteTrace(line);
        return downSent == 1 && upSent == 1
            && (!automaticThrowFlow || (!contextLost && (gameFireSeen || actionSeen)));
    }

    void CleanupActorExecutionState(
        Actor actor,
        Actor peer,
        const char* role)
    {
        if (actor == 0 || !ENTITY::IS_ACTOR_VALID(actor))
            return;

        const int linkedTarget =
            invoke<int>(GET_LINKED_ANIM_TARGET_HASH, actor);
        const bool performing =
            invoke<BOOL>(
                IS_ACTOR_PERFORMING_LINKED_ANIMATION_HASH,
                actor) != FALSE;
        const bool phaseLocked =
            invoke<BOOL>(IS_ACTOR_ANIM_PHASE_LOCKED_HASH, actor)
                != FALSE;

        const bool suspicious =
            linkedTarget == static_cast<int>(peer)
            || performing
            || phaseLocked;
        if (!suspicious)
            return;

        char line[512] = {};
        std::snprintf(
            line,
            sizeof(line),
            "RDK #93 abort cleanup: role=%s actor=%lld linked-target=%d linked-animation=%d phase-locked=%d",
            role,
            static_cast<long long>(actor),
            linkedTarget,
            performing ? 1 : 0,
            phaseLocked ? 1 : 0);
        WriteTrace(line);

        invoke<void>(RESET_REACT_NODE_FOR_ACTOR_HASH, actor);
        invoke<void>(RESET_ACTIONTREE_FOR_ACTOR_HASH, actor);
        if (linkedTarget != 0 || performing)
        {
            invoke<int>(CLEAR_LINKED_ANIM_TARGET_HASH, actor);
            invoke<int>(SET_LINKED_ANIM_TARGET_HASH, actor, 0);
        }
        if (phaseLocked)
            invoke<void>(RELEASE_ACTOR_ANIM_PHASE_LOCK_HASH, actor);
    }

    void CleanupFailedExecutionState(Actor player, Actor target)
    {
        CleanupActorExecutionState(player, target, "player");
        if (target != 0
            && ENTITY::IS_ACTOR_VALID(target)
            && HEALTH::IS_ACTOR_ALIVE(target))
        {
            CleanupActorExecutionState(target, player, "target");
        }
    }

    const char* KnifeExecutionResultName(KnifeExecutionResult result)
    {
        switch (result)
        {
        case KnifeExecutionResult::Success:
            return "success";
        case KnifeExecutionResult::Aborted:
            return "aborted";
        case KnifeExecutionResult::Failed:
        default:
            return "failed";
        }
    }

    using WorldGetAllActorsFn = int(*)(int*, int);

    int GetAllWorldActors(Actor* actors)
    {
        static WorldGetAllActorsFn worldGetAllActors = nullptr;
        static bool resolutionAttempted = false;

        if (!resolutionAttempted)
        {
            resolutionAttempted = true;

            HMODULE scriptHook = GetModuleHandleA("ScriptHookRDR.dll");
            if (scriptHook != nullptr)
            {
                FARPROC proc = GetProcAddress(scriptHook, "worldGetAllActors");
                if (proc == nullptr)
                {
                    proc = GetProcAddress(
                        scriptHook,
                        "?worldGetAllActors@@YAHPEAHH@Z");
                }

                worldGetAllActors =
                    reinterpret_cast<WorldGetAllActorsFn>(proc);
            }

            Trace(
                "RDK world actor export resolved",
                worldGetAllActors != nullptr ? 1 : 0);
        }

        if (worldGetAllActors == nullptr)
            return -1;

        return worldGetAllActors(
            reinterpret_cast<int*>(actors),
            WORLD_ACTOR_CAPACITY);
    }

    Actor ResolveExactAimedTarget(
        Actor player,
        const char* source,
        bool writeTrace = true,
        bool* ambiguousOut = nullptr,
        int* predicateScanCountOut = nullptr)
    {
        if (ambiguousOut != nullptr)
            *ambiguousOut = false;
        if (predicateScanCountOut != nullptr)
            *predicateScanCountOut = -1;

        if (!IsLivingActor(player))
            return 0;

        const Actor reticleTarget =
            invoke<Actor>(
                GET_ACTOR_UNDER_RETICLE_HASH,
                player,
                0);
        const Actor targetActor =
            invoke<Actor>(GET_TARGET_ACTOR_HASH);

        const bool reticleEligible =
            IsEligibleNpc(reticleTarget, player);
        const bool targetActorEligible =
            IsEligibleNpc(targetActor, player);

        Actor resolved = 0;
        const char* method = "none";
        bool ambiguous = false;

        if (reticleEligible
            && targetActorEligible
            && reticleTarget != targetActor)
        {
            ambiguous = true;
            method = "reticle-target-disagree";
        }
        else if (reticleEligible)
        {
            resolved = reticleTarget;
            method = "reticle";
        }
        else if (targetActorEligible)
        {
            resolved = targetActor;
            method = "target-actor";
        }

        int predicateMatches = 0;
        Actor predicateTarget = 0;
        if (resolved == 0 && !ambiguous)
        {
            Actor actors[WORLD_ACTOR_CAPACITY] = {};
            int count = GetAllWorldActors(actors);
            if (predicateScanCountOut != nullptr)
                *predicateScanCountOut = count;
            if (count > WORLD_ACTOR_CAPACITY)
                count = WORLD_ACTOR_CAPACITY;

            if (count > 0)
            {
                const int playerSlot =
                    static_cast<int>(ACTOR::GET_LOCAL_SLOT());
                for (int i = 0; i < count; ++i)
                {
                    const Actor candidate = actors[i];
                    if (!IsEligibleNpc(candidate, player))
                        continue;
                    if (invoke<BOOL>(
                            IS_PLAYER_TARGETTING_ACTOR_HASH,
                            playerSlot,
                            candidate,
                            TRUE) == FALSE)
                    {
                        continue;
                    }

                    ++predicateMatches;
                    if (predicateTarget == 0)
                        predicateTarget = candidate;
                }
            }

            if (predicateMatches == 1)
            {
                resolved = predicateTarget;
                method = "target-predicate";
            }
            else if (predicateMatches > 1)
            {
                ambiguous = true;
                method = "multiple-target-predicates";
            }
        }

        if (writeTrace)
        {
            char line[768] = {};
            std::snprintf(
                line,
                sizeof(line),
                "RDK #94 exact aim probe: source=%s reticle=%lld reticle-eligible=%d target-actor=%lld target-eligible=%d predicate-matches=%d game-target=%d throwing=%d resolved=%lld method=%s ambiguous=%d",
                source != nullptr ? source : "unknown",
                static_cast<long long>(reticleTarget),
                reticleEligible ? 1 : 0,
                static_cast<long long>(targetActor),
                targetActorEligible ? 1 : 0,
                predicateMatches,
                IsGameActionDown("@GENERIC.TARGET") ? 1 : 0,
                invoke<BOOL>(
                    IS_ACTOR_THROWING_HASH,
                    player) != FALSE ? 1 : 0,
                static_cast<long long>(
                    ambiguous ? 0 : resolved),
                method,
                ambiguous ? 1 : 0);
            WriteTrace(line);
        }

        if (ambiguousOut != nullptr)
            *ambiguousOut = ambiguous;

        return ambiguous ? 0 : resolved;
    }

    void UpdatePreThrowAimedTargetCache(
        Actor player,
        bool throwing)
    {
        // Never mutate the cache on the throwing frame. RDR can clear the
        // reticle/target actor as the throw animation begins; preserving the
        // previous non-throwing aim sample is the point of this cache.
        if (throwing
            || g_detector.hasRequest
            || g_executionRunning
            || g_throwExecutionFlow.state
                != ThrowExecutionFlowState::Idle)
        {
            return;
        }

        const bool targetHeld =
            IsGameActionDown("@GENERIC.TARGET");
        const Actor previous =
            g_detector.cachedAimedTarget;

        Actor nextTarget = previous;
        const char* cacheDecision = "retain";
        bool retainedThroughDropout = false;

        if (!targetHeld)
        {
            nextTarget = 0;
            g_detector.cachedAimDropoutRetained = false;
            cacheDecision = "target-released-clear";
        }
        else
        {
            bool ambiguous = false;
            const Actor resolved =
                ResolveExactAimedTarget(
                    player,
                    "pre-throw aim cache",
                    false,
                    &ambiguous);

            if (ambiguous)
            {
                nextTarget = 0;
                g_detector.cachedAimDropoutRetained = false;
                cacheDecision = "ambiguous-clear";
            }
            else if (resolved != 0
                && AutomaticThrowTargetRejectionReason(
                    resolved,
                    player) == nullptr)
            {
                nextTarget = resolved;
                g_detector.cachedAimDropoutRetained = false;
                cacheDecision =
                    resolved == previous
                        ? "exact-unchanged"
                        : "exact-update";
            }
            else if (previous != 0
                && !g_detector.cachedAimDropoutRetained
                && AutomaticThrowTargetRejectionReason(
                    previous,
                    player) == nullptr)
            {
                // RDR can transiently drop its exact target handles on the
                // final non-throwing frame before IS_ACTOR_THROWING flips.
                // Retain exactly one such frame; a second consecutive no-result
                // clears the cache so deliberately aiming away cannot leave a
                // stale NPC owned indefinitely.
                nextTarget = previous;
                g_detector.cachedAimDropoutRetained = true;
                retainedThroughDropout = true;
                cacheDecision = "transient-none-retain-once";
            }
            else
            {
                nextTarget = 0;
                g_detector.cachedAimDropoutRetained = false;
                cacheDecision = "none-clear";
            }
        }

        if (nextTarget == previous)
        {
            if (retainedThroughDropout)
            {
                char retainedLine[512] = {};
                std::snprintf(
                    retainedLine,
                    sizeof(retainedLine),
                    "RDK #94 pre-throw aim cache retained for one transient resolver dropout frame: target=%lld game-target=%d throwing=%d",
                    static_cast<long long>(previous),
                    targetHeld ? 1 : 0,
                    throwing ? 1 : 0);
                WriteTrace(retainedLine);
            }
            return;
        }

        g_detector.cachedAimedTarget =
            nextTarget;

        char line[640] = {};
        std::snprintf(
            line,
            sizeof(line),
            "RDK #94 pre-throw aim cache changed: previous=%lld current=%lld decision=%s game-target=%d throwing=%d",
            static_cast<long long>(previous),
            static_cast<long long>(nextTarget),
            cacheDecision,
            targetHeld ? 1 : 0,
            throwing ? 1 : 0);
        WriteTrace(line);
    }

    bool ProtectAimedThrowTarget(
        AimedThrowRequest& request)
    {
        if (request.target == 0
            || !ENTITY::IS_ACTOR_VALID(request.target)
            || !HEALTH::IS_ACTOR_ALIVE(request.target))
        {
            return false;
        }

        request.originalProof =
            invoke<int>(
                GET_ACTOR_PROOF_HASH,
                request.target);
        request.targetHitBaseline =
            invoke<float>(
                GET_LAST_HIT_TIME_HASH,
                request.target);
        invoke<void>(
            SET_ACTOR_PROOF_HASH,
            request.target,
            -1);

        const int protectedProof =
            invoke<int>(
                GET_ACTOR_PROOF_HASH,
                request.target);
        const bool matched =
            protectedProof == -1;
        request.proofProtected = matched;

        // Fail closed without leaking a changed proof mask if the temporary
        // all-proofs write does not read back exactly as expected.
        if (!matched)
        {
            invoke<void>(
                CLEAR_ACTOR_PROOF_HASH,
                request.target,
                -1);
            if (request.originalProof != 0)
            {
                invoke<void>(
                    SET_ACTOR_PROOF_HASH,
                    request.target,
                    request.originalProof);
            }
        }

        char line[512] = {};
        std::snprintf(
            line,
            sizeof(line),
            "RDK #94 captured target protection applied: throw=%llu target=%lld original-proof=%d protected-readback=%d matched=%d hit-baseline=%.3f",
            request.throwId,
            static_cast<long long>(request.target),
            request.originalProof,
            protectedProof,
            matched ? 1 : 0,
            static_cast<double>(request.targetHitBaseline));
        WriteTrace(line);

        if (!matched)
        {
            const int restored =
                invoke<int>(
                    GET_ACTOR_PROOF_HASH,
                    request.target);
            char restoreLine[512] = {};
            std::snprintf(
                restoreLine,
                sizeof(restoreLine),
                "RDK #94 captured target protection apply rollback: throw=%llu target=%lld expected=%d after=%d matched=%d",
                request.throwId,
                static_cast<long long>(request.target),
                request.originalProof,
                restored,
                restored == request.originalProof ? 1 : 0);
            WriteTrace(restoreLine);
        }

        return matched;
    }

    bool RestoreAimedThrowTargetProof(
        AimedThrowRequest& request,
        const char* reason)
    {
        if (!request.proofProtected)
            return true;

        const Actor target = request.target;
        const int originalProof = request.originalProof;

        if (target == 0
            || !ENTITY::IS_ACTOR_VALID(target))
        {
            request.proofProtected = false;
            char line[512] = {};
            std::snprintf(
                line,
                sizeof(line),
                "RDK #94 captured target protection restore skipped: throw=%llu target=%lld original-proof=%d reason=%s target-invalid=1",
                request.throwId,
                static_cast<long long>(target),
                originalProof,
                reason != nullptr ? reason : "unknown");
            WriteTrace(line);
            return false;
        }

        const int before =
            invoke<int>(
                GET_ACTOR_PROOF_HASH,
                target);
        invoke<void>(
            CLEAR_ACTOR_PROOF_HASH,
            target,
            -1);
        if (originalProof != 0)
        {
            invoke<void>(
                SET_ACTOR_PROOF_HASH,
                target,
                originalProof);
        }

        const int after =
            invoke<int>(
                GET_ACTOR_PROOF_HASH,
                target);
        const bool matched =
            after == originalProof;
        if (matched)
            request.proofProtected = false;

        char line[640] = {};
        std::snprintf(
            line,
            sizeof(line),
            "RDK #94 captured target protection restored: throw=%llu target=%lld before=%d expected=%d after=%d matched=%d alive=%d reason=%s",
            request.throwId,
            static_cast<long long>(target),
            before,
            originalProof,
            after,
            matched ? 1 : 0,
            HEALTH::IS_ACTOR_ALIVE(target) ? 1 : 0,
            reason != nullptr ? reason : "unknown");
        WriteTrace(line);
        return matched;
    }

    bool SuppressAimedThrowTargetWeaponReactions(
        AimedThrowRequest& request)
    {
        if (request.target == 0
            || !ENTITY::IS_ACTOR_VALID(request.target)
            || !HEALTH::IS_ACTOR_ALIVE(request.target))
        {
            return false;
        }

        // There is no matching getter in the available RDR1 native database,
        // so this diagnostic owns a simple FALSE -> TRUE lifecycle on only the
        // exact captured NPC. This blocks the in-flight knife's weapon-hit
        // reaction without resetting or replacing the NPC's action tree.
        invoke<void>(
            SET_ACTOR_ALLOW_WEAPON_REACTIONS_HASH,
            request.target,
            FALSE);
        request.weaponReactionsSuppressed = true;

        char line[448] = {};
        std::snprintf(
            line,
            sizeof(line),
            "RDK #94 captured target weapon reactions suppressed: throw=%llu target=%lld allowed=0",
            request.throwId,
            static_cast<long long>(request.target));
        WriteTrace(line);
        return true;
    }

    bool RestoreAimedThrowTargetWeaponReactions(
        AimedThrowRequest& request,
        const char* reason)
    {
        if (!request.weaponReactionsSuppressed)
            return true;

        const Actor target = request.target;
        if (target == 0
            || !ENTITY::IS_ACTOR_VALID(target))
        {
            request.weaponReactionsSuppressed = false;
            char line[512] = {};
            std::snprintf(
                line,
                sizeof(line),
                "RDK #94 captured target weapon reaction restore skipped: throw=%llu target=%lld reason=%s target-invalid=1",
                request.throwId,
                static_cast<long long>(target),
                reason != nullptr ? reason : "unknown");
            WriteTrace(line);
            return false;
        }

        invoke<void>(
            SET_ACTOR_ALLOW_WEAPON_REACTIONS_HASH,
            target,
            TRUE);
        request.weaponReactionsSuppressed = false;

        char line[512] = {};
        std::snprintf(
            line,
            sizeof(line),
            "RDK #94 captured target weapon reactions restored: throw=%llu target=%lld allowed=1 alive=%d reason=%s",
            request.throwId,
            static_cast<long long>(target),
            HEALTH::IS_ACTOR_ALIVE(target) ? 1 : 0,
            reason != nullptr ? reason : "unknown");
        WriteTrace(line);
        return true;
    }

    bool CaptureAimedThrowTargetStaging(
        AimedThrowRequest& request)
    {
        if (request.target == 0
            || !ENTITY::IS_ACTOR_VALID(request.target)
            || !HEALTH::IS_ACTOR_ALIVE(request.target))
        {
            return false;
        }

        const Vector3 targetPosition =
            ReadPosition(request.target);
        const float targetHeading =
            ReadHeading(request.target);
        if (!IsFinitePosition(targetPosition)
            || !std::isfinite(targetHeading))
        {
            return false;
        }

        request.targetStagingActive = true;
        invoke<void>(
            RESET_REACT_NODE_FOR_ACTOR_HASH,
            request.target);
        invoke<void>(
            TASK_STAND_STILL_HASH,
            request.target,
            AUTOMATIC_TARGET_STAGING_STAND_STILL_SECONDS,
            0,
            0);

        char line[704] = {};
        std::snprintf(
            line,
            sizeof(line),
            "RDK #94 captured target staging started: throw=%llu target=%lld position=(%.3f,%.3f,%.3f) heading=%.2f standstill=%.2f teleport=0 ground=%d",
            request.throwId,
            static_cast<long long>(request.target),
            static_cast<double>(targetPosition.x),
            static_cast<double>(targetPosition.y),
            static_cast<double>(targetPosition.z),
            static_cast<double>(targetHeading),
            static_cast<double>(AUTOMATIC_TARGET_STAGING_STAND_STILL_SECONDS),
            invoke<BOOL>(
                IS_ACTOR_ON_GROUND_HASH,
                request.target) != FALSE ? 1 : 0);
        WriteTrace(line);
        return true;
    }

    bool MaintainAimedThrowTargetStaging(
        AimedThrowRequest& request)
    {
        if (!request.targetStagingActive)
            return false;
        if (request.target == 0
            || !ENTITY::IS_ACTOR_VALID(request.target)
            || !HEALTH::IS_ACTOR_ALIVE(request.target))
        {
            return false;
        }

        // Never fight a linked execution that has already taken ownership.
        if (HasLinkedAction(request.target))
            return true;

        // Keep only a short self-expiring stand-still on the exact target.
        // Do not teleport/pin the NPC: runtime traces proved repeated target
        // teleports make IS_ACTOR_ON_GROUND report false and reject John's
        // later execution-slot reposition.
        invoke<void>(
            RESET_REACT_NODE_FOR_ACTOR_HASH,
            request.target);
        invoke<void>(
            TASK_STAND_STILL_HASH,
            request.target,
            AUTOMATIC_TARGET_STAGING_STAND_STILL_SECONDS,
            0,
            0);
        return true;
    }

    void ReleaseAimedThrowTargetStaging(
        AimedThrowRequest& request,
        const char* reason)
    {
        if (!request.targetStagingActive)
            return;

        request.targetStagingActive = false;
        char line[512] = {};
        std::snprintf(
            line,
            sizeof(line),
            "RDK #94 captured target staging released: throw=%llu target=%lld reason=%s; standstill task will self-expire",
            request.throwId,
            static_cast<long long>(request.target),
            reason != nullptr ? reason : "unknown");
        WriteTrace(line);
    }

    bool RestoreAimedThrowTargetState(
        AimedThrowRequest& request,
        const char* reason)
    {
        ReleaseAimedThrowTargetStaging(
            request,
            reason);
        const bool reactionsRestored =
            RestoreAimedThrowTargetWeaponReactions(
                request,
                reason);
        const bool proofRestored =
            RestoreAimedThrowTargetProof(
                request,
                reason);
        return reactionsRestored && proofRestored;
    }

    bool TakeAimedThrowRequest(
        AimedThrowRequest& out)
    {
        if (!g_detector.hasRequest)
            return false;

        out = g_detector.request;
        g_detector.hasRequest = false;
        g_detector.request = {};

        if (out.shooter != CurrentPlayer()
            || !IsLivingActor(out.shooter)
            || !IsEligibleNpc(out.target, out.shooter))
        {
            RestoreAimedThrowTargetState(
                out,
                "aimed throw request consume validation failed");
            Trace(
                "RDK #94 aimed throw request rejected: shooter/target no longer valid");
            out = {};
            return false;
        }

        char line[384] = {};
        std::snprintf(
            line,
            sizeof(line),
            "RDK #94 aimed throw request consumed: throw=%llu shooter=%lld target=%lld",
            out.throwId,
            static_cast<long long>(out.shooter),
            static_cast<long long>(out.target));
        WriteTrace(line);
        return true;
    }

    void CleanupDetector(const char* reason)
    {
        const bool wasActive =
            g_detector.armed
            || g_detector.hasRequest;

        if (g_detector.hasRequest)
        {
            RestoreAimedThrowTargetState(
                g_detector.request,
                reason);

            char requestLine[384] = {};
            std::snprintf(
                requestLine,
                sizeof(requestLine),
                "RDK #94 aimed throw request cleared: throw=%llu target=%lld reason=%s",
                g_detector.request.throwId,
                static_cast<long long>(
                    g_detector.request.target),
                reason);
            WriteTrace(requestLine);
        }

        g_detector.armed = false;
        g_detector.wasThrowing = false;
        g_detector.cachedAimedTarget = 0;
        g_detector.cachedAimDropoutRetained = false;
        g_detector.hasRequest = false;
        g_detector.request = {};

        if (wasActive)
        {
            char line[384] = {};
            std::snprintf(
                line,
                sizeof(line),
                "RDK detector disarmed: %s",
                reason);
            WriteTrace(line);
        }
    }

    void ResetForPlayer(Actor player)
    {
        CleanupDetector("player context changed");
        g_detector = {};
        g_detector.player = player;

        char line[256] = {};
        std::snprintf(
            line,
            sizeof(line),
            "RDK player context reset: player=%lld",
            static_cast<long long>(player));
        WriteTrace(line);
    }

    void UpdateDetector()
    {
        const Actor player = CurrentPlayer();
        if (player != g_detector.player)
            ResetForPlayer(player);

        if (!IsLivingActor(player))
        {
            g_detector.starterKnivesGranted = false;
            g_detector.nextStarterGrantAttempt = 0;
            CleanupDetector("player invalid or dead");
            return;
        }

        GrantMaxThrowingKnives(player);

        if (IsGameplayInterrupted())
        {
            CleanupDetector("pause/cutscene interruption");
            return;
        }

        if (HasLinkedAction(player))
        {
            CleanupDetector(
                "base-game linked action owns player");
            return;
        }

        if (INVENTORY::GET_WEAPON_IN_HAND(player)
            != THROWING_KNIFE_WEAPON)
        {
            CleanupDetector(
                "throwing knife switched away or holstered");
            return;
        }

        const bool throwing =
            invoke<BOOL>(
                IS_ACTOR_THROWING_HASH,
                player) != FALSE;

        UpdatePreThrowAimedTargetCache(
            player,
            throwing);

        if (!g_detector.armed)
        {
            g_detector.armed = true;
            // Do not adopt a throw already in progress when the detector
            // re-arms after recovery or weapon switching.
            g_detector.wasThrowing = throwing;

            char line[384] = {};
            std::snprintf(
                line,
                sizeof(line),
                "RDK detector armed: player=%lld weapon=%d initial-throwing=%d",
                static_cast<long long>(player),
                THROWING_KNIFE_WEAPON,
                throwing ? 1 : 0);
            WriteTrace(line);
            return;
        }

        const bool throwStarted =
            throwing && !g_detector.wasThrowing;
        g_detector.wasThrowing = throwing;

        if (!throwStarted)
            return;

        if (g_executionRunning
            || g_throwExecutionFlow.state
                != ThrowExecutionFlowState::Idle
            || g_detector.hasRequest)
        {
            Trace(
                "RDK #94 throw start ignored: automatic execution already owns the flow");
            return;
        }

        // Runtime #137 traces exposed a false second throw immediately after a
        // failed contextual execution: IS_ACTOR_THROWING rose again while the
        // actual FIRE action was already up, re-entering the whole handoff and
        // producing a second black blink. Every genuine captured throw in the
        // same trace had game FIRE pressed. Treat a throwing rising edge without
        // FIRE as residual post-flow animation, not a new player throw.
        if (!IsGameActionDown("@GENERIC.FIRE"))
        {
            Trace(
                "RDK #94 throw start ignored: IS_ACTOR_THROWING rose with game FIRE up (residual post-flow animation)");
            return;
        }

        const Actor cachedTarget =
            g_detector.cachedAimedTarget;
        bool currentFrameAmbiguous = false;
        const Actor currentFrameTarget =
            ResolveExactAimedTarget(
                player,
                "automatic throw start",
                true,
                &currentFrameAmbiguous);

        // Ambiguity is never allowed to fall back to an older cached NPC.
        // A true no-result may use the last exact target retained while TARGET
        // stayed held through a transient RDR targeting-handle dropout.
        const Actor target =
            !currentFrameAmbiguous
                ? (currentFrameTarget != 0
                    ? currentFrameTarget
                    : cachedTarget)
                : 0;
        const char* targetSource =
            currentFrameAmbiguous
                ? "current-frame-ambiguous"
                : (currentFrameTarget != 0
                    ? "current-throw-frame"
                    : (cachedTarget != 0
                        ? "pre-throw-cache"
                        : "none"));

        char selectionLine[640] = {};
        std::snprintf(
            selectionLine,
            sizeof(selectionLine),
            "RDK #94 throw target selection: current=%lld cached=%lld selected=%lld source=%s disagreement=%d ambiguous=%d game-target=%d",
            static_cast<long long>(currentFrameTarget),
            static_cast<long long>(cachedTarget),
            static_cast<long long>(target),
            targetSource,
            currentFrameTarget != 0
                && cachedTarget != 0
                && currentFrameTarget != cachedTarget ? 1 : 0,
            currentFrameAmbiguous ? 1 : 0,
            IsGameActionDown("@GENERIC.TARGET") ? 1 : 0);
        WriteTrace(selectionLine);

        // The cache belongs only to this one pre-throw targeting cycle.
        g_detector.cachedAimedTarget = 0;
        g_detector.cachedAimDropoutRetained = false;

        if (target == 0)
        {
            Trace(
                "RDK #94 throw start ignored: no single exact aimed supported NPC in current frame or pre-throw cache");
            return;
        }

        const char* rejection =
            AutomaticThrowTargetRejectionReason(
                target,
                player);
        if (rejection != nullptr)
        {
            char line[512] = {};
            std::snprintf(
                line,
                sizeof(line),
                "RDK #94 aimed throw target rejected by automatic capture rules: target=%lld reason=%s",
                static_cast<long long>(target),
                rejection);
            WriteTrace(line);
            return;
        }

        AimedThrowRequest request{};
        request.shooter = player;
        request.target = target;
        request.throwId =
            g_detector.nextThrowId++;

        // The physical knife no longer selects the victim, but its damage can
        // still kill that victim before John finishes throwing. Protect only
        // this exact captured actor, preserving the actor's original proof mask.
        if (!ProtectAimedThrowTarget(request))
        {
            Trace(
                "RDK #94 aimed throw capture rejected: exact target protection failed");
            return;
        }

        // Suppress only weapon-hit reactions on the exact captured NPC before
        // the knife leaves John's hand. Close-range traces show the projectile
        // can arrive 15-50 ms after release, before the execution FIRE/link.
        // Proof protection stays responsible for damage; this setting only
        // prevents the physical knife from stealing the contextual action via
        // an immediate weapon reaction.
        if (!SuppressAimedThrowTargetWeaponReactions(request))
        {
            RestoreAimedThrowTargetProof(
                request,
                "weapon reaction suppression failed during aimed throw capture");
            Trace(
                "RDK #94 aimed throw capture rejected: target weapon reaction suppression failed");
            return;
        }

        if (!CaptureAimedThrowTargetStaging(request))
        {
            RestoreAimedThrowTargetState(
                request,
                "target staging capture failed during aimed throw capture");
            Trace(
                "RDK #94 aimed throw capture rejected: target staging capture failed");
            return;
        }

        g_detector.request = request;
        g_detector.hasRequest = true;

        // Automatic #94 deliberately applies only a short stand-still to this
        // exact captured NPC. Do not teleport/pin it: runtime traces showed
        // target teleports break the ground-state required by reposition.
        // Manual F8 remains on its old path.
        char line[640] = {};
        std::snprintf(
            line,
            sizeof(line),
            "RDK #94 aimed throw captured: throw=%llu shooter=%lld target=%lld source=%s original-proof=%d proof-protected=%d weapon-reactions-suppressed=%d target-staging=%d game-target=%d game-fire=%d",
            request.throwId,
            static_cast<long long>(player),
            static_cast<long long>(target),
            targetSource,
            request.originalProof,
            request.proofProtected ? 1 : 0,
            request.weaponReactionsSuppressed ? 1 : 0,
            request.targetStagingActive ? 1 : 0,
            IsGameActionDown("@GENERIC.TARGET") ? 1 : 0,
            IsGameActionDown("@GENERIC.FIRE") ? 1 : 0);
        WriteTrace(line);
        TraceCombatState(
            "aimed throw captured",
            player,
            target,
            0);
    }


    void RestoreRelaxedNeutralTargets()
    {
        for (int i = 0; i < g_relaxedNeutralTargetCount; ++i)
        {
            const Actor actor = g_relaxedNeutralTargets[i];
            if (actor == 0 || !ENTITY::IS_ACTOR_VALID(actor))
                continue;

            invoke<void>(
                SET_ACTOR_ONLY_HARDLOCK_IF_HOSTILE_HASH,
                actor,
                TRUE);
        }

        if (g_relaxedNeutralTargetCount > 0)
        {
            char line[320] = {};
            std::snprintf(
                line,
                sizeof(line),
                "RDK #93 neutral target policy restored: count=%d",
                g_relaxedNeutralTargetCount);
            WriteTrace(line);
        }

        g_relaxedNeutralTargetCount = 0;
        g_nextNeutralTargetRefresh = 0;
    }

    void RefreshRelaxedNeutralTargets(Actor player)
    {
        const ULONGLONG now = GetTickCount64();
        if (now < g_nextNeutralTargetRefresh)
            return;
        g_nextNeutralTargetRefresh = now + NEUTRAL_TARGET_REFRESH_MS;

        Actor actors[WORLD_ACTOR_CAPACITY] = {};
        int count = GetAllWorldActors(actors);
        if (count < 0)
            return;
        if (count > WORLD_ACTOR_CAPACITY)
            count = WORLD_ACTOR_CAPACITY;

        Actor next[WORLD_ACTOR_CAPACITY] = {};
        int nextCount = 0;
        const Vector3 playerPosition = ReadPosition(player);
        const float radiusSquared =
            RDK_TARGETING_RADIUS_METRES * RDK_TARGETING_RADIUS_METRES;

        for (int i = 0; i < count && nextCount < WORLD_ACTOR_CAPACITY; ++i)
        {
            const Actor candidate = actors[i];
            if (!IsEligibleNpc(candidate, player))
                continue;

            const Vector3 position = ReadPosition(candidate);
            if (!IsFinitePosition(position)
                || DistanceSquared(position, playerPosition) > radiusSquared)
            {
                continue;
            }

            invoke<void>(
                SET_ACTOR_CAN_BE_HARDLOCKED_HASH,
                candidate,
                TRUE);
            invoke<void>(
                SET_ACTOR_ONLY_HARDLOCK_IF_HOSTILE_HASH,
                candidate,
                FALSE);
            next[nextCount++] = candidate;
        }

        for (int i = 0; i < g_relaxedNeutralTargetCount; ++i)
        {
            const Actor oldActor = g_relaxedNeutralTargets[i];
            bool stillTracked = false;
            for (int j = 0; j < nextCount; ++j)
            {
                if (next[j] == oldActor)
                {
                    stillTracked = true;
                    break;
                }
            }

            if (!stillTracked
                && oldActor != 0
                && ENTITY::IS_ACTOR_VALID(oldActor))
            {
                invoke<void>(
                    SET_ACTOR_ONLY_HARDLOCK_IF_HOSTILE_HASH,
                    oldActor,
                    TRUE);
            }
        }

        for (int i = 0; i < nextCount; ++i)
            g_relaxedNeutralTargets[i] = next[i];
        g_relaxedNeutralTargetCount = nextCount;
    }

    void SetNeutralHardlockPolicy(Actor player, bool enabled)
    {
        if (g_neutralHardlockEnabled
            && (g_neutralHardlockPlayer != player || !enabled))
        {
            if (g_neutralHardlockPlayer != 0
                && ENTITY::IS_ACTOR_VALID(g_neutralHardlockPlayer))
            {
                invoke<void>(
                    SET_CAN_ACTOR_HARDLOCK_NEUTRALS_HASH,
                    g_neutralHardlockPlayer,
                    FALSE);
            }

            RestoreRelaxedNeutralTargets();
            Trace("RDK #93 neutral hardlock policy disabled");
            g_neutralHardlockEnabled = false;
            g_neutralHardlockPlayer = 0;
        }

        if (!enabled || !IsLivingActor(player))
            return;

        if (!g_neutralHardlockEnabled
            || g_neutralHardlockPlayer != player)
        {
            invoke<void>(
                SET_CAN_ACTOR_HARDLOCK_NEUTRALS_HASH,
                player,
                TRUE);
            g_neutralHardlockPlayer = player;
            g_neutralHardlockEnabled = true;

            char line[320] = {};
            std::snprintf(
                line,
                sizeof(line),
                "RDK #93 neutral hardlock policy enabled: player=%lld",
                static_cast<long long>(player));
            WriteTrace(line);
        }

        RefreshRelaxedNeutralTargets(player);
    }

    void UpdateManualTargetingPolicy()
    {
        const Actor player = CurrentPlayer();
        const bool enable =
            IsLivingActor(player)
            && !IsGameplayInterrupted()
            && INVENTORY::GET_WEAPON_IN_HAND(player)
                == THROWING_KNIFE_WEAPON;
        SetNeutralHardlockPolicy(player, enable);
    }



    bool EnsureTestNpcLayout()
    {
        if (g_testNpcLayout != 0
            && OBJECT::IS_LAYOUTREF_VALID(g_testNpcLayout))
        {
            return true;
        }

        g_testNpcLayout =
            OBJECT::CREATE_LAYOUT("RedDeadKnifemareExecutionTests");
        if (g_testNpcLayout == 0
            || !OBJECT::IS_LAYOUTREF_VALID(g_testNpcLayout))
        {
            Trace("RDK #94 test NPC spawn failed: layout invalid");
            g_testNpcLayout = 0;
            return false;
        }

        return true;
    }

    bool EnsureTestNpcStreamed()
    {
        if (STREAM::STREAMING_IS_ACTOR_LOADED(
                TEST_NPC_ACTOR_ENUM,
                -1))
        {
            return true;
        }

        STREAM::STREAMING_REQUEST_ACTOR(
            TEST_NPC_ACTOR_ENUM,
            TRUE,
            FALSE);

        const ULONGLONG started = GetTickCount64();
        while (!STREAM::STREAMING_IS_ACTOR_LOADED(
                    TEST_NPC_ACTOR_ENUM,
                    -1)
            && GetTickCount64() - started
                < TEST_NPC_STREAM_TIMEOUT_MS)
        {
            if (IsGameplayInterrupted())
                return false;
            scriptWait(0);
        }

        return STREAM::STREAMING_IS_ACTOR_LOADED(
            TEST_NPC_ACTOR_ENUM,
            -1) != 0;
    }

    void DestroyPreviousTestNpc()
    {
        if (g_testNpc != 0
            && ENTITY::IS_ACTOR_VALID(g_testNpc))
        {
            OBJECT::DESTROY_ACTOR(g_testNpc);
            Trace(
                "RDK #94 previous execution test NPC destroyed");
        }

        g_testNpc = 0;
    }

    bool SpawnExecutionTestNpc(bool walking)
    {
        if (g_throwExecutionFlow.state
                != ThrowExecutionFlowState::Idle
            || g_executionRunning)
        {
            Trace(
                "RDK #94 test NPC spawn rejected: execution flow active");
            return false;
        }

        const Actor player = CurrentPlayer();
        if (!IsLivingActor(player) || IsGameplayInterrupted())
        {
            Trace(
                "RDK #94 test NPC spawn rejected: player/gameplay invalid");
            return false;
        }

        if (!EnsureTestNpcLayout())
            return false;

        if (!EnsureTestNpcStreamed())
        {
            Trace(
                "RDK #94 test NPC spawn failed: actor enum 202 streaming timeout/interruption");
            return false;
        }

        DestroyPreviousTestNpc();

        char actorName[64] = {};
        std::snprintf(
            actorName,
            sizeof(actorName),
            "RDKExecutionTestNpc_%u",
            ++g_testNpcSpawnSequence);

        const Vector3 bootstrapPosition{0.0f, 0.0f, 0.0f};
        const Vector3 bootstrapOrientation{0.0f, 0.0f, 0.0f};
        const Actor npc = OBJECT::CREATE_ACTOR_IN_LAYOUT(
            g_testNpcLayout,
            actorName,
            TEST_NPC_ACTOR_ENUM,
            bootstrapPosition,
            bootstrapOrientation);

        if (npc == 0
            || !ENTITY::IS_ACTOR_VALID(npc)
            || !HEALTH::IS_ACTOR_ALIVE(npc))
        {
            if (npc != 0 && ENTITY::IS_ACTOR_VALID(npc))
                OBJECT::DESTROY_ACTOR(npc);
            Trace(
                "RDK #94 test NPC spawn failed: actor creation invalid");
            return false;
        }

        const int teleportResult =
            OBJECT::TELEPORT_OBJECT_TO_OBJECT(
                npc,
                player,
                0.0f,
                TEST_NPC_SPAWN_FORWARD_METRES,
                0.0f,
                0.0f,
                0.0f,
                0.0f,
                1.0f);
        if (teleportResult == 0)
        {
            OBJECT::DESTROY_ACTOR(npc);
            Trace(
                "RDK #94 test NPC spawn failed: relative placement failed");
            return false;
        }

        const int snapResult = OBJECT::SNAP_OBJECT_TO_GROUND(
            npc,
            TEST_NPC_GROUND_SNAP_DISTANCE,
            TRUE,
            TEST_NPC_GROUND_SNAP_MODE);

        const float playerHeading = ReadHeading(player);
        const float npcHeading =
            NormalizeHeading(playerHeading + 180.0f);
        invoke<void>(
            SET_ACTOR_HEADING_HASH,
            npc,
            npcHeading,
            TRUE);

        invoke<void>(TASK_CLEAR_HASH, npc);
        if (walking)
        {
            invoke<void>(TASK_WANDER_HASH, npc, 0);
        }
        else
        {
            invoke<void>(
                TASK_STAND_STILL_HASH,
                npc,
                TEST_NPC_STAND_STILL_SECONDS,
                0,
                0);
        }

        g_testNpc = npc;

        const Vector3 position = ReadPosition(npc);
        char line[512] = {};
        std::snprintf(
            line,
            sizeof(line),
            "RDK #94 test NPC spawned: key=%s mode=%s actor=%lld enum=%d position=(%.3f,%.3f,%.3f) heading=%.2f snap=%d",
            walking ? "F7" : "F6",
            walking ? "walking-wander" : "standing",
            static_cast<long long>(npc),
            TEST_NPC_ACTOR_ENUM,
            static_cast<double>(position.x),
            static_cast<double>(position.y),
            static_cast<double>(position.z),
            static_cast<double>(npcHeading),
            snapResult);
        WriteTrace(line);

        HUD::PRINT_HELP_B(
            walking
                ? "RDK test NPC: WALKING (F7)"
                : "RDK test NPC: STANDING (F6)",
            TEST_NPC_HELP_SECONDS,
            TRUE,
            1,
            0,
            FALSE,
            "",
            "");

        return true;
    }

    void HandleTestNpcSpawnHotkeys()
    {
        if (!EXECUTION_DEBUG_CONTROLS_ENABLED)
            return;

        if (IsKeyJustUp(TEST_NPC_STANDING_KEY))
        {
            Trace(
                "RDK #94 test NPC hotkey received: F6 standing");
            SpawnExecutionTestNpc(false);
            return;
        }

        if (IsKeyJustUp(TEST_NPC_WALKING_KEY))
        {
            Trace(
                "RDK #94 test NPC hotkey received: F7 walking");
            SpawnExecutionTestNpc(true);
        }
    }

    bool HandleManualExecutionDebugTrigger()
    {
        if (!EXECUTION_DEBUG_CONTROLS_ENABLED)
            return false;

        if (!IsKeyJustUp(MANUAL_EXECUTION_KEY))
            return false;

        Trace("RDK #93 debug trigger received: F8");

        if (g_throwExecutionFlow.state != ThrowExecutionFlowState::Idle)
        {
            Trace("RDK #94 re-entry rejected: F8 pressed while automatic flow is active");
            return true;
        }

        if (g_executionRunning)
        {
            Trace("RDK #93 debug trigger rejected: execution already running");
            return true;
        }

        const Actor player = CurrentPlayer();
        if (!IsLivingActor(player))
        {
            Trace("RDK #93 debug trigger rejected: local player invalid/dead");
            return true;
        }
        if (IsGameplayInterrupted())
        {
            Trace("RDK #93 debug trigger rejected: pause/cutscene active");
            return true;
        }

        const int weapon = INVENTORY::GET_WEAPON_IN_HAND(player);
        if (weapon != THROWING_KNIFE_WEAPON)
        {
            char line[320] = {};
            std::snprintf(
                line,
                sizeof(line),
                "RDK #93 debug trigger rejected: throwing knife must already be equipped current-weapon=%d",
                weapon);
            WriteTrace(line);
            return true;
        }

        const Actor target =
            ResolveExactAimedTarget(player, "manual F8");
        if (target == 0)
        {
            Trace(
                "RDK #93 debug trigger rejected: no actor under reticle or target lock");
            return true;
        }

        const char* rejection =
            ExecutionTargetRejectionReason(target, player);
        if (rejection != nullptr)
        {
            char line[512] = {};
            std::snprintf(
                line,
                sizeof(line),
                "RDK #93 debug target rejected: target=%lld reason=%s",
                static_cast<long long>(target),
                rejection);
            WriteTrace(line);
            return true;
        }

        char line[384] = {};
        std::snprintf(
            line,
            sizeof(line),
            "RDK #93 exact debug target resolved: player=%lld target=%lld weapon=%d",
            static_cast<long long>(player),
            static_cast<long long>(target),
            weapon);
        WriteTrace(line);

        // F8 keeps the proven contextual execution recipe and discards any
        // unconsumed automatic throw request first.
        CleanupDetector("RDK #93 manual execution taking ownership");

        const KnifeExecutionResult result =
            RunFrontThrowingKnifeExecution(target);
        std::snprintf(
            line,
            sizeof(line),
            "RDK #93 debug execution returned: target=%lld result=%s",
            static_cast<long long>(target),
            KnifeExecutionResultName(result));
        WriteTrace(line);
        return true;
    }

    const char* ThrowExecutionFlowStateName(ThrowExecutionFlowState state)
    {
        switch (state)
        {
        case ThrowExecutionFlowState::Idle:
            return "Idle";
        case ThrowExecutionFlowState::ThrowCaptured:
            return "ThrowCaptured";
        case ThrowExecutionFlowState::Executing:
            return "Executing";
        case ThrowExecutionFlowState::Recovering:
        default:
            return "Recovering";
        }
    }

    void TransitionThrowExecutionFlow(ThrowExecutionFlowState next)
    {
        char line[384] = {};
        std::snprintf(
            line,
            sizeof(line),
            "RDK #94 flow: %s -> %s throw=%llu target=%lld",
            ThrowExecutionFlowStateName(g_throwExecutionFlow.state),
            ThrowExecutionFlowStateName(next),
            g_throwExecutionFlow.request.throwId,
            static_cast<long long>(g_throwExecutionFlow.request.target));
        WriteTrace(line);
        g_throwExecutionFlow.state = next;
        g_throwExecutionFlow.transitionedAt = GetTickCount64();
    }

    void FinishThrowExecutionFlow()
    {
        RestoreAimedThrowTargetState(
            g_throwExecutionFlow.request,
            "flow final cleanup");

        const Actor player = CurrentPlayer();
        const int controllable =
            IsLivingActor(player) && !IsGameplayInterrupted()
                ? invoke<int>(
                    IS_PLAYER_CONTROLLABLE_HASH,
                    static_cast<int>(ACTOR::GET_LOCAL_SLOT()))
                : -1;
        const bool targetValid =
            g_throwExecutionFlow.request.target != 0
            && ENTITY::IS_ACTOR_VALID(g_throwExecutionFlow.request.target);

        char line[512] = {};
        std::snprintf(
            line,
            sizeof(line),
            "RDK #94 post-flow summary: throw=%llu player=%lld controllable=%d target=%lld target-valid=%d",
            g_throwExecutionFlow.request.throwId,
            static_cast<long long>(player),
            controllable,
            static_cast<long long>(g_throwExecutionFlow.request.target),
            targetValid ? 1 : 0);
        WriteTrace(line);

        TransitionThrowExecutionFlow(ThrowExecutionFlowState::Idle);
        g_throwExecutionFlow.request = {};
        g_throwExecutionFlow.transitionedAt = 0;
    }

    void AbortThrowExecutionRequest(const char* reason)
    {
        RestoreAimedThrowTargetState(
            g_throwExecutionFlow.request,
            reason);

        char line[512] = {};
        std::snprintf(
            line,
            sizeof(line),
            "RDK #94 aimed throw request aborted: throw=%llu target=%lld reason=%s",
            g_throwExecutionFlow.request.throwId,
            static_cast<long long>(g_throwExecutionFlow.request.target),
            reason);
        WriteTrace(line);

        CleanupDetector(reason);
        TransitionThrowExecutionFlow(ThrowExecutionFlowState::Recovering);
        FinishThrowExecutionFlow();
    }

    struct AutomaticExecutionLock
    {
        Vector3 targetPosition{};
        float targetHeading = 0.0f;
        bool valid = false;
    };

    bool CaptureAutomaticExecutionLock(
        Actor player,
        Actor target,
        AutomaticExecutionLock& lock)
    {
        if (IsGameplayInterrupted()
            || CurrentPlayer() != player
            || !IsLivingActor(player)
            || target == 0
            || !ENTITY::IS_ACTOR_VALID(target)
            || !HEALTH::IS_ACTOR_ALIVE(target))
        {
            return false;
        }

        lock.targetPosition = ReadPosition(target);
        lock.targetHeading = ReadHeading(target);
        lock.valid =
            IsFinitePosition(lock.targetPosition)
            && std::isfinite(lock.targetHeading);

        if (!lock.valid)
            return false;

        char line[640] = {};
        std::snprintf(
            line,
            sizeof(line),
            "RDK #94 deterministic target lock captured: target=%lld position=(%.3f,%.3f,%.3f) heading=%.2f proof=%d reacting=%d ground=%d incap=%d",
            static_cast<long long>(target),
            static_cast<double>(lock.targetPosition.x),
            static_cast<double>(lock.targetPosition.y),
            static_cast<double>(lock.targetPosition.z),
            static_cast<double>(lock.targetHeading),
            invoke<int>(GET_ACTOR_PROOF_HASH, target),
            invoke<BOOL>(IS_ACTOR_REACTING_HASH, target) != FALSE ? 1 : 0,
            invoke<BOOL>(IS_ACTOR_ON_GROUND_HASH, target) != FALSE ? 1 : 0,
            invoke<BOOL>(GET_ACTOR_INCAPACITATED_HASH, target) != FALSE ? 1 : 0);
        WriteTrace(line);
        return true;
    }

    bool MaintainAutomaticExecutionLock(
        Actor player,
        Actor target,
        const AutomaticExecutionLock& lock,
        const char* stage)
    {
        if (!lock.valid
            || IsGameplayInterrupted()
            || CurrentPlayer() != player
            || !IsLivingActor(player)
            || target == 0
            || !ENTITY::IS_ACTOR_VALID(target)
            || !HEALTH::IS_ACTOR_ALIVE(target))
        {
            return false;
        }

        // Automatic #94 deliberately owns this exact actor now. Cancel the
        // actor's current reaction/action tree and snap the transform back to
        // the captured pose so combat AI, projectile reaction, hostility and
        // locomotion cannot move the target before the execution attempt.
        invoke<void>(
            RESET_REACT_NODE_FOR_ACTOR_HASH,
            target);
        invoke<void>(
            RESET_ACTIONTREE_FOR_ACTOR_HASH,
            target);

        const Vector2 targetXY{
            lock.targetPosition.x,
            lock.targetPosition.y};
        invoke<void>(
            TELEPORT_ACTOR_WITH_HEADING_HASH,
            target,
            targetXY,
            lock.targetPosition.z,
            lock.targetHeading,
            FALSE,
            FALSE,
            FALSE);
        invoke<void>(
            TASK_STAND_STILL_HASH,
            target,
            AUTOMATIC_TARGET_STAND_STILL_SECONDS,
            0,
            0);

        char line[448] = {};
        std::snprintf(
            line,
            sizeof(line),
            "RDK #94 deterministic target lock maintained: stage=%s target=%lld proof=%d alive=%d",
            stage != nullptr ? stage : "unknown",
            static_cast<long long>(target),
            invoke<int>(GET_ACTOR_PROOF_HASH, target),
            HEALTH::IS_ACTOR_ALIVE(target) ? 1 : 0);
        WriteTrace(line);
        return true;
    }

    bool PinAutomaticExecutionTargetForDecision(
        Actor player,
        Actor target,
        const AutomaticExecutionLock& lock,
        const char* stage)
    {
        if (!lock.valid
            || IsGameplayInterrupted()
            || CurrentPlayer() != player
            || !IsLivingActor(player)
            || target == 0
            || !ENTITY::IS_ACTOR_VALID(target)
            || !HEALTH::IS_ACTOR_ALIVE(target))
        {
            return false;
        }

        // Do not reset reaction/action trees or touch John's transform here.
        // RDR must be free to turn the injected click into the linked execution.
        // Only hold the exact NPC at the captured execution pose so ordinary
        // gait/reaction drift cannot destroy the contextual geometry first.
        const Vector2 targetXY{
            lock.targetPosition.x,
            lock.targetPosition.y};
        invoke<void>(
            TELEPORT_ACTOR_WITH_HEADING_HASH,
            target,
            targetXY,
            lock.targetPosition.z,
            lock.targetHeading,
            FALSE,
            FALSE,
            FALSE);

        char line[640] = {};
        std::snprintf(
            line,
            sizeof(line),
            "RDK #94 execution-decision target pinned: stage=%s player=%lld target=%lld target-heading=%.2f target-reacting=%d target-gait=%d player-throwing=%d",
            stage != nullptr ? stage : "unknown",
            static_cast<long long>(player),
            static_cast<long long>(target),
            static_cast<double>(lock.targetHeading),
            invoke<BOOL>(
                IS_ACTOR_REACTING_HASH,
                target) != FALSE ? 1 : 0,
            invoke<int>(
                GET_ACTOR_GAIT_TYPE_HASH,
                target),
            invoke<BOOL>(
                IS_ACTOR_THROWING_HASH,
                player) != FALSE ? 1 : 0);
        WriteTrace(line);
        return true;
    }

    bool RepositionPlayerToAutomaticLock(
        Actor player,
        Actor target,
        const AutomaticExecutionLock& lock,
        const char* stage)
    {
        if (!lock.valid
            || IsGameplayInterrupted()
            || CurrentPlayer() != player
            || !IsLivingActor(player)
            || target == 0
            || !ENTITY::IS_ACTOR_VALID(target))
        {
            return false;
        }

        const float radians =
            lock.targetHeading * PI / 180.0f;
        Vector3 desired{};
        desired.x =
            lock.targetPosition.x
            - std::sin(radians)
                * FRONT_EXECUTION_OFFSET_METRES;
        desired.y = lock.targetPosition.y;
        desired.z =
            lock.targetPosition.z
            - std::cos(radians)
                * FRONT_EXECUTION_OFFSET_METRES;
        const float desiredHeading =
            NormalizeHeading(
                lock.targetHeading + 180.0f);

        const Vector2 desiredXY{
            desired.x,
            desired.y};
        invoke<void>(
            TELEPORT_ACTOR_WITH_HEADING_HASH,
            player,
            desiredXY,
            desired.z,
            desiredHeading,
            FALSE,
            FALSE,
            FALSE);

        char line[704] = {};
        std::snprintf(
            line,
            sizeof(line),
            "RDK #94 deterministic player reposition: stage=%s player=%lld target=%lld destination=(%.3f,%.3f,%.3f) heading=%.2f target-heading=%.2f",
            stage != nullptr ? stage : "unknown",
            static_cast<long long>(player),
            static_cast<long long>(target),
            static_cast<double>(desired.x),
            static_cast<double>(desired.y),
            static_cast<double>(desired.z),
            static_cast<double>(desiredHeading),
            static_cast<double>(lock.targetHeading));
        WriteTrace(line);
        return true;
    }

    bool ForceAutomaticTargetDeath(
        Actor target,
        const char* reason)
    {
        if (target == 0
            || !ENTITY::IS_ACTOR_VALID(target))
        {
            return false;
        }

        const float healthBefore =
            invoke<float>(
                GET_ACTOR_HEALTH_HASH,
                target);
        invoke<void>(
            SET_ACTOR_HEALTH_HASH,
            target,
            0.0f);

        const ULONGLONG started =
            GetTickCount64();
        while (GetTickCount64() - started
            < EXECUTION_FIRST_ATTEMPT_TIMEOUT_MS)
        {
            if (!ENTITY::IS_ACTOR_VALID(target))
                break;
            if (!HEALTH::IS_ACTOR_ALIVE(target))
                break;
            scriptWait(0);
        }

        const bool dead =
            !ENTITY::IS_ACTOR_VALID(target)
            || !HEALTH::IS_ACTOR_ALIVE(target);
        const float healthAfter =
            ENTITY::IS_ACTOR_VALID(target)
                ? invoke<float>(
                    GET_ACTOR_HEALTH_HASH,
                    target)
                : 0.0f;

        char line[640] = {};
        std::snprintf(
            line,
            sizeof(line),
            "RDK #94 forced lethal fallback: target=%lld reason=%s health-before=%.3f health-after=%.3f dead=%d",
            static_cast<long long>(target),
            reason != nullptr ? reason : "unknown",
            static_cast<double>(healthBefore),
            static_cast<double>(healthAfter),
            dead ? 1 : 0);
        WriteTrace(line);
        return dead;
    }

    void TraceDeterministicExecutionDecision(
        const char* stage,
        Actor player,
        Actor target,
        ULONGLONG windowStarted,
        int probeIndex)
    {
        const bool playerValid =
            player != 0
            && ENTITY::IS_ACTOR_VALID(player);
        const bool targetValid =
            target != 0
            && ENTITY::IS_ACTOR_VALID(target);

        const int playerSlot =
            static_cast<int>(ACTOR::GET_LOCAL_SLOT());
        const bool targeted =
            playerValid
            && targetValid
            && invoke<BOOL>(
                IS_PLAYER_TARGETTING_ACTOR_HASH,
                playerSlot,
                target,
                TRUE) != FALSE;
        const Actor targetActor =
            invoke<Actor>(GET_TARGET_ACTOR_HASH);
        const Actor reticleTarget =
            playerValid
                ? invoke<Actor>(
                    GET_ACTOR_UNDER_RETICLE_HASH,
                    player,
                    0)
                : 0;
        const ExecutionLinkState linkState =
            playerValid && targetValid
                ? ReadExecutionLinkState(
                    player,
                    target)
                : ExecutionLinkState{};

        const float lastHitTime =
            targetValid
                ? invoke<float>(
                    GET_LAST_HIT_TIME_HASH,
                    target)
                : -1.0f;
        const Actor lastAttacker =
            targetValid
                ? invoke<Actor>(
                    GET_LAST_ATTACKER_HASH,
                    target)
                : 0;
        const int lastHitWeapon =
            targetValid
                ? invoke<int>(
                    GET_LAST_HIT_WEAPON_HASH,
                    target)
                : -1;

        const Vector3 playerPosition =
            playerValid ? ReadPosition(player) : Vector3{};
        const Vector3 targetPosition =
            targetValid ? ReadPosition(target) : Vector3{};

        char line[1900] = {};
        std::snprintf(
            line,
            sizeof(line),
            "RDK #94 deterministic context probe: stage=%s probe=%d elapsed-ms=%llu player-valid=%d player-current=%d player-alive=%d target-valid=%d target-alive=%d player-controllable=%d player-ready=%d player-throwing=%d player-reacting=%d fire-released=%d contextual-trigger-ready=%d target-ready=%d target-reacting=%d target-ground=%d target-incap=%d target-gait=%d target-posture=%d game-target=%d game-fire=%d LMB=%d RMB=%d exact-targeted=%d target-actor=%lld target-actor-match=%d reticle=%lld reticle-match=%d player-linked=%d target-linked=%d player-animation=%d target-animation=%d player-phase=%d target-phase=%d health=%.3f proof=%d one-shot=%d hostile=%d last-hit-time=%.3f last-attacker=%lld last-attacker-is-john=%d last-hit-weapon=%d last-hit-is-knife=%d player-attack-time=%.3f player-attack-target=%lld player-ground=%d player=(%.3f,%.3f,%.3f) player-heading=%.2f player-action-time=%.3f weapon=%d ammo=%.1f target-y=%.3f",
            stage != nullptr ? stage : "unknown",
            probeIndex,
            static_cast<unsigned long long>(
                windowStarted != 0
                    ? GetTickCount64() - windowStarted
                    : 0),
            playerValid ? 1 : 0,
            playerValid && CurrentPlayer() == player ? 1 : 0,
            playerValid && HEALTH::IS_ACTOR_ALIVE(player) ? 1 : 0,
            targetValid ? 1 : 0,
            targetValid && HEALTH::IS_ACTOR_ALIVE(target) ? 1 : 0,
            playerValid
                ? invoke<int>(
                    IS_PLAYER_CONTROLLABLE_HASH,
                    playerSlot)
                : -1,
            playerValid
                && invoke<BOOL>(
                    IS_ACTOR_READY_FOR_ACTION_HASH,
                    player) != FALSE ? 1 : 0,
            playerValid
                && invoke<BOOL>(
                    IS_ACTOR_THROWING_HASH,
                    player) != FALSE ? 1 : 0,
            playerValid
                && invoke<BOOL>(
                    IS_ACTOR_REACTING_HASH,
                    player) != FALSE ? 1 : 0,
            ((GetAsyncKeyState(VK_LBUTTON) & 0x8000) == 0
                && !IsGameActionDown("@GENERIC.FIRE")) ? 1 : 0,
            (playerValid
                && invoke<BOOL>(
                    IS_ACTOR_THROWING_HASH,
                    player) == FALSE
                && invoke<BOOL>(
                    IS_ACTOR_REACTING_HASH,
                    player) == FALSE
                && (GetAsyncKeyState(VK_LBUTTON) & 0x8000) == 0
                && !IsGameActionDown("@GENERIC.FIRE")) ? 1 : 0,
            targetValid
                && invoke<BOOL>(
                    IS_ACTOR_READY_FOR_ACTION_HASH,
                    target) != FALSE ? 1 : 0,
            targetValid
                && invoke<BOOL>(
                    IS_ACTOR_REACTING_HASH,
                    target) != FALSE ? 1 : 0,
            targetValid
                && invoke<BOOL>(
                    IS_ACTOR_ON_GROUND_HASH,
                    target) != FALSE ? 1 : 0,
            targetValid
                && invoke<BOOL>(
                    GET_ACTOR_INCAPACITATED_HASH,
                    target) != FALSE ? 1 : 0,
            targetValid
                ? invoke<int>(
                    GET_ACTOR_GAIT_TYPE_HASH,
                    target)
                : -1,
            targetValid
                ? invoke<int>(
                    GET_ACTOR_POSTURE_HASH,
                    target)
                : -1,
            IsGameActionDown("@GENERIC.TARGET") ? 1 : 0,
            IsGameActionDown("@GENERIC.FIRE") ? 1 : 0,
            (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0 ? 1 : 0,
            (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0 ? 1 : 0,
            targeted ? 1 : 0,
            static_cast<long long>(targetActor),
            targetActor == target ? 1 : 0,
            static_cast<long long>(reticleTarget),
            reticleTarget == target ? 1 : 0,
            linkState.playerLinkedTarget,
            linkState.targetLinkedTarget,
            linkState.playerPerforming ? 1 : 0,
            linkState.targetPerforming ? 1 : 0,
            linkState.playerPhaseLocked ? 1 : 0,
            linkState.targetPhaseLocked ? 1 : 0,
            targetValid
                ? static_cast<double>(
                    invoke<float>(
                        GET_ACTOR_HEALTH_HASH,
                        target))
                : -1.0,
            targetValid
                ? invoke<int>(
                    GET_ACTOR_PROOF_HASH,
                    target)
                : -1,
            targetValid
                ? invoke<int>(
                    GET_ACTOR_ONE_SHOT_DEATH_STATUS_HASH,
                    target)
                : -1,
            playerValid && targetValid
                && invoke<BOOL>(
                    AI_IS_HOSTILE_OR_ENEMY_HASH,
                    target,
                    player) != FALSE ? 1 : 0,
            static_cast<double>(lastHitTime),
            static_cast<long long>(lastAttacker),
            lastAttacker == player ? 1 : 0,
            lastHitWeapon,
            lastHitWeapon == THROWING_KNIFE_WEAPON ? 1 : 0,
            playerValid
                ? static_cast<double>(
                    invoke<float>(
                        GET_LAST_ATTACK_TIME_HASH,
                        player))
                : -1.0,
            playerValid
                ? static_cast<long long>(
                    invoke<Actor>(
                        GET_LAST_ATTACK_TARGET_HASH,
                        player))
                : 0LL,
            playerValid
                && invoke<BOOL>(IS_ACTOR_ON_GROUND_HASH, player) != FALSE ? 1 : 0,
            static_cast<double>(playerPosition.x),
            static_cast<double>(playerPosition.y),
            static_cast<double>(playerPosition.z),
            playerValid ? static_cast<double>(ReadHeading(player)) : 0.0,
            playerValid
                ? static_cast<double>(invoke<float>(
                    GET_CURR_ACTION_NODE_PLAY_TIME_HASH, player))
                : -1.0,
            playerValid ? INVENTORY::GET_WEAPON_IN_HAND(player) : -1,
            playerValid ? static_cast<double>(ReadKnifeAmmo(player)) : -1.0,
            static_cast<double>(targetPosition.y));
        WriteTrace(line);
        TraceControllerInput(stage, player, target, probeIndex);
    }

    bool ForceAutomaticTargetIdleForExecution(
        Actor target)
    {
        if (target == 0
            || !ENTITY::IS_ACTOR_VALID(target)
            || !HEALTH::IS_ACTOR_ALIVE(target))
        {
            return false;
        }

        const ULONGLONG started = GetTickCount64();
        int attempt = 0;

        while (true)
        {
            ++attempt;

            // Clear any old combat/action task, then immediately give the
            // target an explicit stand-still task. Repeating this for a few
            // frames is intentional: TASK_CLEAR alone did not consistently
            // produce the fresh idle state observed in the successful trace.
            invoke<void>(
                TASK_CLEAR_HASH,
                target);
            invoke<void>(
                RESET_REACT_NODE_FOR_ACTOR_HASH,
                target);
            invoke<void>(
                TASK_STAND_STILL_HASH,
                target,
                AUTOMATIC_TARGET_STAND_STILL_SECONDS,
                0,
                0);

            scriptWait(0);

            if (target == 0
                || !ENTITY::IS_ACTOR_VALID(target)
                || !HEALTH::IS_ACTOR_ALIVE(target))
            {
                return false;
            }

            const int gait =
                invoke<int>(
                    GET_ACTOR_GAIT_TYPE_HASH,
                    target);
            const float actionTime =
                invoke<float>(
                    GET_CURR_ACTION_NODE_PLAY_TIME_HASH,
                    target);
            const bool reacting =
                invoke<BOOL>(
                    IS_ACTOR_REACTING_HASH,
                    target) != FALSE;
            const bool ready =
                invoke<BOOL>(
                    IS_ACTOR_READY_FOR_ACTION_HASH,
                    target) != FALSE;

            char line[640] = {};
            std::snprintf(
                line,
                sizeof(line),
                "RDK #94 target idle settle: target=%lld attempt=%d elapsed-ms=%llu gait=%d action-time=%.3f reacting=%d ready=%d",
                static_cast<long long>(target),
                attempt,
                static_cast<unsigned long long>(
                    GetTickCount64() - started),
                gait,
                static_cast<double>(actionTime),
                reacting ? 1 : 0,
                ready ? 1 : 0);
            WriteTrace(line);
            TraceControllerInput("target-idle-settle-frame", CurrentPlayer(), target, attempt);

            if (gait == 0
                && std::isfinite(actionTime)
                && actionTime
                    <= AUTOMATIC_TARGET_IDLE_ACTION_TIME_MAX_SECONDS
                && !reacting)
            {
                Trace(
                    "RDK #94 target idle settle confirmed: gait=0 with fresh action node");
                return true;
            }

            if (GetTickCount64() - started
                >= AUTOMATIC_TARGET_IDLE_SETTLE_MS)
            {
                Trace(
                    "RDK #94 target idle settle window expired; continuing with final observed state");
                return true;
            }
        }
    }


    bool WaitForAutomaticFireRelease(
        Actor player,
        Actor target)
    {
        const ULONGLONG started = GetTickCount64();
        int probe = 0;

        TraceControllerInput(
            "automatic-fire-release-wait-start",
            player,
            target,
            probe);
        Trace(
            "RDK #94 automatic FIRE release wait started: keep TARGET/LT held; waiting for physical attack input/game FIRE to return up before one synthetic FIRE pulse");

        while (GetTickCount64() - started
            < AUTOMATIC_FIRE_RELEASE_WAIT_MS)
        {
            if (g_throwExecutionFlow.state
                    != ThrowExecutionFlowState::Executing
                || g_throwExecutionFlow.request.shooter != player
                || g_throwExecutionFlow.request.target != target
                || IsGameplayInterrupted()
                || CurrentPlayer() != player
                || !IsLivingActor(player)
                || !IsEligibleNpc(target, player)
                || INVENTORY::GET_WEAPON_IN_HAND(player)
                    != THROWING_KNIFE_WEAPON)
            {
                Trace(
                    "RDK #94 automatic FIRE release wait aborted: execution/player/target/weapon context changed");
                return false;
            }

            if (!IsGameActionDown("@GENERIC.TARGET"))
            {
                Trace(
                    "RDK #94 automatic FIRE release wait aborted: TARGET/LT released");
                return false;
            }

            if (HasLinkedAction(player)
                || HasLinkedAction(target))
            {
                Trace(
                    "RDK #94 automatic FIRE release wait aborted: linked action already active");
                return false;
            }

            if (!MaintainAimedThrowTargetStaging(
                    g_throwExecutionFlow.request))
            {
                Trace(
                    "RDK #94 automatic FIRE release wait aborted: exact target staging could not be maintained");
                return false;
            }

            const bool lmbReleased =
                (GetAsyncKeyState(VK_LBUTTON) & 0x8000) == 0;
            const bool gameFireReleased =
                !IsGameActionDown("@GENERIC.FIRE");

            if (lmbReleased && gameFireReleased)
            {
                char line[384] = {};
                std::snprintf(
                    line,
                    sizeof(line),
                    "RDK #94 automatic FIRE release confirmed: throw=%llu target=%lld elapsed-ms=%llu probes=%d; synthetic FIRE may create a fresh 0->1 edge",
                    g_throwExecutionFlow.request.throwId,
                    static_cast<long long>(target),
                    static_cast<unsigned long long>(
                        GetTickCount64() - started),
                    probe);
                WriteTrace(line);
                TraceControllerInput(
                    "automatic-fire-release-confirmed",
                    player,
                    target,
                    probe);
                return true;
            }

            scriptWait(0);
            ++probe;
        }

        char timeoutLine[384] = {};
        std::snprintf(
            timeoutLine,
            sizeof(timeoutLine),
            "RDK #94 automatic FIRE release wait timed out: throw=%llu target=%lld timeout-ms=%llu game-fire=%d LMB=%d",
            g_throwExecutionFlow.request.throwId,
            static_cast<long long>(target),
            static_cast<unsigned long long>(
                AUTOMATIC_FIRE_RELEASE_WAIT_MS),
            IsGameActionDown("@GENERIC.FIRE") ? 1 : 0,
            (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0 ? 1 : 0);
        WriteTrace(timeoutLine);
        TraceControllerInput(
            "automatic-fire-release-timeout",
            player,
            target,
            probe);
        return false;
    }

    const char* AutomaticFireBlockReason(Actor player, Actor target)
    {
        const auto& request = g_throwExecutionFlow.request;
        if (g_throwExecutionFlow.state != ThrowExecutionFlowState::Executing
            || request.throwId == 0
            || request.shooter != player
            || request.target != target)
            return "automatic FIRE gate: captured request ownership changed; no FIRE";

        if (IsGameplayInterrupted()
            || CurrentPlayer() != player
            || !IsLivingActor(player)
            || !IsEligibleNpc(target, player)
            || INVENTORY::GET_WEAPON_IN_HAND(player) != THROWING_KNIFE_WEAPON)
            return "automatic FIRE gate: player/target/weapon context changed; no FIRE";

        if (invoke<int>(IS_PLAYER_CONTROLLABLE_HASH,
                static_cast<int>(ACTOR::GET_LOCAL_SLOT())) == 0
            || invoke<BOOL>(IS_ACTOR_READY_FOR_ACTION_HASH, player) == FALSE
            || invoke<BOOL>(IS_ACTOR_THROWING_HASH, player) != FALSE
            || invoke<BOOL>(IS_ACTOR_REACTING_HASH, player) != FALSE
            || HasLinkedAction(player) || HasLinkedAction(target))
            return "automatic FIRE gate: player busy or linked action active; no FIRE";

        // Keep TARGET held, and require the attack action to still be up at
        // the final gate so the synthetic pulse can create a fresh FIRE edge.
        if (!IsGameActionDown("@GENERIC.TARGET"))
            return "automatic FIRE gate: aim released; no FIRE";
        if ((GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0
            || IsGameActionDown("@GENERIC.FIRE"))
            return "automatic FIRE gate: attack/FIRE not released; no FIRE";

        // These are observations only. Never assign a newly resolved actor to
        // the captured request. Reject even a stale/ineligible foreign handle.
        const Actor reticle = invoke<Actor>(GET_ACTOR_UNDER_RETICLE_HASH, player, 0);
        const Actor targetActor = invoke<Actor>(GET_TARGET_ACTOR_HASH);
        if ((reticle != 0 && reticle != target)
            || (targetActor != 0 && targetActor != target))
            return "automatic FIRE gate: reticle/target disagrees with captured NPC; no FIRE";

        bool ambiguous = false;
        int predicateScanCount = -1;
        const Actor resolved = ResolveExactAimedTarget(
            player, "automatic pre-FIRE verification", true, &ambiguous,
            &predicateScanCount);
        if (ambiguous)
            return "automatic FIRE gate: ambiguous live aim; no FIRE";
        if (resolved != 0 && resolved != target)
            return "automatic FIRE gate: live aim differs from captured NPC; no FIRE";

        // A missing export or a full/truncated enumeration cannot establish
        // an empty aim reading. The resolver's capture/F8 behavior is unchanged.
        const bool incompleteScan = resolved == 0
            && (predicateScanCount < 0 || predicateScanCount >= WORLD_ACTOR_CAPACITY);
        char line[512] = {};
        std::snprintf(line, sizeof(line),
            "RDK #94 automatic captured-aim gate: throw=%llu player=%lld captured-target=%lld reticle=%lld target-actor=%lld resolved=%lld predicate-scan-count=%d decision=%s",
            request.throwId,
            static_cast<long long>(player),
            static_cast<long long>(target),
            static_cast<long long>(reticle),
            static_cast<long long>(targetActor),
            static_cast<long long>(resolved),
            predicateScanCount,
            incompleteScan ? "reject-incomplete-live-aim-scan"
                : resolved == 0 ? "retain-captured-no-live-aim"
                : "live-aim-matches-captured");
        WriteTrace(line);
        if (incompleteScan)
            return "automatic FIRE gate: incomplete live aim scan; no FIRE";
        return nullptr;
    }

    KnifeExecutionResult RunDeterministicAutomaticKnifeExecution(
        Actor target)
    {
        if (g_executionRunning)
        {
            Trace(
                "RDK #94 throw-cancel contextual execution rejected: another execution is active");
            return KnifeExecutionResult::Failed;
        }

        g_executionRunning = true;
        const Actor player = CurrentPlayer();
        bool allowExecuteEnabled = false;
        bool temporaryEnemyRelationship = false;
        bool targetPreparedThroughNeutral = false;
        bool originalTargetHostile = false;
        int originalTargetFaction = FACTION_NEUTRAL;
        bool oneShotChanged = false;
        int originalOneShotDeath = 0;

        const auto finish =
            [&](KnifeExecutionResult result, const char* reason)
            {
                if (g_primaryAttackDown)
                {
                    INPUT up = {};
                    up.type = INPUT_MOUSE;
                    up.mi.dwFlags = MOUSEEVENTF_LEFTUP;
                    SendInput(1, &up, sizeof(INPUT));
                    g_primaryAttackDown = false;
                }

                // Temporary all-proofs must never erase a throwing-knife hit
                // that John actually landed on the exact captured NPC. Run the
                // compensation from the shared exit path so early failures
                // (for example TARGET/LT release before contextual FIRE) are
                // covered just like a later no-link timeout. Restrict this to
                // the period where this request still owns proof protection;
                // once linked execution restores proof, its later hits are not
                // eligible for this fallback.
                if (result != KnifeExecutionResult::Success
                    && g_throwExecutionFlow.request.proofProtected
                    && player != 0
                    && ENTITY::IS_ACTOR_VALID(player)
                    && target != 0
                    && ENTITY::IS_ACTOR_VALID(target)
                    && HEALTH::IS_ACTOR_ALIVE(target))
                {
                    const float currentHitTime =
                        invoke<float>(
                            GET_LAST_HIT_TIME_HASH,
                            target);
                    const Actor lastAttacker =
                        invoke<Actor>(
                            GET_LAST_ATTACKER_HASH,
                            target);
                    const int lastHitWeapon =
                        invoke<int>(
                            GET_LAST_HIT_WEAPON_HASH,
                            target);
                    const bool exactCapturedKnifeHit =
                        std::fabs(
                            currentHitTime
                            - g_throwExecutionFlow.request.targetHitBaseline)
                            > 0.0001f
                        && lastAttacker == player
                        && lastHitWeapon == THROWING_KNIFE_WEAPON;

                    char fallbackProbe[896] = {};
                    std::snprintf(
                        fallbackProbe,
                        sizeof(fallbackProbe),
                        "RDK #94 shared knife fallback probe: throw=%llu target=%lld exit-result=%s exit-reason=%s hit-baseline=%.3f hit-now=%.3f attacker=%lld attacker-is-john=%d weapon=%d knife=%d exact-captured-hit=%d proof=%d",
                        g_throwExecutionFlow.request.throwId,
                        static_cast<long long>(target),
                        KnifeExecutionResultName(result),
                        reason != nullptr ? reason : "unknown",
                        static_cast<double>(
                            g_throwExecutionFlow.request.targetHitBaseline),
                        static_cast<double>(currentHitTime),
                        static_cast<long long>(lastAttacker),
                        lastAttacker == player ? 1 : 0,
                        lastHitWeapon,
                        lastHitWeapon == THROWING_KNIFE_WEAPON ? 1 : 0,
                        exactCapturedKnifeHit ? 1 : 0,
                        invoke<int>(
                            GET_ACTOR_PROOF_HASH,
                            target));
                    WriteTrace(fallbackProbe);

                    if (exactCapturedKnifeHit)
                    {
                        RestoreAimedThrowTargetState(
                            g_throwExecutionFlow.request,
                            "shared failed handoff after exact captured knife hit");

                        const bool killed =
                            ForceAutomaticTargetDeath(
                                target,
                                "exact captured throwing knife hit while protected; shared handoff exit compensation");

                        char fallbackResult[640] = {};
                        std::snprintf(
                            fallbackResult,
                            sizeof(fallbackResult),
                            "RDK #94 shared knife fallback result: throw=%llu target=%lld original-result=%s killed=%d",
                            g_throwExecutionFlow.request.throwId,
                            static_cast<long long>(target),
                            KnifeExecutionResultName(result),
                            killed ? 1 : 0);
                        WriteTrace(fallbackResult);

                        if (killed)
                        {
                            result = KnifeExecutionResult::Success;
                            reason =
                                "failed handoff after exact captured knife hit; target force-killed";
                        }
                    }
                }

                if (oneShotChanged
                    && target != 0
                    && ENTITY::IS_ACTOR_VALID(target)
                    && HEALTH::IS_ACTOR_ALIVE(target))
                {
                    invoke<void>(
                        SET_ACTOR_ONE_SHOT_DEATH_HASH,
                        target,
                        originalOneShotDeath);
                }

                if (allowExecuteEnabled
                    && target != 0
                    && ENTITY::IS_ACTOR_VALID(target)
                    && HEALTH::IS_ACTOR_ALIVE(target))
                {
                    invoke<void>(
                        SET_ALLOW_EXECUTE_HASH,
                        target,
                        FALSE);
                }

                if (temporaryEnemyRelationship
                    && player != 0
                    && target != 0
                    && ENTITY::IS_ACTOR_VALID(player)
                    && ENTITY::IS_ACTOR_VALID(target)
                    && HEALTH::IS_ACTOR_ALIVE(target))
                {
                    invoke<void>(
                        MEMORY_CLEAR_ALL_HASH,
                        target);
                    invoke<void>(
                        SET_ACTOR_FACTION_HASH,
                        target,
                        originalTargetFaction);

                    if (originalTargetHostile)
                    {
                        invoke<void>(
                            MEMORY_CONSIDER_AS_ENEMY_HASH,
                            target,
                            player);
                    }
                    else
                    {
                        invoke<void>(
                            MEMORY_CONSIDER_ACCORDING_TO_FACTION_HASH,
                            target,
                            player);
                    }

                    char restoreLine[576] = {};
                    std::snprintf(
                        restoreLine,
                        sizeof(restoreLine),
                        "RDK #94 target relationship restored: target=%lld original-faction=%d original-hostile=%d hostile-now=%d",
                        static_cast<long long>(target),
                        originalTargetFaction,
                        originalTargetHostile ? 1 : 0,
                        invoke<BOOL>(
                            AI_IS_HOSTILE_OR_ENEMY_HASH,
                            target,
                            player) != FALSE ? 1 : 0);
                    WriteTrace(restoreLine);
                }

                char line[704] = {};
                std::snprintf(
                    line,
                    sizeof(line),
                    "RDK #94 throw-cancel contextual execution %s: player=%lld target=%lld reason=%s target-valid=%d target-alive=%d proof=%d",
                    KnifeExecutionResultName(result),
                    static_cast<long long>(player),
                    static_cast<long long>(target),
                    reason != nullptr ? reason : "unknown",
                    target != 0
                        && ENTITY::IS_ACTOR_VALID(target) ? 1 : 0,
                    target != 0
                        && ENTITY::IS_ACTOR_VALID(target)
                        && HEALTH::IS_ACTOR_ALIVE(target) ? 1 : 0,
                    target != 0
                        && ENTITY::IS_ACTOR_VALID(target)
                        ? invoke<int>(GET_ACTOR_PROOF_HASH, target)
                        : -1);
                WriteTrace(line);
                g_executionRunning = false;
                return result;
            };

        if (IsGameplayInterrupted()
            || !IsLivingActor(player)
            || target == 0
            || !ENTITY::IS_ACTOR_VALID(target)
            || !HEALTH::IS_ACTOR_ALIVE(target))
        {
            return finish(
                KnifeExecutionResult::Aborted,
                "player/target context invalid at throw-cancel contextual execution start");
        }

        Trace(
            "RDK #94 THROW-CANCEL contextual diagnostic: immediately cancel John's captured throw with CLEAR_TASKS flag 4, reposition John, force every target through Neutral + a fresh idle stand-still state before temporary enemy preparation, enable execution, then send exactly one F8-style FIRE");

        const int playerSlot =
            static_cast<int>(ACTOR::GET_LOCAL_SLOT());
        if (invoke<int>(
                IS_PLAYER_CONTROLLABLE_HASH,
                playerSlot) == 0)
        {
            return finish(
                KnifeExecutionResult::Aborted,
                "player control already disabled before pre-impact task clear");
        }

        TraceDeterministicExecutionDecision(
            "throw-cancel-before-player-handoff",
            player,
            target,
            0,
            0);

        // Cancel the throw immediately. Do not wait for ammo, attack-time, or
        // projectile state. Reposition while control is disabled, restore
        // control, yield one frame so the player action tree can observe the
        // cancellation, then continue regardless of readback.
        invoke<void>(
            SET_PLAYER_CONTROL_HASH,
            playerSlot,
            FALSE,
            PLAYER_CONTROL_CLEAR_TASKS_FLAG,
            FALSE);

        const int controllableAfterDisable =
            invoke<int>(
                IS_PLAYER_CONTROLLABLE_HASH,
                playerSlot);

        bool playerRepositioned = false;
        if (!RepositionPlayerForThrowHandoff(
                player,
                target,
                playerRepositioned,
                true))
        {
            invoke<void>(
                SET_PLAYER_CONTROL_HASH,
                playerSlot,
                TRUE,
                0,
                FALSE);
            return finish(
                KnifeExecutionResult::Failed,
                "throw-cancel handoff reposition failed");
        }

        invoke<void>(
            SET_PLAYER_CONTROL_HASH,
            playerSlot,
            TRUE,
            0,
            FALSE);

        TraceControllerInput("throw-cancel-control-restore-requested", player, target, 0);
        scriptWait(0);

        {
            char taskClearLine[704] = {};
            std::snprintf(
                taskClearLine,
                sizeof(taskClearLine),
                "RDK #94 throw-cancel handoff issued: player=%lld clear-tasks-flag=%d controllable-after-disable=%d controllable-now=%d player-throwing-now=%d ammo-now=%.1f attack-time-now=%.3f target-reacting=%d",
                static_cast<long long>(player),
                PLAYER_CONTROL_CLEAR_TASKS_FLAG,
                controllableAfterDisable,
                invoke<int>(
                    IS_PLAYER_CONTROLLABLE_HASH,
                    playerSlot),
                invoke<BOOL>(
                    IS_ACTOR_THROWING_HASH,
                    player) != FALSE ? 1 : 0,
                static_cast<double>(
                    ReadKnifeAmmo(player)),
                static_cast<double>(
                    invoke<float>(
                        GET_LAST_ATTACK_TIME_HASH,
                        player)),
                invoke<BOOL>(
                    IS_ACTOR_REACTING_HASH,
                    target) != FALSE ? 1 : 0);
            WriteTrace(taskClearLine);
        }

        TraceDeterministicExecutionDecision(
            "throw-cancel-after-one-frame",
            player,
            target,
            0,
            0);

        Trace(
            "RDK #94 throw-cancel handoff continuing immediately; ammo/projectile/throwing readback is diagnostic only");

        if (!MaintainAimedThrowTargetStaging(
                g_throwExecutionFlow.request))
        {
            return finish(
                KnifeExecutionResult::Aborted,
                "exact target staging failed after player task clear");
        }

        // Force every automatic target through one identical relationship
        // sequence before execution. Save its original state, clear its active
        // task/memory, temporarily set true Neutral faction, then apply the
        // temporary enemy override used by the contextual execution.
        originalTargetHostile =
            invoke<BOOL>(
                AI_IS_HOSTILE_OR_ENEMY_HASH,
                target,
                player) != FALSE;
        originalTargetFaction =
            invoke<int>(
                GET_ACTOR_FACTION_HASH,
                target);

        TraceControllerInput("target-before-memory-clear", player, target, 0);
        invoke<void>(
            MEMORY_CLEAR_ALL_HASH,
            target);
        TraceControllerInput("target-after-memory-clear", player, target, 0);
        invoke<void>(
            SET_ACTOR_FACTION_HASH,
            target,
            FACTION_NEUTRAL);
        targetPreparedThroughNeutral = true;
        TraceControllerInput("target-after-neutral-faction", player, target, 0);

        if (!ForceAutomaticTargetIdleForExecution(
                target))
        {
            return finish(
                KnifeExecutionResult::Aborted,
                "target became invalid while forcing fresh idle state");
        }

        if (!MaintainAimedThrowTargetStaging(
                g_throwExecutionFlow.request))
        {
            return finish(
                KnifeExecutionResult::Aborted,
                "exact target staging failed after universal neutral/idle preparation");
        }

        {
            char neutralLine[768] = {};
            std::snprintf(
                neutralLine,
                sizeof(neutralLine),
                "RDK #94 target forced through neutral+idle: target=%lld original-faction=%d original-hostile=%d neutral-faction=%d hostile-after-neutral=%d target-reacting=%d target-gait=%d target-action-time=%.3f",
                static_cast<long long>(target),
                originalTargetFaction,
                originalTargetHostile ? 1 : 0,
                FACTION_NEUTRAL,
                invoke<BOOL>(
                    AI_IS_HOSTILE_OR_ENEMY_HASH,
                    target,
                    player) != FALSE ? 1 : 0,
                invoke<BOOL>(
                    IS_ACTOR_REACTING_HASH,
                    target) != FALSE ? 1 : 0,
                invoke<int>(
                    GET_ACTOR_GAIT_TYPE_HASH,
                    target),
                static_cast<double>(
                    invoke<float>(
                        GET_CURR_ACTION_NODE_PLAY_TIME_HASH,
                        target)));
            WriteTrace(neutralLine);
        }

        invoke<void>(
            MEMORY_CONSIDER_AS_ENEMY_HASH,
            target,
            player);
        temporaryEnemyRelationship = true;
        TraceControllerInput("target-after-enemy-memory", player, target, 0);

        {
            char relationshipLine[704] = {};
            std::snprintf(
                relationshipLine,
                sizeof(relationshipLine),
                "RDK #94 PRE-IMPACT relationship prepared: target=%lld original-hostile=%d original-faction=%d forced-neutral=%d current-faction=%d hostile-now=%d temporary-enemy=%d target-gait=%d target-action-time=%.3f",
                static_cast<long long>(target),
                originalTargetHostile ? 1 : 0,
                originalTargetFaction,
                targetPreparedThroughNeutral ? 1 : 0,
                invoke<int>(
                    GET_ACTOR_FACTION_HASH,
                    target),
                invoke<BOOL>(
                    AI_IS_HOSTILE_OR_ENEMY_HASH,
                    target,
                    player) != FALSE ? 1 : 0,
                temporaryEnemyRelationship ? 1 : 0,
                invoke<int>(
                    GET_ACTOR_GAIT_TYPE_HASH,
                    target),
                static_cast<double>(
                    invoke<float>(
                        GET_CURR_ACTION_NODE_PLAY_TIME_HASH,
                        target)));
            WriteTrace(relationshipLine);
        }

        if (!MaintainAimedThrowTargetStaging(
                g_throwExecutionFlow.request))
        {
            return finish(
                KnifeExecutionResult::Aborted,
                "exact target staging failed after hostility preparation");
        }

        // Keep the target in the fresh idle state right through the last
        // alignment frame. This is the state that correlated with the
        // successful contextual execution in the runtime trace.
        invoke<void>(
            RESET_REACT_NODE_FOR_ACTOR_HASH,
            target);
        invoke<void>(
            TASK_STAND_STILL_HASH,
            target,
            AUTOMATIC_TARGET_STAND_STILL_SECONDS,
            0,
            0);
        scriptWait(0);

        {
            char finalIdleLine[512] = {};
            std::snprintf(
                finalIdleLine,
                sizeof(finalIdleLine),
                "RDK #94 final idle state before execution alignment: target=%lld gait=%d action-time=%.3f reacting=%d",
                static_cast<long long>(target),
                invoke<int>(
                    GET_ACTOR_GAIT_TYPE_HASH,
                    target),
                static_cast<double>(
                    invoke<float>(
                        GET_CURR_ACTION_NODE_PLAY_TIME_HASH,
                        target)),
                invoke<BOOL>(
                    IS_ACTOR_REACTING_HASH,
                    target) != FALSE ? 1 : 0);
            WriteTrace(finalIdleLine);
        }

        // Use the same grounded arrival/control verification as manual F8.
        // Immediate teleport readback does not establish that John's action
        // context has processed the final execution slot. Keep this bounded;
        // alignment failure must return before the single contextual FIRE.
        Trace("RDK #94 automatic final alignment: verifying F8 grounded arrival before FIRE");
        if (!AlignPlayerInFrontOfTarget(
                player,
                target,
                true,
                playerRepositioned))
        {
            return finish(
                KnifeExecutionResult::Failed,
                "automatic F8-style final alignment verification failed; no FIRE");
        }

        TraceExecutionGeometry(
            "throw-cancel automatic repositioned pre-trigger",
            player,
            target,
            1);
        TraceDeterministicExecutionDecision(
            "throw-cancel-before-allow-execute",
            player,
            target,
            0,
            0);

        if (IsGameplayInterrupted()
            || CurrentPlayer() != player
            || !IsLivingActor(player)
            || target == 0
            || !ENTITY::IS_ACTOR_VALID(target)
            || !HEALTH::IS_ACTOR_ALIVE(target))
        {
            return finish(
                KnifeExecutionResult::Aborted,
                "context changed after pre-impact reposition");
        }

        // Enable execution eligibility immediately after the throw-cancel
        // reposition. Do not gate this on projectile or reaction state.
        invoke<void>(
            SET_ALLOW_EXECUTE_HASH,
            target,
            TRUE);
        allowExecuteEnabled = true;

        originalOneShotDeath =
            invoke<int>(
                GET_ACTOR_ONE_SHOT_DEATH_STATUS_HASH,
                target);

        char oneShotDeferredLine[512] = {};
        std::snprintf(
            oneShotDeferredLine,
            sizeof(oneShotDeferredLine),
            "RDK #94 one-shot death deferred until linked execution after throw cancel: target=%lld original-one-shot=%d proof=%d",
            static_cast<long long>(target),
            originalOneShotDeath,
            invoke<int>(
                GET_ACTOR_PROOF_HASH,
                target));
        WriteTrace(oneShotDeferredLine);

        if (!MaintainAimedThrowTargetStaging(
                g_throwExecutionFlow.request))
        {
            return finish(
                KnifeExecutionResult::Aborted,
                "exact target staging failed immediately before execution input");
        }

        TraceDeterministicExecutionDecision(
            "throw-cancel-before-input",
            player,
            target,
            0,
            0);

        if (!WaitForAutomaticFireRelease(player, target))
        {
            return finish(
                KnifeExecutionResult::Failed,
                "RT/game FIRE did not return to released state before contextual attack");
        }

        const char* fireBlockReason = AutomaticFireBlockReason(player, target);
        if (fireBlockReason != nullptr)
        {
            TraceControllerInput("automatic-fire-gate-rejected", player, target, 1);
            return finish(KnifeExecutionResult::Failed, fireBlockReason);
        }
        TraceControllerInput("automatic-fire-gate-passed", player, target, 1);
        Trace("RDK #94 automatic FIRE edge policy: RT/game FIRE release confirmed; sending one contextual FIRE pulse from the released state");

        // Stop refreshing the temporary stand-still immediately before the
        // contextual attack. From here RDR owns the execution decision.
        ReleaseAimedThrowTargetStaging(
            g_throwExecutionFlow.request,
            "throw-cancel contextual FIRE begins");

        // Use the same one-frame LMB pulse as the proven manual F8 harness.
        // Final alignment has already verified three grounded frames and
        // restored player control. No second attack is injected on failure.
        if (!InjectPrimaryAttack(
                player,
                target,
                1,
                false))
        {
            return finish(
                KnifeExecutionResult::Failed,
                "throw-cancel contextual primary attack injection failed");
        }

        const ULONGLONG decisionStarted = GetTickCount64();
        int decisionProbe = 0;
        bool linkedStarted = false;
        ExecutionLinkState linkState{};

        while (GetTickCount64() - decisionStarted
            < EXECUTION_FIRST_ATTEMPT_TIMEOUT_MS)
        {
            scriptWait(0);

            if (IsGameplayInterrupted()
                || CurrentPlayer() != player
                || !IsLivingActor(player)
                || target == 0
                || !ENTITY::IS_ACTOR_VALID(target)
                || !HEALTH::IS_ACTOR_ALIVE(target))
            {
                return finish(
                    KnifeExecutionResult::Aborted,
                    "context changed during throw-cancel contextual execution decision");
            }

            ++decisionProbe;
            linkState =
                ReadExecutionLinkState(
                    player,
                    target);

            TraceDeterministicExecutionDecision(
                "throw-cancel-execution-decision",
                player,
                target,
                decisionStarted,
                decisionProbe);

            if (HasSynchronizedExecutionEvidence(
                    linkState,
                    player,
                    target))
            {
                linkedStarted = true;
                break;
            }
        }

        if (!linkedStarted)
        {
            TraceExecutionLinkState(
                "throw-cancel execution-start timeout",
                player,
                target,
                ReadExecutionLinkState(player, target));
            return finish(
                KnifeExecutionResult::Failed,
                "throw-cancel contextual FIRE produced no linked execution");
        }

        TraceExecutionLinkState(
            "deterministic linked execution started",
            player,
            target,
            linkState);

        // The exact reciprocal linked animation now owns the target. Only now
        // arm one-shot-death, then release temporary throw protection so the
        // execution itself can deliver the lethal hit.
        invoke<void>(
            SET_ACTOR_ONE_SHOT_DEATH_HASH,
            target,
            TRUE);
        oneShotChanged = true;

        char oneShotArmedLine[512] = {};
        std::snprintf(
            oneShotArmedLine,
            sizeof(oneShotArmedLine),
            "RDK #94 one-shot death armed after linked execution: target=%lld original-one-shot=%d current-one-shot=%d proof=%d",
            static_cast<long long>(target),
            originalOneShotDeath,
            invoke<int>(
                GET_ACTOR_ONE_SHOT_DEATH_STATUS_HASH,
                target),
            invoke<int>(
                GET_ACTOR_PROOF_HASH,
                target));
        WriteTrace(oneShotArmedLine);

        RestoreAimedThrowTargetState(
            g_throwExecutionFlow.request,
            "exact linked execution started after one-shot death armed");

        const ULONGLONG playbackStarted = GetTickCount64();
        bool sawLinkedState = true;
        while (GetTickCount64() - playbackStarted
            < EXECUTION_TOTAL_TIMEOUT_MS)
        {
            scriptWait(0);

            if (IsGameplayInterrupted()
                || CurrentPlayer() != player
                || !IsLivingActor(player)
                || target == 0
                || !ENTITY::IS_ACTOR_VALID(target))
            {
                return finish(
                    KnifeExecutionResult::Aborted,
                    "context changed during deterministic linked execution");
            }

            if (!HEALTH::IS_ACTOR_ALIVE(target))
            {
                Trace(
                    "RDK #94 deterministic linked execution lethal outcome observed");
                return finish(
                    KnifeExecutionResult::Success,
                    "linked execution killed exact captured target");
            }

            linkState =
                ReadExecutionLinkState(
                    player,
                    target);
            const bool synchronized =
                HasSynchronizedExecutionEvidence(
                    linkState,
                    player,
                    target)
                || linkState.playerPerforming
                || linkState.targetPerforming
                || linkState.playerPhaseLocked
                || linkState.targetPhaseLocked;

            if (synchronized)
            {
                sawLinkedState = true;
                continue;
            }

            if (sawLinkedState)
            {
                Trace(
                    "RDK #94 deterministic linked execution ended with target alive; forcing lethal outcome");
                const bool killed =
                    ForceAutomaticTargetDeath(
                        target,
                        "linked execution ended with exact target alive");
                return finish(
                    killed
                        ? KnifeExecutionResult::Success
                        : KnifeExecutionResult::Failed,
                    killed
                        ? "linked execution ended alive; exact captured target force-killed"
                        : "linked execution ended alive and forced lethal fallback failed");
            }
        }

        const bool killed =
            ForceAutomaticTargetDeath(
                target,
                "linked execution timeout with exact target alive");
        return finish(
            killed
                ? KnifeExecutionResult::Success
                : KnifeExecutionResult::Failed,
            killed
                ? "linked execution timeout; exact captured target force-killed"
                : "linked execution timeout and forced lethal fallback failed");
    }

    void HandleThrowExecutionFlow()
    {
        if (g_throwExecutionFlow.state
                != ThrowExecutionFlowState::Idle
            || g_executionRunning)
        {
            return;
        }

        AimedThrowRequest request{};
        if (!TakeAimedThrowRequest(request))
            return;

        g_throwExecutionFlow.request = request;
        TransitionThrowExecutionFlow(
            ThrowExecutionFlowState::ThrowCaptured);

        const Actor player = CurrentPlayer();
        const int currentWeapon =
            IsLivingActor(player)
                ? INVENTORY::GET_WEAPON_IN_HAND(player)
                : -1;

        char validation[640] = {};
        std::snprintf(
            validation,
            sizeof(validation),
            "RDK #94 aimed throw validation: throw=%llu shooter=%lld current-player=%lld target=%lld current-weapon=%d target-eligible=%d execution-running=%d interrupted=%d",
            request.throwId,
            static_cast<long long>(request.shooter),
            static_cast<long long>(player),
            static_cast<long long>(request.target),
            currentWeapon,
            IsLivingActor(player)
                && IsEligibleNpc(
                    request.target,
                    player) ? 1 : 0,
            g_executionRunning ? 1 : 0,
            IsGameplayInterrupted() ? 1 : 0);
        WriteTrace(validation);

        if (IsGameplayInterrupted())
        {
            AbortThrowExecutionRequest(
                "pause/cutscene during aimed throw validation");
            return;
        }
        if (!IsLivingActor(player)
            || request.shooter != player
            || !IsLivingActor(request.shooter))
        {
            AbortThrowExecutionRequest(
                "shooter/player context changed before execution");
            return;
        }
        if (currentWeapon != THROWING_KNIFE_WEAPON)
        {
            AbortThrowExecutionRequest(
                "throwing knife no longer equipped");
            return;
        }
        // Exact-target ownership is already fixed at throw start. Before the
        // F8-mirror handoff, only require that the captured actor still exists
        // and is alive; the natural F8 alignment helper performs its own
        // contextual standing/incapacitation validation immediately before FIRE.
        if (request.target == 0
            || !ENTITY::IS_ACTOR_VALID(request.target)
            || !HEALTH::IS_ACTOR_ALIVE(request.target))
        {
            AbortThrowExecutionRequest(
                "exact captured target no longer exists/alive before forced execution");
            return;
        }

        {
            char cancelIntentLine[512] = {};
            std::snprintf(
                cancelIntentLine,
                sizeof(cancelIntentLine),
                "RDK #94 throw-start cancel handoff: throw=%llu player=%lld target=%lld player-throwing=%d ammo=%.1f attack-time=%.3f target-reacting=%d; no release/projectile verification will gate execution",
                request.throwId,
                static_cast<long long>(player),
                static_cast<long long>(request.target),
                invoke<BOOL>(
                    IS_ACTOR_THROWING_HASH,
                    player) != FALSE ? 1 : 0,
                static_cast<double>(
                    ReadKnifeAmmo(player)),
                static_cast<double>(
                    invoke<float>(
                        GET_LAST_ATTACK_TIME_HASH,
                        player)),
                invoke<BOOL>(
                    IS_ACTOR_REACTING_HASH,
                    request.target) != FALSE ? 1 : 0);
            WriteTrace(cancelIntentLine);
        }

        if (g_executionRunning)
        {
            AbortThrowExecutionRequest(
                "execution already active");
            return;
        }

        // Target ownership is fixed at throw start. Do not wait for release and
        // do not inspect projectile state. Disarm detection, then immediately
        // clear John's throw task, reposition, and request the execution.
        CleanupDetector(
            "aimed throw request transferred to deterministic execution");

        const Vector3 preExecutionPosition =
            ReadPosition(player);
        const float preExecutionHeading =
            ReadHeading(player);
        char snapshot[512] = {};
        std::snprintf(
            snapshot,
            sizeof(snapshot),
            "RDK #94 deterministic pre-execution snapshot: throw=%llu player=(%.3f,%.3f,%.3f) heading=%.2f target=%lld proof=%d",
            request.throwId,
            static_cast<double>(preExecutionPosition.x),
            static_cast<double>(preExecutionPosition.y),
            static_cast<double>(preExecutionPosition.z),
            static_cast<double>(preExecutionHeading),
            static_cast<long long>(request.target),
            invoke<int>(
                GET_ACTOR_PROOF_HASH,
                request.target));
        WriteTrace(snapshot);

        TransitionThrowExecutionFlow(
            ThrowExecutionFlowState::Executing);
        const KnifeExecutionResult result =
            RunDeterministicAutomaticKnifeExecution(
                request.target);

        char resultLine[384] = {};
        std::snprintf(
            resultLine,
            sizeof(resultLine),
            "RDK #94 deterministic execution returned: throw=%llu target=%lld result=%s",
            request.throwId,
            static_cast<long long>(request.target),
            KnifeExecutionResultName(result));
        WriteTrace(resultLine);

        TransitionThrowExecutionFlow(
            ThrowExecutionFlowState::Recovering);
        FinishThrowExecutionFlow();
    }

}


KnifeExecutionResult RunFrontThrowingKnifeExecution(
    Actor target,
    bool automaticThrowFlow)
{
    if (g_executionRunning)
    {
        Trace("RDK #93 execution rejected: another execution is active");
        return KnifeExecutionResult::Failed;
    }

    g_executionRunning = true;
    const Actor player = CurrentPlayer();
    bool executionTriggered = false;
    bool allowExecuteEnabled = false;
    bool temporaryEnemyRelationship = false;
    bool playerRepositioned = false;
    bool playerSnapshotReady = false;
    bool exactTargetOneShotDeathArmed = false;
    int exactTargetOriginalOneShotDeath = 0;
    Actor suppressedForeignTargets[2] = {};
    int suppressedForeignTargetCount = 0;
    Vector3 originalPlayerPosition{};
    float originalPlayerHeading = 0.0f;

    const auto finish =
        [&](KnifeExecutionResult result, const char* reason)
        {
            // A failed SendInput up must not leave our synthetic press held.
            if (g_primaryAttackDown)
            {
                INPUT up = {};
                up.type = INPUT_MOUSE;
                up.mi.dwFlags = MOUSEEVENTF_LEFTUP;
                SetLastError(ERROR_SUCCESS);
                const UINT sent = SendInput(1, &up, sizeof(INPUT));
                const DWORD error = GetLastError();
                if (sent == 1)
                    g_primaryAttackDown = false;
                char line[320] = {};
                std::snprintf(line, sizeof(line),
                    "RDK #94 injected mouse cleanup: edge=up sent=%u error=%lu outstanding-down=%d",
                    sent, static_cast<unsigned long>(error), g_primaryAttackDown ? 1 : 0);
                WriteTrace(line);
            }
            TraceCombatState("execution finish before cleanup", player, target, 0);
            if (temporaryEnemyRelationship
                && player != 0
                && target != 0
                && ENTITY::IS_ACTOR_VALID(player)
                && ENTITY::IS_ACTOR_VALID(target))
            {
                invoke<void>(
                    MEMORY_CONSIDER_ACCORDING_TO_FACTION_HASH,
                    target,
                    player);
                const int restoredHostile =
                    invoke<BOOL>(
                        AI_IS_HOSTILE_OR_ENEMY_HASH,
                        target,
                        player) != FALSE
                        ? 1
                        : 0;

                char relationshipLine[384] = {};
                std::snprintf(
                    relationshipLine,
                    sizeof(relationshipLine),
                    "RDK #93 temporary enemy relationship restored: target=%lld hostile-after-restore=%d",
                    static_cast<long long>(target),
                    restoredHostile);
                WriteTrace(relationshipLine);
                temporaryEnemyRelationship = false;
            }

            if (allowExecuteEnabled
                && target != 0
                && ENTITY::IS_ACTOR_VALID(target)
                && HEALTH::IS_ACTOR_ALIVE(target))
            {
                invoke<void>(
                    SET_ALLOW_EXECUTE_HASH,
                    target,
                    FALSE);
                Trace("RDK #93 target execution eligibility cleared after surviving exit (no getter; original value unknown)");
                allowExecuteEnabled = false;
            }

            // Do not reset pre-existing action state when validation/alignment
            // fails before #93 has actually attempted to start an execution.
            if (result != KnifeExecutionResult::Success
                && executionTriggered)
            {
                CleanupFailedExecutionState(player, target);
            }

            if (exactTargetOneShotDeathArmed
                && target != 0
                && ENTITY::IS_ACTOR_VALID(target)
                && HEALTH::IS_ACTOR_ALIVE(target))
            {
                invoke<void>(
                    SET_ACTOR_ONE_SHOT_DEATH_HASH,
                    target,
                    exactTargetOriginalOneShotDeath);

                char oneShotRestoreLine[384] = {};
                std::snprintf(
                    oneShotRestoreLine,
                    sizeof(oneShotRestoreLine),
                    "RDK #94 exact-target lethal guard restored: target=%lld original-one-shot-death=%d",
                    static_cast<long long>(target),
                    exactTargetOriginalOneShotDeath);
                WriteTrace(oneShotRestoreLine);
            }
            exactTargetOneShotDeathArmed = false;

            for (int i = 0; i < suppressedForeignTargetCount; ++i)
            {
                const Actor foreignTarget = suppressedForeignTargets[i];
                if (foreignTarget == 0
                    || !ENTITY::IS_ACTOR_VALID(foreignTarget)
                    || !HEALTH::IS_ACTOR_ALIVE(foreignTarget))
                {
                    continue;
                }

                // We only suppress actors that the base game actually selected
                // as an execution partner during this attempt, so restoring
                // execute eligibility to TRUE returns that observed ability.
                invoke<void>(
                    SET_ALLOW_EXECUTE_HASH,
                    foreignTarget,
                    TRUE);

                char foreignRestoreLine[384] = {};
                std::snprintf(
                    foreignRestoreLine,
                    sizeof(foreignRestoreLine),
                    "RDK #94 foreign execution target restored: target=%lld",
                    static_cast<long long>(foreignTarget));
                WriteTrace(foreignRestoreLine);
            }

            const int playerSlot = static_cast<int>(ACTOR::GET_LOCAL_SLOT());
            const bool playerStillCurrent =
                player != 0
                && CurrentPlayer() == player
                && IsLivingActor(player);

            if (automaticThrowFlow
                && result != KnifeExecutionResult::Success
                && playerRepositioned)
            {
                Trace(
                    "RDK #94 no-rollback policy: keeping John at the execution/recovery location");
            }

            int controllable =
                playerStillCurrent && !IsGameplayInterrupted()
                    ? invoke<int>(
                        IS_PLAYER_CONTROLLABLE_HASH,
                        playerSlot)
                    : -1;

            if (result != KnifeExecutionResult::Success
                && executionTriggered
                && playerStillCurrent
                && !IsGameplayInterrupted()
                && controllable == 0)
            {
                Trace("RDK #93 recovery requesting player control after failed execution");
                invoke<void>(
                    SET_PLAYER_CONTROL_HASH,
                    playerSlot,
                    TRUE,
                    0,
                    0);

                const ULONGLONG controlRecoveryStarted = GetTickCount64();
                while (GetTickCount64() - controlRecoveryStarted
                    < CONTROL_RETURN_TIMEOUT_MS)
                {
                    scriptWait(0);
                    if (IsGameplayInterrupted()
                        || CurrentPlayer() != player
                        || !IsLivingActor(player))
                    {
                        break;
                    }

                    controllable =
                        invoke<int>(
                            IS_PLAYER_CONTROLLABLE_HASH,
                            playerSlot);
                    if (controllable != 0)
                    {
                        Trace("RDK #93 player control recovered after failed execution");
                        break;
                    }
                }

                if (controllable == 0)
                {
                    Trace("RDK #93 player control recovery timed out after failed execution");
                }
            }

            char line[640] = {};
            std::snprintf(
                line,
                sizeof(line),
                "RDK #93 execution %s: player=%lld target=%lld reason=%s final-player-current=%d final-player-controllable=%d target-valid=%d target-alive=%d",
                KnifeExecutionResultName(result),
                static_cast<long long>(player),
                static_cast<long long>(target),
                reason,
                playerStillCurrent ? 1 : 0,
                controllable,
                target != 0 && ENTITY::IS_ACTOR_VALID(target) ? 1 : 0,
                target != 0
                    && ENTITY::IS_ACTOR_VALID(target)
                    && HEALTH::IS_ACTOR_ALIVE(target) ? 1 : 0);
            WriteTrace(line);

            g_executionRunning = false;
            return result;
        };

    if (!IsLivingActor(player) || target == 0)
        return finish(
            KnifeExecutionResult::Failed,
            "invalid player or target");
    if (g_primaryAttackDown)
        return finish(KnifeExecutionResult::Aborted, "previous synthetic mouse-up still outstanding");
    if (IsGameplayInterrupted())
        return finish(
            KnifeExecutionResult::Aborted,
            "pause/cutscene active before setup");
    if (INVENTORY::GET_WEAPON_IN_HAND(player)
        != THROWING_KNIFE_WEAPON)
    {
        return finish(
            KnifeExecutionResult::Failed,
            "throwing knife is not equipped");
    }

    const char* targetRejection =
        ExecutionTargetRejectionReason(target, player);
    if (targetRejection != nullptr)
        return finish(
            KnifeExecutionResult::Failed,
            targetRejection);

    const int playerSlot = static_cast<int>(ACTOR::GET_LOCAL_SLOT());
    if (invoke<int>(
            IS_PLAYER_CONTROLLABLE_HASH,
            playerSlot) == 0)
    {
        return finish(
            KnifeExecutionResult::Failed,
            "player control already disabled before setup");
    }

    originalPlayerPosition = ReadPosition(player);
    originalPlayerHeading = ReadHeading(player);
    playerSnapshotReady =
        IsFinitePosition(originalPlayerPosition)
        && std::isfinite(originalPlayerHeading);
    if (!playerSnapshotReady)
    {
        return finish(
            KnifeExecutionResult::Failed,
            "invalid pre-execution player transform");
    }

    const bool hostileBefore =
        invoke<BOOL>(
            AI_IS_HOSTILE_OR_ENEMY_HASH,
            target,
            player) != FALSE;

    if (!hostileBefore)
    {
        // Prepare the contextual enemy relationship before positioning John.
        // Applying it after alignment can make a neutral NPC react/turn and
        // invalidate the execution slot we just calculated.
        invoke<void>(
            MEMORY_CONSIDER_AS_ENEMY_HASH,
            target,
            player);
        temporaryEnemyRelationship = true;

        // Allow one game frame for the relationship state to become visible
        // to AI/contextual action selection, then align against the resulting
        // transform rather than the pre-hostility transform.
        scriptWait(0);
    }

    {
        const int hostileAfterPrepare =
            invoke<BOOL>(
                AI_IS_HOSTILE_OR_ENEMY_HASH,
                target,
                player) != FALSE
                ? 1
                : 0;

        char relationshipLine[448] = {};
        std::snprintf(
            relationshipLine,
            sizeof(relationshipLine),
            "RDK #93 execution relationship prepared: target=%lld hostile-before=%d temporary-enemy=%d hostile-after-prepare=%d",
            static_cast<long long>(target),
            hostileBefore ? 1 : 0,
            temporaryEnemyRelationship ? 1 : 0,
            hostileAfterPrepare);
        WriteTrace(relationshipLine);
    }

    const ExecutionLinkState before =
        ReadExecutionLinkState(player, target);
    TraceExecutionLinkState(
        "setup snapshot action state",
        player,
        target,
        before);
    if (before.playerLinkedTarget != 0
        || before.playerPerforming
        || before.playerPhaseLocked)
    {
        return finish(
            KnifeExecutionResult::Failed,
            "player already has linked/action state");
    }

    const Vector3 playerPosition = originalPlayerPosition;
    const Vector3 targetPosition = ReadPosition(target);
    const float playerHeading = originalPlayerHeading;
    const float targetHeading = ReadHeading(target);

    char snapshot[640] = {};
    std::snprintf(
        snapshot,
        sizeof(snapshot),
        "RDK #93 setup snapshot: player=(%.3f,%.3f,%.3f) heading=%.2f target=(%.3f,%.3f,%.3f) heading=%.2f weapon=%d",
        static_cast<double>(playerPosition.x),
        static_cast<double>(playerPosition.y),
        static_cast<double>(playerPosition.z),
        static_cast<double>(playerHeading),
        static_cast<double>(targetPosition.x),
        static_cast<double>(targetPosition.y),
        static_cast<double>(targetPosition.z),
        static_cast<double>(targetHeading),
        INVENTORY::GET_WEAPON_IN_HAND(player));
    WriteTrace(snapshot);

    Trace(
        "RDK #93 execution recipe: base-game close-range primary attack from deterministic front alignment");

    bool standStillIssued = false;
    if (automaticThrowFlow)
    {
        // The throw-release handoff reaches here while John's original throw
        // animation is normally still active. Reposition immediately without
        // touching player-control state, then suppress only the captured
        // target's hit reaction while that original throw finishes.
        invoke<void>(
            TASK_STAND_STILL_HASH,
            target,
            AUTOMATIC_TARGET_STAND_STILL_SECONDS,
            0,
            0);
        standStillIssued = true;

        if (!RepositionPlayerForThrowHandoff(
                player,
                target,
                playerRepositioned,
                false))
        {
            return finish(
                KnifeExecutionResult::Failed,
                "early post-release handoff reposition failed");
        }

        if (!WaitForAutomaticExecutionTriggerReady(
                player,
                target))
        {
            return finish(
                KnifeExecutionResult::Aborted,
                "John did not become execution-ready after projectile release");
        }

        TraceCombatState(
            "automatic trigger-ready before final alignment correction",
            player,
            target,
            1);
    }

    // Manual F8 comes here directly. Automatic flow comes here only after the
    // early reposition and after John's original throw is finished. From this
    // point both paths use the proven normal stabilization/alignment recipe.
    if (!StabilizeMovingTargetBriefly(
            player,
            target,
            1,
            automaticThrowFlow,
            standStillIssued))
    {
        return finish(
            KnifeExecutionResult::Aborted,
            automaticThrowFlow
                ? "post-throw target stabilization failed before final execution alignment"
                : "moving-target stabilization failed before alignment");
    }

    if (!AlignPlayerInFrontOfTarget(
            player,
            target,
            automaticThrowFlow,
            playerRepositioned))
    {
        return finish(
            KnifeExecutionResult::Failed,
            automaticThrowFlow
                ? "post-throw final alignment correction failed"
                : "alignment failed");
    }

    TraceExecutionGeometry(
        automaticThrowFlow
            ? "post-throw final alignment pre-trigger"
            : "post-alignment pre-trigger",
        player,
        target,
        1);

    if (IsGameplayInterrupted())
        return finish(
            KnifeExecutionResult::Aborted,
            "pause/cutscene before execution trigger");
    if (CurrentPlayer() != player || !IsLivingActor(player))
        return finish(
            KnifeExecutionResult::Aborted,
            "player context changed before execution trigger");
    targetRejection =
        ExecutionTargetRejectionReason(target, player);
    if (targetRejection != nullptr)
        return finish(
            KnifeExecutionResult::Aborted,
            targetRejection);
    if (INVENTORY::GET_WEAPON_IN_HAND(player)
        != THROWING_KNIFE_WEAPON)
    {
        return finish(
            KnifeExecutionResult::Aborted,
            "throwing knife changed during setup");
    }

    {
        const int hostileAtTrigger =
            invoke<BOOL>(
                AI_IS_HOSTILE_OR_ENEMY_HASH,
                target,
                player) != FALSE
                ? 1
                : 0;

        char relationshipLine[384] = {};
        std::snprintf(
            relationshipLine,
            sizeof(relationshipLine),
            "RDK #93 execution relationship at trigger: target=%lld hostile=%d temporary-enemy=%d",
            static_cast<long long>(target),
            hostileAtTrigger,
            temporaryEnemyRelationship ? 1 : 0);
        WriteTrace(relationshipLine);
    }

    Trace(
        "RDK #93 enabling target execution eligibility and triggering base-game primary attack");
    invoke<void>(
        SET_ALLOW_EXECUTE_HASH,
        target,
        TRUE);
    allowExecuteEnabled = true;

    bool started = false;
    ExecutionLinkState linkState{};
    ULONGLONG triggeredAt = 0;

    for (int attackAttempt = 0; attackAttempt < 2 && !started; ++attackAttempt)
    {
        if (attackAttempt > 0)
        {
            Trace(
                "RDK #93 execution miss detected after short first-attempt window; re-reading target transform and retrying once");

            if (!StabilizeMovingTargetBriefly(
                    player,
                    target,
                    attackAttempt + 1,
                    automaticThrowFlow,
                    standStillIssued))
            {
                return finish(
                    KnifeExecutionResult::Aborted,
                    "moving-target stabilization failed before execution retry");
            }

            if (!AlignPlayerInFrontOfTarget(
                    player,
                    target,
                    automaticThrowFlow,
                    playerRepositioned))
            {
                return finish(
                    KnifeExecutionResult::Failed,
                    "retry realignment failed after execution miss");
            }

            if (IsGameplayInterrupted())
                return finish(
                    KnifeExecutionResult::Aborted,
                    "pause/cutscene before execution retry");
            if (CurrentPlayer() != player || !IsLivingActor(player))
                return finish(
                    KnifeExecutionResult::Aborted,
                    "player changed/died before execution retry");

            const char* retryTargetReason =
                ExecutionTargetRejectionReason(target, player);
            if (retryTargetReason != nullptr)
                return finish(
                    KnifeExecutionResult::Aborted,
                    retryTargetReason);

            TraceExecutionGeometry(
                "post-retry-alignment pre-trigger",
                player,
                target,
                attackAttempt + 1);
        }

        const int triggerWeapon =
            INVENTORY::GET_WEAPON_IN_HAND(player);
        {
            char triggerLine[448] = {};
            std::snprintf(
                triggerLine,
                sizeof(triggerLine),
                "RDK #93 trigger snapshot: attack-attempt=%d player=%lld target=%lld weapon=%d",
                attackAttempt + 1,
                static_cast<long long>(player),
                static_cast<long long>(target),
                triggerWeapon);
            WriteTrace(triggerLine);
        }

        if (triggerWeapon != THROWING_KNIFE_WEAPON)
        {
            return finish(
                KnifeExecutionResult::Failed,
                "throwing knife no longer equipped at execution trigger");
        }

        // Do not inject an overlapping press or retry into a linked action
        // that began after the preceding sample. Let the normal observation
        // loop adopt an exact link without issuing another input edge.
        linkState = ReadExecutionLinkState(player, target);
        const bool alreadyLinked = HasLinkedAction(player) || HasLinkedAction(target);
        if (automaticThrowFlow && !alreadyLinked
            && ((GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0
                || IsGameActionDown("@GENERIC.FIRE")
                || invoke<BOOL>(IS_ACTOR_THROWING_HASH, player)
                || invoke<BOOL>(IS_ACTOR_REACTING_HASH, player)
                || !invoke<BOOL>(IS_ACTOR_READY_FOR_ACTION_HASH, player)))
        {
            TraceCombatState(
                "trigger context unexpectedly not ready after trigger-ready wait; no input",
                player,
                target,
                attackAttempt + 1);
            return finish(
                KnifeExecutionResult::Aborted,
                "context changed after trigger-ready wait/final alignment; no attack injected");
        }

        if (automaticThrowFlow
            && (g_throwExecutionFlow.request.proofProtected
                || g_throwExecutionFlow.request.weaponReactionsSuppressed))
        {
            if (!RestoreAimedThrowTargetState(
                    g_throwExecutionFlow.request,
                    "aligned; immediately before execution attack"))
            {
                return finish(
                    KnifeExecutionResult::Aborted,
                    "captured target state failed to restore before execution attack");
            }

            if (!IsLivingActor(target))
            {
                return finish(
                    KnifeExecutionResult::Failed,
                    "captured target died immediately after proof restoration");
            }

            TraceCombatState(
                "captured target released for execution",
                player,
                target,
                attackAttempt + 1);
        }

        TraceExecutionGeometry(
            "immediately before primary attack",
            player,
            target,
            attackAttempt + 1);

        executionTriggered = true;
        if (!alreadyLinked && !InjectPrimaryAttack(
                player,
                target,
                attackAttempt + 1,
                automaticThrowFlow))
        {
            return finish(
                KnifeExecutionResult::Failed,
                "primary input delivery/acknowledgement failed; no retry");
        }

        triggeredAt = GetTickCount64();
        const ULONGLONG startTimeout =
            automaticThrowFlow
                ? EXECUTION_RETRY_TIMEOUT_MS
                : (attackAttempt == 0
                    ? EXECUTION_FIRST_ATTEMPT_TIMEOUT_MS
                    : EXECUTION_RETRY_TIMEOUT_MS);

        ULONGLONG nextAutomaticDiagnosticAt =
            triggeredAt + AUTOMATIC_EXECUTION_DIAGNOSTIC_INTERVAL_MS;
        int automaticDiagnosticSample = 0;

        while (GetTickCount64() - triggeredAt
            < startTimeout)
        {
            scriptWait(0);

            if (IsGameplayInterrupted())
                return finish(
                    KnifeExecutionResult::Aborted,
                    "pause/cutscene during execution start");
            if (CurrentPlayer() != player || !IsLivingActor(player))
                return finish(
                    KnifeExecutionResult::Aborted,
                    "player changed/died during execution start");
            if (target == 0 || !ENTITY::IS_ACTOR_VALID(target))
                return finish(
                    KnifeExecutionResult::Failed,
                    "target lost during execution start");
            if (!HEALTH::IS_ACTOR_ALIVE(target))
            {
                TraceExecutionGeometry(
                    "target died before linked execution confirmation",
                    player,
                    target,
                    attackAttempt + 1);
                return finish(
                    KnifeExecutionResult::Failed,
                    "target died before synchronized execution was confirmed");
            }

            linkState = ReadExecutionLinkState(player, target);
            if (automaticThrowFlow)
                TraceCombatState("execution-start frame", player, target, attackAttempt + 1);

            const ULONGLONG now = GetTickCount64();
            if (automaticThrowFlow
                && now >= nextAutomaticDiagnosticAt)
            {
                ++automaticDiagnosticSample;
                const ULONGLONG elapsed = now - triggeredAt;

                char waitStage[160] = {};
                std::snprintf(
                    waitStage,
                    sizeof(waitStage),
                    "automatic execution-start wait sample=%d elapsed-ms=%llu attempt=%d",
                    automaticDiagnosticSample,
                    static_cast<unsigned long long>(elapsed),
                    attackAttempt + 1);
                TraceExecutionLinkState(
                    waitStage,
                    player,
                    target,
                    linkState);
                TraceExecutionGeometry(
                    waitStage,
                    player,
                    target,
                    attackAttempt + 1);

                nextAutomaticDiagnosticAt =
                    now + AUTOMATIC_EXECUTION_DIAGNOSTIC_INTERVAL_MS;
            }

            const Actor foreignLinkedTarget =
                linkState.playerLinkedTarget != 0
                    && linkState.playerLinkedTarget
                        != static_cast<int>(target)
                    ? static_cast<Actor>(linkState.playerLinkedTarget)
                    : 0;
            if (foreignLinkedTarget != 0)
            {
                const bool foreignValid =
                    ENTITY::IS_ACTOR_VALID(foreignLinkedTarget) != FALSE;
                const bool foreignAlive =
                    foreignValid
                    && HEALTH::IS_ACTOR_ALIVE(foreignLinkedTarget);

                char foreignLine[640] = {};
                std::snprintf(
                    foreignLine,
                    sizeof(foreignLine),
                    "RDK #94 foreign execution target detected: attempt=%d intended=%lld actual=%lld actual-valid=%d actual-alive=%d; cancelling foreign link before retry",
                    attackAttempt + 1,
                    static_cast<long long>(target),
                    static_cast<long long>(foreignLinkedTarget),
                    foreignValid ? 1 : 0,
                    foreignAlive ? 1 : 0);
                WriteTrace(foreignLine);

                if (foreignValid)
                {
                    bool alreadySuppressed = false;
                    for (int i = 0; i < suppressedForeignTargetCount; ++i)
                    {
                        if (suppressedForeignTargets[i]
                            == foreignLinkedTarget)
                        {
                            alreadySuppressed = true;
                            break;
                        }
                    }

                    if (!alreadySuppressed
                        && suppressedForeignTargetCount < 2)
                    {
                        invoke<void>(
                            SET_ALLOW_EXECUTE_HASH,
                            foreignLinkedTarget,
                            FALSE);
                        suppressedForeignTargets[
                            suppressedForeignTargetCount++] =
                                foreignLinkedTarget;

                        char suppressLine[384] = {};
                        std::snprintf(
                            suppressLine,
                            sizeof(suppressLine),
                            "RDK #94 foreign execution target suppressed for retry: target=%lld",
                            static_cast<long long>(foreignLinkedTarget));
                        WriteTrace(suppressLine);
                    }
                }

                CleanupActorExecutionState(
                    player,
                    foreignLinkedTarget,
                    "player-foreign");
                if (foreignValid)
                {
                    CleanupActorExecutionState(
                        foreignLinkedTarget,
                        player,
                        "foreign-target");
                }

                scriptWait(0);
                break;
            }

            if (HasSynchronizedExecutionEvidence(
                    linkState,
                    player,
                    target))
            {
                started = true;
                Trace("RDK #94 exact execution latched: retry-disabled=1 no-more-injected-attacks=1");
                if (automaticThrowFlow
                    && target != 0
                    && ENTITY::IS_ACTOR_VALID(target)
                    && HEALTH::IS_ACTOR_ALIVE(target)
                    && !exactTargetOneShotDeathArmed)
                {
                    exactTargetOriginalOneShotDeath =
                        invoke<int>(
                            GET_ACTOR_ONE_SHOT_DEATH_STATUS_HASH,
                            target);
                    invoke<void>(
                        SET_ACTOR_ONE_SHOT_DEATH_HASH,
                        target,
                        TRUE);
                    exactTargetOneShotDeathArmed = true;

                    const int armedStatus =
                        invoke<int>(
                            GET_ACTOR_ONE_SHOT_DEATH_STATUS_HASH,
                            target);
                    char lethalGuardLine[512] = {};
                    std::snprintf(
                        lethalGuardLine,
                        sizeof(lethalGuardLine),
                        "RDK #94 exact-target lethal guard armed: target=%lld original-one-shot-death=%d armed-status=%d",
                        static_cast<long long>(target),
                        exactTargetOriginalOneShotDeath,
                        armedStatus);
                    WriteTrace(lethalGuardLine);
                }

                char stage[128] = {};
                std::snprintf(
                    stage,
                    sizeof(stage),
                    "execution-start confirmed attempt=%d",
                    attackAttempt + 1);
                TraceExecutionLinkState(
                    stage,
                    player,
                    target,
                    linkState);
                TraceExecutionGeometry(
                    "execution-start confirmed geometry",
                    player,
                    target,
                    attackAttempt + 1);
                break;
            }

            // 131832 attempt 2 starts another projectile, then kills the victim
            // with it. That is not an execution and must never earn a retry.
            if (automaticThrowFlow
                && invoke<BOOL>(IS_ACTOR_THROWING_HASH, player) != FALSE)
            {
                TraceCombatState("unexpected throw during execution start; retry-disabled=1",
                    player, target, attackAttempt + 1);
                return finish(
                    KnifeExecutionResult::Failed,
                    "player entered throwing action instead of exact execution; no retry");
            }
        }

        if (!started)
        {
            TraceExecutionGeometry(
                "execution-start miss geometry",
                player,
                target,
                attackAttempt + 1);

            if (attackAttempt == 0)
            {
                TraceExecutionLinkState(
                    "execution-start miss before bounded retry",
                    player,
                    target,
                    ReadExecutionLinkState(player, target));
            }
        }
    }

    if (!started)
    {
        TraceExecutionLinkState(
            "execution-start timeout state after retry",
            player,
            target,
            ReadExecutionLinkState(player, target));
        return finish(
            KnifeExecutionResult::Failed,
            "base-game execution missed both bounded attack attempts");
    }

    bool sawLethalOutcome = false;
    float playbackAttackTime = invoke<float>(GET_LAST_ATTACK_TIME_HASH, player);
    while (GetTickCount64() - triggeredAt
        < EXECUTION_TOTAL_TIMEOUT_MS)
    {
        scriptWait(0);

        if (IsGameplayInterrupted())
            return finish(
                KnifeExecutionResult::Aborted,
                "pause/cutscene during synchronized execution");
        if (CurrentPlayer() != player || !IsLivingActor(player))
            return finish(
                KnifeExecutionResult::Aborted,
                "player changed/died during synchronized execution");
        if (target == 0 || !ENTITY::IS_ACTOR_VALID(target))
            return finish(
                KnifeExecutionResult::Failed,
                "target became invalid during synchronized execution");

        const bool targetAlive = HEALTH::IS_ACTOR_ALIVE(target);
        if (automaticThrowFlow)
        {
            TraceCombatState("synchronized playback frame; retry-disabled=1", player, target, 0);
            const float attackTime = invoke<float>(GET_LAST_ATTACK_TIME_HASH, player);
            if (std::fabs(attackTime - playbackAttackTime) > 0.0001f)
            {
                TraceCombatState("attack timestamp changed during playback; no input injected",
                    player, target, 0);
                playbackAttackTime = attackTime;
            }
        }
        if (!targetAlive && !sawLethalOutcome)
        {
            sawLethalOutcome = true;
            Trace("RDK #93 victim lethal outcome observed");
        }

        linkState = ReadExecutionLinkState(player, target);
        if ((linkState.playerLinkedTarget != 0
                && linkState.playerLinkedTarget != static_cast<int>(target))
            || (linkState.targetLinkedTarget != 0
                && linkState.targetLinkedTarget != static_cast<int>(player)))
        {
            TraceExecutionLinkState("foreign partner during playback", player, target, linkState);
            return finish(KnifeExecutionResult::Failed, "exact execution partner changed during playback");
        }
        const bool synchronized =
            HasSynchronizedExecutionEvidence(
                linkState,
                player,
                target)
            || linkState.playerPerforming || linkState.targetPerforming
            || linkState.playerPhaseLocked || linkState.targetPhaseLocked;

        if (!synchronized)
        {
            if (!sawLethalOutcome)
            {
                TraceExecutionLinkState(
                    "linked execution ended before lethal outcome",
                    player,
                    target,
                    linkState);
                return finish(
                    KnifeExecutionResult::Failed,
                    "linked execution ended before lethal outcome");
            }

            TraceExecutionLinkState(
                "synchronized playback completed",
                player,
                target,
                linkState);

            const ULONGLONG controlWaitStarted = GetTickCount64();
            while (GetTickCount64() - controlWaitStarted
                < CONTROL_RETURN_TIMEOUT_MS)
            {
                if (invoke<int>(
                        IS_PLAYER_CONTROLLABLE_HASH,
                        playerSlot) != 0)
                {
                    Trace(
                        "RDK #93 player control restored after execution");
                    return finish(
                        KnifeExecutionResult::Success,
                        "synchronized execution completed and victim died");
                }
                scriptWait(0);
                if (IsGameplayInterrupted()
                    || CurrentPlayer() != player
                    || !IsLivingActor(player))
                {
                    break;
                }
            }

            return finish(
                KnifeExecutionResult::Failed,
                "execution ended but player control did not return");
        }
    }

    TraceExecutionLinkState(
        "execution timeout state",
        player,
        target,
        ReadExecutionLinkState(player, target));
    return finish(
        KnifeExecutionResult::Failed,
        "overall synchronized execution timeout");
}

void ScriptMain()
{
    InitTrace(g_moduleHandle);
    Trace("ScriptMain entered");
    Trace("RDK #94 aimed-throw gameplay loop loaded");
    Trace("RDK Assassin Knife UI: Throwing Knife labels overridden; wheel/manual red artwork comes from deploy-time stock WTD mapres replacement");
    TraceAssassinKnifeFileLoaderDiagnostics();
    Trace("RDK #94 automatic FIRE-edge revision 20260929: after throw cancel/reposition, keep TARGET/LT held and wait up to 1000ms for attack/game FIRE to return released before the single synthetic FIRE pulse; final gate rechecks FIRE-up; no retry; manual F8 unchanged");
    Trace("RDK #94 captured-aim retention revision 20260928: after reposition, retain the original captured NPC when live aim is empty and the predicate scan is complete; reject foreign/ambiguous readings and incomplete empty scans; TARGET must stay held; one FIRE pulse; no target replacement; manual F8 unchanged");
    Trace("RDK #94 automatic distance-only reposition revision 20260928: preserve the existing target-to-John ground-plane direction and John's heading; if farther than 0.85m only shorten that distance to 0.85m; never rotate the NPC; manual F8 front-slot behavior unchanged");
    Trace("RDK #94 automatic native-axis diagnostics revision 20260928: GET_ACTOR_AXIS index 2 for John and the exact captured NPC at existing facing samples before/after reposition, control restoration and FIRE; forward is negative axis 2; negative signed horizontal projection means the other actor is in front; raw axes remain diagnostic only; manual F8 unchanged");
    Trace("RDK #94 controller diagnostics revision 000616: XInput LT/RT (0-255), LX/LY/RX/RY (-32768..32767), buttons and packet per connected slot; no deadzone applied; unavailable remains explicit; controller state is diagnostic only");
    char controllers[704] = {};
    ReadControllerInput(controllers, sizeof(controllers));
    Trace(controllers);
    Trace("RDK #94 player alignment revision 175717: automatic final reposition uses F8 verification (3 grounded frames and restored control) before single FIRE; manual F8 unchanged");
    Trace("RDK #94 trigger policy: while TARGET is held before a throw, cache the exact supported RDR target; one non-throwing non-ambiguous resolver-dropout frame may retain that exact cache, a second no-result or actual ambiguity clears it; at throw start prefer the current exact target and fall back to cache only for a true no-result; physical projectile impact never selects/replaces the target");
    Trace("RDK #94 exact-target policy: reticle/target actor must agree when both are valid; otherwise exactly one IS_PLAYER_TARGETTING_ACTOR match is required; no nearest NPC or camera guess");
    Trace("RDK #94 captured-target protection policy: save the exact aimed NPC's original proof mask and apply temporary all-proofs from throw start; after knife release the deterministic automatic path keeps that exact NPC protected through the execution-start attempt and restores proof only when linked execution owns the target or before forced lethal fallback");
    Trace("RDK #94 automatic stabilization policy: at exact throw capture, apply only a refreshed short stand-still plus best-effort reaction reset to that NPC while proof protection is active; never teleport/pin the target; immediately cancel John's throw task and hand off to reposition/execution without waiting for projectile release");
    Trace("RDK #94 deterministic handoff policy: at captured throw start use SET_PLAYER_CONTROL CLEAR_TASKS flag 4 immediately, reposition John while control is disabled, restore control, then force every target through MEMORY_CLEAR_ALL + FACTION_Neutral + repeated TASK_CLEAR/TASK_STAND_STILL idle settling; keep TARGET/LT held, wait for FIRE-up, then send one F8-style contextual FIRE; original faction/hostility is restored on surviving failure; ammo/projectile state never gates the handoff; no linked-target preseed, target teleport/action-tree reset, or retry");
    Trace("RDK #94 position policy: automatic failures keep John at the execution/recovery location; no rollback warp");
    Trace("RDK #94 exact execution policy: cancel/suppress any foreign linked execution partner before retry");
    Trace("RDK #94 lethal policy: never arm one-shot-death before contextual execution start; after exact synchronized linked ownership is confirmed, arm one-shot-death and only then restore throw proof; on any failed/aborted handoff while temporary proof is still owned, a confirmed new exact-target hit from John's throwing knife restores proof and uses the exact-target lethal fallback so protection cannot erase that hit; restore the target's original one-shot value if it survives");
    Trace("RDK #137 release build: single 120ms full-black blink enabled; residual no-FIRE throw re-trigger blocked; FOV code removed; arrival shock retained; max-strength direct XInput rumble enabled; RMPTFX smoke and camera-focus rush remain disabled; F6/F7/F8 execution debug controls disabled");
    Trace("RDK #93 manual front execution harness available only when execution debug controls are enabled");
    Trace("RDK #94 F6/F7 execution test NPC hotkeys available only when execution debug controls are enabled");
    Trace("RDK #118 Assassin outfit prototype loaded: Deadly Assassin variation 18");
    Trace("RDK #118 default preset: hat off + bandana + gloves; release F9 to toggle hat-off bandana-only / hat-off bandana+gloves");
    TraceFloat("RDK #93 front execution offset metres", FRONT_EXECUTION_OFFSET_METRES);
    TraceFloat("RDK #94 automatic execution distance metres", FRONT_EXECUTION_OFFSET_METRES);
    Trace("RDK #93 trigger policy: manual F8 remains unchanged; automatic #94 captures the exact target at throw start, immediately cancels John's throw task, repositions, enables execute, waits only for attack/game FIRE release while TARGET/LT stays held, then sends one F8-style FIRE; it does not wait for physical knife/projectile release");
    Trace("RDK #93 moving-target policy: manual F8 retains its existing moving-target stabilization; automatic #94 uses only short stand-still refreshes on the exact captured NPC and never teleports/pins that NPC; stand-still refresh stops after the exact-target FIRE gate passes");
    Trace("RDK #94 alignment policy: verify grounded X/Z arrival, restored control and target transform drift; tolerate engine elevation ground-snap; John facing diagnostic only; automatic FIRE retains captured ownership through an empty live-aim reading and rejects conflicting/ambiguous aim");
    Trace("RDK #94 targeting diagnostic: execution geometry logs exact-target predicate, GET_TARGET_ACTOR, reticle actor, and game TARGET/FIRE state; deterministic execution additionally logs every execution-start frame with aim/ground/readiness/link/last-hit context so linked-start successes can be compared directly with fallbacks");
    Trace("RDK #93 runtime gate: F8 still requires exact linked execution state + natural victim death + player-control return; automatic #94 accepts linked execution or its exact-target forced lethal fallback");

    while (true)
    {
        UpdateAssassinKnifeUiIdentity();
        UpdateManualTargetingPolicy();
        HandleTestNpcSpawnHotkeys();
        if (!HandleManualExecutionDebugTrigger())
        {
            UpdateDetector();
            HandleThrowExecutionFlow();
        }
        HandleOutfitInspectionTrigger();
        UpdateAssassinOutfit();
        scriptWait(0);
    }
}
