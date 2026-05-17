#pragma once

#include <Windows.h>
#include <Xinput.h>

#include <string>

namespace OpenVREmulatorDriver
{

// Configuration for the HMD device input mapping.
struct HMDInputConfig
{
    static constexpr float DefaultMouseSensitivity = 0.003f;  // radians per pixel
    static constexpr float DefaultLookSpeed = 1.5f;           // radians/sec for left-stick look

    float mouse_sensitivity = DefaultMouseSensitivity;  // radians per pixel
    float look_speed = DefaultLookSpeed;                // radians/sec for left-stick look
    float move_speed = 1.0f;                            // m/s for left-trigger movement
    int key_mouse_toggle = VK_SPACE;                    // toggle mouse look on/off
};

// Configuration for the left controller input mapping.
// Note: LT, LB, left stick and gamepad X/Y are consumed by the HMD and
// right-controller pose respectively and are not mapped as VR inputs.
struct LeftControllerConfig
{
    WORD btn_joystick_click = XINPUT_GAMEPAD_LEFT_THUMB;
    // Note: BACK is reserved as the controller-swap toggle and cannot be remapped.
};

// Configuration for the right controller input mapping.
struct RightControllerConfig
{
    static constexpr int DefaultKeyA = 'E';                      // VK_E (keyboard fallback for A button)
    static constexpr int DefaultKeyB = 'R';                      // VK_R (keyboard fallback for B button)
    static constexpr float DefaultTriggerClickThreshold = 0.75f;
    static constexpr float DefaultPoseMoveSpeed = 0.5f;  // m/s for d-pad / X / Y pose adjustment

    int key_a = DefaultKeyA;  // E (keyboard fallback for A button)
    int key_b = DefaultKeyB;  // R (keyboard fallback for B button)

    WORD btn_a = XINPUT_GAMEPAD_A;
    WORD btn_b = XINPUT_GAMEPAD_B;
    WORD btn_grip = XINPUT_GAMEPAD_RIGHT_SHOULDER;
    WORD btn_system = XINPUT_GAMEPAD_START;
    WORD btn_joystick_click = XINPUT_GAMEPAD_RIGHT_THUMB;

    float trigger_click_threshold = DefaultTriggerClickThreshold;
    float pose_move_speed = DefaultPoseMoveSpeed;  // m/s for d-pad / X / Y pose adjustment
};

// Top-level config loaded from resources/input_mapping.ini.
// Defaults match the original hard-coded behaviour.
struct InputConfig
{
    HMDInputConfig hmd;
    LeftControllerConfig left_controller;
    RightControllerConfig right_controller;

    // Return a config with all defaults (no file needed).
    static InputConfig Defaults();

    // Parse an INI file at the given absolute path.
    // Missing keys fall back to struct defaults.
    static InputConfig LoadFromFile(const std::string &path);

    // Locate the driver root directory relative to this DLL and load
    // resources/input_mapping.ini from it.  Falls back to defaults if the
    // file is absent or unreadable.
    static InputConfig LoadFromDriverRoot();
};

}  // namespace OpenVREmulatorDriver
